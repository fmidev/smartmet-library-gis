// ======================================================================

#include "Box.h"
#include <fmt/format.h>
#include <macgyver/Exception.h>
#include <macgyver/Hash.h>

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
  try
  {
    if (itsXMin == itsXMax || itsYMin == itsYMax)
    {
      Fmi::Exception exception(BCP, "Empty Fmi::Box constructed!");
      exception.addParameter("X1", std::to_string(itsX1));
      exception.addParameter("Y1", std::to_string(itsY1));
      exception.addParameter("X2", std::to_string(itsX2));
      exception.addParameter("Y2", std::to_string(itsY2));
      throw exception;
    }

    itsXalpha = itsWidth * 1 / (itsX2 - itsX1);
    itsXbeta = itsWidth * itsX1 / (itsX1 - itsX2);
    itsYalpha = itsHeight * 1 / (itsY1 - itsY2);
    itsYbeta = itsHeight * itsY2 / (itsY2 - itsY1);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Constructor failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * Construct box with identity transformation
 */
// ----------------------------------------------------------------------

Fmi::Box::Box(double theX1, double theY1, double theX2, double theY2)
    : itsX1(theX1),
      itsY1(theY1),
      itsX2(theX2),
      itsY2(theY2),
      itsXMin(std::min(theX1, theX2)),
      itsYMin(std::min(theY1, theY2)),
      itsXMax(std::max(theX1, theX2)),
      itsYMax(std::max(theY1, theY2)),
      itsWidth(0),
      itsHeight(0),
      itsXalpha(1),
      itsXbeta(0),
      itsYalpha(1),
      itsYbeta(0)
{
}

// ----------------------------------------------------------------------
/*!
 * \brief Return an identify transformation (the simplest version)
 */
// ----------------------------------------------------------------------

Fmi::Box Fmi::Box::identity()
{
  return {0, 1, 1, 0, 1, 1};
}

// ----------------------------------------------------------------------
/*!
 * \brief Box hash value
 */
// ----------------------------------------------------------------------

std::size_t Fmi::Box::hashValue() const
{
  auto hash = Fmi::hash_value(itsX1);
  Fmi::hash_combine(hash, Fmi::hash_value(itsY1));
  Fmi::hash_combine(hash, Fmi::hash_value(itsX2));
  Fmi::hash_combine(hash, Fmi::hash_value(itsY2));
  Fmi::hash_combine(hash, Fmi::hash_value(itsXMin));
  Fmi::hash_combine(hash, Fmi::hash_value(itsYMin));
  Fmi::hash_combine(hash, Fmi::hash_value(itsXMax));
  Fmi::hash_combine(hash, Fmi::hash_value(itsYMax));
  return hash;
}
