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

  // Optional speedup hint: any cluster whose total geographic area (km^2) is
  // below this threshold is skipped before triangulation. The cluster's total
  // area is an upper bound on the merged-polygon area it could ever produce,
  // so a cluster smaller than the downstream km^2 minarea filter cannot
  // possibly contribute anything that survives that filter — we can drop the
  // whole cluster up front. Set this to the value of the downstream km^2
  // minarea filter (e.g. WMS Map.minarea). Default 0 disables the filter.
  void minTotalArea(double km2) { m_minTotalArea = km2; }

  std::size_t hash_value() const;
  void apply(std::vector<OGRGeometryPtr>& geoms) const;

 private:
  double m_lengthLimit = 0;     // max gap triangle edge length (coordinate units)
  double m_areaLimit = 0;       // min polygon area to keep (coordinate units^2)
  double m_minTotalArea = 0;    // optional pre-CDT cluster total-area filter (km^2)
};

}  // namespace Fmi
