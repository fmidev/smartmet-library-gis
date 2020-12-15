PROG = $(patsubst %.cpp,%,$(wildcard *Test.cpp))

REQUIRES = fmt gdal geos

include $(shell echo $${PREFIX-/usr})/share/smartmet/devel/makefile.inc

FLAGS = -std=$(CXX_STD) -Wall -W -Wno-unused-parameter

ifeq ($(TSAN), yes)
  FLAGS += -fsanitize=thread
endif
ifeq ($(ASAN), yes)
  FLAGS += -fsanitize=address -fsanitize=pointer-compare -fsanitize=pointer-subtract -fsanitize=undefined -fsanitize-address-use-after-scope
endif

CFLAGS = -DUNIX -DUSE_UNSTABLE_GEOS_CPP_API -O0 -g $(FLAGS) -Wno-write-strings

INCLUDES += -I../gis \

LIBS += ../libsmartmet-gis.so -lboost_filesystem -lboost_chrono $(REQUIRED_LIBS)

# Testdata directories
GIS_RASTERS_DIR = $(shell test -d /usr/share/smartmet/test/data/gis/rasters && echo /usr/share/smartmet/test/data/gis/rasters || echo /smartmet/share/gis/rasters )
GIS_VIEWFINDER = $(GIS_RASTERS_DIR)/viewfinder
GIS_GLOBCOVER = $(GIS_RASTERS_DIR)/globcover
GIS_SMALLTESTDATA = $(shell test -d /usr/share/smartmet/test/data/gis/rasters && echo 1 || echo 0)

all: $(PROG)
clean:
	rm -f $(PROG) *~

test: $(PROG)
	@echo Running tests:
	@echo
	@echo Using $(GIS_RASTERS_DIR) for rasterdata
	@rm -f *.err
	@for prog in $(PROG); do \
	  ( ./$$prog || touch $$prog.err ) ; \
	done
	@test `find . -name \*.err | wc -l` = "0" || ( echo ; echo "The following tests have errors:" ; \
		for i in *.err ; do echo `basename $$i .err`; done ; rm -f *.err ; false )

$(PROG) : % : %.cpp ../libsmartmet-gis.so Makefile TestDefs.h
	$(CXX) $(CFLAGS) -DGIS_VIEWFINDER=\"$(GIS_VIEWFINDER)\" -DGIS_GLOBCOVER=\"$(GIS_GLOBCOVER)\" -DGIS_SMALLTESTDATA=$(GIS_SMALLTESTDATA) -o $@ $@.cpp $(INCLUDES) $(LIBS)
