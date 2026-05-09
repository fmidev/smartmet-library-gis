// ======================================================================
/*!
 * \file
 * \brief Reader for binary GSHHS shoreline files (legacy v1.x format).
 *
 * GSHHS (Global Self-consistent Hierarchical High-resolution Shorelines)
 * is the binary global coastline / lake / island database used for example
 * by GMT (Generic Mapping Tools).
 *
 * The reader understands the legacy binary gshhs*.b format (the same one
 * accepted by the deprecated NFmiGshhsTools in libsmartmet-imagine). It
 * does not understand the newer NetCDF (*.cdf, *.nc) variant, nor the
 * GSHHG v2.x flag-encoded binary format.
 *
 * Polygons are returned as an OGRMultiPolygon in WGS84 (EPSG:4326),
 * with traditional longitude/latitude axis order.
 */
// ======================================================================

#pragma once

#include <memory>
#include <optional>
#include <string>

class OGRGeometry;

namespace Fmi
{
namespace Gshhs
{
struct Options
{
  // Bounding box filter. Polygons whose bounding box does not overlap
  // the box are skipped. Defaults cover the whole globe.
  double minLongitude = -180.0;
  double minLatitude = -90.0;
  double maxLongitude = 180.0;
  double maxLatitude = 90.0;

  // Maximum hierarchical level to include
  // (1 = land, 2 = lake, 3 = island in lake, 4 = pond in island in lake).
  // Empty optional → keep all levels.
  std::optional<int> maxLevel;

  // Area filters in km^2. 0 disables.
  //
  // Top-level (level == 1, continents and islands) are kept iff
  //     area >= minIslandAreaKm2.
  // Nested  (level >= 2, lakes / islands in lakes / ponds) are kept iff
  //     area >= minLakeAreaKm2  AND  4πA/L² >= minLakeRoundness.
  //
  // Roundness is the isoperimetric compactness 4πA/L²: 1.0 for a circle,
  // ~0.785 for a square, near 0 for a fractal shoreline (e.g. Lake Saimaa).
  // It is intentionally only applied to nested polygons — real continents
  // are also fractal and would otherwise all be rejected.
  double minIslandAreaKm2 = 0.0;
  double minLakeAreaKm2 = 0.0;
  double minLakeRoundness = 0.0;
};

/**
 * \brief Read a binary GSHHS file into an OGRMultiPolygon in WGS84.
 *
 * Caller takes ownership of the returned geometry. Throws Fmi::Exception
 * if the file cannot be opened or is corrupt.
 *
 * Each GSHHS record becomes a separate top-level polygon: lake / island
 * polygons are NOT promoted to interior rings of their containing land
 * polygon.
 *
 * The bounding-box filter only skips polygons whose extent lies entirely
 * outside the box; selected polygons are returned in full. Use
 * Fmi::OGR::polyclip on the result if exact clipping is required.
 */
std::unique_ptr<OGRGeometry> read(const std::string& filename, const Options& options = {});

}  // namespace Gshhs
}  // namespace Fmi
