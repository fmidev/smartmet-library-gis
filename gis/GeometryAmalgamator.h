// ======================================================================
/*!
 * \brief Amalgamate nearby polygons using constrained Delaunay triangulation
 *
 * Merges nearby small polygons into larger ones (e.g. archipelago islands)
 * by triangulating the gaps between them and accepting gap triangles with
 * short enough edges.
 */
// ======================================================================

#pragma once

#include "Types.h"
#include <vector>

namespace Fmi
{

class GeometryAmalgamator
{
 public:
  void lengthLimit(double limit) { m_lengthLimit = limit; }
  void areaLimit(double limit) { m_areaLimit = limit; }

  std::size_t hash_value() const;
  void apply(std::vector<OGRGeometryPtr>& geoms) const;

 private:
  double m_lengthLimit = 0;  // max gap triangle edge length (coordinate units)
  double m_areaLimit = 0;    // min polygon area to keep (coordinate units^2)
};

}  // namespace Fmi
