#include "RectClipper.h"
#include "Box.h"
#include "GeometryBuilder.h"
#include "OGR.h"
#include <macgyver/Exception.h>
#include <cassert>
#include <iostream>

namespace
{

// ----------------------------------------------------------------------
/*!
 * \brief Build a ring out of the bbox
 */
// ----------------------------------------------------------------------

OGRLinearRing *make_exterior(const Fmi::Box &theBox, double max_length = 0)
{
  try
  {
    auto *ring = new OGRLinearRing;
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
 * \brief Build a hole out of the bbox
 */
// ----------------------------------------------------------------------

OGRLinearRing *make_hole(const Fmi::Box &theBox, double max_length = 0)
{
  try
  {
    auto *ring = new OGRLinearRing;
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
    std::cerr << "Reconnecting " << lines.size() << " lines\n";

    // Nothing to reconnect if there aren't at least two lines
    if (lines.size() < 2)
      return;

    for (auto pos1 = lines.begin(); pos1 != lines.end();)
    {
      auto *line1 = *pos1;
      int n1 = line1->getNumPoints();

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
        n1 = line1->getNumPoints();
        delete line2;
        line2 = nullptr;
        pos2 = lines.erase(pos2);

        // The merge may have closed a linearring if the intersections
        // have collapsed to a single point. This can happen if there is
        // a tiny sliver polygon just outside the rectangle, and the
        // intersection coordinates will be identical.

        if (line1->get_IsClosed())
        {
          auto *ring = new OGRLinearRing;
          ring->addSubLineString(line1, 0, -1);
          if (exterior)
            clipper.addExterior(ring);
          else
            clipper.addInterior(ring);

          delete line1;

          pos1 = lines.erase(pos1);
          if (pos1 == lines.end())
            return;

          line1 = *pos1;
          n1 = line1->getNumPoints();
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

// ----------------------------------------------------------------------
/*!
 * \brief Search for matching line segment clockwise (cutting)
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
    std::cerr << "Searching cw\n";
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
 * \brief Search for matching line segment counter-clockwise (clipping)
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
    std::cerr << "Searching ccw\n";

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

#if 1
    std::cerr << "connectLines: " << theLines.size() << " lines, "
              << "box=(" << theBox.xmin() << "," << theBox.ymin() << "," << theBox.xmax() << ","
              << theBox.ymax() << ")\n";
    int lineIdx = 0;
    for (auto *line : theLines)
      std::cerr << "Line: " << ++lineIdx << " " << Fmi::OGR::exportToWkt(*line) << "\n";

#endif

    bool cw = false;
    if (keep_inside)
      cw = !exterior;  // clipping: holes CW, exteriors CCW
    else
      cw = exterior;  // cutting: exteriors CW, holes CCW

    OGRLinearRing *ring = nullptr;
    int cornerSteps = 0;

    while (!theLines.empty() || ring != nullptr)
    {
      if (ring == nullptr)
      {
        ring = new OGRLinearRing;
        auto *line = theLines.front();
        theLines.pop_front();
        ring->addSubLineString(line);
        delete line;
        cornerSteps = 0;
#if 1
        std::cerr << "  started new ring at (" << ring->getX(0) << "," << ring->getY(0) << ")\n";
#endif
      }

      int nr = ring->getNumPoints();
      double x1 = ring->getX(nr - 1);
      double y1 = ring->getY(nr - 1);
      double x2 = x1;
      double y2 = y1;

      auto best = (cw ? search_cw(ring, theLines, x1, y1, x2, y2, theBox)
                      : search_ccw(ring, theLines, x1, y1, x2, y2, theBox));

      if (best != theLines.end())
      {
        cornerSteps = 0;
#if 1
        std::cerr << "  found match at (" << (*best)->getX(0) << "," << (*best)->getY(0)
                  << ") from (" << x1 << "," << y1 << ")\n";
#endif
        if (x1 != (*best)->getX(0) || y1 != (*best)->getY(0))
          ring->addSubLineString(*best);
        else
          ring->addSubLineString(*best, 1);
        delete *best;
        theLines.erase(best);
      }
      else
      {
        ++cornerSteps;
#if 1
        std::cerr << "  no match from (" << x1 << "," << y1 << ")"
                  << " -> corner (" << x2 << "," << y2 << ")"
                  << " cornerSteps=" << cornerSteps << "\n";
#endif

        if (cornerSteps > 4)
        {
          throw Fmi::Exception(BCP, "Stuck, discarding ring");
#if 1
          std::cerr << "  stuck, discarding ring\n";
          delete ring;
          ring = nullptr;
          cornerSteps = 0;
          continue;
#endif
        }

        ring->addPoint(x2, y2);
      }

      if (ring->get_IsClosed())
      {
#if 1
        std::cerr << "  ring closed with " << ring->getNumPoints() << " points\n";
#endif
        Fmi::OGR::normalize(*ring);
        theRings.push_back(ring);
        ring = nullptr;
        cornerSteps = 0;
      }
    }
    theLines.clear();
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

}  // namespace

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
    Fmi::Exception exception(BCP, "Destructor failed", nullptr);
    exception.printError();
  }
}

void Fmi::RectClipper::reconnect()
{
  try
  {
    reconnectLines(itsExteriorLines, *this, /*exterior=*/true);
    reconnectLines(itsInteriorLines, *this, /*exterior=*/false);
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
#if 1
    std::cerr << "  RectClipper::addExterior ring pts=" << theRing->getNumPoints()
              << " isClockwise=" << theRing->isClockwise() << "\n";
#endif

    OGR::normalize(*theRing);
    if (theRing->isClockwise() == 1)
      theRing->reversePoints();
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
#if 1
    std::cerr << "Adding exterior line " << Fmi::OGR::exportToWkt(*theLine) << "\n";
#endif
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
#if 1
    std::cerr << "  RectClipper::addInterior ring pts=" << theRing->getNumPoints()
              << " isClockwise=" << theRing->isClockwise() << "\n";
#endif

    OGR::normalize(*theRing);
    if (theRing->isClockwise() == 0)
      theRing->reversePoints();
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

#if 1
    std::cerr << "Reconnecting with bbox\n";
    std::cerr << "\nitsKeepInsideFlag=" << itsKeepInsideFlag << "  itsAddBoxFlag=" << itsAddBoxFlag
              << "\n";
#endif

    if (itsKeepInsideFlag && itsAddBoxFlag && itsExteriorLines.empty())
    {
#if 1
      std::cerr << "Making exterior\n";
#endif
      auto *ring = make_exterior(itsBox, theMaximumSegmentLength);
      itsExteriorRings.push_back(ring);
    }

    // Make hole if necessary

    if (!itsKeepInsideFlag && itsAddBoxFlag && itsInteriorLines.empty())
    {
#if 1
      std::cerr << "Making hole\n";
#endif

      auto *ring = make_hole(itsBox, theMaximumSegmentLength);
      itsInteriorRings.push_back(ring);
    }

    // Reconnect lines into polygons (exterior or hole)
    // Since clipped holes always become part of the exterior, and cut
    // holes are either part of the interior unless the exterior is also clipped,
    // if we have both lines they must by definition all belong to the exterior.

#if 1
    std::cerr << "Exterior lines:\n";
    for (const auto *line : itsExteriorLines)
      std::cerr << Fmi::OGR::exportToWkt(*line) << "\n";
    std::cerr << "Interior lines:\n";
    for (const auto *line : itsInteriorLines)
      std::cerr << Fmi::OGR::exportToWkt(*line) << "\n";
#endif

    if (!itsExteriorLines.empty() && !itsInteriorLines.empty())
    {
      std::move(
          itsInteriorLines.begin(), itsInteriorLines.end(), std::back_inserter(itsExteriorLines));
      itsInteriorLines.clear();
    }

#if 1
    std::cerr << "Connecting exteriors\n";
#endif

    connectLines(itsExteriorRings,
                 itsExteriorLines,
                 itsBox,
                 theMaximumSegmentLength,
                 itsKeepInsideFlag,
                 /*exterior=*/true);

#if 1
    std::cerr << "Connecting interiors\n";
#endif
    connectLines(itsInteriorRings,
                 itsInteriorLines,
                 itsBox,
                 theMaximumSegmentLength,
                 itsKeepInsideFlag,
                 /*exterior=*/false);

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

    std::move(
        itsInteriorLines.begin(), itsInteriorLines.end(), std::back_inserter(itsExteriorLines));

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
    std::cerr << "Reconnecting without bbox\n";

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

    std::move(
        itsInteriorLines.begin(), itsInteriorLines.end(), std::back_inserter(itsExteriorLines));

    itsInteriorRings.clear();
    itsInteriorLines.clear();
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}
