#pragma once

#include <string>

namespace geos
{
  namespace geom
  {
    class Geometry;
  }
}

namespace Fmi
{
namespace GEOS
{
std::string exportToWkb(const geos::geom::Geometry& theGeom);

std::string exportToSvg(const geos::geom::Geometry& theGeom, int thePrecision = -1);

}  // namespace GEOS
}  // namespace Fmi
