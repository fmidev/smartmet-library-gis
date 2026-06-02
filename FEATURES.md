# smartmet-library-gis — Feature List

A structured inventory of capabilities provided by the gis library.
Use as a checklist when drafting release notes. When new functionality
is added, append the new entry under the matching section (and bump
the *Last updated* line at the bottom).

`smartmet-library-gis` is the SmartMet Server's GIS workhorse — a
C++ wrapper around GDAL/OGR, GEOS, and PROJ that provides coordinate
projection, geometry clipping/cutting/smoothing/simplification,
DEM and land-cover raster access, and PostGIS integration. All public
API is in the `Fmi::` namespace; the library produces both shared
(`libsmartmet-gis.so`) and static (`libsmartmet-gis.a`) artefacts.

---

## 1. Coordinate reference systems

- **`Fmi::SpatialReference`** — proxy for `OGRSpatialReference`.
  Accepts:
  - **EPSG codes** (integer).
  - **WKT strings**.
  - **PROJ strings**.
  - **Cached internally** — repeated construction with the same
    descriptor returns the same backing object.
- **`Fmi::OGRSpatialReferenceFactory`** — cached factory for
  building `OGRSpatialReference` objects.
- **`Fmi::ProjInfo`** — PROJ-string parsing helpers.
- **`Fmi::EPSGInfo`** — EPSG metadata lookup (units, name, axis
  order).

## 2. Coordinate transformations

- **`Fmi::CoordinateTransformation`** — wraps
  `OGRCoordinateTransformation` with:
  - **Antimeridian-aware** `transformGeometry()`.
  - Single-point transforms and batched transforms.
- **`Fmi::OGRCoordinateTransformationFactory`** — cached
  source-to-target transform factory.
- **`Fmi::BilinearCoordinateTransformation`** — fast bilinear
  approximation built from a sample grid; useful for repeated
  same-projection lookups.
- **`Fmi::CoordinateMatrix`** — grid-based coordinate transformation
  result (per-cell transformed lat/lon).
- **`Fmi::CoordinateMatrixCache`** — caches `CoordinateMatrix`
  results across requests.
- **`Fmi::CoordinateMatrixAnalysis`** — analyses a coordinate matrix
  for properties like wraparound, discontinuities, validity.

## 3. Geometry clipping & cutting

The `Fmi::OGR` family of operations. The library distinguishes:

- **clip** — keep geometry **inside** the region.
- **cut** — keep geometry **outside** the region (i.e. remove the
  interior).

Public operations:

- **`lineclip` / `linecut`** — clip / cut line and polygon
  geometries to a rectangle or shape; polygons may break into
  polylines.
- **`polyclip` / `polycut`** — clip / cut while preserving polygon
  topology.
- **`shapeClip`** — clip with an arbitrary closed shape.
- **`OGR-clip`**, **`OGR-shapeClip`** — implementation files.
- **Internal classes**: `ShapeClipper`, `RectClipper`.

## 4. Shape primitives

The shape hierarchy used by clip/cut and friends:

- **`Shape`** — abstract base.
- **`Shape_rect`** — axis-aligned rectangle in projected coords.
- **`Shape_circle`** — circle.
- **`Shape_sphere`** — sphere on the globe.
- **`Box`** — projected rectangle with pixel-coordinate transform
  (used for rasterisation).
- **`BBox`** — lat/lon bounding box.

## 5. Geometry construction & manipulation

- **`Fmi::GeometryBuilder`** — accumulator that builds final
  `OGRGeometry` outputs from clipping fragments.
- **`Fmi::GeometryProjector`** — high-level "project + densify +
  clip-to-bounds" pipeline in one call.
- **`Fmi::GeometrySmoother`** — weighted moving-average smoothing of
  line / polygon vertices.
- **`Fmi::GeometrySimplifier`** — vertex reduction with topology
  preservation:
  - **Douglas-Peucker** algorithm.
  - **Visvalingam-Whyatt** algorithm.
- **`Fmi::GeometryAmalgamator`** — merge nearby polygons via
  **Constrained Delaunay Triangulation** (CDT is included as
  `gis/CDT/`).
- **`OGR-normalize.cpp`** — geometry normalisation (snap rounding,
  self-intersection cleanup).
- **`OGR-despeckle.cpp`** — remove polygon "speckles" (very small
  parts).
- **`OGR-compactness.cpp`** — compactness measurement.
- **`OGR-inside.cpp`** — point-in-geometry tests.
- **`OGR-transform.cpp`** — geometry-level transforms.

## 6. Antimeridian / interrupts

