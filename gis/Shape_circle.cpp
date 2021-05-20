#include "Shape_circle.h"
#include "ShapeClipper.h"
#include <macgyver/Exception.h>
#include <ogr_geometry.h>
#include "OGR.h"


namespace Fmi
{


#ifndef PI
  #define PI 3.14159265358979323846
  #define PI2 6.28318530718
#endif

#define DELTA 1000000000



Shape_circle::Shape_circle(double theX, double theY, double theRadius)
{
  try
  {
    itsX = theX;
    itsY = theY;
    itsRadius = theRadius;
    itsRadius2 = theRadius*theRadius;
    itsXDelta = theX + DELTA;
    itsYDelta = theY + DELTA;
    itsXMin = itsX - itsRadius;
    itsYMin = itsY - itsRadius;
    itsXMax = itsX + itsRadius;
    itsYMax = itsY + itsRadius;
    itsBorderStep = 3.1415926535898 / 180;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}





Shape_circle::~Shape_circle()
{
}





double Shape_circle::angleDistance_cw(double a,double b) const
{
  try
  {
    if (b <= a)
      return (a-b);

    return (PI2 - (b - a));
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}





double Shape_circle::angleDistance_ccw(double a,double b) const
{
  try
  {
    if (a <= b)
      return (b-a);

    return (PI2 - (a - b));
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}





double Shape_circle::distance(double a,double b) const
{
  try
  {
    return fabs((a+DELTA) - (b+DELTA));
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}





int Shape_circle::getPosition(double x, double y) const
{
  try
  {
    double dx = distance(x+DELTA,itsXDelta);
    double dy = distance(y+DELTA,itsYDelta);
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





void Shape_circle::getPointByAngle(double angle,double& x, double& y) const
{
  try
  {
    x = itsX + cos(angle)*itsRadius;
    y = itsY + sin(angle)*itsRadius;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}





void Shape_circle::setBorderStep(double theBorderStep)
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





void Shape_circle::setRadius(double theRadius)
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





double Shape_circle::getAngle(double x, double y) const
{
  try
  {
    double dx = distance(x + DELTA, itsXDelta);
    double dy = distance(y + DELTA, itsYDelta);

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





bool Shape_circle::isOnEdge(double x, double y, double& angle) const
{
  try
  {
    double dx = distance(x + DELTA, itsXDelta);
    double dy = distance(y + DELTA, itsYDelta);

    double r2 = (dx * dx) + (dy * dy);
    double dist = distance(r2, itsRadius2);
    //printf("OnEdge %f,%f  %f %f   %f\n", x, y, r2, itsRadius2, dist);

    if (dist < 0.0001)
    {
      if (x >= itsX && y >= itsY)
      {
        angle = asin(dy / itsRadius);
        return true;
      }

      if (x < itsX && y >= itsY)
      {
        angle = PI - asin(dy / itsRadius);
        return true;
      }

      if (x < itsX && y < itsY)
      {
        angle = PI + asin(dy / itsRadius);
        return true;
      }

      if (x >= itsX && y < itsY)
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





bool Shape_circle::isOnEdge(double x, double y) const
{
  try
  {
    double dx = distance(x + DELTA, itsXDelta);
    double dy = distance(y + DELTA, itsYDelta);

    double r2 = (dx * dx) + (dy * dy);
    double dist = distance(r2, itsRadius2);

    if (dist < 0.0001)
      return true;

    return false;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}





int Shape_circle::getLineIntersectionPoints(double aX, double aY, double bX, double bY, double& pX1, double& pY1, double& pX2, double& pY2) const
{
  try
  {
    //printf("Intersection %f,%f,%f,%f   %f,%f r=%f\n", aX, aY, bX, bY, centerX, centerY, radius);

    // Fast checks before more detailed x calculations. Checking if the both
    // end points on the same side of the ring (=> No intersection)

    if (aX < itsXMin  &&  bX < itsXMin)
      return 0;

    if (aX > itsXMax  &&  bX > itsXMax)
      return 0;

    if (aY < itsYMin  &&  bY < itsYMin)
      return 0;

    if (aY > itsYMax  &&  bY > itsYMax)
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

    if (baX > -0.0001  &&  baX < 0.0001  &&  baY > -0.0001  &&  baY < 0.0001)
    {
      // Both end points are almost the same (=> same point, not a line)
      return 5;
    }

    double caX = itsXDelta - aX;
    double caY = itsYDelta - aY;
    double cbX = itsXDelta - bX;
    double cbY = itsYDelta - bY;

    double ar2 = (caX * caX) + (caY * caY);
    double br2 = (cbX * cbX) + (cbY * cbY);

    // Both endpoint are inside the circle
    if (ar2 <= itsRadius2 && br2 <= itsRadius2)
      return 1;

    double abDist2 = baX * baX + baY * baY;
    double bBy2 = baX * caX + baY * caY;
    double c = ar2 - itsRadius2;
    double pBy2 = bBy2 / abDist2;
    double q = c / abDist2;
    double d = pBy2 * pBy2 - q;

    if (d < 0)    // No intersection points
      return 0;

    if (d == 0)   // One intersection point (= tangent). We can ignore it.
      return 0;

    double tmpSqrt = sqrt(d);
    double abScalingFactor1 = -pBy2 + tmpSqrt;
    double abScalingFactor2 = -pBy2 - tmpSqrt;

    // Intersection points:

    pX1 = aX - baX * abScalingFactor1;
    pY1 = aY - baY * abScalingFactor1;
    pX2 = aX - baX * abScalingFactor2;
    pY2 = aY - baY * abScalingFactor2;

    if (std::max(aX, bX) < std::min(pX1, pX2))
      return 0;

    if (std::max(aY, bY) < std::min(pY1, pY2))
      return 0;

    if (std::min(aX, bX) > std::max(pX1, pX2))
      return 0;

    if (std::min(aY, bY) > std::max(pY1, pY2))
      return 0;


    //printf("RAD %.20f  %.20f   %f %f %f\n",ar2-itsRadius2,br2-itsRadius2,ar2,br2,itsRadius2);
    //printf("%f,%f  %f,%f\n",pX1,pY1,pX2,pY2);
    if (ar2 <= itsRadius2 && br2 > itsRadius2)
    {
      // The first endpoint is inside the circle

      double bpx1 = pX1-bX;
      double bpy1 = pY1-bY;
      double bpx2 = pX2-bX;
      double bpy2 = pY2-bY;
      double dp1 = bpx1*bpx1 + bpy1*bpy1;
      double dp2 = bpx2*bpx2 + bpy2*bpy2;

      if (dp2 < dp1)
      {
        pX1 = pX2;
        pY1 = pY2;
        //printf("SWAP 1\n");
      }
      pX1 = pX1 - DELTA;
      pY1 = pY1 - DELTA;
      return 2;
    }


    if (ar2 > itsRadius2 && br2 <= itsRadius2)
    {
      // The second endpoint is inside the circle

      double apx1 = pX1-aX;
      double apy1 = pY1-aY;
      double apx2 = pX2-aX;
      double apy2 = pY2-aY;
      double dp1 = apx1*apx1 + apy1*apy1;
      double dp2 = apx2*apx2 + apy2*apy2;

      if (dp2 < dp1)
      {
        pX1 = pX2;
        pY1 = pY2;
        //printf("SWAP 2\n");
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
      // Both points are on the same side of the circle
      //printf("Same side %f %f\n",dd,abDist2);
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





OGRLinearRing* Shape_circle::makeRing(double theMaximumSegmentLength) const
{
  try
  {
    OGRLinearRing *ring = new OGRLinearRing;
    double angle = PI2;
    while (angle > 0)
    {
      double xx = 0;
      double yy = 0;
      getPointByAngle(angle, xx, yy);
      ring->addPoint(xx, yy);
      angle = angle - itsBorderStep;
    }

    if (theMaximumSegmentLength > 0)
      ring->segmentize(theMaximumSegmentLength);

    return ring;
  }
  catch (...)
  {
    throw Fmi::Exception(BCP, "Operation failed!", nullptr);
  }
}





OGRLinearRing* Shape_circle::makeHole(double theMaximumSegmentLength) const
{
  try
  {
    OGRLinearRing* ring = makeRing(theMaximumSegmentLength);
    ring->reverseWindingOrder();
    //ring->reversePoints();
    return ring;
  }
  catch (...)
  {
    throw Fmi::Exception(BCP, "Operation failed!", nullptr);
  }
}





int Shape_circle::cut(const OGRLineString *theGeom, ShapeClipper &theClipper, bool exterior) const
{
  try
  {
    int n = theGeom->getNumPoints();
    if (theGeom == nullptr || n < 1)
      return 0;

    const OGRLineString &g = *theGeom;
    auto *line = new OGRLineString();
    double xA = g.getX(0);
    double yA = g.getY(0);
    auto posA = getPosition(xA, yA);
    auto posB = posA;
    auto position = posA;

    if (posA == Position::Outside)
      line->addPoint(xA, yA);

    for (int i = 1; i < n; i++)
    {
      double xB = g.getX(i);
      double yB = g.getY(i);
      posB = getPosition(xB, yB);
      position |= posB;

      double pX1 = 0, pY1 = 0, pX2 = 0, pY2 = 0;
      int res = getLineIntersectionPoints(xA, yA, xB, yB, pX1, pY1, pX2, pY2);
      // printf("getLineIntersectionPoints(%f,%f,%f,%f  %f,%f,%f,%f) = %d\n",xA, yA, xB, yB, pX1, pY1, pX2, pY2,res);

      switch (res)
      {
        case 0:   // Both points are outside
          line->addPoint(xB, yB);
          break;

        case 2: // The first point is inside, the second point is outside
          if (pX1 != xB || pY1 != yB)
            line->addPoint(pX1, pY1);

          line->addPoint(xB, yB);
          break;

        case 3:   // The first point is outside, the second point is inside
          line->addPoint(pX1, pY1);
          if (exterior)
            theClipper.addExterior(line);
          else
            theClipper.addInterior(line);

          line = new OGRLineString();
          break;

        case 4:   // Both end point are outside, but the line intersects with the circle
          position |= Position::Outside | Position::Inside;
          line->addPoint(pX1, pY1);
          if (exterior)
            theClipper.addExterior(line);
          else
            theClipper.addInterior(line);

          line = new OGRLineString();
          line->addPoint(pX2, pY2);
          if (pX2 != xB || pY2 != yB)
            line->addPoint(xB, yB);
          break;
      }

      posA = posB;
      xA = xB;
      yA = yB;
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
    throw Fmi::Exception(BCP, "Operation failed!", nullptr);
  }
}




int Shape_circle::clip(const OGRLineString *theGeom, ShapeClipper &theClipper, bool exterior) const
{
  try
  {
    int n = theGeom->getNumPoints();
    if (theGeom == nullptr || n < 1)
      return 0;

    const OGRLineString &g = *theGeom;
    auto *line = new OGRLineString();
    double xA = g.getX(0);
    double yA = g.getY(0);
    auto posA = getPosition(xA, yA);
    auto posB = posA;
    auto position = posA;

    if (posA == Position::Inside)
      line->addPoint(xA, yA);

    for (int i = 1; i < n; i++)
    {
      // Establish initial position


      double xB = g.getX(i);
      double yB = g.getY(i);
      posB = getPosition(xB, yB);

      //printf("%f,%f %d    %f,%f %d\n",xA,yA,posA,xB,yB,posB);
      position |= posB;

      double pX1 = 0, pY1 = 0, pX2 = 0, pY2 = 0;
      int res = getLineIntersectionPoints(xA, yA, xB, yB, pX1, pY1, pX2, pY2);
      //printf("getLineIntersectionPoints(%f,%f,%f,%f  %f,%f,%f,%f) = %d\n",xA, yA, xB, yB, pX1, pY1, pX2, pY2,res);

      switch (res)
      {
        case 1: // Both points are inside
          line->addPoint(xB, yB);
          break;

        case 2: // The first point is inside, the second point is outside
          line->addPoint(pX1, pY1);
          if (exterior)
            theClipper.addExterior(line);
          else
            theClipper.addInterior(line);
          line = new OGRLineString();
          break;

        case 3: // The first point is outside, the second point is inside
          if (pX1 != xB || pY1 != yB)
            line->addPoint(pX1, pY1);

          line->addPoint(xB, yB);
          break;

        case 4: // Both end point are outside, but the line intersects with the circle
          position |= Position::Outside | Position::Inside;
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
    throw Fmi::Exception(BCP, "Operation failed!", nullptr);
  }
}





bool Shape_circle::isInsideRing(const OGRLinearRing &theRing) const
{
  try
  {
    Shape_circle circle(itsX,itsY,itsRadius-0.0001);

    uint points = 36;
    double step = 2*PI / (double)points;
    double angle = 0;
    for (uint t=0; t<points; t++)
    {
      double xx = 0, yy = 0;
      circle.getPointByAngle(angle,xx,yy);
      if (!OGR::inside(theRing,xx,yy))
        return false;

      angle = angle + step;
    }
    return true;
  }
  catch (...)
  {
    throw Fmi::Exception(BCP, "Operation failed!", nullptr);
  }
}




bool Shape_circle::isRingInside(const OGRLinearRing& theRing) const
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
    throw Fmi::Exception(BCP, "Operation failed!", nullptr);
  }
}





// ----------------------------------------------------------------------
/*!
 * \brief Search for matching line segment clockwise (clipping)
 */
// ----------------------------------------------------------------------

LineIterator Shape_circle::search_cw(OGRLinearRing *ring,std::list<OGRLineString *> &lines,
                               double x1,double y1,double &x2,double &y2) const
{
  try
  {
    auto best = lines.end();
    double angle1 = 0;
    double bestAngleDiff = 1000;

    if (isOnEdge(x1,y1,angle1))
    {
      double angle2 = 0;

      if (isOnEdge(x2,y2,angle2))
      {
        // Sometimes the best option is to connect the end points
        // of the current line.

        double angleDiff = angle1 - angle2;

        if (angleDiff > PI)
          angleDiff = PI2-angleDiff;

        if (angleDiff < -PI)
          angleDiff = PI2+angleDiff;

        if (angleDiff > 0)
          bestAngleDiff = angleDiff;
      }

      for (auto iter = lines.begin(); iter != lines.end(); ++iter)
      {
        double x = (*iter)->getX(0);
        double y = (*iter)->getY(0);

        if (isOnEdge(x,y,angle2))
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
              angle2 = angle2 - 2*PI;
            // printf("++ BEST ANGLE %f %f => %f,%f   %f %f %f\n",x1,y1,x2,y2,angle1,angle2,bestAngleDiff);
          }
        }
      }
    }
    return best;
  }
  catch (...)
  {
    throw Fmi::Exception(BCP, "Operation failed!", nullptr);
  }
}





// ----------------------------------------------------------------------
/*!
 * \brief Search for matching line segment counter-clockwise (cutting)
 */
// ----------------------------------------------------------------------

LineIterator Shape_circle::search_ccw(OGRLinearRing *ring,std::list<OGRLineString *> &lines,
                                double x1,double y1,double &x2,double &y2) const
{
  try
  {
    auto best = lines.end();
    double angle1 = 0;
    double bestAngleDiff = 1000;

    if (isOnEdge(x1,y1,angle1))
    {
      double angle2 = 0;

      if (isOnEdge(x2,y2,angle2))
      {
        double angleDiff = angle2 - angle1;
        if (angleDiff > PI)
          angleDiff = PI2-angleDiff;

        if (angleDiff < -PI)
          angleDiff = PI2+angleDiff;

        bestAngleDiff = angleDiff;
      }

      for (auto iter = lines.begin(); iter != lines.end(); ++iter)
      {
        double x = (*iter)->getX(0);
        double y = (*iter)->getY(0);

        if (isOnEdge(x,y,angle2))
        {
          double angleDiff = angle2 - angle1;
          if (angle2 < angle1)
            angleDiff = PI2 - (angle1 - angle2);

          if (angleDiff < bestAngleDiff)
          {
            x2 = x;
            y2 = y;
            best = iter;
            bestAngleDiff = angleDiff;
            // printf("-- BEST ANGLE CCW %f %f %f\n",angle1,angle2,bestAngleDiff);
          }
        }
      }
    }
    return best;
  }
  catch (...)
  {
    throw Fmi::Exception(BCP, "Operation failed!", nullptr);
  }
}





bool Shape_circle::connectPoints_cw(OGRLinearRing& ring,double x1,double y1,double x2,double y2,double theMaximumSegmentLength) const
{
  try
  {

    double angle1 = 0;
    double angle2 = 0;

    if (!isOnEdge(x1, y1, angle1) || !isOnEdge(x2, y2, angle2))
      return false;  // The end points are not on the edge of the circle

    Shape_circle outerCircle(itsX,itsY,itsRadius + 0.0001);

    double angleDiff = 0;
    angleDiff = -angleDistance_cw(angle1, angle2);

    if (angleDiff > PI)
      angleDiff = PI2 - angleDiff;

    if (angleDiff < -PI)
      angleDiff = PI2 + angleDiff;

    if (fabs(angleDiff) > itsBorderStep)
    {
      double xx = 0, yy = 0;
      outerCircle.getPointByAngle(angle1, xx, yy);

      ring.addPoint(x1, y1);
      ring.addPoint(xx, yy);
      ring.addPoint(x1, y1);

      uint points = abs(angleDiff / itsBorderStep);
      double ad = angleDiff / points;
      for (uint t = 0; t < points; t++)
      {
        getPointByAngle(angle1, xx, yy);
        // printf("++ getPoint %f   %f,%f\n", angle1, xx, yy);
        ring.addPoint(xx, yy);
        angle1 = angle1 + ad;
      }
      if (points > 0)
        return true;
    }
    return false;
  }
  catch (...)
  {
    throw Fmi::Exception(BCP, "Operation failed!", nullptr);
  }
}




bool Shape_circle::connectPoints_ccw(OGRLinearRing& ring,double x1,double y1,double x2,double y2,double theMaximumSegmentLength) const
{
  try
  {

    double angle1 = 0;
    double angle2 = 0;

    if (!isOnEdge(x1, y1, angle1) || !isOnEdge(x2, y2, angle2))
      return false;  // The end points are not on the edge of the circle

    Shape_circle innerCircle(itsX,itsY,itsRadius - 0.0001);

    double angleDiff = 0;
    angleDiff = angleDistance_ccw(angle1, angle2);

    if (angleDiff > PI)
      angleDiff = PI2 - angleDiff;

    if (angleDiff < -PI)
      angleDiff = PI2 + angleDiff;

    if (fabs(angleDiff) > itsBorderStep)
    {
      double xx = 0, yy = 0;
      innerCircle.getPointByAngle(angle1, xx, yy);

      ring.addPoint(x1, y1);
      ring.addPoint(xx, yy);
      ring.addPoint(x1, y1);

      uint points = abs(angleDiff / itsBorderStep);
      double ad = angleDiff / points;
      for (uint t = 0; t < points; t++)
      {
        getPointByAngle(angle1, xx, yy);
        //printf("-- getPoint %f   %f,%f\n", angle1, xx, yy);
        ring.addPoint(xx, yy);
        angle1 = angle1 + ad;
      }

      ring.addPoint(x2, y2);

      return true;
    }
    return false;
  }
  catch (...)
  {
    throw Fmi::Exception(BCP, "Operation failed!", nullptr);
  }
}




void Shape_circle::print(std::ostream& stream)
{
  try
  {
    stream << "Shape_cirle\n";
    stream << "- itsX           = " << itsX << "\n";
    stream << "- itsY           = " << itsY << "\n";
    stream << "- itsRadius      = " << itsRadius << "\n";
    stream << "- itsXMin        = " << itsXMin << "\n";
    stream << "- itsYMin        = " << itsYMin << "\n";
    stream << "- itsXMax        = " << itsXMax << "\n";
    stream << "- itsYMax        = " << itsYMax << "\n";
    stream << "- itsBorderStep  = " << itsBorderStep << "\n";

  }
  catch (...)
  {
    throw Fmi::Exception(BCP,"Operation failed!",nullptr);
  }
}


}  // namespace Fmi
