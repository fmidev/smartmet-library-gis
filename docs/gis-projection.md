# Coordinate Systems and Transformations

## SpatialReference

`#include <gis/SpatialReference.h>`

A lightweight proxy around `OGRSpatialReference` that adds caching and convenience constructors.

```cpp
// Construction
Fmi::SpatialReference sr1(4326);                  // EPSG code
Fmi::SpatialReference sr2("EPSG:3067");           // PROJ/WKT string
Fmi::SpatialReference sr3(wktString);             // WKT string
Fmi::SpatialReference sr4(ogrSpatialRef);         // From existing OGR object
```

Key methods:

```cpp
bool isGeographic() const;         // True if CRS uses degrees (lat/lon)
bool isAxisSwapped() const;        // True if axis order is lat/lon instead of x/y
bool EPSGTreatsAsLatLong() const;  // True if EPSG definition has lat before lon
std::optional<int> getEPSG() const; // Returns EPSG code if known

const ProjInfo& projInfo() const;  // Parsed PROJ string parameters
const std::string& WKT() const;    // WKT representation
const std::string& projStr() const; // PROJ string (debug)

// Cache control
static void setCacheSize(std::size_t newMaxSize);
static Cache::CacheStats getCacheStats();
```

The internal cache avoids repeatedly parsing the same CRS definition. Call `setCacheSize` early in application initialization if the default is insufficient for the number of distinct CRS in use.

---

## CoordinateTransformation

`#include <gis/CoordinateTransformation.h>`

Wraps `OGRCoordinateTransformation` with an intelligent `transformGeometry` method that handles antimeridian crossing and uses `GeometryProjector` for densification.

```cpp
Fmi::SpatialReference wgs84(4326);
Fmi::SpatialReference laea("EPSG:3035");
Fmi::CoordinateTransformation ct(wgs84, laea);

// Transform individual coordinates
double x = 25.0, y = 60.0;
ct.transform(x, y);  // returns false on failure

// Transform vectors of coordinates
std::vector<double> xs = {25.0, 26.0};
std::vector<double> ys = {60.0, 61.0};
ct.transform(xs, ys);

// Transform an OGR geometry in-place
ct.transform(*geom);

// Intelligent transform: handles antimeridian and densifies segments
OGRGeometry* projected = ct.transformGeometry(*geom);

// Intelligent transform with explicit densification control
Fmi::GeometryProjector projector(sourceSRS, targetSRS);
OGRGeometry* projected2 = ct.transformGeometry(*geom, projector);

// Accessors
const Fmi::SpatialReference& src = ct.getSourceCS();
const Fmi::SpatialReference& dst = ct.getTargetCS();
```

---

## GeometryProjector

`#include <gis/GeometryProjector.h>`

Projects geometries from one CRS to another with geographic densification and projected-bounds clipping. This is the preferred way to project geometries when rendering maps, as it avoids visual artifacts from straight-line segments crossing discontinuities.

```cpp
Fmi::GeometryProjector projector(sourceSRS, targetSRS);

// Set the valid area in projected coordinates (used for clipping)
projector.setProjectedBounds(-3000000, -2000000, 3000000, 2000000);

// Geographic densification: split long segments before projecting.
// Default is 50 km. Set to <=0 to disable.
projector.setDensifyResolutionKm(10.0);

// Jump threshold: discard projected segments longer than this
// to suppress wrap-around artifacts near projection singularities.
projector.setJumpThreshold(1e6);

// Project (and clip to bounds if set). Returns nullptr only if input is nullptr.
std::unique_ptr<OGRGeometry> result = projector.projectGeometry(geom);
```

### How Geographic Densification Works

When the source CRS is geographic (lat/lon), long line segments are split into shorter pieces before projection. This is important because straight lines in geographic coordinates become curves in most projected coordinate systems; without densification, polygons can bulge the wrong way or appear to wrap around the map.

The segment length is estimated using approximate meters per degree:

```
dx = (lon2 - lon1) * cos(midLat * π/180) * 111320
dy = (lat2 - lat1) * 111320
length = sqrt(dx² + dy²)
```

Segments longer than `densifyResolutionKm * 1000` metres are split into equal sub-segments.

### Jump Threshold

Near the edges of some projections (e.g. the back hemisphere of a perspective projection) coordinates jump discontinuously. Setting a jump threshold causes projected segments longer than the threshold to be discarded, which prevents visible lines from crossing the entire map.

---

## CoordinateMatrix

`#include <gis/CoordinateMatrix.h>`

A 2-D grid of (x, y) coordinate pairs. Used to hold the projected positions of a regular grid of geographic points, which is the backbone of contour and raster rendering.

