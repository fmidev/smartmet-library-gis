# smartmet-library-gis

Part of [SmartMet Server](https://github.com/fmidev/smartmet-library-spine). See the [SmartMet Server documentation](https://github.com/fmidev/smartmet-library-spine) for an overview of the ecosystem.

## Overview

The gis library provides GIS (Geographic Information System) operations for SmartMet Server. It handles coordinate projections, geometry operations, and geographic data access needed by the server's spatial processing capabilities.

## Features

- **Coordinate projections** — coordinate system transformations using PROJ
- **Geometry clipping** — clipping geographic features to bounding boxes and arbitrary areas
- **Antimeridian handling** — correct handling of geographic interrupts at the antimeridian
- **Raster/DEM data** — Digital Elevation Model (SRTM) and land cover raster access
- **PostGIS integration** — reading geographic data from PostGIS databases

## Documentation

Detailed documentation is available in the [docs/](docs/) directory:

- [Overview and quick start](docs/gis.md)
- [Coordinate projections](docs/gis-projection.md)
- [Geometry clipping](docs/gis-clipping.md)
- [Geographic interrupts and antimeridian](docs/gis-interrupts.md)
- [Raster and DEM data](docs/gis-raster.md)
- [PostGIS integration](docs/gis-postgis.md)

## Usage

Used by [smartmet-engine-gis](https://github.com/fmidev/smartmet-engine-gis), [smartmet-plugin-wms](https://github.com/fmidev/smartmet-plugin-wms), and other components requiring geographic data processing.

## License

MIT — see [LICENSE](LICENSE)

## Contributing

Bug reports and pull requests are welcome on [GitHub](../../issues).
