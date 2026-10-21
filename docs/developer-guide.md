# gis developer guide

This guide is for developers who change `smartmet-library-gis`, or build on it. gis wraps
GDAL/OGR, GEOS and PROJ for the rest of SmartMet: spatial references and transformations,
clipping and cutting of geometries (used for contours and map layers), projection of
geometries with interrupts, DEM and land cover data, and PostGIS reading. newbase, the
contour, gis and querydata engines, and the WMS, WFS and EDR plugins depend on it. It also
resolves coordinates to time zones (`TimeZoneFinder`) and grids point observations with the
help of the terrain (`PointGridding`).

The topic documents describe each part in detail; this guide is the maintainer's view of how
they fit together and what to watch out for.

| Document | Covers |
|----------|--------|
| [gis.md](gis.md) | Overview and quick start |
| [gis-projection.md](gis-projection.md) | `SpatialReference`, `CoordinateTransformation`, `GeometryProjector`, `CoordinateMatrix`, `ProjInfo`, `EPSGInfo`, `Box`, `BBox` |
| [gis-clipping.md](gis-clipping.md) | `Fmi::OGR` clip/cut functions, shapes, builder, smoother, simplifier |
| [gis-amalgamator.md](gis-amalgamator.md) | Polygon amalgamation and its tuning |
| [gis-interrupts.md](gis-interrupts.md) | Projection discontinuities, antimeridian, poles |
| [gis-raster.md](gis-raster.md) | `DEM`, SRTM tiles, `LandCover` |
| [gis-postgis.md](gis-postgis.md) | `PostGIS`, `Host` |
| [gis-timezones.md](gis-timezones.md) | `TimeZoneFinder`: coordinate to IANA time zone from polygons |
| [gis-pointgridding.md](gis-pointgridding.md) | `PointGridding`: terrain-aware gridding of point observations |
| [proj-gdal-thread-safety.md](proj-gdal-thread-safety.md) | What PROJ and GDAL allow across threads |

[CLAUDE.md](../CLAUDE.md) has the build summary.

## Contents

