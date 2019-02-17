
/*

  KLayout Layout Viewer
  Copyright (C) 2006-2019 Matthias Koefferlein

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


#include "dbHierarchyBuilder.h"
#include "dbReader.h"
#include "dbTestSupport.h"
#include "dbEdges.h"
#include "dbRegion.h"
#include "dbDeepShapeStore.h"
#include "tlUnitTest.h"
#include "tlStream.h"

TEST(1)
{
  db::Layout ly;
  {
    std::string fn (tl::testsrc ());
    fn += "/testdata/algo/deep_region_l1.gds";
    tl::InputStream stream (fn);
    db::Reader reader (stream);
    reader.read (ly);
  }

  db::cell_index_type top_cell_index = *ly.begin_top_down ();

  db::DeepShapeStore dss;
  db::Layout target;

  //  deliberately using vector to force reallocation ...
  std::vector<db::Edges> edges;
  std::vector<unsigned int> target_layers;

  for (db::Layout::layer_iterator li = ly.begin_layers (); li != ly.end_layers (); ++li) {

    unsigned int li1 = (*li).first;
    db::RecursiveShapeIterator iter (ly, ly.cell (top_cell_index), li1);
    target_layers.push_back (target.insert_layer (*(*li).second));

    edges.push_back (db::Edges (iter, dss));

    EXPECT_EQ (edges.back ().size (), db::Edges (iter).size ());
    EXPECT_EQ (edges.back ().bbox (), db::Edges (iter).bbox ());

  }

  unsigned int target_top_cell_index = target.add_cell (ly.cell_name (top_cell_index));

  for (std::vector<db::Edges>::const_iterator r = edges.begin (); r != edges.end (); ++r) {
    target.insert (target_top_cell_index, target_layers [r - edges.begin ()], *r);
  }

  CHECKPOINT();
  db::compare_layouts (_this, target, tl::testsrc () + "/testdata/algo/deep_edges_au1.gds");
}

TEST(2_MergeEdges)
{
  db::Layout ly;
  {
    std::string fn (tl::testsrc ());
    fn += "/testdata/algo/deep_region_l1.gds";
    tl::InputStream stream (fn);
    db::Reader reader (stream);
    reader.read (ly);
  }

  db::cell_index_type top_cell_index = *ly.begin_top_down ();
  db::Cell &top_cell = ly.cell (top_cell_index);

  db::DeepShapeStore dss;

  unsigned int l2 = ly.get_layer (db::LayerProperties (2, 0));
  unsigned int l3 = ly.get_layer (db::LayerProperties (3, 0));

  db::Edges r2 (db::RecursiveShapeIterator (ly, top_cell, l2), dss);
  r2.merge ();
  db::Edges r3 (db::RecursiveShapeIterator (ly, top_cell, l3), dss);
  db::Edges r3_merged (r3.merged ());

  db::Layout target;
  unsigned int target_top_cell_index = target.add_cell (ly.cell_name (top_cell_index));

  target.insert (target_top_cell_index, target.get_layer (db::LayerProperties (10, 0)), r2);
  target.insert (target_top_cell_index, target.get_layer (db::LayerProperties (11, 0)), r3_merged);

  CHECKPOINT();
  db::compare_layouts (_this, target, tl::testsrc () + "/testdata/algo/deep_edges_au2.gds");
}

TEST(3_Edge2EdgeBooleans)
{
  db::Layout ly;
  {
    std::string fn (tl::testsrc ());
    fn += "/testdata/algo/deep_region_l1.gds";
    tl::InputStream stream (fn);
    db::Reader reader (stream);
    reader.read (ly);
  }

  db::cell_index_type top_cell_index = *ly.begin_top_down ();
  db::Cell &top_cell = ly.cell (top_cell_index);

  db::DeepShapeStore dss;

  unsigned int l2 = ly.get_layer (db::LayerProperties (2, 0));
  unsigned int l3 = ly.get_layer (db::LayerProperties (3, 0));

  db::Region r2 (db::RecursiveShapeIterator (ly, top_cell, l2), dss);
  db::Region r3 (db::RecursiveShapeIterator (ly, top_cell, l3), dss);
  db::Region r2and3 = r2 & r3;

  db::Edges e3 = r3.edges ();
  db::Edges e2and3 = r2and3.edges ();

  db::Layout target;
  unsigned int target_top_cell_index = target.add_cell (ly.cell_name (top_cell_index));

  target.insert (target_top_cell_index, target.get_layer (db::LayerProperties (2, 0)), r2);
  target.insert (target_top_cell_index, target.get_layer (db::LayerProperties (3, 0)), r3);
  target.insert (target_top_cell_index, target.get_layer (db::LayerProperties (10, 0)), e3);
  target.insert (target_top_cell_index, target.get_layer (db::LayerProperties (11, 0)), e2and3);
  target.insert (target_top_cell_index, target.get_layer (db::LayerProperties (20, 0)), e3 & e2and3);
  target.insert (target_top_cell_index, target.get_layer (db::LayerProperties (21, 0)), e3 - e2and3);
  target.insert (target_top_cell_index, target.get_layer (db::LayerProperties (22, 0)), e3 ^ e2and3);

  CHECKPOINT();
  db::compare_layouts (_this, target, tl::testsrc () + "/testdata/algo/deep_edges_au3.gds");
}

TEST(4_Edge2PolygonBooleans)
{
  db::Layout ly;
  {
    std::string fn (tl::testsrc ());
    fn += "/testdata/algo/deep_region_l1.gds";
    tl::InputStream stream (fn);
    db::Reader reader (stream);
    reader.read (ly);
  }

  db::cell_index_type top_cell_index = *ly.begin_top_down ();
  db::Cell &top_cell = ly.cell (top_cell_index);

  db::DeepShapeStore dss;

  unsigned int l2 = ly.get_layer (db::LayerProperties (2, 0));
  unsigned int l3 = ly.get_layer (db::LayerProperties (3, 0));

  db::Region r2 (db::RecursiveShapeIterator (ly, top_cell, l2), dss);
  db::Region r3 (db::RecursiveShapeIterator (ly, top_cell, l3), dss);
  db::Region r2and3 = r2 & r3;

  db::Edges e3 = r3.edges ();

  db::Layout target;
  unsigned int target_top_cell_index = target.add_cell (ly.cell_name (top_cell_index));

  target.insert (target_top_cell_index, target.get_layer (db::LayerProperties (2, 0)), r2);
  target.insert (target_top_cell_index, target.get_layer (db::LayerProperties (3, 0)), r3);

  target.insert (target_top_cell_index, target.get_layer (db::LayerProperties (10, 0)), e3 & r2);
  target.insert (target_top_cell_index, target.get_layer (db::LayerProperties (11, 0)), e3 & r2and3);
  target.insert (target_top_cell_index, target.get_layer (db::LayerProperties (12, 0)), e3 - r2);
  target.insert (target_top_cell_index, target.get_layer (db::LayerProperties (13, 0)), e3 - r2and3);

  target.insert (target_top_cell_index, target.get_layer (db::LayerProperties (20, 0)), e3.inside_part (r2));
  target.insert (target_top_cell_index, target.get_layer (db::LayerProperties (21, 0)), e3.inside_part (r2and3));
  target.insert (target_top_cell_index, target.get_layer (db::LayerProperties (22, 0)), e3.outside_part (r2));
  target.insert (target_top_cell_index, target.get_layer (db::LayerProperties (23, 0)), e3.outside_part (r2and3));

  CHECKPOINT();
  db::compare_layouts (_this, target, tl::testsrc () + "/testdata/algo/deep_edges_au4.gds");
}

TEST(5_Filters)
{
  db::Layout ly;
  {
    std::string fn (tl::testsrc ());
    fn += "/testdata/algo/deep_region_area_peri_l1.gds";
    tl::InputStream stream (fn);
    db::Reader reader (stream);
    reader.read (ly);
  }

  db::cell_index_type top_cell_index = *ly.begin_top_down ();
  db::Cell &top_cell = ly.cell (top_cell_index);

  db::DeepShapeStore dss;

  unsigned int l2 = ly.get_layer (db::LayerProperties (2, 0));

  db::Region r2 (db::RecursiveShapeIterator (ly, top_cell, l2), dss);
  db::Edges e2 = r2.edges ();

  {
    db::Layout target;
    unsigned int target_top_cell_index = target.add_cell (ly.cell_name (top_cell_index));

    target.insert (target_top_cell_index, target.get_layer (db::LayerProperties (2, 0)), r2);

    db::EdgeLengthFilter elf1 (0, 40000, false);
    db::EdgeLengthFilter elf2 (0, 30000, true);

    target.insert (target_top_cell_index, target.get_layer (db::LayerProperties (10, 0)), e2.filtered (elf1));
    target.insert (target_top_cell_index, target.get_layer (db::LayerProperties (11, 0)), e2.filtered (elf2));

    CHECKPOINT();
    db::compare_layouts (_this, target, tl::testsrc () + "/testdata/algo/deep_edges_au5a.gds");
  }

  {
    db::Layout target;
    unsigned int target_top_cell_index = target.add_cell (ly.cell_name (top_cell_index));

    target.insert (target_top_cell_index, target.get_layer (db::LayerProperties (2, 0)), r2);

    db::EdgeOrientationFilter eof1 (0, 1, false);
    db::EdgeOrientationFilter eof2 (0, 1, true);

    target.insert (target_top_cell_index, target.get_layer (db::LayerProperties (10, 0)), e2.filtered (eof1));
    target.insert (target_top_cell_index, target.get_layer (db::LayerProperties (11, 0)), e2.filtered (eof2));

    CHECKPOINT();
    db::compare_layouts (_this, target, tl::testsrc () + "/testdata/algo/deep_edges_au5b.gds");
  }
}

TEST(6_Extended)
{
  db::Layout ly;
  {
    std::string fn (tl::testsrc ());
    fn += "/testdata/algo/deep_region_area_peri_l1.gds";
    tl::InputStream stream (fn);
    db::Reader reader (stream);
    reader.read (ly);
  }

  db::cell_index_type top_cell_index = *ly.begin_top_down ();
  db::Cell &top_cell = ly.cell (top_cell_index);

  db::DeepShapeStore dss;

  unsigned int l2 = ly.get_layer (db::LayerProperties (2, 0));

  db::Region r2 (db::RecursiveShapeIterator (ly, top_cell, l2), dss);
  db::Edges e2 = r2.edges ();

  db::Layout target;
  unsigned int target_top_cell_index = target.add_cell (ly.cell_name (top_cell_index));

  target.insert (target_top_cell_index, target.get_layer (db::LayerProperties (2, 0)), r2);

  db::EdgeLengthFilter elf1 (0, 40000, false);
  db::Edges e2f = e2.filtered (elf1);

  db::Region e2e1;
  e2.extended (e2e1, 100, 200, 300, 50);
  db::Region e2e2;
  e2f.extended (e2e2, 0, 0, 300, 0);
  db::Region e2e3;
  e2.extended (e2e3, 100, 200, 300, 50, true);
  db::Region e2e4;
  e2f.extended (e2e4, 0, 0, 300, 0, true);

  target.insert (target_top_cell_index, target.get_layer (db::LayerProperties (10, 0)), e2e1);
  target.insert (target_top_cell_index, target.get_layer (db::LayerProperties (11, 0)), e2e2);
  target.insert (target_top_cell_index, target.get_layer (db::LayerProperties (12, 0)), e2e3);
  target.insert (target_top_cell_index, target.get_layer (db::LayerProperties (13, 0)), e2e4);

  CHECKPOINT();
  db::compare_layouts (_this, target, tl::testsrc () + "/testdata/algo/deep_edges_au6.gds");
}

TEST(7_Partial)
{
  db::Layout ly;
  {
    std::string fn (tl::testsrc ());
    fn += "/testdata/algo/deep_region_area_peri_l1.gds";
    tl::InputStream stream (fn);
    db::Reader reader (stream);
    reader.read (ly);
  }

  db::cell_index_type top_cell_index = *ly.begin_top_down ();
  db::Cell &top_cell = ly.cell (top_cell_index);

  db::DeepShapeStore dss;

  unsigned int l2 = ly.get_layer (db::LayerProperties (2, 0));

  db::Region r2 (db::RecursiveShapeIterator (ly, top_cell, l2), dss);
  db::Edges e2 = r2.edges ();

  db::Layout target;
  unsigned int target_top_cell_index = target.add_cell (ly.cell_name (top_cell_index));

  target.insert (target_top_cell_index, target.get_layer (db::LayerProperties (2, 0)), r2);

  db::EdgeLengthFilter elf1 (0, 40000, false);
  db::Edges e2f = e2.filtered (elf1);

  target.insert (target_top_cell_index, target.get_layer (db::LayerProperties (10, 0)), e2.start_segments (1000, 0.0));
  target.insert (target_top_cell_index, target.get_layer (db::LayerProperties (11, 0)), e2.start_segments (0, 0.2));
  target.insert (target_top_cell_index, target.get_layer (db::LayerProperties (12, 0)), e2f.start_segments (1000, 0.0));
  target.insert (target_top_cell_index, target.get_layer (db::LayerProperties (13, 0)), e2f.start_segments (0, 0.2));

  target.insert (target_top_cell_index, target.get_layer (db::LayerProperties (20, 0)), e2.end_segments (1000, 0.0));
  target.insert (target_top_cell_index, target.get_layer (db::LayerProperties (21, 0)), e2.end_segments (0, 0.2));
  target.insert (target_top_cell_index, target.get_layer (db::LayerProperties (22, 0)), e2f.end_segments (1000, 0.0));
  target.insert (target_top_cell_index, target.get_layer (db::LayerProperties (23, 0)), e2f.end_segments (0, 0.2));

  target.insert (target_top_cell_index, target.get_layer (db::LayerProperties (30, 0)), e2.centers (1000, 0.0));
  target.insert (target_top_cell_index, target.get_layer (db::LayerProperties (31, 0)), e2.centers (0, 0.2));
  target.insert (target_top_cell_index, target.get_layer (db::LayerProperties (32, 0)), e2f.centers (1000, 0.0));
  target.insert (target_top_cell_index, target.get_layer (db::LayerProperties (33, 0)), e2f.centers (0, 0.2));

  CHECKPOINT();
  db::compare_layouts (_this, target, tl::testsrc () + "/testdata/algo/deep_edges_au7.gds");
}

TEST(8_SelectInteracting)
{
  db::Layout ly;
  {
    std::string fn (tl::testsrc ());
    fn += "/testdata/algo/deep_region_l1.gds";
    tl::InputStream stream (fn);
    db::Reader reader (stream);
    reader.read (ly);
  }

  db::cell_index_type top_cell_index = *ly.begin_top_down ();
  db::Cell &top_cell = ly.cell (top_cell_index);

  db::DeepShapeStore dss;

  unsigned int l2 = ly.get_layer (db::LayerProperties (2, 0));
  unsigned int l3 = ly.get_layer (db::LayerProperties (3, 0));

  db::Region r2 (db::RecursiveShapeIterator (ly, top_cell, l2), dss);
  db::Region r3 (db::RecursiveShapeIterator (ly, top_cell, l3), dss);
  db::Edges e2 = r2.edges ();
  db::Edges e3 = r3.edges ();

  db::Layout target;
  unsigned int target_top_cell_index = target.add_cell (ly.cell_name (top_cell_index));

  target.insert (target_top_cell_index, target.get_layer (db::LayerProperties (2, 0)), r2);
  target.insert (target_top_cell_index, target.get_layer (db::LayerProperties (3, 0)), r3);

  target.insert (target_top_cell_index, target.get_layer (db::LayerProperties (10, 0)), e2.selected_interacting (e3));
  target.insert (target_top_cell_index, target.get_layer (db::LayerProperties (11, 0)), e2.selected_not_interacting (e3));
  target.insert (target_top_cell_index, target.get_layer (db::LayerProperties (12, 0)), e3.selected_interacting (e2));
  target.insert (target_top_cell_index, target.get_layer (db::LayerProperties (13, 0)), e3.selected_not_interacting (e2));

  target.insert (target_top_cell_index, target.get_layer (db::LayerProperties (20, 0)), e2.selected_interacting (r3));
  target.insert (target_top_cell_index, target.get_layer (db::LayerProperties (21, 0)), e2.selected_not_interacting (r3));
  target.insert (target_top_cell_index, target.get_layer (db::LayerProperties (22, 0)), e3.selected_interacting (r2));
  target.insert (target_top_cell_index, target.get_layer (db::LayerProperties (23, 0)), e3.selected_not_interacting (r2));

  CHECKPOINT();
  db::compare_layouts (_this, target, tl::testsrc () + "/testdata/algo/deep_edges_au8.gds");
}

