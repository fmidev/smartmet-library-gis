# Clipping and Cutting

## Overview

The library provides two complementary operations:

- **clip** — keep what is *inside* a region; discard what is outside.
- **cut** — keep what is *outside* a region; discard what is inside.

Each is available in two flavours:

- **lineclip / linecut** — polygons may break into linestrings along the region boundary.
- **polyclip / polycut** — polygons remain polygons; new edges are added along the boundary.

All functions are in `Fmi::OGR` (`#include <gis/OGR.h>`).

---

## Box-Based Operations

The simplest form clips or cuts to an axis-aligned rectangle defined by a `Fmi::Box`.

```cpp
#include <gis/OGR.h>
#include <gis/Box.h>

Fmi::Box box(xmin, ymin, xmax, ymax);  // identity-pixel box for clipping only

// Lines and polygons → linestrings
OGRGeometry* result = Fmi::OGR::lineclip(*geom, box);
OGRGeometry* result = Fmi::OGR::linecut(*geom, box);

// Lines and polygons → polygons (new edges added along box boundary)
OGRGeometry* result = Fmi::OGR::polyclip(*geom, box);
OGRGeometry* result = Fmi::OGR::polycut(*geom, box);

// Optional maximum segment length along box edges (prevents very long new edges)
OGRGeometry* result = Fmi::OGR::polyclip(*geom, box, maxSegmentLength);
OGRGeometry* result = Fmi::OGR::polycut(*geom, box, maxSegmentLength);
```

The returned pointer is owned by the caller and must be deleted.

---

## Shape-Based Operations

For non-rectangular regions use a `Shape_sptr` (a `std::shared_ptr<Fmi::Shape>`). The same clip/cut/lineclip/polyclip API applies.

```cpp
#include <gis/OGR.h>
#include <gis/Shape_rect.h>
#include <gis/Shape_circle.h>
#include <gis/Shape_sphere.h>

// Rectangular shape
auto rect = std::make_shared<Fmi::Shape_rect>(x1, y1, x2, y2);

// Circular shape (center x, center y, radius)
auto circle = std::make_shared<Fmi::Shape_circle>(cx, cy, radius);

// Spherical shape (geographic center lon, lat; radius in degrees)
auto sphere = std::make_shared<Fmi::Shape_sphere>(lon, lat, radius);

Fmi::Shape_sptr shape = rect;  // or circle / sphere

OGRGeometry* result = Fmi::OGR::lineclip(*geom, shape);
OGRGeometry* result = Fmi::OGR::linecut(*geom, shape);
OGRGeometry* result = Fmi::OGR::polyclip(*geom, shape, maxSegmentLength);
OGRGeometry* result = Fmi::OGR::polycut(*geom, shape, maxSegmentLength);
```

### Shape_rect

Cohen-Sutherland line clipping extended to polygons. Handles all nine position cases for line segments (in-in, in-edge, in-out, edge-in, edge-edge, edge-out, out-in, out-edge, out-out).

```cpp
auto shape = std::make_shared<Fmi::Shape_rect>(x1, y1, x2, y2);
```

### Shape_circle

Angle-parametrised circular region in projected coordinates. Uses distance from centre for inside/outside tests. The border step (default π/180 radians) controls the angular resolution of the ring that is added when a polygon is closed along the circle boundary.

```cpp
auto shape = std::make_shared<Fmi::Shape_circle>(centerX, centerY, radius);
```

### Shape_sphere

Geographic sphere defined by a centre in WGS84 lon/lat and a radius in degrees. Internally works in azimuthal equidistant projection to perform Euclidean distance calculations; converts back to geographic coordinates for output.

```cpp
// Centre at Helsinki (25°E, 60°N), radius 500 km ≈ 4.5°
auto shape = std::make_shared<Fmi::Shape_sphere>(25.0, 60.0, 4.5);
```

Use `Shape_sphere` instead of `Shape_circle` when the input geometries are in geographic (lat/lon) coordinates, because a circle in projected space is not a circle on the sphere.

---

## GeometryBuilder

`#include <gis/GeometryBuilder.h>`

Accumulates polygons, linestrings and points and builds a minimal well-typed OGR geometry from them. Useful when assembling clip/cut results piece by piece.

```cpp
Fmi::GeometryBuilder builder;
builder.add(polygon);     // takes ownership
builder.add(linestring);
builder.add(point);

OGRGeometry* result = builder.build();  // returns minimal geometry; caller owns it
```

The output type is:
- `OGRPolygon` or `OGRMultiPolygon` if only polygons were added.
- `OGRLineString` or `OGRMultiLineString` if only linestrings were added.
- `OGRPoint` or `OGRMultiPoint` if only points were added.
- `OGRGeometryCollection` for mixed types.

You can pass a `GeometryBuilder` directly to the builder-accepting overloads:

```cpp
Fmi::GeometryBuilder builder;
Fmi::OGR::polyclip(builder, *geom, shape, maxSegmentLength);
Fmi::OGR::polycut(builder, *geom, shape, maxSegmentLength);
OGRGeometry* result = builder.build();
```

---

## Utility Functions

### despeckle

Removes small polygons below an area threshold. Useful after clipping to discard tiny slivers.

```cpp
OGRGeometry* clean = Fmi::OGR::despeckle(*geom, areaLimit);
```

### normalize / normalizeWindingOrder

```cpp
// Normalize ring vertices to lexicographic order (deterministic for testing)
Fmi::OGR::normalize(polygon);
Fmi::OGR::normalize(ring);

// Set winding order to RFC 7946 (exterior CCW, holes CW)
Fmi::OGR::normalizeWindingOrder(geom);
```

