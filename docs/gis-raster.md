# Elevation and Land Cover

## DEM — Digital Elevation Model

`#include <gis/DEM.h>`

Queries elevation at geographic coordinates from a directory of elevation tiles.

```cpp
Fmi::DEM dem("/path/to/dem/data");

// Returns elevation in metres at a WGS84 location.
// Returns NaN if elevation data is unavailable.
double elev = dem.elevation(lon, lat);

// Query elevation at a given target resolution (in degrees).
// Internally averages tiles appropriate for the resolution.
double elev = dem.elevation(lon, lat, resolution);
```

The `DEM` class is non-copyable and non-movable. Construct once and query repeatedly.

---

## SrtmTile

`#include <gis/SrtmTile.h>`

Represents a single SRTM (Shuttle Radar Topography Mission) `.hgt` tile.

An SRTM tile covers a 1°×1° cell. Each tile file is named by the south-west corner of the cell, e.g. `N60E024.hgt` for the cell spanning 60–61°N, 24–25°E.

```cpp
Fmi::SrtmTile tile("/path/to/N60E024.hgt");

// Query elevation at a point within this tile's extent
double elev = tile.elevation(lon, lat);
```

Elevations are interpolated bilinearly from the 16-bit integer raster data.

---

## SrtmMatrix

`#include <gis/SrtmMatrix.h>`

A 360×180 sparse matrix of `SrtmTile` objects covering the whole globe. Tiles are loaded on demand.

```cpp
Fmi::SrtmMatrix srtm("/path/to/srtm/directory");

double elev = srtm.elevation(lon, lat);
double elev = srtm.elevation(lon, lat, resolution);
```

This is the class used internally by `DEM` when the data path contains SRTM `.hgt` files.

---

## LandCover

`#include <gis/LandCover.h>`

Classifies geographic locations by land cover type.

```cpp
Fmi::LandCover lc("/path/to/landcover/data");

// Returns a land cover classification value
// (crop, forest, urban, water, etc.)
int type = lc.coverType(lon, lat);
```

The return values correspond to standard land cover classification codes. Typical categories:

| Code | Description |
|------|-------------|
| 10 | Cropland |
| 20 | Vegetation / shrubs |
| 30 | Grassland |
| 40 | Cropland/vegetation mosaic |
| 50 | Urban |
| 60 | Bare / sparse |
| 70 | Snow and ice |
| 80 | Permanent water |
| 90 | Herbaceous wetland |
| 100 | Moss and lichen |
| 200+ | Forest variants |

The exact codes depend on the underlying dataset (e.g. GlobCover, ESA CCI).
