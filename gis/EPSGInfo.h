#pragma once

#ifdef UNIX

// EPSG information from PROJ database

#include "BBox.h"
#include <macgyver/Cache.h>
#include <string>

namespace Fmi
{
namespace EPSGInfo
{
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

bool isValid(int code);             // Is the EPSG code valid?
EPSG getEPSG(int code);             // Get all EPSG information
BBox getBBox(int code);             // Get just the WGS84 bounding box
BBox getProjectedBounds(int code);  // Get bounding box in native coordinates

void setCacheSize(std::size_t newMaxSize);
Cache::CacheStats getCacheStats();

std::string to_string(const Fmi::EPSGInfo::EPSG& epsg);

}  // namespace EPSGInfo
}  // namespace Fmi

#endif
