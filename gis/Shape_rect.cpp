#include "Shape_rect.h"
#include "ShapeClipper.h"
#include "OGR.h"

namespace Fmi
{


Shape_rect::Shape_rect(double theX1, double theY1, double theX2, double theY2)
    : itsX1(theX1),
      itsY1(theY1),
      itsX2(theX2),
      itsY2(theY2),
      itsXMin(std::min(theX1, theX2)),
      itsYMin(std::min(theY1, theY2)),
      itsXMax(std::max(theX1, theX2)),
      itsYMax(std::max(theY1, theY2))
{
}





Shape_rect::~Shape_rect()
{
}





int Shape_rect::clip(const OGRLineString *theGeom, ShapeClipper &theClipper, bool exterior) const
{
  try
  {
    int position = 0;

    int n = theGeom->getNumPoints();

    if (theGeom == nullptr || n < 1)
      return position;

    // For shorthand code

    const OGRLineString &g = *theGeom;

    // Keep a record of the point where a line segment entered
    // the rectangle. If the boolean is set, we must insert
    // the point to the beginning of the linestring which then
    // continues inside the rectangle.

    double x0 = 0;
    double y0 = 0;
    bool add_start = false;
    int start_index = 0;

    // Start iterating

    int i = 0;

    while (i < n)
    {
      // Establish initial position

      double x = g.getX(i);
      double y = g.getY(i);
      auto pos = getPosition(x, y);

      // Update global status
      position |= pos;

      if (pos == Position::Outside)
      {
        // Skip points as fast as possible until something has to be checked
        // in more detail.

        ++i;  // we already know it is outside

        if (x < itsXMin)
          while (i < n && g.getX(i) < itsXMin)
            ++i;

        else if (x > itsXMax)
          while (i < n && g.getX(i) > itsXMax)
            ++i;

        else if (y < itsYMin)
          while (i < n && g.getY(i) < itsYMin)
            ++i;

        else if (y > itsYMax)
          while (i < n && g.getY(i) > itsYMax)
            ++i;

        if (i >= n)
          return position;

        /*
         * 7. out-in
         *    - begin new linestring at intersection point
         * 8. out-edge
         *    - if passes inside the rectangle, produce linestring from the intersection points
         *    - ignore otherwise
         * 9. out-out
         *    a) ignore if both points completely below, above, left or right of rectangle
         *    b) calculate possible intersection.
         *      - produce line between intersection points are of non zero length
         *        and are not on the same edge (segment is colinear with the edge)
         */

        // Establish new position

        x = g.getX(i);
        y = g.getY(i);
        pos = getPosition(x, y);

        position |= pos;

        // Calculate possible intersection coordinate with the box

        x0 = g.getX(i - 1);
        y0 = g.getY(i - 1);
        clip_to_edges(x0, y0, x, y);

        if (pos == Position::Inside)  // out-in
        {
          // x0,y0 begings the new linestring
          start_index = i;
          add_start = true;
          // Main loop will enter the Inside/Edge section
        }
        else if (pos == Position::Outside)  // out-out
        {
          // Clip the other end too
          clip_to_edges(x, y, x0, y0);

          auto prev_pos = getPosition(x0, y0);
          pos = getPosition(x, y);

          if (different(x0, y0, x, y) &&  // discard corners etc
              onEdge(prev_pos) &&    // discard if does not intersect rect
              onEdge(pos) && !onSameEdge(prev_pos, pos)  // discard if travels along edge
          )
          {
            position |= Position::Inside;  // something is inside
            auto *line = new OGRLineString;
            line->addPoint(x0, y0);
            line->addPoint(x, y);
            if (exterior)
              theClipper.addExterior(line);
            else
              theClipper.addInterior(line);
          }

          // Continue main loop outside the rect
        }
        else  // out-edge
        {
          auto prev_pos = getPosition(x0, y0);

          if (!onSameEdge(pos, prev_pos))
          {
            position |= Position::Inside;  // something is inside
            auto *line = new OGRLineString;
            line->addPoint(x0, y0);
            line->addPoint(x, y);
            if (exterior)
              theClipper.addExterior(line);
            else
              theClipper.addInterior(line);
          }
        }
      }

      /*
       *  1. in-in
       *     - continue clip
       *  2. in-edge
       *     - terminate clip
       *  3. in-out
       *     - terminate clip at intersection point
       */

      else if (pos == Position::Inside)
      {
        while (++i < n)
        {
          x = g.getX(i);
          y = g.getY(i);

          pos = getPosition(x, y);
          position |= pos;

          if (pos == Position::Inside)  // in-in
          {
          }
          else if (pos == Position::Outside)  // in-out
          {
            // Clip the outside point to edges
            clip_to_edges(x, y, g.getX(i - 1), g.getY(i - 1));

            auto *line = new OGRLineString();
            if (add_start)
              line->addPoint(x0, y0);
            add_start = false;
            if (start_index <= i - 1)
              line->addSubLineString(&g, start_index, i - 1);
            line->addPoint(x, y);
            theClipper.addExterior(line);

            break;  // going to outside the loop
          }
          else  // in-edge
          {
            if (start_index == 0 && i == n - 1)
              return Position::Inside;  // All inside?

            auto *line = new OGRLineString();
            if (add_start)
              line->addPoint(x0, y0);
            add_start = false;
            line->addSubLineString(&g, start_index, i);
            if (exterior)
              theClipper.addExterior(line);
            else
              theClipper.addInterior(line);

            start_index = i;  // potentially going in again at this point
            break;            // going to the edge loop
          }
        }

        // Was everything in? If so, generate no output here, just clone the linestring elsewhere
        if (start_index == 0 && i >= n)
          return Position::Inside;

        // Flush the last line segment if data ended and there is something to flush
        if (pos == Position::Inside && (start_index < i - 1 || add_start))
        {
          auto *line = new OGRLineString();
          if (add_start)
          {
            line->addPoint(x0, y0);
            add_start = false;
          }
          line->addSubLineString(&g, start_index, i - 1);
          if (exterior)
            theClipper.addExterior(line);
          else
            theClipper.addInterior(line);
        }
      }

      /*
       *  4. edge-in
       *     - begin new linestring
       *  5. edge-edge
       *     a) from edge 1 to edge 1
       *        - ignore segment for travelling on the edges only
       *     b) from edge 1 to edge 2
       *        - produce segment between endpoints
       *  6. edge-out
       *     - if passes inside the rectangle, produce a linesegment
       *     - ignore segment if goes straight out
       */

      else
      {
        while (++i < n)
        {
          auto prev_pos = pos;

          x = g.getX(i);
          y = g.getY(i);

          pos = getPosition(x, y);
          position |= pos;

          if (pos == Position::Inside)  // edge-in
          {
            // Start new linestring
            start_index = i - 1;
            break;  // And continue main loop on the inside
          }
          else if (pos == Position::Outside)  // edge-out
          {
            // Clip the outside point to edges
            clip_to_edges(x, y, g.getX(i - 1), g.getY(i - 1));
            pos = getPosition(x, y);

            // Does the line exit through the inside of the box?

            bool through_box =
                (different(x, y, g.getX(i), g.getY(i)) && !onSameEdge(prev_pos, pos));

            // Output a LineString if it at least one segment long

            if (through_box)
            {
              auto *line = new OGRLineString();
              line->addPoint(g.getX(i - 1), g.getY(i - 1));
              line->addPoint(x, y);
              theClipper.addExterior(line);
            }
            break;  // And continue main loop on the outside
          }
          else  // edge-edge
          {
            // travel to different edge?
            if (!onSameEdge(prev_pos, pos))
            {
              position |= Position::Inside;  // passed through
              auto *line = new OGRLineString();
              line->addPoint(g.getX(i - 1), g.getY(i - 1));
              line->addPoint(x, y);
              theClipper.addExterior(line);
              start_index = i;
            }
          }
        }
      }
    }
    return position;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}





int Shape_rect::cut(const OGRLineString *theGeom, ShapeClipper &theClipper, bool exterior) const
{
  try
  {
    int position = 0;

    int n = theGeom->getNumPoints();

    if (theGeom == nullptr || n < 1)
      return position;

    // For shorthand code

    const OGRLineString &g = *theGeom;

    // Keep a record of the point where a line segment entered
    // the rectangle. If the boolean is set, we must insert
    // the point to the end of the linestring which began outside the rectangle.

    double x0 = 0;
    double y0 = 0;

    bool add_start = false;
    double start_x = 0;
    double start_y = 0;

    // Start iterating

    int i = 0;
    int start_index = 0;

    while (i < n)
    {
      // Establish initial position

      double x = g.getX(i);
      double y = g.getY(i);
      auto pos = getPosition(x, y);

      position |= pos;

      if (pos == Position::Outside)
      {
        // Skip points as fast as possible until something has to be checked
        // in more detail.

        ++i;  // we already know it is outside

        if (x < itsXMin)
          while (i < n && g.getX(i) < itsXMin)
            ++i;

        else if (x > itsXMax)
          while (i < n && g.getX(i) > itsXMax)
            ++i;

        else if (y < itsYMin)
          while (i < n && g.getY(i) < itsYMin)
            ++i;

        else if (y > itsYMax)
          while (i < n && g.getY(i) > itsYMax)
            ++i;

        if (i >= n)
        {
          if (start_index == 0)
            return position;  // everything was out

          if (start_index > 0 && start_index < n)
          {
            auto *line = new OGRLineString();
            if (add_start)
              line->addPoint(start_x, start_y);
            line->addSubLineString(&g, start_index, n - 1);
            if (exterior)
              theClipper.addExterior(line);
            else
              theClipper.addInterior(line);
          }
          return position;
        }

        // Establish new position

        x = g.getX(i);
        y = g.getY(i);
        pos = getPosition(x, y);

        position |= pos;

        /*
         *  7. out-in
         *     - end current linestring at intersection point
         *  8. out-edge
         *     - if passes inside the rectangle, terminate current linestring at the intersection
         *     - otherwise terminate at the end point
         *  9. out-out
         *     a) keep if both points completely below, above, left or right of rectangle
         *     b) calculate possible intersection.
         *        - if line between intersections points is of non zero length, end current
         *          linestring at the first intersection and begin a new one at the second
         * intersection
         *        - otherwise keep growing the current linestring with the segment
         */

        x0 = g.getX(i - 1);
        y0 = g.getY(i - 1);

        clip_to_edges(x0, y0, x, y);

        if (pos == Position::Inside)  // out-in
        {
          auto *line = new OGRLineString();

          if (add_start)
            line->addPoint(start_x, start_y);
          add_start = false;
          line->addSubLineString(&g, start_index, i - 1);
          line->addPoint(x0, y0);
          if (exterior)
            theClipper.addExterior(line);
          else
            theClipper.addInterior(line);
          // Main loop will enter the Inside/Edge section
        }
        else if (pos == Position::Outside)  // out-out
        {
          // Clip the other end too
          clip_to_edges(x, y, x0, y0);

          auto prev_pos = getPosition(x0, y0);
          pos = getPosition(x, y);
          position |= pos;

          if (different(x0, y0, x, y) &&  // discard corners etc
              onEdge(prev_pos))      // discard if does not intersect rect
          {
            // Intersection occurred. Must generate a linestring from start to intersection point
            // then.
            auto *line = new OGRLineString();

            if (add_start)
              line->addPoint(start_x, start_y);
            line->addSubLineString(&g, start_index, i - 1);
            line->addPoint(x0, y0);
            if (exterior)
              theClipper.addExterior(line);
            else
              theClipper.addInterior(line);

            position |= Position::Inside;  // mark we visited inside

            // Start next line from the end intersection point
            start_index = i;
            start_x = x;
            start_y = y;
            add_start = true;
          }

          // Continue main loop outside the rect
        }
        else
        {
          auto *line = new OGRLineString();
          if (add_start)
            line->addPoint(start_x, start_y);
          line->addSubLineString(&g, start_index, i - 1);
          line->addPoint(x0, y0);
          if (x != g.getX(i) || y != g.getY(i))
            position |= Position::Inside;
          if (exterior)
            theClipper.addExterior(line);
          else
            theClipper.addInterior(line);

          add_start = false;
        }
      }

      else if (pos == Position::Inside)
      {
        /*
         *  1. in-in
         *     - ignore segment
         *  2. in-edge
         *     - ignore segment
         *  3. in-out
         *     - begin new linestring at intersection point
         */

        while (++i < n)
        {
          x = g.getX(i);
          y = g.getY(i);
          pos = getPosition(x, y);

          position |= pos;

          if (pos != Position::Outside)
            continue;

          // Clip the outside point to edges
          clip_to_edges(x, y, g.getX(i - 1), g.getY(i - 1));
          pos = getPosition(x, y);

          position |= pos;

          start_index = i;
          add_start = true;
          start_x = x;
          start_y = y;
          break;
        }

        // Was everything in? If so, generate no output
        if (start_index == 0 && i >= n)
          return position;
      }
      else  // edge
      {
        /*
         *  4. edge-in
         *     - ignore segment
         *  5. edge-edge
         *     - ignore segment
         *  6. edge-out
         *     - if passes inside the rectangle, start linestring at the exit intersection point
         *     - otherwise begin new linestring at start point
         */

        while (++i < n)
        {
          auto prev_pos = pos;
          x = g.getX(i);
          y = g.getY(i);
          pos = getPosition(x, y);

          position |= pos;

          if (pos == Position::Inside)
            continue;
          if (pos != Position::Outside)  // on edge
          {
            if (!onSameEdge(prev_pos, pos))
              position |= Position::Inside;  // visited inside
            continue;
          }

          // Clip the outside point to edges
          clip_to_edges(x, y, g.getX(i - 1), g.getY(i - 1));

          if (x != g.getX(i) || y != g.getY(i))
            position |= Position::Inside;

          start_index = i;
          add_start = true;
          start_x = x;
          start_y = y;
          break;
        }

        // Was everything in? If so, generate no output
        if (start_index == 0 && i >= n)
          return position;
      }
    }

    if (add_start)
    {
      auto *line = new OGRLineString();

      line->addPoint(start_x, start_y);
      line->addSubLineString(&g, start_index, n - 1);
      if (exterior)
        theClipper.addExterior(line);
      else
        theClipper.addInterior(line);
    }

    return position;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}





bool Shape_rect::connectPoints_cw(OGRLinearRing& ring,double x1,double y1,double x2,double y2,double theMaximumSegmentLength) const
{
  try
  {
    if (theMaximumSegmentLength > 0)
    {
      const auto dx = x2 - x1;
      const auto dy = y2 - y1;

      auto length = std::hypot(dx, dy);
      if (length > theMaximumSegmentLength)
      {
        auto num = static_cast<int>(std::ceil(length / theMaximumSegmentLength));
        for (auto i = 1; i < num; i++)
        {
          auto fraction = 1.0 * i / num;
          ring.addPoint(x1 + fraction * dx, y1 + fraction * dy);
        }
      }
    }
    ring.addPoint(x2, y2);
    return true;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}





bool Shape_rect::connectPoints_ccw(OGRLinearRing& ring,double x1,double y1,double x2,double y2,double theMaximumSegmentLength) const
{
  try
  {
    if (theMaximumSegmentLength > 0)
    {
      const auto dx = x2 - x1;
      const auto dy = y2 - y1;

      auto length = std::hypot(dx, dy);
      if (length > theMaximumSegmentLength)
      {
        auto num = static_cast<int>(std::ceil(length / theMaximumSegmentLength));
        for (auto i = 1; i < num; i++)
        {
          auto fraction = 1.0 * i / num;
          ring.addPoint(x1 + fraction * dx, y1 + fraction * dy);
        }
      }
    }
    ring.addPoint(x2, y2);
    return true;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}





int Shape_rect::getPosition(double x, double y) const
{
  try
  {
    if (x > itsXMin && x < itsXMax && y > itsYMin && y < itsYMax)
      return Inside;

    if (x < itsXMin || x > itsXMax || y < itsYMin || y > itsYMax)
      return Outside;

    int pos = 0;
    if (x == itsXMin)
      pos |= Left;
    else if (x == itsXMax)
      pos |= Right;
    if (y == itsYMin)
      pos |= Bottom;
    else if (y == itsYMax)
      pos |= Top;

    return pos;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}





int Shape_rect::getLineIntersectionPoints(double aX, double aY, double bX, double bY,double& pX1, double& pY1, double& pX2, double& pY2) const
{
  try
  {
    return 0;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}





bool Shape_rect::isInsideRing(const OGRLinearRing &theRing) const
{
  try
  {
    return (OGR::inside(theRing, itsXMin, itsYMin) &&
            OGR::inside(theRing, itsXMin, itsYMax) &&
            OGR::inside(theRing, itsXMax, itsYMin) &&
            OGR::inside(theRing, itsXMax, itsYMax));
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}





bool Shape_rect::isOnEdge(double x, double y) const
{
  try
  {
    if ((x == itsXMin || x == itsXMax)  &&  (y == itsYMin || y == itsYMax))
      return true;

    return false;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}





bool Shape_rect::isRingInside(const OGRLinearRing& theRing) const
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





OGRLinearRing* Shape_rect::makeRing(double theMaximumSegmentLength) const
{
  try
  {
    OGRLinearRing *ring = new OGRLinearRing;
    ring->addPoint(itsXMin, itsYMin);
    ring->addPoint(itsXMin, itsYMax);
    ring->addPoint(itsXMax, itsYMax);
    ring->addPoint(itsXMax, itsYMin);
    ring->addPoint(itsXMin, itsYMin);

    if (theMaximumSegmentLength > 0)
      ring->segmentize(theMaximumSegmentLength);

    return ring;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}





OGRLinearRing* Shape_rect::makeHole(double theMaximumSegmentLength) const
{
  try
  {
    OGRLinearRing *ring = new OGRLinearRing;
    ring->addPoint(itsXMin, itsYMin);
    ring->addPoint(itsXMax, itsYMin);
    ring->addPoint(itsXMax, itsYMax);
    ring->addPoint(itsXMin, itsYMax);
    ring->addPoint(itsXMin, itsYMin);

    if (theMaximumSegmentLength > 0)
      ring->segmentize(theMaximumSegmentLength);

    return ring;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}





LineIterator Shape_rect::search_cw(OGRLinearRing *ring,std::list<OGRLineString *> &lines,double x1,double y1,double &x2,double &y2) const
{
  try
  {
    x2 = x1;
    y2 = y1;
    auto best = lines.end();

    if (y1 == itsYMin && x1 > itsXMin)
    {
      // On lower edge going left, worst we can do is left corner, closing might be better

      if (ring->getY(0) == y1 && ring->getX(0) < x1)
        x2 = ring->getX(0);
      else
        x2 = itsXMin;

      // Look for a better match from the remaining linestrings.

      for (auto iter = lines.begin(); iter != lines.end(); ++iter)
      {
        double x = (*iter)->getX(0);
        double y = (*iter)->getY(0);
        if (y == y1 && x > x2 && x <= x1)  // if not to the right and better than previous best
        {
          x2 = x;
          best = iter;
        }
      }
    }
    else if (x1 == itsXMin && y1 < itsYMax)
    {
      // On left edge going up, worst we can do is upper conner, closing might be better

      if (ring->getX(0) == x1 && ring->getY(0) > y1)
        y2 = ring->getY(0);
      else
        y2 = itsYMax;

      for (auto iter = lines.begin(); iter != lines.end(); ++iter)
      {
        double x = (*iter)->getX(0);
        double y = (*iter)->getY(0);
        if (x == x1 && y > y1 && y <= y2)
        {
          y2 = y;
          best = iter;
        }
      }
    }
    else if (y1 == itsYMax && x1 < itsXMax)
    {
      // On top edge going right, worst we can do is right corner, closing might be better

      if (ring->getY(0) == y1 && ring->getX(0) > x1)
        x2 = ring->getX(0);
      else
        x2 = itsXMax;

      for (auto iter = lines.begin(); iter != lines.end(); ++iter)
      {
        double x = (*iter)->getX(0);
        double y = (*iter)->getY(0);

        if (y == y1 && x >= x1 && x <= x2)
        {
          x2 = x;
          best = iter;
        }
      }
    }
    else
    {
      // On right edge going down, worst we can do is bottom corner, closing might be better

      if (ring->getX(0) == x1 && ring->getY(0) < y1)
        y2 = ring->getY(0);
      else
        y2 = itsYMin;

      for (auto iter = lines.begin(); iter != lines.end(); ++iter)
      {
        double x = (*iter)->getX(0);
        double y = (*iter)->getY(0);

        if (x == x2 && y <= y1 && y >= y2)
        {
          y2 = y;
          best = iter;
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





LineIterator Shape_rect::search_ccw(OGRLinearRing *ring,std::list<OGRLineString *> &lines,double x1,double y1,double &x2,double &y2) const
{
  try
  {
    x2 = x1;
    y2 = y1;
    auto best = lines.end();

    if (y1 == itsYMin && x1 < itsXMax)
    {
      // On lower edge going right, worst we can do is right corner, closing might be better

      if (ring->getY(0) == y1 && ring->getX(0) > x1)
        x2 = ring->getX(0);
      else
        x2 = itsXMax;

      // Look for a better match from the remaining linestrings.

      for (auto iter = lines.begin(); iter != lines.end(); ++iter)
      {
        double x = (*iter)->getX(0);
        double y = (*iter)->getY(0);
        if (y == y1 && x < x2 && x >= x1)  // if not to the left and better than previous best
        {
          x2 = x;
          best = iter;
        }
      }
    }
    else if (x1 == itsXMin && y1 > itsYMin)
    {
      // On left edge going down, worst we can do is lower conner, closing might be better

      if (ring->getX(0) == x1 && ring->getY(0) < y1)
        y2 = ring->getY(0);
      else
        y2 = itsYMin;

      for (auto iter = lines.begin(); iter != lines.end(); ++iter)
      {
        double x = (*iter)->getX(0);
        double y = (*iter)->getY(0);
        if (x == x1 && y < y1 && y >= y2)
        {
          y2 = y;
          best = iter;
        }
      }
    }
    else if (y1 == itsYMax && x1 > itsXMin)
    {
      // On top edge going left, worst we can do is right corner, closing might be better

      if (ring->getY(0) == y1 && ring->getX(0) < x1)
        x2 = ring->getX(0);
      else
        x2 = itsXMin;

      for (auto iter = lines.begin(); iter != lines.end(); ++iter)
      {
        double x = (*iter)->getX(0);
        double y = (*iter)->getY(0);

        if (y == y1 && x <= x1 && x >= x2)
        {
          x2 = x;
          best = iter;
        }
      }
    }
    else
    {
      // On right edge going up, worst we can do is upper corner, closing might be better

      if (ring->getX(0) == x1 && ring->getY(0) > y1)
        y2 = ring->getY(0);
      else
        y2 = itsYMax;

      for (auto iter = lines.begin(); iter != lines.end(); ++iter)
      {
        double x = (*iter)->getX(0);
        double y = (*iter)->getY(0);

        if (x == x2 && y >= y1 && y <= y2)
        {
          y2 = y;
          best = iter;
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
 * \brief Calculate a line intersection point
 *
 * Note:
 *   - Calling this with x1,y1 and x2,y2 swapped cuts the other end of the line
 *   - Calling this with x and y swapped cuts in Y-direction instead
 *   - Calling with 1<->2 and x<->y swapped works too
 */
// ----------------------------------------------------------------------

void Shape_rect::clip_one_edge(double &x1, double &y1, double x2, double y2, double limit) const
{
  try
  {
    if (x1 != x2)
    {
      y1 += (y2 - y1) * (limit - x1) / (x2 - x1);
      x1 = limit;
    }
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}





// ----------------------------------------------------------------------
/*!
 * \brief Start point is outside, endpoint is definitely inside
 *
 * Note: Even though one might think using >= etc operators would produce
 *       the same result, that is not the case. We rely on the fact
 *       that nothing is clipped unless the point is truly outside the
 *       rectangle! Without this handling lines ending on the edges of
 *       the rectangle would be very difficult.
 */
// ----------------------------------------------------------------------

void Shape_rect::clip_to_edges(double &x1, double &y1, double x2, double y2) const
{
  try
  {
    if (x1 < itsXMin)
      clip_one_edge(x1, y1, x2, y2, itsXMin);
    else if (x1 > itsXMax)
      clip_one_edge(x1, y1, x2, y2, itsXMax);

    if (y1 < itsYMin)
      clip_one_edge(y1, x1, y2, x2, itsYMin);
    else if (y1 > itsYMax)
      clip_one_edge(y1, x1, y2, x2, itsYMax);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}




bool Shape_rect::onEdge(int pos)
{
  return (pos > Outside);
}





bool Shape_rect::onSameEdge(int pos1, int pos2)
{
  return onEdge(Position(pos1 & pos2));
}





int Shape_rect::nextEdge(int pos)
{
  try
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
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}




bool Shape_rect::different(double x1, double y1, double x2, double y2)
{
  return !(x1 == x2 && y1 == y2);
}




void Shape_rect::print(std::ostream& stream)
{
  try
  {
    stream << "Shape_rect\n";
    stream << "- itsX1  = " << itsX1 << "\n";
    stream << "- itsY1  = " << itsY1 << "\n";
    stream << "- itsX2  = " << itsX2 << "\n";
    stream << "- itsY2  = " << itsY2 << "\n";
    stream << "- itsXMin  = " << itsXMin << "\n";
    stream << "- itsYMin  = " << itsYMin << "\n";
    stream << "- itsXMax  = " << itsXMax << "\n";
    stream << "- itsYMax  = " << itsYMax << "\n";

  }
  catch (...)
  {
    throw Fmi::Exception(BCP,"Operation failed!",nullptr);
  }
}


}

