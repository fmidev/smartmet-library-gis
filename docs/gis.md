# SmartMet GIS Library

The `smartmet-library-gis` is a C++ geometric processing library used by [SmartMet Server](https://github.com/fmidev/smartmet-server), a high-capacity MetOcean data server developed at the Finnish Meteorological Institute (FMI). The library is built as a static library (`libsmartmet-gis.a`) and wraps GDAL/OGR and GEOS with additional algorithms for geographic data.

All public API lives under the `Fmi` namespace.

## Contents

- [Coordinate Systems and Transformations](gis-projection.md) — `SpatialReference`, `CoordinateTransformation`, `GeometryProjector`, `CoordinateMatrix`, `ProjInfo`, `EPSGInfo`
- [Clipping and Cutting](gis-clipping.md) — `OGR` namespace functions, `Box`, `Shape` hierarchy, `GeometryBuilder`, `GeometrySmoother`
- [Geographic Interrupts](gis-interrupts.md) — `Interrupt`, map projection discontinuities, antimeridian handling
- [Elevation and Land Cover](gis-raster.md) — `DEM`, `SrtmTile`, `SrtmMatrix`, `LandCover`
- [PostGIS Integration](gis-postgis.md) — `PostGIS`, `Host`

## Quick Start

```cpp
#include <gis/OGR.h>
#include <gis/Box.h>
#include <gis/SpatialReference.h>
#include <gis/CoordinateTransformation.h>

// Clip a line geometry to a rectangle
Fmi::Box box(0, 0, 1000, 800, 1000, 800);  // xmin,ymin,xmax,ymax,width,height
OGRGeometry* clipped = Fmi::OGR::lineclip(*geom, box);

// Transform a geometry from WGS84 to Finnish national grid
Fmi::SpatialReference wgs84(4326);
Fmi::SpatialReference etrs_tm35fin(3067);
Fmi::CoordinateTransformation ct(wgs84, etrs_tm35fin);
OGRGeometry* projected = ct.transformGeometry(*geom);

// Project with geographic densification and bounds clipping
Fmi::GeometryProjector projector(sourceSRS, targetSRS);
projector.setProjectedBounds(-3000000, -2000000, 3000000, 2000000);
projector.setDensifyResolutionKm(10.0);
auto result = projector.projectGeometry(geom);
```

## Dependencies

| Library | Purpose |
|---------|---------|
| GDAL/OGR | Geometry types, coordinate systems, data sources |
| GEOS | Geometry operations (union, buffer, etc.) |
| Boost | Utilities |
| MacGyver | Caching infrastructure |
| fmt | String formatting |

## Building

```bash
cmake -B build
cmake --build build
```

The library produces `libsmartmet-gis.a`.

## Key Design Patterns

**Geometry ownership.** Functions in `Fmi::OGR` that return `OGRGeometry*` transfer ownership to the caller. The caller must delete the pointer. Functions returning `std::unique_ptr<OGRGeometry>` (e.g. `GeometryProjector::projectGeometry`) manage ownership automatically.

**Coordinate system construction.** `Fmi::SpatialReference` accepts EPSG codes (integers), WKT strings, or PROJ strings, and caches parsed references internally.

**Clipping vs. cutting.** The library distinguishes two complementary operations throughout: *clip* keeps what is inside a region, *cut* removes what is inside a region (keeps the outside).
