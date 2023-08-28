#pragma once

#ifdef UNIX

#include "BBox.h"
#include <boost/optional.hpp>
#include <macgyver/Cache.h>
#include <string>

namespace Fmi
{
namespace EPSGInfo
{
// EPSG information from PROJ database
struct EPSG
{
  BBox bbox;    // WGS84 bounds
  BBox bounds;  // Native coordinate bounds
  std::string name;
  std::string scope;
  int number = 0;
  bool geodetic = false;
  bool deprecated = false;
};

bool isValid(int code);                   // Is the EPSG code valid?
boost::optional<EPSG> getInfo(int code);  // Get all EPSG information

void setCacheSize(std::size_t newMaxSize);
Cache::CacheStats getCacheStats();

std::string to_string(const Fmi::EPSGInfo::EPSG& epsg);

}  // namespace EPSGInfo
}  // namespace Fmi

#endif