### inside

Point-in-polygon test.

```cpp
bool hit = Fmi::OGR::inside(*geom, x, y);
bool hit = Fmi::OGR::inside(polygon, x, y);
```

### constructGeometry

Build an OGR geometry from a list of coordinates.

```cpp
using CoordinatePoints = std::list<std::pair<double, double>>;
CoordinatePoints pts = {{25.0, 60.0}, {26.0, 61.0}};

// wkbPoint, wkbLineString, wkbLinearRing, wkbPolygon
OGRGeometry* line = Fmi::OGR::constructGeometry(pts, wkbLineString, 4326);
```

### expandGeometry

Buffer a geometry outward by a distance in metres.

```cpp
OGRGeometry* buffered = Fmi::OGR::expandGeometry(geom, 1000.0);  // 1 km buffer
```

### Geometry I/O

```cpp
// WKT export
std::string wkt = Fmi::OGR::exportToWkt(*geom);
std::string wkt = Fmi::OGR::exportToWkt(*geom, 6);  // 6 decimal places
std::string wkt = Fmi::OGR::exportToWkt(srs);        // from spatial reference
std::string proj = Fmi::OGR::exportToProj(srs);      // PROJ string

// SVG export (maps geometry to box pixel coordinates)
std::string svg = Fmi::OGR::exportToSvg(*geom, box, precision);

// WKT import
OGRGeometry* geom = Fmi::OGR::createFromWkt(wktString);
OGRGeometry* geom = Fmi::OGR::createFromWkt(wktString, 4326);  // with EPSG

// Geometry translation (shift by dx, dy)
Fmi::OGR::translate(*geom, dx, dy);

// Transform to pixel coordinates using Box
Fmi::OGR::transform(*geom, box);

// Import from GEOS
OGRGeometry* ogr = Fmi::OGR::importFromGeos(*geosGeom, srs);
```

---

## GeometrySmoother

`#include <gis/GeometrySmoother.h>`

Applies a weighted moving-average smoothing filter to geometries. Designed for smoothing isolines and isoband boundaries before rendering.

```cpp
Fmi::GeometrySmoother smoother;
smoother.type(Fmi::GeometrySmoother::Type::Gaussian);
smoother.radius(3.0);       // in pixels
smoother.iterations(2);     // number of passes
smoother.bbox(box);         // optional: constrain output to box

std::vector<OGRGeometryPtr> geoms = { ... };
smoother.apply(geoms, /*preserve_topology=*/true);
```

### Smoothing Types

| Type | Weight Function |
|------|----------------|
| `None` | No smoothing |
| `Average` | 1 (uniform) |
| `Linear` | 1 / (1 + distance) |
| `Gaussian` | Gaussian, σ derived from radius |
| `Tukey` | (1 − (d/r)²)² biweight |

`preserve_topology` prevents self-intersections from being introduced during smoothing.

---

## The Shape Abstraction

`#include <gis/Shape.h>`

All clip/cut shapes inherit from `Fmi::Shape`. The key virtual methods are:

```cpp
// Clip/cut a linestring, appending results to theClipper
int clip(const OGRLineString*, ShapeClipper&, bool exterior) const;
int cut(const OGRLineString*, ShapeClipper&, bool exterior) const;

// Connect two boundary points clockwise / counter-clockwise around the shape edge
bool connectPoints_cw(OGRLinearRing&, x1, y1, x2, y2, maxSegLen) const;
bool connectPoints_ccw(OGRLinearRing&, x1, y1, x2, y2, maxSegLen) const;

// Position of a point relative to the shape
int getPosition(double x, double y) const;

// Ring containment tests
bool isInsideRing(const OGRLinearRing&) const;
bool isRingInside(const OGRLinearRing&) const;

// Geometry factories used when a shape-boundary ring is needed
OGRLinearRing* makeRing(double maxSegLen) const;
OGRLinearRing* makeHole(double maxSegLen) const;
```

To implement a custom clipping shape, subclass `Fmi::Shape` and implement these methods. Pass the shape via `std::make_shared<MyShape>(...)` to any `lineclip`/`polyclip`/`linecut`/`polycut` overload.

Position constants mirror those in `Box`:

```
Shape::Inside, Outside, Left, Top, Right, Bottom,
TopLeft, TopRight, BottomLeft, BottomRight
```

---

## VertexCounter

`#include <gis/VertexCounter.h>`

Counts how many times each vertex coordinate appears across a set of geometries. Useful for detecting shared vertices in topology operations.

```cpp
Fmi::VertexCounter counter;
counter.add(geom1);
counter.add(geom2);

OGRPoint pt;
pt.setX(25.0); pt.setY(60.0);
int n = counter.getCount(pt);  // 0 if not present
```

---

## GEOS Namespace

`#include <gis/GEOS.h>`

Exports GEOS geometries (from `geos::geom::Geometry`) to WKB or SVG.

```cpp
std::string wkb = Fmi::GEOS::exportToWkb(*geosGeom);
std::string svg = Fmi::GEOS::exportToSvg(*geosGeom, /*precision=*/6);
```

Use `Fmi::OGR::importFromGeos` to convert a GEOS geometry to an OGR geometry.

---

## Internal: Ring Reconnection

After clipping, the library may produce a set of open linestrings that together form closed rings. The reconnection logic merges linestrings whose endpoints match, and converts closed linestrings into `OGRLinearRing`. This is transparent to the caller but important to understand when debugging unexpected geometry types in output.

The `ShapeClipper` container accumulates rings and open linestrings separately; `GeometryBuilder` then assembles them into the minimal output type.