- **`Fmi::Interrupt`** — detect and handle projection
  discontinuities (e.g. the antimeridian).
- **Interrupt-aware** transforms — geometries that cross the
  antimeridian are split into valid sub-geometries.
- **`docs/gis-interrupts.md`** — full design notes.

## 7. SVG export

- **`OGR-exportToSvg.cpp`** — render `OGRGeometry` to SVG path
  strings.
- **`GEOS-exportToSvg.cpp`** — render `geos::geom::Geometry` to SVG.
- **`Fmi::GEOS`** — GEOS-side geometry helpers used by the WMS and
  cross-section plugins.

## 8. Digital Elevation Model

- **`Fmi::DEM`** — high-level DEM-query interface (elevation by
  lat/lon).
- **`Fmi::SrtmTile`** — single SRTM tile loader.
- **`Fmi::SrtmMatrix`** — multi-tile mosaic with interpolation.
- **Configurable resolution** — different DEM tile sets supported.

## 9. Land cover

- **`Fmi::LandCover`** — GlobCover land-classification lookup by
  lat/lon (used together with the DEM for landscape-aware
  temperature interpolation).
- **`Fmi::Gshhs`** — GSHHS (Global Self-consistent Hierarchical
  High-resolution Geography) shoreline data access.

## 10. PostGIS integration

- **`Fmi::PostGIS`** — read features from PostGIS:
  - **Spatial filters** (bbox, polygon).
  - **Time filters** (start/end).
  - **Attribute filters** (WHERE clause).
  - **CRS reprojection** at read time.
- **`Fmi::Host`** — PostgreSQL connection parameters.

## 11. Utilities

- **`Fmi::BoolMatrix`** — 2-D boolean grid (used for mask
  computation).
- **`Fmi::VertexCounter`** — count vertices in a geometry tree.
- **Ownership convention**:
  - Functions returning raw `OGRGeometry*` transfer ownership;
    callers must `delete`.
  - Functions returning `std::unique_ptr<OGRGeometry>` are
    self-managing.
  - Type aliases: `OGRGeometryPtr = std::shared_ptr<OGRGeometry>`,
    `GeometryPtr = std::shared_ptr<geos::geom::Geometry>`.

## 12. Documentation

Detailed documentation under `docs/`:

- **`gis.md`** — overview and quick start.
- **`gis-projection.md`** — coordinate projections.
- **`gis-clipping.md`** — geometry clipping.
- **`gis-interrupts.md`** — antimeridian handling.
- **`gis-raster.md`** — raster / DEM access.
- **`gis-postgis.md`** — PostGIS reads.
- **`gis-amalgamator.md`** — polygon amalgamation and
  simplification.

## 13. Testing

- **Google Test** (gtest) framework; one binary per `*Test.cpp`.
- **Per-test build**:
  `cd test && make BoxTest && ./BoxTest`.
- **`ShapeTester`** — text-driven runner that processes
  `test/tests/*.txt` (line / polygon clip and cut scenarios for
  rect, circle, and sphere shapes).
- **Sanitiser builds**:
  - `make -C test ASAN=yes test` — address + UB sanitiser.
  - `make -C test TSAN=yes test` — thread sanitiser.
- **Test data paths** — `GIS_VIEWFINDER` and `GIS_GLOBCOVER` macros
  point at DEM and land-cover rasters. The Makefile auto-detects
  large vs small datasets and sets `GIS_SMALLTESTDATA=1` when only
  the smaller test bundle is present (skipping the heavy
  DEM/LandCover tests).

## 14. Build & integration

- **Library outputs**: `libsmartmet-gis.so` (shared) and
  `libsmartmet-gis.a` (static).
- **CMake support**: `CMakeLists.txt` alongside the hand-written
  `Makefile`.
- **Build**: `make`.
- **Format**: `make format` runs clang-format (Google-based, Allman
  braces, 100-col).
- **Install**: `make install` — headers under
  `$(includedir)/smartmet/gis/`, library under `$(libdir)`.
- **RPM**: `make rpm`.
- **pkg-config requirements**: `fmt`, `gdal`, `geos`, `proj`,
  `sqlite3`.
- **Linked SmartMet libraries**: `smartmet-library-macgyver`.
- **External libraries**: GDAL/OGR, GEOS, PROJ, SQLite, Boost,
  double-conversion.
- **CI**: CircleCI on RHEL 8 / RHEL 10 with the
  `fmidev/smartmet-cibase-{8,10}` Docker images (build → test →
  upload workflow).
- **Public headers** installed under `/usr/include/smartmet/gis/`.

---

*Last updated: 2026-06-01.*
