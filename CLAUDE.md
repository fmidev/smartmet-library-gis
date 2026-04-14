# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

`smartmet-library-gis` — a C++ GIS library for the SmartMet Server ecosystem. Wraps GDAL/OGR, GEOS, and PROJ with algorithms for coordinate projection, geometry clipping/cutting, DEM/land-cover raster access, and PostGIS integration. Produces `libsmartmet-gis.so`. All public API is in the `Fmi` namespace.

## Build commands

```bash
make                # Build libsmartmet-gis.so
make test           # Build and run all tests (cd test && make test)
make format         # clang-format all source and test files
make clean          # Clean build artifacts
make rpm            # Build RPM package
make install        # Install headers to $(includedir)/smartmet/gis/, library to $(libdir)
```

### Building a single test

```bash
cd test
make BoxTest        # Compile one test (any *Test.cpp → executable)
./BoxTest           # Run it
```

Tests use **Google Test** (gtest), not Boost.Test. Each `*Test.cpp` compiles to a standalone executable. Additionally, `ShapeTester` is a text-driven test runner that processes `test/tests/*.txt` files (line/polygon clip/cut scenarios for rect, circle, sphere shapes).

### Test data

DEM and land-cover tests require raster data at paths defined by `GIS_VIEWFINDER` and `GIS_GLOBCOVER` macros (set in `test/Makefile`). The test Makefile auto-detects whether data exists at `/usr/share/smartmet/test/data/gis/rasters` or `/smartmet/share/gis/rasters` and sets `GIS_SMALLTESTDATA=1` when only a small dataset is available (some DEM/LandCover tests are skipped).

### Sanitizers

```bash
make -C test TSAN=yes test   # Thread sanitizer
make -C test ASAN=yes test   # Address + UB sanitizer
```

## Dependencies

`REQUIRES = fmt gdal geos proj sqlite3` (resolved via pkg-config). Also links `libsmartmet-macgyver` (from sibling `macgyver/` repo), Boost, and double-conversion.

## Architecture

### Source layout

- `gis/` — all `.cpp` and `.h` source files (flat, no subdirectories)
- `test/` — test executables and `tests/` subdirectory with `.txt` test scenarios
- `docs/` — detailed documentation on projections, clipping, interrupts, rasters, PostGIS

### Key modules

**Coordinate systems & projection:**
- `SpatialReference` — proxy for `OGRSpatialReference`; accepts EPSG int, WKT string, or PROJ string; internally cached
- `CoordinateTransformation` — wraps `OGRCoordinateTransformation` with antimeridian-aware `transformGeometry()`
- `GeometryProjector` — high-level project + densify + clip-to-bounds in one step
- `CoordinateMatrix` / `CoordinateMatrixCache` — grid-based coordinate transformation with caching
- `OGRCoordinateTransformationFactory`, `OGRSpatialReferenceFactory` — cached factories
- `ProjInfo`, `EPSGInfo` — PROJ string parsing and EPSG metadata

**Geometry clipping/cutting (`Fmi::OGR` namespace):**
- `lineclip` / `linecut` — clip/cut geometries to rectangle or shape (polygons may break into polylines)
- `polyclip` / `polycut` — clip/cut preserving polygon type
- Shape hierarchy: `Shape` (base) → `Shape_rect`, `Shape_circle`, `Shape_sphere`
- `Box` — rectangle in projected coords with pixel-coordinate transform
- `ShapeClipper`, `RectClipper` — internal clipping algorithm implementations
- `GeometryBuilder` — collects clipping output fragments
- `GeometrySmoother` — Douglas-Peucker-style smoothing

**Raster data:**
- `DEM` — Digital Elevation Model queries (SRTM tiles)
- `SrtmTile` / `SrtmMatrix` — SRTM tile loading and interpolation
- `LandCover` — GlobCover land classification lookups

**Database:**
- `PostGIS` — reads features from PostGIS with optional spatial/time filters
- `Host` — PostGIS connection parameters

**Utilities:**
- `GEOS` — GEOS geometry helpers and SVG export
- `Interrupt` — antimeridian/map-projection discontinuity handling
- `BoolMatrix` — 2D boolean grid
- `VertexCounter` — geometry vertex counting

### Geometry ownership convention

Functions returning raw `OGRGeometry*` transfer ownership to the caller (caller must delete). Functions returning `std::unique_ptr<OGRGeometry>` manage ownership automatically. Shared types: `OGRGeometryPtr = std::shared_ptr<OGRGeometry>`, `GeometryPtr = std::shared_ptr<geos::geom::Geometry>`.

### Clip vs. cut distinction

Throughout the codebase, *clip* keeps geometry **inside** the region, *cut* keeps geometry **outside** the region (removes the interior).

## Code style

Defined in `.clang-format`: Google-based, Allman braces, 100-column limit. Run `make format` before committing.

## CI

CircleCI builds and tests RPMs on RHEL 8 and RHEL 10 (`fmidev/smartmet-cibase-{8,10}` Docker images). The workflow: `build → test → upload` for each target.
