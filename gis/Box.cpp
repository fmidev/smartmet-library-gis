// ======================================================================

#include "Box.h"
#include <fmt/format.h>
#include <stdexcept>

// ----------------------------------------------------------------------
/*!
 * \brief Construct the box
 *
 * Note: X1,Y1 are not necessarily the minimum coordinates
 *       and X2,Y2 the maximum coordinates. In particular,
 *       we usually reverse the direction of the Y-axis when
 *       producing SVG data.
 */
// ----------------------------------------------------------------------

Fmi::Box::Box(
    double theX1, double theY1, double theX2, double theY2, std::size_t width, std::size_t height)
    : itsX1(theX1),
      itsY1(theY1),
      itsX2(theX2),
      itsY2(theY2),
      itsXMin(std::min(theX1, theX2)),
      itsYMin(std::min(theY1, theY2)),
      itsXMax(std::max(theX1, theX2)),
      itsYMax(std::max(theY1, theY2)),
      itsWidth(width),
      itsHeight(height)
{
  if (itsXMin == itsXMax || itsYMin == itsYMax)
    throw std::runtime_error(fmt::format(
        "Empty Fmi::Box constructed with x1={} y1={} x2={} y2={}", itsX1, itsY1, itsX2, itsY2));

  itsXalpha = itsWidth * 1 / (itsX2 - itsX1);
  itsXbeta = itsWidth * itsX1 / (itsX1 - itsX2);
  itsYalpha = itsHeight * 1 / (itsY1 - itsY2);
  itsYbeta = itsHeight * itsY2 / (itsY2 - itsY1);
}

// ----------------------------------------------------------------------
/*!
 * \brief Return an identify transformation (the simplest version)
 */
// ----------------------------------------------------------------------

Fmi::Box Fmi::Box::identity() { return {0, 1, 1, 0, 1, 1}; }
