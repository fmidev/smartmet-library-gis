#include "Shape_sphere.h"
#include "OGR.h"
#include "ShapeClipper.h"
#include <macgyver/Exception.h>
#include <ogr_geometry.h>

namespace Fmi
{
#ifndef PI
#define PI 3.14159265358979323846
#define PI2 6.28318530718
#define EARTH_RADIUS 6378137
#define DEG_TO_RAD 0.0174532925199
#endif

#define DELTA 1000000000

Shape_sphere::Shape_sphere(double theX, double theY, double theRadius)
{
  try
  {
    itsX = theX;
    itsY = theY;
    itsRadius = theRadius;
    itsRadius2 = theRadius * theRadius;
    itsXDelta = theX + DELTA;
    itsYDelta = theY + DELTA;
    itsBorderStep = 10000;
    itsBorderAngleStep = PI / 360;

    sr_latlon.importFromEPSG(4326);
    sr_latlon.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);

    sr.SetAE(itsX, itsY, 0, 0);
    sr.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
    /*
    // Mercator
    sr.importFromEPSG(3395);
    sr.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
*/

    transformation = OGRCreateCoordinateTransformation(&sr_latlon, &sr);
    reverseTransformation = OGRCreateCoordinateTransformation(&sr, &sr_latlon);

    getMetricCoordinates(itsX, itsY, itsXX, itsYY);
    /*
        double xx = 0;
        double yy = 0;
        getLatLonCoordinates(itsXX,itsYY,xx,yy);
        if (itsX != xx  || itsY != yy )
        {
          printf("REVERSE FAILED %f,%f => %f,%f  (%f,%f)\n",itsXX,itsYY,xx,yy,itsX,itsY);
          exit(1);
        }
    */
    itsXXMin = itsXX - itsRadius;
    itsYYMin = itsYY - itsRadius;
    itsXXMax = itsXX + itsRadius;
    itsYYMax = itsYY + itsRadius;
    itsXXDelta = itsXX + DELTA;
    itsYYDelta = itsYY + DELTA;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

Shape_sphere::~Shape_sphere()
{
  try
  {
    if (transformation != nullptr)
      OCTDestroyCoordinateTransformation(transformation);

    if (reverseTransformation != nullptr)
      OCTDestroyCoordinateTransformation(reverseTransformation);
  }
  catch (...)
  {
    Fmi::Exception exception(BCP, "Destructor failed", nullptr);
    exception.printError();
  }
}

double Shape_sphere::angleDistance_cw(double a, double b) const
{
  try
  {
    if (b <= a)
      return (a - b);

    return (PI2 - (b - a));
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

double Shape_sphere::angleDistance_ccw(double a, double b) const
{
  try
  {
    if (a <= b)
      return (b - a);

    return (PI2 - (a - b));
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

double Shape_sphere::distance(double a, double b) const
{
  try
  {
    return fabs((a + DELTA) - (b + DELTA));
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

int Shape_sphere::getPosition(double x, double y) const
{
  try
  {
    getMetricCoordinates(x, y, x, y);
    return getPositionByMetricCoordinates(x, y);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

int Shape_sphere::getPositionByMetricCoordinates(double x, double y) const
{
  try
  {
    double dx = distance(x + DELTA, itsXXDelta);
    double dy = distance(y + DELTA, itsYYDelta);
    double r2 = (dx * dx) + (dy * dy);

    if (r2 <= itsRadius2)
      return Inside;

    return Outside;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

void Shape_sphere::getLatLonPointByAngle(double angle, double& x, double& y) const
{
  try
  {
    x = itsXX + cos(angle) * itsRadius;
    y = itsYY + sin(angle) * itsRadius;

    getLatLonCoordinates(x, y, x, y);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

void Shape_sphere::getMetricPointByAngle(double angle, double& x, double& y) const
{
  try
  {
    x = itsXX + cos(angle) * itsRadius;
    y = itsYY + sin(angle) * itsRadius;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

void Shape_sphere::setBorderStep(double theBorderStep)
{
  try
  {
    itsBorderStep = theBorderStep;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

void Shape_sphere::setRadius(double theRadius)
{
  try
  {
    itsRadius = theRadius;
    itsRadius2 = theRadius * theRadius;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

double Shape_sphere::getAngle(double x, double y) const
{
  try
  {
    double dx = distance(x + DELTA, itsX + DELTA);
    double dy = distance(y + DELTA, itsY + DELTA);

    double r = sqrt((dx * dx) + (dy * dy));

    if (x >= itsX && y >= itsY)
      return asin(dy / r);

    if (x < itsX && y >= itsY)
      return (PI - asin(dy / r));

    if (x < itsX && y < itsY)
      return (PI + asin(dy / r));

    if (x >= itsX && y < itsY)
      return (PI2 - asin(dy / r));

    return 0;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

bool Shape_sphere::isOnEdge(double xx, double yy, double& angle) const
{
  try
  {
    double dx = distance(xx + DELTA, itsXXDelta);
    double dy = distance(yy + DELTA, itsYYDelta);

    double r2 = (dx * dx) + (dy * dy);
    double dist = distance(r2, itsRadius2);
    // printf(" -- OnEdge %f,%f (%f,%f)  %f %f   %f\n", xx, yy, itsXXDelta, itsYYDelta, r2,
    // itsRadius2, dist);

    if (dist < 10.0)
    {
      if (xx >= itsXX && yy >= itsYY)
      {
        angle = asin(dy / itsRadius);
        return true;
      }

      if (xx < itsXX && yy >= itsYY)
      {
        angle = PI - asin(dy / itsRadius);
        return true;
      }

      if (xx < itsXX && yy < itsYY)
      {
        angle = PI + asin(dy / itsRadius);
        return true;
      }

      if (xx >= itsXX && yy < itsYY)
      {
        angle = PI2 - asin(dy / itsRadius);
        return true;
      }
    }

    return false;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

bool Shape_sphere::isOnEdge(double xx, double yy) const
{
  try
  {
    double dx = distance(xx + DELTA, itsXXDelta);
    double dy = distance(yy + DELTA, itsYYDelta);

    double r2 = (dx * dx) + (dy * dy);
    double dist = distance(r2, itsRadius2);
    // printf(" -- OnEdge %f,%f (%f,%f)  %f %f   %f\n", xx, yy, itsXXDelta, itsYYDelta, r2,
    // itsRadius2, dist);

    if (dist < 10.0)
      return true;

    return false;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

int Shape_sphere::getLineIntersectionPoints(double aX,
                                            double aY,
                                            double bX,
                                            double bY,
                                            double& pX1,
                                            double& pY1,
                                            double& pX2,
                                            double& pY2) const
{
  try
  {
    // printf("Intersection %f,%f,%f,%f   %f,%f r=%f\n", aX, aY, bX, bY, centerX, centerY, radius);

    // Fast checks before more detailed x calculations. Checking if the both
    // end points on the same side of the ring (=> No intersection)

    if (aX < itsXXMin && bX < itsXXMin)
      return 0;

    if (aX > itsXXMax && bX > itsXXMax)
      return 0;

    if (aY < itsYYMin && bY < itsYYMin)
      return 0;

    if (aY > itsYYMax && bY > itsYYMax)
      return 0;

    // Shifting coordinates so that they are all positive numbers, because
    // formulas does not necessary work if there are positive and negative
    // coordinates. Making better formulas might make this faster.

    aX = aX + DELTA;
    aY = aY + DELTA;
    bX = bX + DELTA;
    bY = bY + DELTA;

    double baX = bX - aX;
    double baY = bY - aY;

    if (baX > -0.0001 && baX < 0.0001 && baY > -0.0001 && baY < 0.0001)
    {
      // Both end points are almost the same (=> same point, not a line)
      return 5;
    }

    double caX = itsXXDelta - aX;
    double caY = itsYYDelta - aY;
    double cbX = itsXXDelta - bX;
    double cbY = itsYYDelta - bY;

    double ar2 = (caX * caX) + (caY * caY);
    double br2 = (cbX * cbX) + (cbY * cbY);

    // Both endpoint are inside the sphere
    if (ar2 <= itsRadius2 && br2 <= itsRadius2)
      return 1;

    double abDist2 = baX * baX + baY * baY;
    double bBy2 = baX * caX + baY * caY;
    double c = ar2 - itsRadius2;
    double pBy2 = bBy2 / abDist2;
    double q = c / abDist2;
    double d = pBy2 * pBy2 - q;

    if (d < 0)  // No intersection points
      return 0;

    if (d == 0)  // One intersection point (= tangent). We can ignore it.
      return 0;

    double tmpSqrt = sqrt(d);
    double abScalingFactor1 = -pBy2 + tmpSqrt;
    double abScalingFactor2 = -pBy2 - tmpSqrt;

    // Intersection points:

    pX1 = aX - baX * abScalingFactor1;
    pY1 = aY - baY * abScalingFactor1;
    pX2 = aX - baX * abScalingFactor2;
    pY2 = aY - baY * abScalingFactor2;

    if (std::isnan(pX1) || std::isnan(pY1) || std::isnan(pX2) || std::isnan(pY2))
      return 0;

    if (std::max(aX, bX) < std::min(pX1, pX2))
      return 0;

    if (std::max(aY, bY) < std::min(pY1, pY2))
      return 0;

    if (std::min(aX, bX) > std::max(pX1, pX2))
      return 0;

    if (std::min(aY, bY) > std::max(pY1, pY2))
      return 0;

    if (ar2 <= itsRadius2 && br2 > itsRadius2)
    {
      // The first endpoint is inside the circle

      double bpx1 = pX1 - bX;
      double bpy1 = pY1 - bY;
      double bpx2 = pX2 - bX;
      double bpy2 = pY2 - bY;
      double dp1 = bpx1 * bpx1 + bpy1 * bpy1;
      double dp2 = bpx2 * bpx2 + bpy2 * bpy2;

      if (dp2 < dp1)
      {
        pX1 = pX2;
        pY1 = pY2;
        // printf("SWAP 1\n");
      }
      pX1 = pX1 - DELTA;
      pY1 = pY1 - DELTA;
      return 2;
    }

    if (ar2 > itsRadius2 && br2 <= itsRadius2)
    {
      // The second endpoint is inside the circle

      double apx1 = pX1 - aX;
      double apy1 = pY1 - aY;
      double apx2 = pX2 - aX;
      double apy2 = pY2 - aY;
      double dp1 = apx1 * apx1 + apy1 * apy1;
      double dp2 = apx2 * apx2 + apy2 * apy2;

      if (dp2 < dp1)
      {
        pX1 = pX2;
        pY1 = pY2;
        // printf("SWAP 2\n");
      }
      pX1 = pX1 - DELTA;
      pY1 = pY1 - DELTA;
      return 3;
    }

    double distX = pX1 - pX2;
    double distY = pY1 - pY2;

    double dd = (distX) * (distX) + (distY) * (distY);
    if (dd > abDist2)
    {
      // Both points are on the same side of the sphere
      // printf("Same side %f %f\n",dd,a);
      return 6;
    }

    if (dd < 0.0001)
    {
      // Both intersection points are so close that they are in practice the same point
      return 7;
    }

    if ((pX1 < pX2 && aX > bX) || (pX1 == pX2 && pY1 < pY2 && aY > bY))
    {
      double tX = pX1;
      double tY = pY1;
      pX1 = pX2 - DELTA;
      pY1 = pY2 - DELTA;
      pX2 = tX - DELTA;
      pY2 = tY - DELTA;

      return 4;
    }

    pX1 = pX1 - DELTA;
    pY1 = pY1 - DELTA;
    pX2 = pX2 - DELTA;
    pY2 = pY2 - DELTA;

    return 4;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

OGRLinearRing* Shape_sphere::makeRing(double theMaximumSegmentLength) const
{
  try
  {
    OGRLinearRing* ring = new OGRLinearRing;
    double angle = PI2;
    while (angle > 0)
    {
      double xx = 0;
      double yy = 0;
      getLatLonPointByAngle(angle, xx, yy);
      ring->addPoint(xx, yy);
      angle = angle - itsBorderAngleStep;
    }

    if (theMaximumSegmentLength > 0)
      ring->segmentize(theMaximumSegmentLength);

    return ring;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

OGRLineString* Shape_sphere::makeLineRing(double theMaximumSegmentLength) const
{
  try
  {
    OGRLineString* ring = new OGRLineString;
    double angle = PI2;
    while (angle > 0)
    {
      double xx = 0;
      double yy = 0;
      getLatLonPointByAngle(angle, xx, yy);
      ring->addPoint(xx, yy);
      angle = angle - itsBorderAngleStep;
    }

    if (theMaximumSegmentLength > 0)
      ring->segmentize(theMaximumSegmentLength);

    return ring;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

OGRLinearRing* Shape_sphere::makeHole(double theMaximumSegmentLength) const
{
  try
  {
    OGRLinearRing* ring = makeRing(theMaximumSegmentLength);
    ring->reverseWindingOrder();
    // ring->reversePoints();
    return ring;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

int Shape_sphere::cut(const OGRLineString* theGeom, ShapeClipper& theClipper, bool exterior) const
{
  try
  {
    int n = theGeom->getNumPoints();
    if (theGeom == nullptr || n < 1)
      return 0;

    const OGRLineString& g = *theGeom;
    auto* line = new OGRLineString();
    double xA = g.getX(0);
    double yA = g.getY(0);
    double xxA = xA;
    double yyA = xA;

    getMetricCoordinates(xA, yA, xxA, yyA);

    auto posA = getPositionByMetricCoordinates(xxA, yyA);
    auto posB = posA;
    auto position = posA;

    if (posA == Position::Outside)
      line->addPoint(xA, yA);

    for (int i = 1; i < n; i++)
    {
      double xB = g.getX(i);
      double yB = g.getY(i);
      double xxB = xB;
      double yyB = yB;
      getMetricCoordinates(xB, yB, xxB, yyB);

      posB = getPositionByMetricCoordinates(xxB, yyB);
      position |= posB;

      double pX1 = 0, pY1 = 0, pX2 = 0, pY2 = 0;
      int res = getLineIntersectionPoints(xxA, yyA, xxB, yyB, pX1, pY1, pX2, pY2);
      // printf("getLineIntersectionPoints(%f,%f,%f,%f  %f,%f,%f,%f  %f,%f,%f,%f) = %d\n",xA, yA,
      // xB, yB, xxA, yyA, xxB, yyB, pX1, pY1, pX2, pY2,res);

      switch (res)
      {
        case 0:  // Both points are outside
          line->addPoint(xB, yB);
          break;

        case 2:  // The first point is inside, the second point is outside
          if (round(pX1) != round(xxB) || round(pY1) != round(yyB))
          {
            getLatLonCoordinates(pX1, pY1, pX1, pY1);
            line->addPoint(pX1, pY1);
          }

          line->addPoint(xB, yB);
          break;

        case 3:  // The first point is outside, the second point is inside
          getLatLonCoordinates(pX1, pY1, pX1, pY1);
          line->addPoint(pX1, pY1);
          if (exterior)
            theClipper.addExterior(line);
          else
            theClipper.addInterior(line);

          line = new OGRLineString();
          break;

        case 4:  // Both end point are outside, but the line intersects with the sphere
          position |= Position::Outside | Position::Inside;
          getLatLonCoordinates(pX1, pY1, pX1, pY1);
          line->addPoint(pX1, pY1);
          if (exterior)
            theClipper.addExterior(line);
          else
            theClipper.addInterior(line);

          line = new OGRLineString();
          getLatLonCoordinates(pX2, pY2, pX2, pY2);
          line->addPoint(pX2, pY2);
          if (pX2 != xB || pY2 != yB)
            line->addPoint(xB, yB);
          break;
      }

      posA = posB;
      xA = xB;
      yA = yB;
      xxA = xxB;
      yyA = yyB;
    }

    if (line->getNumPoints() > 0)
    {
      if (exterior)
        theClipper.addExterior(line);
      else
        theClipper.addInterior(line);
    }
    else
    {
      delete line;
    }

    return position;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

int Shape_sphere::clip(const OGRLineString* theGeom, ShapeClipper& theClipper, bool exterior) const
{
  try
  {
    int n = theGeom->getNumPoints();
    if (theGeom == nullptr || n < 1)
      return 0;

    const OGRLineString& g = *theGeom;
    auto* line = new OGRLineString();
    double xA = g.getX(0);
    double yA = g.getY(0);
    double xxA = xA;
    double yyA = xA;

    getMetricCoordinates(xA, yA, xxA, yyA);

    auto posA = getPositionByMetricCoordinates(xxA, yyA);
    auto posB = posA;
    auto position = posA;

    if (posA == Position::Inside)
      line->addPoint(xA, yA);

    for (int i = 1; i < n; i++)
    {
      // Establish initial position

      double xB = g.getX(i);
      double yB = g.getY(i);
      double xxB = xB;
      double yyB = yB;
      getMetricCoordinates(xB, yB, xxB, yyB);

      posB = getPositionByMetricCoordinates(xxB, yyB);

      position |= posB;

      double pX1 = 0, pY1 = 0, pX2 = 0, pY2 = 0;
      int res = getLineIntersectionPoints(xxA, yyA, xxB, yyB, pX1, pY1, pX2, pY2);
      // printf("getLineIntersectionPoints(%f,%f,%f,%f  %f,%f,%f,%f  %f,%f,%f,%f) = %d\n",xA, yA,
      // xB, yB, xxA, yyA, xxB, yyB, pX1, pY1, pX2, pY2,res);

      switch (res)
      {
        case 1:  // Both points are inside
          line->addPoint(xB, yB);
          break;

        case 2:  // The first point is inside, the second point is outside
          getLatLonCoordinates(pX1, pY1, pX1, pY1);
          line->addPoint(pX1, pY1);
          if (exterior)
            theClipper.addExterior(line);
          else
            theClipper.addInterior(line);
          line = new OGRLineString();
          break;

        case 3:  // The first point is outside, the second point is inside
          getLatLonCoordinates(pX1, pY1, pX1, pY1);
          if (pX1 != xB || pY1 != yB)
            line->addPoint(pX1, pY1);

          line->addPoint(xB, yB);
          break;

        case 4:  // Both end point are outside, but the line intersects with the sphere
          position |= Position::Outside | Position::Inside;
          getLatLonCoordinates(pX1, pY1, pX1, pY1);
          getLatLonCoordinates(pX2, pY2, pX2, pY2);
          line->addPoint(pX1, pY1);
          line->addPoint(pX2, pY2);
          if (exterior)
            theClipper.addExterior(line);
          else
            theClipper.addInterior(line);
          line = new OGRLineString();
          break;
      }

      posA = posB;
      xA = xB;
      yA = yB;
      xxA = xxB;
      yyA = yyB;
    }

    if (line->getNumPoints() > 0)
    {
      if (exterior)
        theClipper.addExterior(line);
      else
        theClipper.addInterior(line);
    }
    else
    {
      delete line;
    }

    return position;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

bool Shape_sphere::isInsideRing(const OGRLinearRing& theRing) const
{
  try
  {
    Shape_sphere sphere(itsX, itsY, itsRadius - 0.0001);

    uint points = 36;
    double step = 2 * PI / (double)points;
    double angle = 0;
    for (uint t = 0; t < points; t++)
    {
      double xx = 0, yy = 0;
      sphere.getLatLonPointByAngle(angle, xx, yy);
      if (!OGR::inside(theRing, xx, yy))
        return false;

      angle = angle + step;
    }
    return true;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

bool Shape_sphere::isRingInside(const OGRLinearRing& theRing) const
{
  try
  {
    int n = theRing.getNumPoints();
    if (n < 1)
      return false;

    for (int i = 0; i < n; ++i)
    {
      double x = theRing.getX(i);
      double y = theRing.getY(i);

      auto pos = getPosition(x, y);

      if (pos == Position::Outside)
        return false;

      if (pos == Position::Inside)
        return true;
    }

    return false;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Search for matching line segment clockwise (clipping)
 */
// ----------------------------------------------------------------------

LineIterator Shape_sphere::search_cw(OGRLinearRing* ring,
                                     std::list<OGRLineString*>& lines,
                                     double x1,
                                     double y1,
                                     double& x2,
                                     double& y2) const
{
  try
  {
    double xx1 = x1;
    double yy1 = y1;
    double xx2 = x2;
    double yy2 = y2;
    getMetricCoordinates(x1, y1, xx1, yy1);
    getMetricCoordinates(x2, y2, xx2, yy2);

    auto best = lines.end();
    double angle1 = 0;
    double bestAngleDiff = 1000;

    if (isOnEdge(xx1, yy1, angle1))
    {
      double angle2 = 0;
      if (isOnEdge(xx2, yy2, angle2))
      {
        // Sometimes the best option is to connect the end points
        // of the current line.

        double angleDiff = angle1 - angle2;

        if (angleDiff > PI)
          angleDiff = PI2 - angleDiff;

        if (angleDiff < -PI)
          angleDiff = PI2 + angleDiff;

        if (angleDiff > 0)
          bestAngleDiff = angleDiff;
      }

      for (auto iter = lines.begin(); iter != lines.end(); ++iter)
      {
        double x = (*iter)->getX(0);
        double y = (*iter)->getY(0);
        double xx = x;
        double yy = y;
        getMetricCoordinates(x, y, xx, yy);

        if (isOnEdge(xx, yy, angle2))
        {
          double angleDiff = angle1 - angle2;
          if (angle2 > angle1)
            angleDiff = PI2 - (angle2 - angle1);

          if (angleDiff < bestAngleDiff)
          {
            x2 = x;
            y2 = y;
            best = iter;
            bestAngleDiff = angleDiff;
            if (angle2 > angle1)
              angle2 = angle2 - 2 * PI;
            // printf("++ BEST ANGLE %f %f => %f,%f   %f %f
            // %f\n",x1,y1,x2,y2,angle1,angle2,bestAngleDiff);
          }
        }
      }
    }
    return best;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Search for matching line segment counter-clockwise (cutting)
 */
// ----------------------------------------------------------------------

LineIterator Shape_sphere::search_ccw(OGRLinearRing* ring,
                                      std::list<OGRLineString*>& lines,
                                      double x1,
                                      double y1,
                                      double& x2,
                                      double& y2) const
{
  try
  {
    double xx1 = x1;
    double yy1 = y1;
    double xx2 = x2;
    double yy2 = y2;
    getMetricCoordinates(x1, y1, xx1, yy1);
    getMetricCoordinates(x2, y2, xx2, yy2);

    auto best = lines.end();
    double angle1 = 0;
    double bestAngleDiff = 1000;

    if (isOnEdge(xx1, yy1, angle1))
    {
      double angle2 = 0;
      if (isOnEdge(xx2, yy2, angle2))
      {
        double angleDiff = angle2 - angle1;
        if (angleDiff > PI)
          angleDiff = PI2 - angleDiff;

        if (angleDiff < -PI)
          angleDiff = PI2 + angleDiff;

        bestAngleDiff = angleDiff;
      }

      for (auto iter = lines.begin(); iter != lines.end(); ++iter)
      {
        double x = (*iter)->getX(0);
        double y = (*iter)->getY(0);
        double xx = x;
        double yy = y;
        getMetricCoordinates(x, y, xx, yy);

        if (isOnEdge(xx, yy, angle2))
        {
          // printf("Angles %f,%f => %f,%f  %f %f %f\n",x1,y1,x,y,angle1,angle2,bestAngleDiff);
          double angleDiff = angle2 - angle1;
          if (angle2 < angle1)
            angleDiff = PI2 - (angle1 - angle2);

          if (angleDiff < bestAngleDiff)
          {
            x2 = x;
            y2 = y;
            best = iter;
            bestAngleDiff = angleDiff;
            // printf("-- BEST ANGLE %f %f => %f,%f   %f %f
            // %f\n",x1,y1,x2,y2,angle1,angle2,bestAngleDiff);
          }
        }
      }
    }
    return best;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

bool Shape_sphere::connectPoints_cw(OGRLinearRing& ring,
                                    double x1,
                                    double y1,
                                    double x2,
                                    double y2,
                                    double theMaximumSegmentLength) const
{
  try
  {
    double xx1 = x1;
    double yy1 = y1;
    double xx2 = x2;
    double yy2 = y2;
    getMetricCoordinates(x1, y1, xx1, yy1);
    getMetricCoordinates(x2, y2, xx2, yy2);

    double angle1 = 0;
    double angle2 = 0;

    // printf(" ++ connect points %f,%f => %f,%f\n",x1,y1,x2,y2);
    if (!isOnEdge(xx1, yy1, angle1) || !isOnEdge(xx2, yy2, angle2))
      return false;  // The end points are not on the edge of the sphere

    double xd = distance(xx1, xx2);
    double yd = distance(yy1, yy2);
    double dist = sqrt(xd * xd + yd * yd);
    if (dist < itsBorderStep)
      return false;

    // printf(" ++ angles %f  %f  dist=%f\n",angle1,angle2,dist);
    Shape_sphere outerCircle(itsX, itsY, itsRadius + 0.0001);

    double angleDiff = 0;
    angleDiff = -angleDistance_cw(angle1, angle2);

    if (angleDiff > PI)
      angleDiff = PI2 - angleDiff;

    if (angleDiff < -PI)
      angleDiff = PI2 + angleDiff;

    double xx = 0, yy = 0;
    outerCircle.getMetricPointByAngle(angle1, xx, yy);

    double x = xx, y = yy;
    getLatLonCoordinates(xx, yy, x, y);

    ring.addPoint(x1, y1);
    ring.addPoint(x, y);
    ring.addPoint(x1, y1);

    uint points = dist / itsBorderStep;
    double ad = angleDiff / points;
    for (uint t = 0; t < points; t++)
    {
      getMetricPointByAngle(angle1, xx, yy);
      // printf("++ getPoint %f   %f,%f\n", angle1, xx, yy);
      getLatLonCoordinates(xx, yy, x, y);
      ring.addPoint(x, y);
      angle1 = angle1 + ad;
    }
    return true;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

bool Shape_sphere::connectPoints_ccw(OGRLinearRing& ring,
                                     double x1,
                                     double y1,
                                     double x2,
                                     double y2,
                                     double theMaximumSegmentLength) const
{
  try
  {
    double xx1 = x1;
    double yy1 = y1;
    double xx2 = x2;
    double yy2 = y2;
    getMetricCoordinates(x1, y1, xx1, yy1);
    getMetricCoordinates(x2, y2, xx2, yy2);

    double angle1 = 0;
    double angle2 = 0;

    // printf(" -- connect points ccw %f,%f => %f,%f\n",x1,y1,x2,y2);
    if (!isOnEdge(xx1, yy1, angle1) || !isOnEdge(xx2, yy2, angle2))
      return false;  // The end points are not on the edge of the sphere

    double xd = distance(xx1, xx2);
    double yd = distance(yy1, yy2);
    double dist = sqrt(xd * xd + yd * yd);

    // printf(" -- angles %f  %f  dist=%f\n",angle1,angle2,dist);
    if (dist < itsBorderStep)
      return false;

    Shape_sphere innerCircle(itsX, itsY, itsRadius - 0.0001);

    double angleDiff = 0;
    angleDiff = angleDistance_ccw(angle1, angle2);

    if (angleDiff > PI)
      angleDiff = PI2 - angleDiff;

    if (angleDiff < -PI)
      angleDiff = PI2 + angleDiff;

    double xx = 0, yy = 0;
    innerCircle.getMetricPointByAngle(angle1, xx, yy);

    double x = xx, y = yy;
    getLatLonCoordinates(xx, yy, x, y);

    ring.addPoint(x1, y1);
    ring.addPoint(x, y);
    ring.addPoint(x1, y1);

    uint points = dist / itsBorderStep;
    // printf("POINTS %u  = %f / %f\n",points,dist,itsBorderStep);
    double ad = angleDiff / points;
    for (uint t = 0; t < points; t++)
    {
      getMetricPointByAngle(angle1, xx, yy);
      // printf("-- getPoint %f   %f,%f\n", angle1, xx, yy);
      getLatLonCoordinates(xx, yy, x, y);
      ring.addPoint(x, y);
      angle1 = angle1 + ad;
    }

    // printf("-- add %f,%f\n",x2,y2);
    ring.addPoint(x2, y2);

    return true;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

void Shape_sphere::getMetricCoordinates(double lon, double lat, double& x, double& y) const
{
  try
  {
    x = lon;
    y = lat;
    transformation->Transform(1, &x, &y);
    // printf("** COORD %f,%f => %f,%f\n",lon,lat,x,y);
  }
  catch (...)
  {
    throw Fmi::Exception(BCP, "Operation failed!", nullptr);
  }
}

void Shape_sphere::getLatLonCoordinates(double x, double y, double& lon, double& lat) const
{
  try
  {
    lon = x;
    lat = y;
    reverseTransformation->Transform(1, &lon, &lat);
    // printf("LATCOORD %f,%f => %f,%f\n",x,y,lon,lat);
  }
  catch (...)
  {
    throw Fmi::Exception(BCP, "Operation failed!", nullptr);
  }
}

void Shape_sphere::print(std::ostream& stream)
{
  try
  {
    stream << "Shape_cirle\n";
    stream << "- itsX           = " << itsX << "\n";
    stream << "- itsY           = " << itsY << "\n";
    stream << "- itsRadius      = " << itsRadius << "\n";
    stream << "- itsXXMin       = " << itsXXMin << "\n";
    stream << "- itsYYMin       = " << itsYYMin << "\n";
    stream << "- itsXXMax       = " << itsXXMax << "\n";
    stream << "- itsXYMax       = " << itsYYMax << "\n";
    stream << "- itsBorderStep  = " << itsBorderStep << "\n";
  }
  catch (...)
  {
    throw Fmi::Exception(BCP, "Operation failed!", nullptr);
  }
}

}  // namespace Fmi
