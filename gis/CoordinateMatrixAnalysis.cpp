#include "CoordinateMatrixAnalysis.h"
#include "CoordinateMatrix.h"
#include <boost/optional.hpp>

#include <cmath>
#include <iomanip>
#include <iostream>

namespace Fmi
{
namespace
{
// We discard any grid cell whose bbox exceeds 1000 kilometers in size

const double cell_size_limit = 1000 * 1000;

enum class Handedness
{
  ClockwiseConvex,
  CounterClockwiseConvex,
  Invalid,
  NotConvex,
  Huge,
  Oblong
};

// ----------------------------------------------------------------------
/*!
 * A polygon is convex if all cross products of adjacent edges are of the same sign,
 * and the sign itself says whether the polygon is clockwise or not.
 * Ref: http://www.easywms.com/node/3602
 * We must disallow non-convex cells such as V-shaped ones, since the intersections
 * formulas may then produce values outside the cell.
 *
 * Note that we permit colinear adjacent edges, since for example in latlon grids
 * the poles are represented by multiple grid points. This test thus passes the
 * redundant case of a rectangle with no area, but the rest of the code can handle it.
 * empty boolean for such grid cells.
 */
// ----------------------------------------------------------------------

Handedness analyze_cell(
    double x1, double y1, double x2, double y2, double x3, double y3, double x4, double y4)
{
  // Disallow cells with any invalid coordinates
  if (std::isnan(x1) || std::isnan(y1) || std::isnan(x2) || std::isnan(y2) || std::isnan(x3) ||
      std::isnan(y3) || std::isnan(x4) || std::isnan(y4))
    return Handedness::Invalid;

  // Check for oblong cells which typically occur at projection discontinuities. One could
  // use the Polsby-Popper test here, but calculating the edge lengths in addition to the
  // areas below would be slower tahn simply testing the cell bbox

  const auto xmin = std::min(std::min(x1, x2), std::min(x3, x4));
  const auto xmax = std::max(std::max(x1, x2), std::max(x3, x4));
  const auto ymin = std::min(std::min(y1, y2), std::min(y3, y4));
  const auto ymax = std::max(std::max(y1, y2), std::max(y3, y4));

  const auto dx = xmax - xmin;
  const auto dy = ymax - ymin;

  // Empty cell?
  if (dx == 0 || dy == 0) return Handedness::Invalid;

  // Huge cell? (most likely due to projection instabilities)
  if (dx >= cell_size_limit || dy >= cell_size_limit) return Handedness::Huge;

  const auto ratio = dy / dx;

  if (ratio < 0.01 || ratio > 100) return Handedness::Oblong;

  // Check for convexness and orientation

  const auto area1 = (x2 - x1) * (y3 - y2) - (y2 - y1) * (x3 - x2);
  const auto area2 = (x3 - x2) * (y4 - y3) - (y3 - y2) * (x4 - x3);
  const auto area3 = (x4 - x3) * (y1 - y4) - (y4 - y3) * (x1 - x4);
  const auto area4 = (x1 - x4) * (y2 - y1) - (y1 - y4) * (x2 - x1);

  if (area1 <= 0 && area2 <= 0 && area3 <= 0 && area4 <= 0) return Handedness::ClockwiseConvex;

  if (area1 >= 0 && area2 >= 0 && area3 >= 0 && area4 >= 0)
    return Handedness::CounterClockwiseConvex;

  return Handedness::NotConvex;
}

}  // namespace

CoordinateAnalysis analysis(const CoordinateMatrix& coords)
{
  const auto nx = coords.width();
  const auto ny = coords.height();

  BoolMatrix valid(nx - 1, ny - 1, true);
  BoolMatrix clockwise(nx - 1, ny - 1, false);

  std::size_t cw = 0;
  std::size_t ccw = 0;
  std::size_t bad = 0;

#if 0
  std::size_t huge = 0;
  std::size_t oblong = 0;
  std::size_t notconvex = 0;
#endif

  // Go through the coordinates once

  for (std::size_t j = 0; j < ny - 1; j++)
    for (std::size_t i = 0; i < nx - 1; i++)
    {
      auto hand = analyze_cell(coords.x(i, j),
                               coords.y(i, j),
                               coords.x(i, j + 1),
                               coords.y(i, j + 1),
                               coords.x(i + 1, j + 1),
                               coords.y(i + 1, j + 1),
                               coords.x(i + 1, j),
                               coords.y(i + 1, j));

      if (hand == Handedness::ClockwiseConvex)
      {
        clockwise.set(i, j, true);
        ++cw;
      }
      else if (hand == Handedness::CounterClockwiseConvex)
      {
        // clockwise.set(i, j, false);
        ++ccw;
      }
      else
      {
        valid.set(i, j, false);
        ++bad;
#if 0
        if (hand == Handedness::Huge)
          ++huge;
        else if (hand == Handedness::Oblong)
          ++oblong;
        else if (hand == Handedness::NotConvex)
          ++notconvex;
#endif
      }
    }

  // The coordinates are likely to be upside done if there are many more CCW cells than CW cells

  bool needs_flipping = (ccw > 2 * cw);

  return CoordinateAnalysis{valid, clockwise, needs_flipping};
}

}  // namespace Fmi
