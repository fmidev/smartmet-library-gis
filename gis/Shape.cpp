#include "Shape.h"
#include "ShapeClipper.h"

namespace Fmi
{
Shape::Shape() = default;

Shape::~Shape() = default;

int Shape::clip(const OGRLineString * /* theGeom */,
                ShapeClipper & /*theClipper */,
                bool /* exterior */) const
{
  throw Fmi::Exception(BCP, "Not implemented!");
}

int Shape::cut(const OGRLineString * /* theGeom */,
               ShapeClipper & /* theClipper */,
               bool /* exterior */) const
{
  throw Fmi::Exception(BCP, "Not implemented!");
}

bool Shape::connectPoints_cw(OGRLinearRing & /* ring */,
                             double /* x1 */,
                             double /* y1 */,
                             double /* x2 */,
                             double /* y2 */,
                             double /* theMaximumSegmentLength */) const
{
  throw Fmi::Exception(BCP, "Not implemented!");
}

bool Shape::connectPoints_ccw(OGRLinearRing & /* ring */,
                              double /* x1 */,
                              double /* y1 */,
                              double /* x2 */,
                              double /* y2 */,
                              double /* theMaximumSegmentLength */) const
{
  throw Fmi::Exception(BCP, "Not implemented!");
}

int Shape::getPosition(double /* x */, double /* y */) const
{
  throw Fmi::Exception(BCP, "Not implemented!");
}

/*
int Shape::getLineIntersectionPoints(double aX, double aY, double bX, double bY,double& pX1, double&
pY1, double& pX2, double& pY2) const
{
  throw Fmi::Exception(BCP,"Not implemented!");
}
*/

bool Shape::isInsideRing(const OGRLinearRing & /* theRing */) const
{
  throw Fmi::Exception(BCP, "Not implemented!");
}

/*
bool Shape::isOnEdge(double x, double y) const
{
  throw Fmi::Exception(BCP,"Not implemented!");
}
*/

bool Shape::isRingInside(const OGRLinearRing & /* theRing */) const
{
  throw Fmi::Exception(BCP, "Not implemented!");
}

OGRLinearRing *Shape::makeRing(double /* theMaximumSegmentLength */) const
{
  throw Fmi::Exception(BCP, "Not implemented!");
}

OGRLineString *Shape::makeLineRing(double /* theMaximumSegmentLength */) const
{
  throw Fmi::Exception(BCP, "Not implemented!");
}

OGRLinearRing *Shape::makeHole(double /* theMaximumSegmentLength */) const
{
  throw Fmi::Exception(BCP, "Not implemented!");
}

LineIterator Shape::search_cw(OGRLinearRing * /* ring */,
                              std::list<OGRLineString *> & /* lines */,
                              double /* x1 */,
                              double /* y1 */,
                              double & /* x2 */,
                              double & /* y2 */) const
{
  throw Fmi::Exception(BCP, "Not implemented!");
}

LineIterator Shape::search_ccw(OGRLinearRing * /* ring */,
                               std::list<OGRLineString *> & /* lines */,
                               double /* x1 */,
                               double /* y1 */,
                               double & /* x2 */,
                               double & /* y2 */) const
{
  throw Fmi::Exception(BCP, "Not implemented!");
}

void Shape::print(std::ostream & /* stream */)
{
  throw Fmi::Exception(BCP, "Not implemented!");
}

// ----------------------------------------------------------------------
/*!
 * \brief Test if all positions were outside the box or on the edges
 */
// ----------------------------------------------------------------------
bool Shape::all_not_inside(int position)
{
  return ((position & Shape::Position::Inside) == 0);
}

// ----------------------------------------------------------------------
/*!
 * \brief Test if all positions were on the edges or inside the box
 */
// ----------------------------------------------------------------------
bool Shape::all_not_outside(int position)
{
  return ((position & Shape::Position::Outside) == 0);
}

// ----------------------------------------------------------------------
/*!
 * \brief Test if all positions were inside the box
 */
// ----------------------------------------------------------------------

bool Shape::all_only_inside(int position)
{
  return (position & Shape::Position::Inside) != 0 && !(position & (position - 1));
}

// ----------------------------------------------------------------------
/*!
 * \brief Test if all positions were outside the box
 */
// ----------------------------------------------------------------------

bool Shape::all_only_outside(int position)
{
  return (position & Shape::Position::Outside) != 0 && !(position & (position - 1));
}

}  // namespace Fmi
