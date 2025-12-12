#include "CoordinateMatrixAnalysis.h"
#include "CoordinateMatrix.h"
#include <macgyver/Exception.h>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <optional>

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
  Oblong,
  Pole,
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
  try
  {
    // Disallow cells with any invalid coordinates
    if (std::isnan(x1) || std::isnan(y1) || std::isnan(x2) || std::isnan(y2) || std::isnan(x3) ||
        std::isnan(y3) || std::isnan(x4) || std::isnan(y4))
      return Handedness::Invalid;

    // Disallow highly triangular cells near the poles
    const auto dx1 = std::abs(x4 - x1);
    const auto dx2 = std::abs(x3 - x2);

    if (dx2 > dx1 * 1000 || dx1 > dx2 * 1000)
      return Handedness::Pole;

    // Check for oblong cells which typically occur at projection discontinuities. One could
    // use the Polsby-Popper test here, but calculating the edge lengths in addition to the
    // areas below would be slower than simply testing the cell bbox

    const auto xmin = std::min(std::min(x1, x2), std::min(x3, x4));
    const auto xmax = std::max(std::max(x1, x2), std::max(x3, x4));
    const auto ymin = std::min(std::min(y1, y2), std::min(y3, y4));
    const auto ymax = std::max(std::max(y1, y2), std::max(y3, y4));

    const auto dx = xmax - xmin;
    const auto dy = ymax - ymin;

    // Empty cell?
    if (dx == 0 || dy == 0)
      return Handedness::Invalid;

    // Huge cell? (most likely due to projection instabilities)
    // if (!wraparound)
    {
      if (dx >= cell_size_limit || dy >= cell_size_limit)
        return Handedness::Huge;

      const auto ratio = dy / dx;

      if (ratio < 0.01 || ratio > 100)
        return Handedness::Oblong;
    }

    // Check for convexness and orientation

    const auto area1 = (x2 - x1) * (y3 - y2) - (y2 - y1) * (x3 - x2);
    const auto area2 = (x3 - x2) * (y4 - y3) - (y3 - y2) * (x4 - x3);
    const auto area3 = (x4 - x3) * (y1 - y4) - (y4 - y3) * (x1 - x4);
    const auto area4 = (x1 - x4) * (y2 - y1) - (y1 - y4) * (x2 - x1);

    if (area1 <= 0 && area2 <= 0 && area3 <= 0 && area4 <= 0)
      return Handedness::ClockwiseConvex;

    if (area1 >= 0 && area2 >= 0 && area3 >= 0 && area4 >= 0)
      return Handedness::CounterClockwiseConvex;

    return Handedness::NotConvex;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Detect wraparound shift for global data
 */
// ----------------------------------------------------------------------

std::size_t detect_shift(const CoordinateMatrix& coords)
{
  // We use the center latitude since coordinates may be distorted on the
  // first and last lines due to PROJ.x producing identical coordinates
  // for the poles

  const auto j = coords.height() / 2;

  // Now look for a possible wraparond on the row

  const auto nx = coords.width();

  // First calculate distances between adjacent vertices
  std::vector<double> distances;
  distances.reserve(nx);

  for (auto i = 0UL; i < nx - 1; i++)
  {
    auto dist =
        std::hypot(coords.x(i, j) - coords.x(i + 1, j), coords.y(i, j) - coords.y(i + 1, j));
    distances.push_back(dist);
  }

  // Maximum distance and its position
  auto maxpos = std::max_element(distances.begin(), distances.end());
  auto maxdist = *maxpos;
  std::size_t shift = std::distance(distances.begin(), maxpos);

  if (maxdist == 0 || shift == 0)  // safety checks
    return 0UL;

  for (auto i = 0UL; i < distances.size(); i++)
  {
    if (i != shift)
    {
      // discard the found max shift if it is not huge in comparison to other distances
      if (distances[i] / maxdist > 0.01)
        return 0UL;
    }
  }

  return shift + 1;
}

}  // namespace

CoordinateAnalysis analysis(const CoordinateMatrix& coords)
{
  try
  {
    auto shift = detect_shift(coords);

    const auto nx = coords.width();
    const auto ny = coords.height();

    BoolMatrix valid(nx - 1, ny - 1, true);
    BoolMatrix clockwise(nx - 1, ny - 1, false);

    std::size_t cw = 0;
    std::size_t ccw = 0;
    // std::size_t bad = 0;

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
          // ++bad;
        }
      }

    // The coordinates are likely to be upside done if there are many more CCW cells than CW cells
    bool needs_flipping = (ccw > 2 * cw);

    return CoordinateAnalysis{valid, clockwise, needs_flipping, shift};
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

}  // namespace Fmi
