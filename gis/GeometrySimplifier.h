// ======================================================================
/*!
 * \brief Visvalingam-Whyatt simplification filter for isolines/isobands and
 *        polygon outlines, with optional cross-feature topology preservation.
 *
 * Douglas-Peucker support was removed in favour of Visvalingam-Whyatt
 * exclusively. DP's "kept furthest-from-chord" rule produces visibly
 * spiky/saw-toothed output on natural coastlines, and its O(n^2) synthetic-
 * anchor selection on closed rings without topology preservation makes it
 * unsuitable for production use on dense coastline data.
 */
// ======================================================================

#pragma once

#include "Types.h"
#include <vector>

namespace Fmi
{
class Box;

class GeometrySimplifier
{
 public:
  enum class Type
  {
    None,              // disabled
    VisvalingamWhyatt  // area-based vertex removal
  };

  std::size_t hash_value() const;

  void tolerance(double t) { m_tolerance = t; }
  void type(Type t) { m_type = t; }

  bool active() const { return m_type != Type::None && m_tolerance > 0; }

  void bbox(const Box& box);
  void apply(std::vector<OGRGeometryPtr>& geoms, bool preserve_topology) const;

 private:
  Type m_type = Type::None;
  double m_tolerance = 0;  // pixel-area threshold (in pixel units before bbox conversion)
};

}  // namespace Fmi
