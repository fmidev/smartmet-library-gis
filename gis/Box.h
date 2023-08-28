// ======================================================================
/*!
 * \brief Define an area in a spatial reference and its size in pixel units
 *
 * This class defines the conversion coefficients for converting a
 * projected geometry to pixel coordinates. The conversion
 * is linear, the coordinates are expected to be defined in the
 * units of the projected data.
 *
 * Note: Box(0,0,1,1,1,1) is an identify transformation
 */
// ======================================================================

#pragma once

#include <algorithm>

namespace Fmi
{
class Box
{
 public:
  // Construct the transformation
  Box(double theX1,
      double theY1,
      double theX2,
      double theY2,
      std::size_t width,
      std::size_t height);

  // Construct identity transformation for clipping purposes only
  Box(double theX1, double theY1, double theX2, double theY2);

  Box() = delete;

  // Transform the given coordinate
  // Note how Y-axis is inverted to get the proper side up.

  void transform(double& x, double& y) const
  {
#if 0
	  x = (x-itsX1)/(itsX2-itsX1) * itsWidth;
	  y = (itsY2-y)/(itsY2-itsY1) * itsHeight;
#else
    x = itsXalpha * x + itsXbeta;
    y = itsYalpha * y + itsYbeta;
#endif
  }

  void itransform(double& x, double& y) const
  {
    x = (x - itsXbeta) / itsXalpha;
    y = (y - itsYbeta) / itsYalpha;
  }

  // Return an area with an identify transformation
  static Box identity();

  // Accessors
  double xmin() const { return itsXMin; }
  double xmax() const { return itsXMax; }
  double ymin() const { return itsYMin; }
  double ymax() const { return itsYMax; }
  std::size_t width() const { return itsWidth; }
  std::size_t height() const { return itsHeight; }
  // Position with respect to the box

  enum Position
  {
    Inside = 1,
    Outside = 2,

    Left = 4,
    Top = 8,
    Right = 16,
    Bottom = 32,

    TopLeft = Top | Left,         // 12
    TopRight = Top | Right,       // 24
    BottomLeft = Bottom | Left,   // 36
    BottomRight = Bottom | Right  // 48
  };

  Position position(double x, double y) const;

  static bool onEdge(Position pos) { return (pos > Outside); }
  static bool onSameEdge(Position pos1, Position pos2) { return onEdge(Position(pos1 & pos2)); }
  static Position nextEdge(Position pos)
  {
    switch (pos)
    {
      case BottomLeft:
      case Left:
        return Top;
      case TopLeft:
      case Top:
        return Right;
      case TopRight:
      case Right:
        return Bottom;
      case BottomRight:
      case Bottom:
        return Left;

      default:
        return pos;  // should never be happen!
    }
  }

  std::size_t hashValue() const;

 private:
  double itsX1;  // bottom left coordinates
  double itsY1;
  double itsX2;  // top right coordinates
  double itsY2;

  double itsXMin;  // clipping rectangle
  double itsYMin;
  double itsXMax;
  double itsYMax;

  std::size_t itsWidth;  // size in pixel coordinates
  std::size_t itsHeight;

  double itsXalpha;
  double itsXbeta;
  double itsYalpha;
  double itsYbeta;
};

// ----------------------------------------------------------------------
/*!
 * \brief Calculate position with respect to the rectangle
 *
 * We call this with the expectation for the point to be in, the tests
 * are ordered accordingly.
 */
// ----------------------------------------------------------------------

inline Box::Position Box::position(double x, double y) const
{
  if (x > itsXMin && x < itsXMax && y > itsYMin && y < itsYMax)
    return Inside;
  if (x < itsXMin || x > itsXMax || y < itsYMin || y > itsYMax)
    return Outside;

  unsigned int pos = 0;
  if (x == itsXMin)
    pos |= Left;
  else if (x == itsXMax)
    pos |= Right;
  if (y == itsYMin)
    pos |= Bottom;
  else if (y == itsYMax)
    pos |= Top;
  return Position(pos);
}

}  // namespace Fmi
