// ======================================================================
/*!
 * \file
 * \brief Implementation of the legacy GSHHS binary reader.
 */
// ======================================================================

#include "Gshhs.h"

#include <fmt/format.h>
#include <macgyver/Exception.h>
#include <ogr_geometry.h>
#include <ogr_spatialref.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

namespace Fmi
{
namespace Gshhs
{
namespace
{
// Legacy GSHHS v1.x on-disk header: 8 32-bit ints + 2 16-bit ints = 36 bytes.
struct RawHeader
{
  std::int32_t id;
  std::int32_t n;
  std::int32_t level;
  std::int32_t west;
  std::int32_t east;
  std::int32_t south;
  std::int32_t north;
  std::int32_t area;
  std::int16_t greenwich;
  std::int16_t source;
};
static_assert(sizeof(RawHeader) == 36, "Unexpected GSHHS header layout");

struct RawPoint
{
  std::int32_t x;
  std::int32_t y;
};
static_assert(sizeof(RawPoint) == 8, "Unexpected GSHHS point layout");

void byteswap(RawHeader& h)
{
  h.id = __builtin_bswap32(h.id);
  h.n = __builtin_bswap32(h.n);
  h.level = __builtin_bswap32(h.level);
  h.west = __builtin_bswap32(h.west);
  h.east = __builtin_bswap32(h.east);
  h.south = __builtin_bswap32(h.south);
  h.north = __builtin_bswap32(h.north);
  h.area = __builtin_bswap32(h.area);
  h.greenwich = __builtin_bswap16(h.greenwich);
  h.source = __builtin_bswap16(h.source);
}

void byteswap(RawPoint& p)
{
  p.x = __builtin_bswap32(p.x);
  p.y = __builtin_bswap32(p.y);
}

// True if the polygon bounding box overlaps the requested box.
bool overlaps(const RawHeader& h, const Options& opts)
{
  const double w = 1.0e-6 * h.west;
  const double e = 1.0e-6 * h.east;
  const double s = 1.0e-6 * h.south;
  const double n = 1.0e-6 * h.north;

  const auto wrap = [](double x)
  {
    if (x < -180.0)
      return x + 360.0;
    if (x > 180.0)
      return x - 360.0;
    return x;
  };

  const double ww = wrap(w);
  const double ee = wrap(e);

  const auto outside = [&](double a, double b)
  {
    return (a > opts.maxLongitude || b < opts.minLongitude || s > opts.maxLatitude ||
            n < opts.minLatitude);
  };

  if (ee < ww)
  {
    // Polygon crosses the antimeridian; treat the two halves separately.
    return !(outside(ww, 180.0) && outside(-180.0, ee));
  }
  return !outside(ww, ee);
}

using FilePtr = std::unique_ptr<std::FILE, int (*)(std::FILE*)>;

FilePtr open_for_reading(const std::string& filename)
{
  FilePtr fp(std::fopen(filename.c_str(), "rbe"), &std::fclose);
  if (!fp)
    throw Fmi::Exception(BCP, fmt::format("Failed to open '{}' for reading", filename));
  return fp;
}

void skip_points(std::FILE* fp, int n, const std::string& filename)
{
  if (std::fseek(fp, static_cast<long>(n) * static_cast<long>(sizeof(RawPoint)), SEEK_CUR) != 0)
    throw Fmi::Exception(BCP, fmt::format("Failed to skip points in '{}'", filename));
}

struct PolygonData
{
  OGRPolygon polygon;
  double perimeter_km = 0.0;  // sum of edge lengths around the closed ring
};

PolygonData read_polygon(std::FILE* fp,
                         const RawHeader& header,
                         bool flip,
                         std::int32_t wrap_threshold,
                         const std::string& filename)
{
  std::vector<RawPoint> points(header.n);
  if (std::fread(points.data(), sizeof(RawPoint), header.n, fp) !=
      static_cast<std::size_t>(header.n))
    throw Fmi::Exception(BCP, fmt::format("Truncated GSHHS file '{}'", filename));

  OGRLinearRing ring;
  ring.setNumPoints(header.n);

  // Decode points and accumulate perimeter as we go. The earth-flattening
  // formula (cos(avgLat) for lon scaling, 1° = 111.32 km) is the same one
  // qdless uses for its lake-roundness filter, so the compactness values
  // computed from the binary and binned-NetCDF formats agree.
  double perimeter_km = 0.0;
  double prev_lon = 0.0;
  double prev_lat = 0.0;
  for (int k = 0; k < header.n; ++k)
  {
    if (flip)
      byteswap(points[k]);

    const double lon = (header.greenwich && points[k].x > wrap_threshold
                            ? points[k].x * 1.0e-6 - 360.0
                            : points[k].x * 1.0e-6);
    const double lat = points[k].y * 1.0e-6;
    ring.setPoint(k, lon, lat);

    if (k > 0)
    {
      const double avgLatRad = (lat + prev_lat) * 0.5 * M_PI / 180.0;
      const double dLon = (lon - prev_lon) * std::cos(avgLatRad);
      const double dLat = lat - prev_lat;
      perimeter_km += std::sqrt(dLon * dLon + dLat * dLat) * 111.32;
    }
    prev_lon = lon;
    prev_lat = lat;
  }

  // Closing edge: contributes 0 if the file already stores a closed ring.
  if (header.n > 1)
  {
    OGRPoint first;
    ring.getPoint(0, &first);
    const double avgLatRad = (first.getY() + prev_lat) * 0.5 * M_PI / 180.0;
    const double dLon = (first.getX() - prev_lon) * std::cos(avgLatRad);
    const double dLat = first.getY() - prev_lat;
    perimeter_km += std::sqrt(dLon * dLon + dLat * dLat) * 111.32;
  }

  ring.closeRings();

  PolygonData result;
  result.polygon.addRing(&ring);  // addRing copies
  result.perimeter_km = perimeter_km;
  return result;
}

// Returns true if this nested polygon should be dropped on roundness grounds.
// Continents and large islands (level == 1) are intentionally exempt — their
// coastlines are fractal and would all score very low.
bool fails_roundness(const RawHeader& header, double perimeter_km, const Options& options)
{
  if (header.level < 2 || options.minLakeRoundness <= 0 || perimeter_km <= 0)
    return false;
  const double area_km2 = 0.1 * header.area;
  const double compactness = 4.0 * M_PI * area_km2 / (perimeter_km * perimeter_km);
  return compactness < options.minLakeRoundness;
}

// Header-only filters: anything that can be decided without reading the points.
bool should_skip_by_header(const RawHeader& header, const Options& options)
{
  if (options.maxLevel && header.level > *options.maxLevel)
    return true;

  if (!overlaps(header, options))
    return true;

  const double area_km2 = 0.1 * header.area;
  const bool top_level = (header.level == 1);
  if (top_level)
  {
    if (options.minIslandAreaKm2 > 0 && area_km2 < options.minIslandAreaKm2)
      return true;
  }
  else
  {
    if (options.minLakeAreaKm2 > 0 && area_km2 < options.minLakeAreaKm2)
      return true;
  }
  return false;
}

}  // namespace

std::unique_ptr<OGRGeometry> read(const std::string& filename, const Options& options)
{
  try
  {
    auto multipoly = std::make_unique<OGRMultiPolygon>();

    OGRSpatialReference* wgs84 = OGRSpatialReference::GetWGS84SRS();
    multipoly->assignSpatialReference(wgs84);

    if (options.minLongitude >= options.maxLongitude ||
        options.minLatitude >= options.maxLatitude)
      return multipoly;

    auto fp = open_for_reading(filename);

    RawHeader header{};
    if (std::fread(&header, sizeof(RawHeader), 1, fp.get()) != 1)
      return multipoly;  // empty file is not an error

    // Detect endianness from the level field of the first record.
    // Level is always 1..4 in a valid file.
    auto valid_level = [](std::int32_t l) { return l >= 1 && l <= 4; };
    const bool flip = !valid_level(header.level);

    // The very first polygon in a legacy GSHHS file (Eurafrica) may include
    // x values up to 270 degrees so that greenwich-crossing rings stay
    // continuous. Subsequent polygons cap out at 180.
    std::int32_t wrap_threshold = 270'000'000;

    while (true)
    {
      if (flip)
        byteswap(header);

      if (header.n < 0 || !valid_level(header.level))
        throw Fmi::Exception(BCP, fmt::format("Corrupt GSHHS header in '{}'", filename));

      if (should_skip_by_header(header, options))
      {
        skip_points(fp.get(), header.n, filename);
      }
      else
      {
        auto data = read_polygon(fp.get(), header, flip, wrap_threshold, filename);
        if (!fails_roundness(header, data.perimeter_km, options))
          multipoly->addGeometry(&data.polygon);  // addGeometry copies
      }

      wrap_threshold = 180'000'000;

      if (std::fread(&header, sizeof(RawHeader), 1, fp.get()) != 1)
        break;
    }

    return multipoly;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

}  // namespace Gshhs
}  // namespace Fmi
