#include "RectClipper.h"
#include "Box.h"
#include "GeometryBuilder.h"
#include "OGR.h"
#include <macgyver/Exception.h>
#include <cassert>
#include <iostream>

// ----------------------------------------------------------------------
/*!
 * \brief Build a ring out of the bbox
 */
// ----------------------------------------------------------------------

OGRLinearRing *make_exterior(const Fmi::Box &theBox, double max_length = 0)
{
  try
  {
    OGRLinearRing *ring = new OGRLinearRing;
    ring->addPoint(theBox.xmin(), theBox.ymin());
    ring->addPoint(theBox.xmin(), theBox.ymax());
    ring->addPoint(theBox.xmax(), theBox.ymax());
    ring->addPoint(theBox.xmax(), theBox.ymin());
    ring->addPoint(theBox.xmin(), theBox.ymin());
    if (max_length > 0)
      ring->segmentize(max_length);
    return ring;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Build a hole out of the bbox
 */
// ----------------------------------------------------------------------

OGRLinearRing *make_hole(const Fmi::Box &theBox, double max_length = 0)
{
  try
  {
    OGRLinearRing *ring = new OGRLinearRing;
    ring->addPoint(theBox.xmin(), theBox.ymin());
    ring->addPoint(theBox.xmax(), theBox.ymin());
    ring->addPoint(theBox.xmax(), theBox.ymax());
    ring->addPoint(theBox.xmin(), theBox.ymax());
    ring->addPoint(theBox.xmin(), theBox.ymin());
    if (max_length > 0)
      ring->segmentize(max_length);
    return ring;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Destructor must delete all objects left behind in case of throw
 *
 * Normally build succeeds and clears the containers
 */
// ----------------------------------------------------------------------

Fmi::RectClipper::~RectClipper()
{
  try
  {
    for (auto *ptr : itsExteriorRings)
      delete ptr;
    for (auto *ptr : itsInteriorRings)
      delete ptr;
    for (auto *ptr : itsExteriorLines)
      delete ptr;
    for (auto *ptr : itsInteriorLines)
      delete ptr;
    for (auto *ptr : itsPolygons)
      delete ptr;
  }
  catch (...)
  {
    Fmi::Exception exception(BCP,"Destructor failed",nullptr);
    exception.printError();
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Reconnect disjointed parts
 *
 * When we clip a LinearRing we may get multiple linestrings.
 * Often the first and last ones can be reconnected to simplify
 * output.
 *
 * Sample clip with a rectangle 0,0 --> 10,10 without reconnecting:
 *
 *   Input:   POLYGON ((5 10,0 0,10 0,5 10))
 *   Output:  MULTILINESTRING ((5 10,0 0),(10 0,5 10))
 *   Desired: LINESTRING (10 0,5 10,0 0)
 */
// ----------------------------------------------------------------------

void reconnectLines(std::list<OGRLineString *> &lines, Fmi::RectClipper &clipper, bool exterior)
{
  try
  {
    // Nothing to reconnect if there aren't at least two lines
    if (lines.size() < 2)
      return;

    for (auto pos1 = lines.begin(); pos1 != lines.end();)
    {
      auto *line1 = *pos1;
      const int n1 = line1->getNumPoints();

      if (n1 == 0)  // safety check
      {
        ++pos1;
        continue;
      }

      for (auto pos2 = lines.begin(); pos2 != lines.end();)
      {
        auto *line2 = *pos2;
        const int n2 = line2->getNumPoints();

        // Continue if the ends do not match
        if (pos1 == pos2 || n2 == 0 || line1->getX(n1 - 1) != line2->getX(0) ||
            line1->getY(n1 - 1) != line2->getY(0))
        {
          ++pos2;
          continue;
        }

        // The lines are joinable

        line1->addSubLineString(line2, 1, n2 - 1);
        delete line2;
        pos2 = lines.erase(pos2);

        // The merge may have closed a linearring if the intersections
        // have collapsed to a single point. This can happen if there is
        // a tiny sliver polygon just outside the rectangle, and the
        // intersection coordinates will be identical.

        if (line1->get_IsClosed())
        {
          OGRLinearRing *ring = new OGRLinearRing;
          ring->addSubLineString(line1, 0, -1);
          if (exterior)
            clipper.addExterior(ring);
          else
            clipper.addInterior(ring);
          delete line1;
          pos1 = lines.erase(pos1);
          line1 = *pos1;
          pos2 = lines.begin();  // safety measure
        }
      }

      if (pos1 != lines.end())
        ++pos1;
    }
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

void Fmi::RectClipper::reconnect()
{
  try
  {
    reconnectLines(itsExteriorLines, *this, true);
    reconnectLines(itsInteriorLines, *this, false);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Export parts to another container
 */
// ----------------------------------------------------------------------

void Fmi::RectClipper::release(GeometryBuilder &theBuilder)
{
  try
  {
    for (auto *ptr : itsPolygons)
      theBuilder.add(ptr);
    for (auto *ptr : itsExteriorLines)
      theBuilder.add(ptr);

    clear();
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Clear the parts having transferred ownership elsewhere
 */
// ----------------------------------------------------------------------

void Fmi::RectClipper::clear()
{
  try
  {
    itsExteriorRings.clear();
    itsExteriorLines.clear();
    itsInteriorRings.clear();
    itsInteriorLines.clear();
    itsPolygons.clear();
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Test if there are no parts at all
 */
// ----------------------------------------------------------------------

bool Fmi::RectClipper::empty() const
{
  try
  {
    return itsExteriorRings.empty() && itsExteriorLines.empty() && itsInteriorRings.empty() &&
           itsInteriorLines.empty();
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Add box to the result
 */
// ----------------------------------------------------------------------

void Fmi::RectClipper::addBox()
{
  try
  {
    itsAddBoxFlag = true;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Add intermediate OGR Polygon
 */
// ----------------------------------------------------------------------

void Fmi::RectClipper::addExterior(OGRLinearRing *theRing)
{
  try
  {
    OGR::normalize(*theRing);
    if (theRing->isClockwise() == 0)
      theRing->reverseWindingOrder();
    itsExteriorRings.push_back(theRing);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Add intermediate OGR LineString
 */
// ----------------------------------------------------------------------

void Fmi::RectClipper::addExterior(OGRLineString *theLine)
{
  try
  {
    auto n = theLine->getNumPoints();

    // We may have just touched the exterior at a single point
    if (n < 2)
      delete theLine;
    else
      itsExteriorLines.push_back(theLine);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Add intermediate OGR Polygon
 */
// ----------------------------------------------------------------------

void Fmi::RectClipper::addInterior(OGRLinearRing *theRing)
{
  try
  {
    OGR::normalize(*theRing);
    if (theRing->isClockwise() == 1)
      theRing->reverseWindingOrder();
    itsInteriorRings.push_back(theRing);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Add intermediate OGR LineString
 */
// ----------------------------------------------------------------------

void Fmi::RectClipper::addInterior(OGRLineString *theLine)
{
  try
  {
    itsInteriorLines.push_back(theLine);
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

std::list<OGRLineString *>::iterator search_cw(OGRLinearRing *ring,
                                               std::list<OGRLineString *> &lines,
                                               double x1,
                                               double y1,
                                               double &x2,
                                               double &y2,
                                               const Fmi::Box &box)
{
  try
  {
    auto best = lines.end();

    if (y1 == box.ymin() && x1 > box.xmin())
    {
      // On lower edge going left, worst we can do is left corner, closing might be better

      if (ring->getY(0) == y1 && ring->getX(0) < x1)
        x2 = ring->getX(0);
      else
        x2 = box.xmin();

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
    else if (x1 == box.xmin() && y1 < box.ymax())
    {
      // On left edge going up, worst we can do is upper conner, closing might be better

      if (ring->getX(0) == x1 && ring->getY(0) > y1)
        y2 = ring->getY(0);
      else
        y2 = box.ymax();

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
    else if (y1 == box.ymax() && x1 < box.xmax())
    {
      // On top edge going right, worst we can do is right corner, closing might be better

      if (ring->getY(0) == y1 && ring->getX(0) > x1)
        x2 = ring->getX(0);
      else
        x2 = box.xmax();

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
        y2 = box.ymin();

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

// ----------------------------------------------------------------------
/*!
 * \brief Search for matching line segment counter-clockwise (cutting)
 */
// ----------------------------------------------------------------------

std::list<OGRLineString *>::iterator search_ccw(OGRLinearRing *ring,
                                                std::list<OGRLineString *> &lines,
                                                double x1,
                                                double y1,
                                                double &x2,
                                                double &y2,
                                                const Fmi::Box &box)
{
  try
  {
    auto best = lines.end();

    if (y1 == box.ymin() && x1 < box.xmax())
    {
      // On lower edge going right, worst we can do is right corner, closing might be better

      if (ring->getY(0) == y1 && ring->getX(0) > x1)
        x2 = ring->getX(0);
      else
        x2 = box.xmax();

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
    else if (x1 == box.xmin() && y1 > box.ymin())
    {
      // On left edge going down, worst we can do is lower conner, closing might be better

      if (ring->getX(0) == x1 && ring->getY(0) < y1)
        y2 = ring->getY(0);
      else
        y2 = box.ymin();

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
    else if (y1 == box.ymax() && x1 > box.xmin())
    {
      // On top edge going left, worst we can do is right corner, closing might be better

      if (ring->getY(0) == y1 && ring->getX(0) < x1)
        x2 = ring->getX(0);
      else
        x2 = box.xmin();

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
        y2 = box.ymax();

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
 * \brief Reconnect lines into polygons along box edges
 */
// ----------------------------------------------------------------------

void connectLines(std::list<OGRLinearRing *> &theRings,
                  std::list<OGRLineString *> &theLines,
                  const Fmi::Box &theBox,
                  double max_length,
                  bool keep_inside,
                  bool exterior)
{
  try
  {
    if (theLines.empty())
      return;

    bool cw = false;
    if (keep_inside)
      cw = exterior;

    OGRLinearRing *ring = nullptr;

    while (!theLines.empty() || ring != nullptr)
    {
      if (ring == nullptr)
      {
        ring = new OGRLinearRing;
        auto *line = theLines.front();
        theLines.pop_front();
        ring->addSubLineString(line);
        delete line;
      }

      // Proceed clockwise until the ring can be closed or the
      // start point of another linestring is encountered.
      // Always make the choice that is next in clockwise order.

      int nr = ring->getNumPoints();
      double x1 = ring->getX(nr - 1);
      double y1 = ring->getY(nr - 1);
      double x2 = x1;
      double y2 = y1;

      // No linestring to move onto found next - meaning we'd
      // either move to the next corner or close the current ring.

      auto best = (cw ? search_cw(ring, theLines, x1, y1, x2, y2, theBox)
                      : search_ccw(ring, theLines, x1, y1, x2, y2, theBox));

      if (best != theLines.end())
      {
        // Found a matching linestring to continue to from the same edge we were studying. Move to it
        // and continue building. The line might continue from the same point, in which case we must
        // skip the first point.

        if (x1 != (*best)->getX(0) || y1 != (*best)->getY(0))
          ring->addSubLineString(*best);
        else
          ring->addSubLineString(*best, 1);  // start from 2nd point
        delete *best;
        theLines.erase(best);
      }
      else
      {
        // Couldn't find a matching best line. Either close the ring or move to next corner.
        if (max_length > 0)
        {
          OGRPoint startpoint;
          ring->EndPoint(&startpoint);
          const auto x1 = startpoint.getX();
          const auto y1 = startpoint.getY();
          const auto dx = x2 - x1;
          const auto dy = y2 - y1;

          auto length = std::hypot(dx, dy);
          if (length > max_length)
          {
            auto num = static_cast<int>(std::ceil(length / max_length));
            for (auto i = 1; i < num; i++)
            {
              auto fraction = 1.0 * i / num;
              ring->addPoint(x1 + fraction * dx, y1 + fraction * dy);
            }
          }
        }
        ring->addPoint(x2, y2);
      }

      if (ring->get_IsClosed())
      {
        Fmi::OGR::normalize(*ring);
        theRings.push_back(ring);
        ring = nullptr;
      }
    }
    theLines.clear();
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Build polygons from parts left by clipping one
 *
 * 1. Build exterior ring(s) from lines
 * 2. Attach polygons as holes to the exterior ring(s)
 *
 * Building new exterior:
 * 1. Pick first linestring as the beginning of a ring (until there are none left)
 * 2. Proceed outputting the bbox edges in clockwise orientation (clipping, ccw for cutting)
 *    until the ring itself or another linestring is encountered.
 * 3. If the ring became closed by joining the bbox edges, output it and restart at step 1.
 * 4. Attach the new linestring to the ring and jump back to step 2.
 */
// ----------------------------------------------------------------------

void Fmi::RectClipper::reconnectWithBox(double theMaximumSegmentLength)
{
  try
  {
    // Make exterior box if necessary

    if (itsKeepInsideFlag && itsAddBoxFlag && itsExteriorLines.empty())
    {
      auto *ring = make_exterior(itsBox, theMaximumSegmentLength);
      itsExteriorRings.push_back(ring);
    }

    // Make hole if necessary

    if (!itsKeepInsideFlag && itsAddBoxFlag && itsInteriorLines.empty())
    {
      auto *ring = make_hole(itsBox, theMaximumSegmentLength);
      itsInteriorRings.push_back(ring);
    }

    // Reconnect lines into polygons (exterior or hole)
    // Since clipped holes always become part of the exterior, and cut
    // holes are either part of the interior unless the exterior is also clipped,
    // if we have both lines they must by definition all belong to the exterior.

    if (!itsExteriorLines.empty() && !itsInteriorLines.empty())
    {
      std::move(
          itsInteriorLines.begin(), itsInteriorLines.end(), std::back_inserter(itsExteriorLines));
      itsInteriorLines.clear();
    }

    connectLines(
        itsExteriorRings, itsExteriorLines, itsBox, theMaximumSegmentLength, itsKeepInsideFlag, true);

    connectLines(itsInteriorRings,
                 itsInteriorLines,
                 itsBox,
                 theMaximumSegmentLength,
                 itsKeepInsideFlag,
                 false);

    // Build polygons starting from the built exterior rings
    for (auto *exterior : itsExteriorRings)
    {
      auto *poly = new OGRPolygon;
      poly->addRingDirectly(exterior);
      itsPolygons.push_back(poly);
    }
    itsExteriorRings.clear();

    // Then assign the holes to them

    for (auto *hole : itsInteriorRings)
    {
      if (itsPolygons.size() == 1)
        itsPolygons.front()->addRingDirectly(hole);
      else
      {
        OGRPoint point;
        hole->getPoint(0, &point);
        for (auto *poly : itsPolygons)
        {
          if (poly->getExteriorRing()->isPointInRing(&point, 0) != 0)
          {
            poly->addRingDirectly(hole);
            break;
          }
        }
      }
    }

    // Merge all unjoinable lines to one list of lines

    std::move(itsInteriorLines.begin(), itsInteriorLines.end(), std::back_inserter(itsExteriorLines));

    itsInteriorRings.clear();
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Build results without connecting with the box
 */
// ----------------------------------------------------------------------

void Fmi::RectClipper::reconnectWithoutBox()
{
  try
  {
    // Make exterior box if necessary

    if (itsKeepInsideFlag && itsAddBoxFlag && itsExteriorLines.empty())
    {
      auto *ring = make_exterior(itsBox);
      itsExteriorRings.push_back(ring);
    }

    // Make hole if necessary

    if (!itsKeepInsideFlag && itsAddBoxFlag && !itsInteriorLines.empty())
    {
      auto *ring = make_hole(itsBox);
      itsInteriorRings.push_back(ring);
    }

    // Build polygons starting from the built exterior rings
    for (auto *exterior : itsExteriorRings)
    {
      auto *poly = new OGRPolygon;
      poly->addRingDirectly(exterior);
      itsPolygons.push_back(poly);
    }
    itsExteriorRings.clear();

    // Then assign the holes to them

    for (auto *hole : itsInteriorRings)
    {
      if (itsPolygons.size() == 1)
        itsPolygons.front()->addRing(hole);
      else
      {
        OGRPoint point;
        hole->getPoint(0, &point);
        for (auto *poly : itsPolygons)
        {
          if (poly->getExteriorRing()->isPointInRing(&point, 0) != 0)
          {
            poly->addRingDirectly(hole);
            break;
          }
        }
      }
      delete hole;
    }

    // Merge all unjoinable lines to one list of lines

    std::move(itsInteriorLines.begin(), itsInteriorLines.end(), std::back_inserter(itsExteriorLines));

    itsInteriorRings.clear();
    itsInteriorLines.clear();
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}
