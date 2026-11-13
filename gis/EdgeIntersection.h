// ======================================================================
/*!
 * \brief Accurate intersection of a line segment with a rectangle edge
 *
 * The rectangle clippers calculate the point where a segment crosses
 * the line x=limit (or y=limit). The intersection points of different
 * segments with the same edge must keep their order along the edge, or
 * the clipped polygon may be reconnected around the entire rectangle.
 * Plain double precision arithmetic may swap the order of points which
 * are less than a few ulps apart, for example when a vertex is barely
 * outside the rectangle.
 *
 * The calculation is done using double-double arithmetic so that the
 * result is practically correctly rounded. Since rounding is monotonic,
 * the intersection points can then at worst coincide, but never swap.
 */
// ======================================================================

#pragma once

#include <cmath>

namespace Fmi
{
namespace EdgeIntersection
{
// Unevaluated sum hi+lo with |lo| <= ulp(hi)/2
struct DoubleDouble
{
  double hi;
  double lo;
};

inline DoubleDouble two_sum(double a, double b)
{
  const double s = a + b;
  const double bb = s - a;
  return {s, (a - (s - bb)) + (b - bb)};
}

inline DoubleDouble quick_two_sum(double a, double b)
{
  const double s = a + b;
  return {s, b - (s - a)};
}

inline DoubleDouble add(const DoubleDouble& a, const DoubleDouble& b)
{
  const DoubleDouble s = two_sum(a.hi, b.hi);
  const DoubleDouble t = two_sum(a.lo, b.lo);
  DoubleDouble r = quick_two_sum(s.hi, s.lo + t.hi);
  return quick_two_sum(r.hi, r.lo + t.lo);
}

inline DoubleDouble mul(const DoubleDouble& a, const DoubleDouble& b)
{
  const double p = a.hi * b.hi;
  const double e = std::fma(a.hi, b.hi, -p);
  return quick_two_sum(p, e + (a.hi * b.lo + a.lo * b.hi));
}

inline DoubleDouble div(const DoubleDouble& a, const DoubleDouble& b)
{
  // Long division with three partial quotients
  const double q1 = a.hi / b.hi;
  DoubleDouble r = add(a, mul({-q1, 0}, b));
  const double q2 = r.hi / b.hi;
  r = add(r, mul({-q2, 0}, b));
  const double q3 = r.hi / b.hi;
  return add(quick_two_sum(q1, q2), {q3, 0});
}

// ----------------------------------------------------------------------
/*!
 * \brief The y-coordinate where the segment (x1,y1)-(x2,y2) crosses x=limit
 *
 * Requires x1 != x2. Swap x and y to intersect with y=limit instead.
 */
// ----------------------------------------------------------------------

inline double crossing(double x1, double y1, double x2, double y2, double limit)
{
  // y1 + (y2 - y1) * (limit - x1) / (x2 - x1), the differences are exact
  const DoubleDouble dy = two_sum(y2, -y1);
  const DoubleDouble dl = two_sum(limit, -x1);
  const DoubleDouble dx = two_sum(x2, -x1);
  const DoubleDouble r = add({y1, 0}, div(mul(dy, dl), dx));
  return r.hi + r.lo;
}

}  // namespace EdgeIntersection
}  // namespace Fmi
