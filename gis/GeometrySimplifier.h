// ======================================================================
/*!
 * \brief Simplification filters for isolines/isobands (Douglas-Peucker, Visvalingam-Whyatt)
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
    DouglasPeucker,    // distance-based vertex removal
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
  double m_tolerance = 0;  // distance for D-P, area for V-W (in pixel units before bbox conversion)
};

}  // namespace Fmi
