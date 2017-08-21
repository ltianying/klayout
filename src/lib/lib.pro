
DESTDIR = $$OUT_PWD/..
TARGET = klayout_lib

include($$PWD/../klayout.pri)
include($$PWD/../lib.pri)

DEFINES += MAKE_LIB_LIBRARY

HEADERS = \
  libBasicArc.h \
  libBasicCircle.h \
  libBasicDonut.h \
  libBasicEllipse.h \
  libBasicPie.h \
  libBasicRoundPath.h \
  libBasicRoundPolygon.h \
  libBasicStrokedPolygon.h \
  libBasicText.h \
    libForceLink.h

SOURCES = \
  libForceLink.cc \
  libBasic.cc \
  libBasicArc.cc \
  libBasicCircle.cc \
  libBasicDonut.cc \
  libBasicEllipse.cc \
  libBasicPie.cc \
  libBasicRoundPath.cc \
  libBasicRoundPolygon.cc \
  libBasicStrokedPolygon.cc \
  libBasicText.cc

RESOURCES = \
  libResources.qrc

INCLUDEPATH += ../gsi ../tl ../db
DEPENDPATH += ../gsi ../tl ../db
LIBS += -L$$DESTDIR -lklayout_gsi -lklayout_tl -lklayout_db

