// ======================================================================
/*!
 * \brief Various filters for isolines/isobands to smoothen them etc
 */
// ======================================================================

#pragma once

#include "Types.h"
#include <vector>

namespace Fmi
{
class Box;

class GeometrySmoother
{
 public:
  enum class Type
  {
    None,      // disabled
    Average,   // moving average, weight = 1
    Linear,    // moving average, weight = 1/(1+distance)
    Gaussian,  // moving average, weight = Gaussian where stdev is selected based on radius
    Tukey      // moving average, weight = Tukey's biweight = (1-(distance/radius)^2)^2
  };

  std::size_t hash_value() const;

  void radius(double r) { m_radius = r; }
  void iterations(uint n) { m_iterations = n; }
  void type(Type t) { m_type = t; }

  void bbox(const Box& box);
  void apply(std::vector<OGRGeometryPtr>& geoms, bool preserve_topology) const;

 private:
  Type m_type = Type::None;  // no filtering done by default

  double m_radius = 0;    // in pixels
  uint m_iterations = 1;  // one pass only by default
};

}  // namespace Fmi
