#ifdef UNIX

#include "EPSGInfo.h"
#include "CoordinateTransformation.h"
#include "SpatialReference.h"
#include <fmt/format.h>
#include <macgyver/Exception.h>
#include <macgyver/StringConversion.h>
#include <proj/io.hpp>
#include <sqlite3pp/sqlite3pp.h>
#include <sqlite3pp/sqlite3ppext.h>
#include <sqlite3.h>

namespace Fmi
{
namespace EPSGInfo
{
namespace
{
// Internal caches
const int default_cache_size = 1000;
using EPSGCache = Cache::Cache<int, EPSG>;
EPSGCache g_epsg_cache(default_cache_size);

// Calculating the area in degrees is not really accurate, but this is good enough for SmartMet
double area(const BBox& bbox)
{
  return (bbox.east - bbox.west) * (bbox.north - bbox.south);
}

// Get EPSG information from the cache, getting the information from PROJ first if necessary
boost::optional<EPSG> lookup_epsg(int code)
{
  const auto& obj = g_epsg_cache.find(code);
  if (obj)
    return obj;

  auto db_context = NS_PROJ::io::DatabaseContext::create().as_nullable();
  auto* sqlite_handle = reinterpret_cast<sqlite3*>(db_context->getSqliteHandle());
  auto projdb = sqlite3pp::ext::borrow(sqlite_handle);

  // At least these tables have extent information
  const std::vector<std::string> tables = {"projected_crs", "geodetic_crs"};
  boost::optional<EPSG> ret;

  for (const auto& object_table_name : tables)
  {
    std::string sql = fmt::format(
        "select usage.object_code, "
        "extent.west_lon, extent.east_lon, extent.south_lat, extent.north_lat, "
        "{1}.name, scope.scope, {1}.deprecated "
        "from {1},extent,scope,usage "
        "where extent.code=usage.extent_code "
        "and scope.code=usage.scope_code "
        "and {1}.code=usage.object_code "
        "and usage.object_auth_name='EPSG' "
        "and usage.object_table_name='{1}' "
        "and {1}.auth_name='EPSG' "
        "and scope.auth_name='EPSG' "
        "and extent.auth_name='EPSG' "
        "and usage.object_code='{0}'",
        code,
        object_table_name);

    bool geodetic = (object_table_name == "geodetic_crs");

    sqlite3pp::query qry(projdb, sql.c_str());

    // Look for the largest valid area
    for (const auto& row : qry)
    {
      EPSG epsg;
      epsg.number = Fmi::stoi(row.get<std::string>(0));
      epsg.bbox.west = row.get<double>(1);
      epsg.bbox.east = row.get<double>(2);
      epsg.bbox.south = row.get<double>(3);
      epsg.bbox.north = row.get<double>(4);
      epsg.name = row.get<std::string>(5);
      epsg.scope = row.get<std::string>(6);
      epsg.deprecated = row.get<bool>(7);
      epsg.geodetic = geodetic;

      // If longitude_east < longitude_west, add 360.0 to logitude_east
      if (epsg.bbox.east < epsg.bbox.west)
        epsg.bbox.east += 360.0;

      if (!ret || area(epsg.bbox) > area(ret->bbox))
        ret = epsg;
    }

    if (ret)
      break;
  }

  if (!ret)
    return ret;

  // Calculate projected bounds

  // SpatialReference WGS84("WGS84");
  // SpatialReference crs(code);
  CoordinateTransformation transformation("WGS84", code);
  std::vector<double> x{ret->bbox.west, ret->bbox.east};
  std::vector<double> y{ret->bbox.south, ret->bbox.north};
  transformation.transform(x, y);
  ret->bounds.west = x[0];
  ret->bounds.south = y[0];
  ret->bounds.east = x[1];
  ret->bounds.north = y[1];

  // Cache the result
  g_epsg_cache.insert(code, *ret);

  return ret;
}

}  // namespace

// Is the EPSG code valid?
bool isValid(int code)
{
  const auto& obj = lookup_epsg(code);
  return !!obj;
}

// Get all EPSG information
EPSG getEPSG(int code)
{
  const auto& obj = lookup_epsg(code);
  if (obj)
    return *obj;
  throw Fmi::Exception(BCP, "Unknown EPSG code").addParameter("Code", Fmi::to_string(code));
}

// Get just the WGS84 bounding box
BBox getBBox(int code)
{
  return getEPSG(code).bbox;
}

// Get bounding box in native coordinates
BBox getProjectedBounds(int code)
{
  return getEPSG(code).bounds;
}

// Resize the cache from the default
void setCacheSize(std::size_t newMaxSize)
{
  try
  {
    g_epsg_cache.resize(newMaxSize);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Failed to resize the EPSG cache!")
        .addParameter("Wanted size", Fmi::to_string(newMaxSize));
  }
}

Cache::CacheStats getCacheStats()
{
  return g_epsg_cache.statistics();
}

std::string to_string(const Fmi::EPSGInfo::EPSG& epsg)
{
  return fmt::format(
      "Name: {}\n"
      "Scope: {}\n"
      "Number: {}\n"
      "BBox: {},{},{},{}\n"
      "Bounds: {},{},{},{}\n",
      epsg.name,
      epsg.scope,
      epsg.number,
      epsg.bbox.west,
      epsg.bbox.south,
      epsg.bbox.east,
      epsg.bbox.north,
      epsg.bounds.west,
      epsg.bounds.south,
      epsg.bounds.east,
      epsg.bounds.north);
}

}  // namespace EPSGInfo
}  // namespace Fmi

#endif  // UNIX