```cpp
// Create a rectilinear grid covering a bounding box
Fmi::CoordinateMatrix grid(nx, ny, x1, y1, x2, y2);

// Access individual coordinates
double x = grid.x(i, j);
double y = grid.y(i, j);
auto [px, py] = grid(i, j);

// Set a coordinate
grid.set(i, j, px, py);

// Project all coordinates in-place
Fmi::CoordinateTransformation ct(wgs84, targetSRS);
grid.transform(ct);
```

Always uses lon/lat (x/y) ordering during `transform`, regardless of CRS axis order.

### CoordinateMatrixAnalysis

`#include <gis/CoordinateMatrixAnalysis.h>`

Analyzes a projected coordinate grid for properties useful in contouring and rendering:

- Whether the grid is valid (no missing values)
- Whether the grid has clockwise orientation
- Whether it wraps horizontally (useful for global grids)

### CoordinateMatrixCache

`#include <gis/CoordinateMatrixCache.h>`

Caches projected coordinate matrices keyed by grid dimensions and bounding box to avoid recomputing projections for identical inputs.

---

## ProjInfo

`#include <gis/ProjInfo.h>`

Parses a PROJ string and provides typed access to its parameters.

```cpp
Fmi::ProjInfo info("+proj=lcc +lat_1=60 +lat_2=55 +lon_0=15 +ellps=GRS80");

std::optional<double> lat1 = info.getDouble("lat_1");  // => 60.0
std::optional<std::string> ellps = info.getString("ellps");  // => "GRS80"
bool hasOver = info.getBool("over");

// Invert the projection string (swap source/target)
std::string inv = info.inverseProjStr();
```

---

## EPSGInfo

`#include <gis/EPSGInfo.h>` (Linux only)

Queries the PROJ EPSG database for coordinate system metadata.

```cpp
bool valid = Fmi::EPSGInfo::isValid(3067);

auto info = Fmi::EPSGInfo::getInfo(3067);
if (info) {
  std::cout << info->name;       // "ETRS89 / TM35FIN(E,N)"
  std::cout << info->bbox.west;  // WGS84 geographic bounds
  std::cout << info->geodetic;   // true if geodetic CRS
  std::cout << info->deprecated; // true if deprecated
}

// Cache control
Fmi::EPSGInfo::setCacheSize(512);
```

---

## BilinearCoordinateTransformation

`#include <gis/BilinearCoordinateTransformation.h>`

Builds a bilinear interpolation grid over a rectilinear source domain to approximate a projection without calling the full PROJ transform for every point. Useful for high-volume point transformations where approximate accuracy is acceptable and speed is critical.

```cpp
// Build the grid (nx×ny sample points spanning [x1,y1]–[x2,y2])
Fmi::BilinearCoordinateTransformation bct(transformation, nx, ny, x1, y1, x2, y2);

// Interpolate a point within the domain
double px = 25.0, py = 60.0;
bool ok = bct.transform(px, py);  // modifies px, py in-place; returns false outside domain

// Access the underlying CoordinateMatrix
const Fmi::CoordinateMatrix& mat = bct.coordinateMatrix();
```

Increasing `nx` and `ny` improves accuracy at the cost of setup time. A 100×100 grid is sufficient for most regional map projections.

---

## Box

`#include <gis/Box.h>`

Defines the linear mapping from projected coordinates to pixel coordinates. Also used as the clipping rectangle for rectangular clip/cut operations.

```cpp
// Full constructor: projected bbox and pixel size
Fmi::Box box(xmin, ymin, xmax, ymax, pixelWidth, pixelHeight);

// Clipping-only constructor (identity pixel transform)
Fmi::Box box(xmin, ymin, xmax, ymax);

// Transform projected coords to pixels
double px = 1234.5, py = 5678.9;
box.transform(px, py);   // modifies in-place

// Inverse: pixels back to projected
box.itransform(px, py);

// Position query
Box::Position pos = box.position(x, y);
// pos == Box::Inside, Outside, Left, Top, Right, Bottom,
//        TopLeft, TopRight, BottomLeft, BottomRight

// Y-axis convention: the transform inverts the Y axis so that
// the geographic north-up convention maps to the screen top-down convention.
```

---

## BBox

`#include <gis/BBox.h>`

Plain struct holding west/east/south/north bounds.

```cpp
Fmi::BBox bb(west, east, south, north);
Fmi::BBox bb2(box);  // construct from Box
```

---

## gridNorth

`#include <gis/OGR.h>`

Returns the direction of grid north at a given WGS84 location, expressed as degrees clockwise from screen up. Useful for displaying north arrows on maps in non-geographic projections.

```cpp
Fmi::CoordinateTransformation ct(wgs84, targetSRS);
auto angle = Fmi::OGR::gridNorth(ct, lon, lat);
if (angle) {
  // rotate north arrow by *angle degrees
}
```
