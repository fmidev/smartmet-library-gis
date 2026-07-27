# Standalone reproducers for the PROJ / GDAL thread-safety analysis in
# ../proj-gdal-thread-safety.md
#
# These deliberately do NOT use the SmartMet build system: they must be
# buildable against any PROJ / GDAL pair so results can be compared across
# versions and reported upstream.
#
#   make                      # build all
#   make run                  # build and run the full matrix
#   make PROJ_PREFIX=/usr/proj97 GDAL_PREFIX=/usr/gdal312
#
# proj_race and ctx_cost need PROJ only; the srs_* programs need GDAL too.

PROJ_PREFIX ?= /usr/proj97
GDAL_PREFIX ?= /usr/gdal312

PROJ_CFLAGS := -I$(PROJ_PREFIX)/include
PROJ_LIBS   := -L$(PROJ_PREFIX)/lib64 -L$(PROJ_PREFIX)/lib -lproj
GDAL_CFLAGS := -I$(GDAL_PREFIX)/include
GDAL_LIBS   := -L$(GDAL_PREFIX)/lib -L$(GDAL_PREFIX)/lib64 -lgdal

CXX      ?= g++
CXXFLAGS ?= -std=c++17 -g -O1 -fno-omit-frame-pointer -Wall

PROJ_ONLY := proj_race ctx_cost
WITH_GDAL := srs_race srs_share srs_scale
PROGS     := $(PROJ_ONLY) $(WITH_GDAL)

all: $(PROGS)

$(PROJ_ONLY): %: %.cpp
	$(CXX) $(CXXFLAGS) $(PROJ_CFLAGS) $< -o $@ $(PROJ_LIBS) -pthread

$(WITH_GDAL): %: %.cpp
	$(CXX) $(CXXFLAGS) $(GDAL_CFLAGS) $(PROJ_CFLAGS) $< -o $@ $(GDAL_LIBS) $(PROJ_LIBS) -pthread

# P1 and P4a are expected to abort; keep going regardless of exit status.
run: all
	@echo "=== P1  shared PJ, concurrent proj_as_wkt() ==================="
	-./proj_race 1 4 4
	@echo "=== P4a shared default context, proj_create(nullptr, ...) ====="
	-./proj_race 2 5 4
	@echo "=== G2  shared OGRSpatialReference, getter mix ================"
	-./srs_race 0 5
	-./srs_race 1 5
	@echo "=== G2  shared OGRSpatialReference + CT creation =============="
	-./srs_share 0 5 4
	-./srs_share 1 5 4
	@echo "=== G6  read scaling: private clone vs shared object =========="
	@for n in 1 2 4 8; do ./srs_scale percpu $$n 3; ./srs_scale shared $$n 3; done
	@echo "=== per-thread PJ_CONTEXT resident cost ======================="
	@for n in 1 4 8 16 24; do ./ctx_cost $$n | tail -1; done

clean:
	rm -f $(PROGS)

.PHONY: all run clean