1. [Building and testing](#1-building-and-testing)
2. [Spatial references](#2-spatial-references)
3. [Coordinate transformations](#3-coordinate-transformations)
4. [Geometries and ownership](#4-geometries-and-ownership)
5. [Threads](#5-threads)
6. [Compatibility](#6-compatibility)
7. [Known pitfalls](#7-known-pitfalls)

---

## 1. Building and testing

```bash
make
make test                        # test programs (tframe or Google Test) + ShapeTester scenarios
make -C test BoxTest && ./test/BoxTest
make -C test TSAN=yes test       # or ASAN=yes
```

GDAL comes from FMI's own package under `/usr/gdal312` (found through pkg-config); when
compiling small programs by hand, set `PKG_CONFIG_PATH=/usr/gdal312/lib/pkgconfig`.
`ShapeTester` runs the clipping scenarios in `test/tests/*.txt`: each file gives a shape, an
input geometry and the expected clip and cut results as WKT. Add a scenario there when
changing the clipper. The DEM and land cover tests need raster data; with only the small
test data set installed, some are skipped.

## 2. Spatial references

`Fmi::SpatialReference` is the type to pass around. It is created from an EPSG number,
`"EPSG:3067"`, a PROJ string, WKT, `"WGS84"`, or one of the datum and ellipsoid names that
`OGRSpatialReferenceFactory` knows (`FMI`, `NAD27`, `intl`, `GRS80`, …, each meaning a
geographic system on that datum).

Construction goes through one store, keyed by the description string (1000 entries,
`SpatialReference::setCacheSize()`):

1. The first use of a description parses it into a **master** `OGRSpatialReference`,
   which is never handed out. Parsing is serialised by the factory's mutex, since PROJ's
   database access is not safe to use concurrently.
2. The derived data (WKT, `ProjInfo`, hash, EPSG code, geographic and axis flags) is
   computed once, on a private clone, when first asked for, and shared by every
   `SpatialReference` made from the same string. Copies share it too.
3. `OGRSpatialReferenceFactory::Create(desc)` hands out a **fresh clone** that belongs
   to the caller. Each thread keeps its own sample of every description it uses, so the
   clones are made without a lock.

So creating the same `SpatialReference` again is cheap, and its accessors never call
GDAL. `get()`, `operator*` and the conversion operators return an `OGRSpatialReference`
private to that `SpatialReference` instance, cloned on first use. Creating a
`SpatialReference` from an `OGRSpatialReference` clones it and derives everything again
(about 1.7 ms instead of 0.2 µs), and is not cached: prefer the description string.

Every spatial reference is set to `OAMS_TRADITIONAL_GIS_ORDER`: coordinates are always
**x = longitude / easting, y = latitude / northing**, whatever the EPSG axis order.
`isAxisSwapped()` and `EPSGTreatsAsLatLong()` tell what the official order would be, for
output formats (WMS 1.3, WFS) that must follow it.

`getEPSG()` returns the code only if the top-level WKT node has an `AUTHORITY`; a system
made from a PROJ string has none. `hashValue()` is the hash of the WKT.

## 3. Coordinate transformations

`Fmi::CoordinateTransformation(source, target)` gets an `OGRCoordinateTransformation` from
`OGRCoordinateTransformationFactory`:

* The factory keeps a **pool** of idle transformations, keyed by the hash of the source
  and target definitions (at most 1600). `Create()` takes one out of the pool or creates a
  new one; the `Ptr` deleter puts it back when the owner is destroyed. A transformation is
  therefore used by one owner at a time.
* The transformation is created from the **WKT** of the two systems, which keeps the
  authority codes, so PROJ chooses the same operation as GDAL (EPSG:4326 → EPSG:27700
  matches GDAL exactly). The PROJ strings are used only if the WKT cannot be used.
* Copying a `CoordinateTransformation` gets another transformation from the factory.
* `transformGeometry()` handles the antimeridian and projection interrupts, and can
  densify segments; `GeometryProjector` adds clipping to projected bounds. See
  [gis-projection.md](gis-projection.md) and [gis-interrupts.md](gis-interrupts.md).

`CoordinateMatrix` holds the projected coordinates of a whole grid, so that a model grid
is projected once instead of point by point; `CoordinateMatrixCache` can cache them.

## 4. Geometries and ownership

* `Fmi::OGR` functions returning `OGRGeometry*` give ownership to the caller; functions
  returning `std::unique_ptr` or `OGRGeometryPtr` (`shared_ptr`) manage it themselves.
* *Clip* keeps what is inside a shape, *cut* what is outside. `lineclip`/`linecut` may turn
  polygons into polylines (for stroking), `polyclip`/`polycut` keep polygons.
* Clipping and cutting are done by gis's own `RectClipper` / `ShapeClipper`, not by GEOS;
  `GeometryBuilder` collects their output fragments into the result.
* `OGR::normalizeWindingOrder()` sets the RFC 7946 orientation (exterior
  counter-clockwise, holes clockwise), for GeoJSON and other formats that require it.

## 5. Threads

* A `SpatialReference` may be shared between threads for its accessors (WKT, PROJ
  string, EPSG code, flags), which only read the shared derived data. The
  `OGRSpatialReference` returned by `get()` or the conversion operators belongs to that
  one instance: modifying it does not affect other instances, but like any GDAL object it
  must be used by one thread at a time. Give each thread its own `SpatialReference` (a
  copy is cheap) when the threads need the GDAL object.
* A `CoordinateTransformation` must be used by **one thread at a time**; create one per
  thread (they are cheap to get from the pool).
* See [proj-gdal-thread-safety.md](proj-gdal-thread-safety.md) for the PROJ and GDAL
  rules behind this.

## 6. Compatibility

The installed headers (`$(includedir)/smartmet/gis/`) are used by newbase, several engines
and plugins. `SpatialReference` and `CoordinateTransformation` use a pimpl, so their
internals can change freely, but inline functions, templates (`CoordinateMatrix`,
`BoolMatrix`) and plain structs (`Box`, `BBox`, `Interrupt`) are compiled into the
dependants. A change there needs a spec version bump and rebuilt dependants.

The library supports GDAL 3.x and PROJ 9; code specific to older versions is still guarded
with `GDAL_VERSION_MAJOR` and `PROJ_VERSION_MAJOR` checks.

## 7. Known pitfalls

* **Axis order is always lon/lat (east/north)** in gis, even for EPSG:4326 (§2).
* **The `OGRSpatialReference` of one `SpatialReference` instance is not for concurrent
  use** (§5); copy the `SpatialReference` per thread.
* **One `CoordinateTransformation` per thread** (§5).
* **`getEPSG()` is empty for systems built from PROJ strings**, even when an EPSG code
  exists for the same definition.
* **The `FMI` datum is a sphere of radius 6 371 220 m**, the same as newbase's legacy
  projections (`kRearth`). Model data on the 6 371 229 m sphere must name it with `+R`.
* **Raw `OGRGeometry*` results must be deleted** by the caller (§4).
