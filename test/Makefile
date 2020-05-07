PROG = $(patsubst %.cpp,%,$(wildcard *Test.cpp))

-include $(HOME)/.smartmet.mk
GCC_DIAG_COLOR ?= always
CXX_STD ?= c++11

FLAGS = -std=$(CXX_STD) -Wall -W -Wno-unused-parameter

EXTRAFLAGS = -Werror -pedantic -Wpointer-arith -Wcast-qual \
	-Wcast-align -Wconversion -Winline \
	-Wctor-dtor-privacy -Wnon-virtual-dtor -Wno-pmf-conversions \
	-Wsign-promo -Wchar-subscripts -Wold-style-cast \
	-Wshadow -Woverloaded-virtual

DIFFICULTFLAGS = -Weffc++ -Wunreachable-code -Wredundant-decls

ifeq ($(TSAN), yes)
  FLAGS += -fsanitize=thread
endif
ifeq ($(ASAN), yes)
  FLAGS += -fsanitize=address -fsanitize=pointer-compare -fsanitize=pointer-subtract -fsanitize=undefined -fsanitize-address-use-after-scope
endif

CXX = g++
CFLAGS = -DUNIX -O0 -g $(FLAGS) -Wno-write-strings

# Boost 1.69

ifneq "$(wildcard /usr/include/boost169)" ""
  INCLUDES += -I/usr/include/boost169
  LIBS += -L/usr/lib64/boost169
endif

ifneq "$(wildcard /usr/gdal30/include)" ""
  INCLUDES += -I/usr/gdal30/include
  LIBS += -L$(PREFIX)/gdal30/lib
else
  INCLUDES += -I/usr/include/gdal
endif


INCLUDES += \
	-I../gis \
	-I/usr/include/smartmet

LIBS += ../libsmartmet-gis.so -lboost_filesystem -lgeos -lgdal -lfmt

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
	@echo Package contents:
	@rpm -ql smartmet-test-data
	@rm -f *.err
	@for prog in $(PROG); do \
	  ( ./$$prog || touch $$prog.err ) ; \
	done
	@test `find . -name \*.err | wc -l` = "0" || ( echo ; echo "The following tests have errors:" ; \
		for i in *.err ; do echo `basename $$i .err`; done ; rm -f *.err ; false )

$(PROG) : % : %.cpp ../libsmartmet-gis.so Makefile TestDefs.h
	$(CXX) $(CFLAGS) -DGIS_VIEWFINDER=\"$(GIS_VIEWFINDER)\" -DGIS_GLOBCOVER=\"$(GIS_GLOBCOVER)\" -DGIS_SMALLTESTDATA=$(GIS_SMALLTESTDATA) -o $@ $@.cpp $(INCLUDES) $(LIBS)
