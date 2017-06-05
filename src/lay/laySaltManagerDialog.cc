
/*

  KLayout Layout Viewer
  Copyright (C) 2006-2017 Matthias Koefferlein

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

*/

#include "laySaltManagerDialog.h"
#include "laySaltModel.h"
#include "laySaltGrainPropertiesDialog.h"
#include "laySaltDownloadManager.h"
#include "laySalt.h"
#include "layVersion.h"
#include "ui_SaltGrainTemplateSelectionDialog.h"
#include "tlString.h"
#include "tlExceptions.h"

#include <QTextDocument>
#include <QPainter>
#include <QDir>
#include <QTextStream>
#include <QBuffer>
#include <QResource>
#include <QMessageBox>
#include <QAbstractItemModel>
#include <QStyledItemDelegate>

namespace lay
{

// --------------------------------------------------------------------------------------

/**
 *  @brief A tiny dialog to select a template and a name for the grain
 */
class SaltGrainTemplateSelectionDialog
  : public QDialog, private Ui::SaltGrainTemplateSelectionDialog
{
public:
  SaltGrainTemplateSelectionDialog (QWidget *parent, lay::Salt *salt)
    : QDialog (parent), mp_salt (salt)
  {
    Ui::SaltGrainTemplateSelectionDialog::setupUi (this);

    m_salt_templates.add_location (":/salt_templates");
    salt_view->setModel (new SaltModel (this, &m_salt_templates));
    salt_view->setItemDelegate (new SaltItemDelegate (this));
    salt_view->setCurrentIndex (salt_view->model ()->index (0, 0, QModelIndex ()));
  }

  lay::SaltGrain templ () const
  {
    SaltModel *model = dynamic_cast<SaltModel *> (salt_view->model ());
    tl_assert (model != 0);

    SaltGrain *g = model->grain_from_index (salt_view->currentIndex ());
    tl_assert (g != 0);

    return *g;
  }

  std::string name () const
  {
    return tl::to_string (name_edit->text ());
  }

  void accept ()
  {
    name_alert->clear ();
    std::string name = tl::to_string (name_edit->text ().simplified ());
    if (name.empty ()) {
      name_alert->error () << tr ("Name must not be empty");
    } else if (! SaltGrain::valid_name (name)) {
      name_alert->error () << tr ("Name is not valid (must be composed of letters, digits or underscores.\nGroups and names need to be separated with slashes.");
    } else {

      //  check, if this name does not exist yet
      for (Salt::flat_iterator g = mp_salt->begin_flat (); g != mp_salt->end_flat (); ++g) {
        if ((*g)->name () == name) {
          name_alert->error () << tr ("A package with this name already exists");
          return;
        }
      }

      QDialog::accept ();

    }
  }

private:
  lay::Salt m_salt_templates;
  lay::Salt *mp_salt;
};

// --------------------------------------------------------------------------------------
//  SaltManager implementation

SaltManagerDialog::SaltManagerDialog (QWidget *parent, lay::Salt *salt, const std::string &salt_mine_url)
  : QDialog (parent),
    m_salt_mine_url (salt_mine_url),
    dm_update_models (this, &SaltManagerDialog::update_models), m_current_tab (-1)
{
  Ui::SaltManagerDialog::setupUi (this);
  mp_properties_dialog = new lay::SaltGrainPropertiesDialog (this);

  //  TODO: clarify where the delete button should go ... currently it's too easy to
  //  mistake add and delete buttons
  delete_button->hide ();

  connect (edit_button, SIGNAL (clicked ()), this, SLOT (edit_properties ()));
  connect (create_button, SIGNAL (clicked ()), this, SLOT (create_grain ()));
  connect (delete_button, SIGNAL (clicked ()), this, SLOT (delete_grain ()));
  connect (apply_new_button, SIGNAL (clicked ()), this, SLOT (apply ()));
  connect (apply_update_button, SIGNAL (clicked ()), this, SLOT (apply ()));

  mp_salt = salt;

  try {
    if (! m_salt_mine_url.empty ()) {
      tl::log << tl::to_string (tr ("Downloading package repository from %1").arg (tl::to_qstring (m_salt_mine_url)));
      m_salt_mine.load (m_salt_mine_url);
    }
  } catch (tl::Exception &ex) {
    tl::error << ex.msg ();
  }

  SaltModel *model = new SaltModel (this, mp_salt);
  model->set_empty_explanation (tr ("No packages are present on this system"));
  salt_view->setModel (model);
  salt_view->setItemDelegate (new SaltItemDelegate (this));

  SaltModel *mine_model;

  //  This model will show only the grains of mp_salt_mine which are not present in mp_salt yet.
  mine_model = new SaltModel (this, &m_salt_mine, mp_salt, true);
  mine_model->set_empty_explanation (tr ("All available packages are installed"));
  salt_mine_view_new->setModel (mine_model);
  salt_mine_view_new->setItemDelegate (new SaltItemDelegate (this));

  //  This model will show only the grains of mp_salt_mine which are present in mp_salt already.
  mine_model = new SaltModel (this, &m_salt_mine, mp_salt, false);
  mine_model->set_empty_explanation (tr ("No packages are installed"));
  salt_mine_view_update->setModel (mine_model);
  salt_mine_view_update->setItemDelegate (new SaltItemDelegate (this));

  mode_tab->setCurrentIndex (0);

  connect (mode_tab, SIGNAL (currentChanged (int)), this, SLOT (mode_changed ()));
  m_current_tab = mode_tab->currentIndex ();

  connect (mp_salt, SIGNAL (collections_changed ()), this, SLOT (salt_changed ()));
  connect (mp_salt, SIGNAL (collections_about_to_change ()), this, SLOT (salt_about_to_change ()));
  connect (&m_salt_mine, SIGNAL (collections_changed ()), this, SLOT (salt_mine_changed ()));
  connect (&m_salt_mine, SIGNAL (collections_about_to_change ()), this, SLOT (salt_mine_about_to_change ()));

  update_models ();

  connect (salt_view->selectionModel (), SIGNAL (currentChanged (const QModelIndex &, const QModelIndex &)), this, SLOT (current_changed ()));
  connect (salt_view, SIGNAL (doubleClicked (const QModelIndex &)), this, SLOT (edit_properties ()));
  connect (salt_mine_view_new->selectionModel (), SIGNAL (currentChanged (const QModelIndex &, const QModelIndex &)), this, SLOT (mine_new_current_changed ()), Qt::QueuedConnection);
  connect (salt_mine_view_update->selectionModel (), SIGNAL (currentChanged (const QModelIndex &, const QModelIndex &)), this, SLOT (mine_update_current_changed ()), Qt::QueuedConnection);
  connect (salt_mine_view_new, SIGNAL (doubleClicked (const QModelIndex &)), this, SLOT (mark_clicked ()));
  connect (salt_mine_view_update, SIGNAL (doubleClicked (const QModelIndex &)), this, SLOT (mark_clicked ()));

  search_installed_edit->set_clear_button_enabled (true);
  search_new_edit->set_clear_button_enabled (true);
  search_update_edit->set_clear_button_enabled (true);
  connect (search_installed_edit, SIGNAL (textChanged (const QString &)), this, SLOT (search_text_changed (const QString &)));
  connect (search_new_edit, SIGNAL (textChanged (const QString &)), this, SLOT (search_text_changed (const QString &)));
  connect (search_update_edit, SIGNAL (textChanged (const QString &)), this, SLOT (search_text_changed (const QString &)));

  connect (mark_new_button, SIGNAL (clicked ()), this, SLOT (mark_clicked ()));
  connect (mark_update_button, SIGNAL (clicked ()), this, SLOT (mark_clicked ()));

  QAction *a;

  salt_view->addAction (actionCreatePackage);
  salt_view->addAction (actionDelete);
  salt_view->setContextMenuPolicy (Qt::ActionsContextMenu);

  salt_mine_view_new->addAction (actionMarkNew);
  salt_mine_view_new->addAction (actionMarkAllNew);
  salt_mine_view_new->addAction (actionUnmarkNew);
  salt_mine_view_new->addAction (actionUnmarkAllNew);
  a = new QAction (this);
  a->setSeparator (true);
  salt_mine_view_new->addAction (a);
  salt_mine_view_new->addAction (actionShowMarkedOnlyNew);
  actionShowMarkedOnlyNew->setCheckable (true);
  a = new QAction (this);
  a->setSeparator (true);
  salt_mine_view_new->addAction (a);
  salt_mine_view_new->addAction (actionRefresh);
  salt_mine_view_new->setContextMenuPolicy (Qt::ActionsContextMenu);

  salt_mine_view_update->addAction (actionMarkForUpdate);
  salt_mine_view_update->addAction (actionMarkAllUpdate);
  salt_mine_view_update->addAction (actionUnmarkForUpdate);
  salt_mine_view_update->addAction (actionUnmarkAllUpdate);
  a = new QAction (this);
  a->setSeparator (true);
  salt_mine_view_update->addAction (a);
  salt_mine_view_update->addAction (actionShowMarkedOnlyUpdate);
  actionShowMarkedOnlyUpdate->setCheckable (true);
  a = new QAction (this);
  a->setSeparator (true);
  salt_mine_view_update->addAction (a);
  salt_mine_view_update->addAction (actionRefresh);
  salt_mine_view_update->setContextMenuPolicy (Qt::ActionsContextMenu);

  connect (actionCreatePackage, SIGNAL (triggered ()), this, SLOT (create_grain ()));
  connect (actionDelete, SIGNAL (triggered ()), this, SLOT (delete_grain ()));
  connect (actionUnmarkAllNew, SIGNAL (triggered ()), this, SLOT (unmark_all_new ()));
  connect (actionMarkAllNew, SIGNAL (triggered ()), this, SLOT (mark_all_new ()));
  connect (actionShowMarkedOnlyNew, SIGNAL (triggered ()), this, SLOT (show_marked_only_new ()));
  connect (actionUnmarkAllUpdate, SIGNAL (triggered ()), this, SLOT (unmark_all_update ()));
  connect (actionMarkAllUpdate, SIGNAL (triggered ()), this, SLOT (mark_all_update ()));
  connect (actionShowMarkedOnlyUpdate, SIGNAL (triggered ()), this, SLOT (show_marked_only_update ()));
  connect (actionRefresh, SIGNAL (triggered ()), this, SLOT (refresh ()));
  connect (actionMarkNew, SIGNAL (triggered ()), this, SLOT (mark_clicked ()));
  connect (actionUnmarkNew, SIGNAL (triggered ()), this, SLOT (mark_clicked ()));
  connect (actionMarkForUpdate, SIGNAL (triggered ()), this, SLOT (mark_clicked ()));
  connect (actionUnmarkForUpdate, SIGNAL (triggered ()), this, SLOT (mark_clicked ()));

  mine_update_current_changed ();
  mine_new_current_changed ();
  current_changed ();
}

void
SaltManagerDialog::mode_changed ()
{
  //  commits edits:
  setFocus (Qt::NoFocusReason);

  QList<int> sizes;
  if (m_current_tab == 2) {
    sizes = splitter->sizes ();
  } else if (m_current_tab == 1) {
    sizes = splitter_update->sizes ();
  } else if (m_current_tab == 0) {
    sizes = splitter_new->sizes ();
  }

  //  keeps the splitters in sync
  if (!sizes.empty ()) {
    splitter_new->setSizes (sizes);
    splitter_update->setSizes (sizes);
    splitter->setSizes (sizes);
  }

  actionShowMarkedOnlyNew->setChecked (false);
  actionShowMarkedOnlyUpdate->setChecked (false);

  if (mode_tab->currentIndex () < 2) {
    show_marked_only_new ();
    show_marked_only_update ();
  }

  m_current_tab = mode_tab->currentIndex ();
}

void
SaltManagerDialog::show_marked_only_new ()
{
  bool show_marked_only = actionShowMarkedOnlyNew->isChecked ();

  search_new_edit->clear ();

  SaltModel *model = dynamic_cast <SaltModel *> (salt_mine_view_new->model ());
  if (! model) {
    return;
  }

  salt_mine_view_new->setCurrentIndex (QModelIndex ());

  for (int i = model->rowCount (QModelIndex ()); i > 0; ) {
    --i;
    SaltGrain *g = model->grain_from_index (model->index (i, 0, QModelIndex ()));
    salt_mine_view_new->setRowHidden (i, show_marked_only && !(g && model->is_marked (g->name ())));
    mine_new_current_changed ();
  }
}

void
SaltManagerDialog::show_marked_only_update ()
{
  bool show_marked_only = actionShowMarkedOnlyUpdate->isChecked ();

  search_update_edit->clear ();

  SaltModel *model = dynamic_cast <SaltModel *> (salt_mine_view_update->model ());
  if (! model) {
    return;
  }

  salt_mine_view_update->setCurrentIndex (QModelIndex ());

  for (int i = model->rowCount (QModelIndex ()); i > 0; ) {
    --i;
    SaltGrain *g = model->grain_from_index (model->index (i, 0, QModelIndex ()));
    salt_mine_view_update->setRowHidden (i, show_marked_only && !(g && model->is_marked (g->name ())));
    mine_update_current_changed ();
  }
}

void
SaltManagerDialog::unmark_all_new ()
{
  SaltModel *model = dynamic_cast <SaltModel *> (salt_mine_view_new->model ());
  if (model) {
    model->clear_marked ();
    actionShowMarkedOnlyNew->setChecked (false);
    show_marked_only_new ();
    update_apply_state ();
  }
}

void
SaltManagerDialog::mark_all_new ()
{
  SaltModel *model = dynamic_cast <SaltModel *> (salt_mine_view_new->model ());
  if (model) {
    model->mark_all ();
    actionShowMarkedOnlyNew->setChecked (false);
    show_marked_only_new ();
    update_apply_state ();
  }
}

void
SaltManagerDialog::unmark_all_update ()
{
  SaltModel *model = dynamic_cast <SaltModel *> (salt_mine_view_update->model ());
  if (model) {
    model->clear_marked ();
    actionShowMarkedOnlyUpdate->setChecked (false);
    show_marked_only_update ();
    update_apply_state ();
  }
}

void
SaltManagerDialog::mark_all_update ()
{
  SaltModel *model = dynamic_cast <SaltModel *> (salt_mine_view_update->model ());
  if (model) {
    model->mark_all ();
    actionShowMarkedOnlyUpdate->setChecked (false);
    show_marked_only_update ();
    update_apply_state ();
  }
}

void
SaltManagerDialog::search_text_changed (const QString &text)
{
  QListView *view = 0;
  if (sender () == search_installed_edit) {
    view = salt_view;
  } else if (sender () == search_new_edit) {
    view = salt_mine_view_new;
  } else if (sender () == search_update_edit) {
    view = salt_mine_view_update;
  } else {
    return;
  }

  SaltModel *model = dynamic_cast <SaltModel *> (view->model ());
  if (! model) {
    return;
  }

  if (text.isEmpty ()) {

    for (int i = model->rowCount (QModelIndex ()); i > 0; ) {
      --i;
      view->setRowHidden (i, false);
    }

  } else {

    QRegExp re (text, Qt::CaseInsensitive);

    for (int i = model->rowCount (QModelIndex ()); i > 0; ) {
      --i;
      QModelIndex index = model->index (i, 0, QModelIndex ());
      SaltGrain *g = model->grain_from_index (index);
      bool hidden = (!g || re.indexIn (tl::to_qstring (g->name ())) < 0);
      view->setRowHidden (i, hidden);
    }

  }
}

void
SaltManagerDialog::mark_clicked ()
{
  QListView *view;
  if (sender () == salt_mine_view_new || sender () == mark_new_button || sender () == actionMarkNew || sender () == actionUnmarkNew) {
    view = salt_mine_view_new;
  } else {
    view = salt_mine_view_update;
  }

  bool toggle = (sender () != actionMarkNew && sender () != actionUnmarkNew && sender () != actionMarkForUpdate && sender () != actionUnmarkForUpdate);
  bool set = (sender () == actionMarkNew || sender () == actionMarkForUpdate);

  SaltModel *model = dynamic_cast <SaltModel *> (view->model ());
  if (! model) {
    return;
  }

  SaltGrain *g = model->grain_from_index (view->currentIndex ());
  if (g) {
    model->set_marked (g->name (), toggle ? ! model->is_marked (g->name ()) : set);
    update_apply_state ();
  }
}

void
SaltManagerDialog::update_apply_state ()
{
  SaltModel *model;

  model  = dynamic_cast <SaltModel *> (salt_mine_view_new->model ());
  if (model) {

    int marked = 0;

    for (int i = model->rowCount (QModelIndex ()); i > 0; ) {
      --i;
      QModelIndex index = model->index (i, 0, QModelIndex ());
      SaltGrain *g = model->grain_from_index (index);
      if (g && model->is_marked (g->name ())) {
        marked += 1;
      }
    }

    apply_new_button->setEnabled (marked > 0);
    if (marked == 0) {
      apply_label_new->setText (QString ());
    } else if (marked == 1) {
      apply_label_new->setText (tr ("One package selected"));
    } else if (marked > 1) {
      apply_label_new->setText (tr ("%1 packages selected").arg (marked));
    }

  }

  model  = dynamic_cast <SaltModel *> (salt_mine_view_update->model ());
  if (model) {

    int marked = 0;

    for (int i = model->rowCount (QModelIndex ()); i > 0; ) {
      --i;
      QModelIndex index = model->index (i, 0, QModelIndex ());
      SaltGrain *g = model->grain_from_index (index);
      if (g && model->is_marked (g->name ())) {
        marked += 1;
      }
    }

    apply_update_button->setEnabled (marked > 0);
    if (marked == 0) {
      apply_label_update->setText (QString ());
    } else if (marked == 1) {
      apply_label_update->setText (tr ("One package selected"));
    } else if (marked > 1) {
      apply_label_update->setText (tr ("%1 packages selected").arg (marked));
    }

  }
}

void
SaltManagerDialog::apply ()
{
BEGIN_PROTECTED

  bool update = (sender () == apply_update_button);

  lay::SaltDownloadManager manager;

  bool any = false;

  //  fetch all marked grains and register for download
  SaltModel *model;
  if (update) {
    model = dynamic_cast <SaltModel *> (salt_mine_view_update->model ());
  } else {
    model = dynamic_cast <SaltModel *> (salt_mine_view_new->model ());
  }

  if (model) {
    for (int i = model->rowCount (QModelIndex ()); i > 0; ) {
      --i;
      QModelIndex index = model->index (i, 0, QModelIndex ());
      SaltGrain *g = model->grain_from_index (index);
      if (g && model->is_marked (g->name ())) {
        manager.register_download (g->name (), g->url (), g->version ());
        any = true;
      }
    }
  }

  if (! any) {
    if (update) {
      throw tl::Exception (tl::to_string (tr ("No packages marked for update")));
    } else {
      throw tl::Exception (tl::to_string (tr ("No packages marked for installation")));
    }
  }

  manager.compute_dependencies (*mp_salt, m_salt_mine);

  if (manager.show_confirmation_dialog (this, *mp_salt)) {
    if (update) {
      unmark_all_update ();
    } else {
      unmark_all_new ();
    }
    if (! manager.execute (*mp_salt)) {
      throw tl::Exception (tl::to_string (tr ("Failed to install some of the selected packages. Please see log for details.")));
    }
  }

END_PROTECTED
}

void
SaltManagerDialog::edit_properties ()
{
  SaltGrain *g = current_grain ();
  if (g) {
    if (g->is_readonly ()) {
      QMessageBox::critical (this, tr ("Package is not Editable"),
                                   tr ("This package cannot be edited.\n\nEither you don't have write permissions on the directory or the package was installed from a repository."));
    } else if (mp_properties_dialog->exec_dialog (g, mp_salt)) {
      current_changed ();
    }
  }
}

void
SaltManagerDialog::set_current_grain_by_name (const std::string &current)
{
  SaltModel *model = dynamic_cast <SaltModel *> (salt_view->model ());
  if (!model) {
    return;
  }

  for (int i = model->rowCount (QModelIndex ()); i > 0; ) {
    --i;
    QModelIndex index = model->index (i, 0, QModelIndex ());
    SaltGrain *g = model->grain_from_index (index);
    if (g && g->name () == current) {
      salt_view->setCurrentIndex (index);
      break;
    }
  }
}

void
SaltManagerDialog::create_grain ()
{
BEGIN_PROTECTED

  SaltGrainTemplateSelectionDialog temp_dialog (this, mp_salt);
  if (temp_dialog.exec ()) {

    SaltGrain target;
    target.set_name (temp_dialog.name ());

    if (mp_salt->create_grain (temp_dialog.templ (), target)) {

      //  select the new one
      SaltModel *model = dynamic_cast <SaltModel *> (salt_view->model ());
      if (model) {

        //  NOTE: this is basically redundant (because it happens in the background later
        //  through dm_update_models). But we need this now to establish the selection.
        model->update();

        set_current_grain_by_name (target.name ());

      }

    } else {
      throw tl::Exception (tl::to_string (tr ("Initialization of new package failed - see log window (File/Log Viewer) for details")));
    }

  }

END_PROTECTED
}

void
SaltManagerDialog::delete_grain ()
{
BEGIN_PROTECTED

  SaltGrain *g = current_grain ();
  if (! g) {
    throw tl::Exception (tl::to_string (tr ("No package selected to delete")));
  }

  if (QMessageBox::question (this, tr ("Delete Package"), tr ("Are you sure to delete package '%1'?").arg (tl::to_qstring (g->name ())), QMessageBox::Yes, QMessageBox::No) == QMessageBox::Yes) {
    mp_salt->remove_grain (*g);
  }

END_PROTECTED
}

void
SaltManagerDialog::salt_about_to_change ()
{
  SaltModel *model = dynamic_cast <SaltModel *> (salt_view->model ());
  tl_assert (model != 0);
  model->begin_update ();
}

void
SaltManagerDialog::salt_changed ()
{
  dm_update_models ();
}

void
SaltManagerDialog::salt_mine_about_to_change ()
{
  SaltModel *model;

  model = dynamic_cast <SaltModel *> (salt_mine_view_new->model ());
  tl_assert (model != 0);
  model->begin_update ();

  model = dynamic_cast <SaltModel *> (salt_mine_view_update->model ());
  tl_assert (model != 0);
  model->begin_update ();
}

void
SaltManagerDialog::refresh ()
{
BEGIN_PROTECTED

  if (! m_salt_mine_url.empty ()) {

    tl::log << tl::to_string (tr ("Downloading package repository from %1").arg (tl::to_qstring (m_salt_mine_url)));

    lay::Salt new_mine;
    new_mine.load (m_salt_mine_url);
    m_salt_mine = new_mine;

    salt_mine_changed ();

  }

END_PROTECTED
}

void
SaltManagerDialog::salt_mine_changed ()
{
  dm_update_models ();
}

void
SaltManagerDialog::update_models ()
{
  actionShowMarkedOnlyNew->setChecked (false);
  actionShowMarkedOnlyUpdate->setChecked (false);

  SaltModel *model = dynamic_cast <SaltModel *> (salt_view->model ());
  tl_assert (model != 0);

  model->clear_messages ();

  //  Maintain the current index while updating
  std::string current;
  if (salt_view->currentIndex ().isValid ()) {
    const lay::SaltGrain *g = model->grain_from_index (salt_view->currentIndex ());
    if (g) {
      current = g->name ();
    }
  }

  //  Establish a message saying that an update is available
  for (Salt::flat_iterator g = mp_salt->begin_flat (); g != mp_salt->end_flat (); ++g) {
    SaltGrain *gm = m_salt_mine.grain_by_name ((*g)->name ());
    if (gm && SaltGrain::compare_versions (gm->version (), (*g)->version ()) > 0) {
      model->set_message ((*g)->name (), SaltModel::Warning, tl::to_string (tr ("An update to version %1 is available").arg (tl::to_qstring (gm->version ()))));
    }
  }

  model->update ();

  if (! current.empty ()) {
    set_current_grain_by_name (current);
  }

  if (mp_salt->is_empty ()) {

    list_stack->setCurrentIndex (1);
    details_frame->hide ();

  } else {

    list_stack->setCurrentIndex (0);
    details_frame->show ();

    //  select the first grain if required
    if (! salt_view->currentIndex ().isValid () && model->rowCount (QModelIndex ()) > 0) {
      salt_view->setCurrentIndex (model->index (0, 0, QModelIndex ()));
    }

  }

  SaltModel *mine_model;

  mine_model = dynamic_cast <SaltModel *> (salt_mine_view_update->model ());
  tl_assert (mine_model != 0);

  mine_model->clear_order ();
  mine_model->clear_messages ();
  mine_model->enable_all ();

  bool has_warning = false;

  //  Establish a message saying that an update is available
  for (Salt::flat_iterator g = mp_salt->begin_flat (); g != mp_salt->end_flat (); ++g) {
    SaltGrain *gm = m_salt_mine.grain_by_name ((*g)->name ());
    if (gm && SaltGrain::compare_versions (gm->version (), (*g)->version ()) > 0) {
      has_warning = true;
      mine_model->set_message ((*g)->name (), SaltModel::Warning, tl::to_string (tr ("The installed version is outdated (%1)").arg (tl::to_qstring ((*g)->version ()))));
      mine_model->set_order ((*g)->name (), -1);
    } else if (gm) {
      mine_model->set_message ((*g)->name (), SaltModel::None, tl::to_string (tr ("This package is up to date")));
      mine_model->set_order ((*g)->name (), 1);
      mine_model->set_enabled ((*g)->name (), false);
    }
  }

  //  Establish a message indicating whether the API version does not match
  for (Salt::flat_iterator g = m_salt_mine.begin_flat (); g != m_salt_mine.end_flat (); ++g) {
    if (SaltGrain::compare_versions (lay::Version::version (), (*g)->api_version ()) < 0) {
      mine_model->set_message ((*g)->name (), SaltModel::Warning, tl::to_string (tr ("This package requires a newer API (%1)").arg (tl::to_qstring ((*g)->api_version ()))));
      mine_model->set_enabled ((*g)->name (), false);
    }
  }

  if (has_warning) {
    mode_tab->setTabIcon (1, QIcon (":/warn_16.png"));
  } else {
    mode_tab->setTabIcon (1, QIcon ());
  }

  mine_model->update ();

  //  select the first grain
  if (mine_model->rowCount (QModelIndex ()) > 0) {
    salt_mine_view_update->setCurrentIndex (mine_model->index (0, 0, QModelIndex ()));
  }

  mine_model = dynamic_cast <SaltModel *> (salt_mine_view_new->model ());
  tl_assert (mine_model != 0);

  mine_model->clear_order ();
  mine_model->clear_messages ();
  mine_model->enable_all ();

  //  Establish a message indicating whether the API version does not match
  for (Salt::flat_iterator g = m_salt_mine.begin_flat (); g != m_salt_mine.end_flat (); ++g) {
    if (SaltGrain::compare_versions (lay::Version::version (), (*g)->api_version ()) < 0) {
      mine_model->set_message ((*g)->name (), SaltModel::Warning, tl::to_string (tr ("This package requires a newer API (%1)").arg (tl::to_qstring ((*g)->api_version ()))));
      mine_model->set_enabled ((*g)->name (), false);
    }
  }

  mine_model->update ();

  //  select the first grain
  if (mine_model->rowCount (QModelIndex ()) > 0) {
    salt_mine_view_new->setCurrentIndex (mine_model->index (0, 0, QModelIndex ()));
  }

  mine_new_current_changed ();
  mine_update_current_changed ();
  current_changed ();
  update_apply_state ();
}

void
SaltManagerDialog::current_changed ()
{
  SaltGrain *g = current_grain ();
  details_text->set_grain (g);
  if (!g) {
    details_frame->setEnabled (false);
    delete_button->setEnabled (false);
  } else {
    details_frame->setEnabled (true);
    delete_button->setEnabled (true);
    edit_button->setEnabled (! g->is_readonly ());
  }
}

lay::SaltGrain *
SaltManagerDialog::current_grain ()
{
  SaltModel *model = dynamic_cast <SaltModel *> (salt_view->model ());
  return model ? model->grain_from_index (salt_view->currentIndex ()) : 0;
}

void
SaltManagerDialog::mine_update_current_changed ()
{
BEGIN_PROTECTED

  SaltModel *model = dynamic_cast <SaltModel *> (salt_mine_view_update->model ());
  tl_assert (model != 0);
  SaltGrain *g = model->grain_from_index (salt_mine_view_update->currentIndex ());

  details_update_frame->setEnabled (g != 0);

  get_remote_grain_info (g, details_update_text);

END_PROTECTED
}

void
SaltManagerDialog::mine_new_current_changed ()
{
BEGIN_PROTECTED

  SaltModel *model = dynamic_cast <SaltModel *> (salt_mine_view_new->model ());
  tl_assert (model != 0);
  SaltGrain *g = model->grain_from_index (salt_mine_view_new->currentIndex ());

  details_new_frame->setEnabled (g != 0);

  get_remote_grain_info (g, details_new_text);

END_PROTECTED
}

void
SaltManagerDialog::get_remote_grain_info (lay::SaltGrain *g, SaltGrainDetailsTextWidget *details)
{
  if (! g) {
    details->setHtml (QString ());
    return;
  }

  std::auto_ptr<lay::SaltGrain> remote_grain;

  //  Download actual grain definition file
  try {

    if (g->url ().empty ()) {
      throw tl::Exception (tl::to_string (tr ("No download link available")));
    }

    QString html = tr (
      "<html>"
        "<body>"
          "<font color=\"#c0c0c0\">"
            "<h2>Fetching Package Definition ...</h2>"
            "<p><b>URL</b>: %1</p>"
          "</font>"
        "</body>"
      "</html>"
    )
    .arg (tl::to_qstring (SaltGrain::spec_url (g->url ())));

    details->setHtml (html);

    QApplication::processEvents (QEventLoop::ExcludeUserInputEvents);

    remote_grain.reset (new SaltGrain (SaltGrain::from_url (g->url ())));

    if (g->name () != remote_grain->name ()) {
      throw tl::Exception (tl::to_string (tr ("Name mismatch between repository and actual package (repository: %1, package: %2)").arg (tl::to_qstring (g->name ())).arg (tl::to_qstring (remote_grain->name ()))));
    }
    if (SaltGrain::compare_versions (g->version (), remote_grain->version ()) != 0) {
      throw tl::Exception (tl::to_string (tr ("Version mismatch between repository and actual package (repository: %1, package: %2)").arg (tl::to_qstring (g->version ())).arg (tl::to_qstring (remote_grain->version ()))));
    }

    details->set_grain (remote_grain.get ());

  } catch (tl::Exception &ex) {

    remote_grain.reset (0);

    QString html = tr (
      "<html>"
        "<body>"
          "<font color=\"#ff0000\">"
            "<h2>Error Fetching Package Definition</h2>"
            "<p><b>URL</b>: %1</p>"
            "<p><b>Error</b>: %2</p>"
          "</font>"
        "</body>"
      "</html>"
    )
    .arg (tl::to_qstring (SaltGrain::spec_url (g->url ())))
    .arg (tl::to_qstring (tl::escaped_to_html (ex.msg ())));

    details->setHtml (html);

  }
}

}
