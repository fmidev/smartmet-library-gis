#include "ClipParts.h"
#include "Box.h"
#include "OGR.h"  // TODO: REMOVE!
#include <cassert>

// ----------------------------------------------------------------------
/*!
 * \brief Destructor must delete all objects left behind in case of throw
 *
 * Normally build succeeds and clears the containers
 */
// ----------------------------------------------------------------------

Fmi::ClipParts::~ClipParts()
{
  for (auto *ptr : itsPolygons)
    delete ptr;
  for (auto *ptr : itsLines)
    delete ptr;
  for (auto *ptr : itsPoints)
    delete ptr;
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
 *
 * TODO: If there is a very sharp spike from inside the rectangle
 *       outside, and then back in, it is possible that the
 *       intersection points at the edge are equal. In this
 *       case we could reconnect the linestrings. The task is
 *       the same we're already doing for the 1st/last linestrings,
 *       we'd just do it for any adjacent pair as well.
 */
// ----------------------------------------------------------------------

void Fmi::ClipParts::reconnect()
{
  // Nothing to reconnect if there aren't at least two lines
  if (itsLines.size() < 2) return;

  OGRLineString *line1 = itsLines.front();
  OGRLineString *line2 = itsLines.back();

  const int n1 = line1->getNumPoints();
  const int n2 = line2->getNumPoints();

  // Safety check against bad input to prevent segfaults
  if (n1 == 0 || n2 == 0) return;

  if (line1->getX(0) != line2->getX(n2 - 1) || line1->getY(0) != line2->getY(n2 - 1))
  {
    return;
  }

  // Merge the two linestrings

  line2->addSubLineString(line1, 1, n1 - 1);
  delete line1;
  itsLines.pop_front();
  itsLines.pop_back();
  itsLines.push_front(line2);
}

// ----------------------------------------------------------------------
/*!
 * \brief Export parts to another container
 */
// ----------------------------------------------------------------------

void Fmi::ClipParts::release(ClipParts &theParts)
{
  for (auto *ptr : itsPolygons)
    theParts.add(ptr);
  for (auto *ptr : itsLines)
    theParts.add(ptr);
  for (auto *ptr : itsPoints)
    theParts.add(ptr);

  clear();
}

// ----------------------------------------------------------------------
/*!
 * \brief Clear the parts having transferred ownership elsewhere
 */
// ----------------------------------------------------------------------

void Fmi::ClipParts::clear()
{
  itsPolygons.clear();
  itsLines.clear();
  itsPoints.clear();
}

// ----------------------------------------------------------------------
/*!
 * \brief Test if there are no parts at all
 */
// ----------------------------------------------------------------------

bool Fmi::ClipParts::empty() const
{
  return itsPolygons.empty() && itsLines.empty() && itsPoints.empty();
}

// ----------------------------------------------------------------------
/*!
 * \brief Add intermediate OGR Polygon
 */
// ----------------------------------------------------------------------

void Fmi::ClipParts::add(OGRPolygon *thePolygon) { itsPolygons.push_back(thePolygon); }
// ----------------------------------------------------------------------
/*!
 * \brief Add intermediate OGR LineString
 */
// ----------------------------------------------------------------------

void Fmi::ClipParts::add(OGRLineString *theLine) { itsLines.push_back(theLine); }
// ----------------------------------------------------------------------
/*!
 * \brief Add intermediate OGR Point
 */
// ----------------------------------------------------------------------

void Fmi::ClipParts::add(OGRPoint *thePoint) { itsPoints.push_back(thePoint); }
// ----------------------------------------------------------------------
/*!
 * \brief Build the result geometry from partial results and clean up
 */
// ----------------------------------------------------------------------

OGRGeometry *Fmi::ClipParts::build()
{
  auto *ptr = internalBuild();
  clear();
  return ptr;
}

// ----------------------------------------------------------------------
/*!
 * \brief Build the result geometry from the partial results
 *
 * Does NOT clear the used data!
 */
// ----------------------------------------------------------------------

OGRGeometry *Fmi::ClipParts::internalBuild() const
{
  // Total number of objects

  std::size_t n = itsPolygons.size() + itsLines.size() + itsPoints.size();

  // We wish to avoid nullptr pointers due to more prone segfault mistakes

  if (n == 0) return new OGRGeometryCollection;

  // Simplify to LineString, Polygon or Point if possible
  if (n == 1)
  {
    if (itsPolygons.size() == 1)
      return itsPolygons.front();
    else if (itsLines.size() == 1)
      return itsLines.front();
    else
      return itsPoints.front();
  }

  // Simplify to MultiLineString, MultiPolygon or MultiPoint if possible

  if (!itsPolygons.empty() && itsLines.empty() && itsPoints.empty())
  {
    auto *geom = new OGRMultiPolygon;
    for (auto *ptr : itsPolygons)
      geom->addGeometryDirectly(ptr);
    return geom;
  }

  if (!itsLines.empty() && itsPolygons.empty() && itsPoints.empty())
  {
    auto *geom = new OGRMultiLineString;
    for (auto *ptr : itsLines)
      geom->addGeometryDirectly(ptr);
    return geom;
  }

  if (!itsPoints.empty() && itsLines.empty() && itsPolygons.empty())
  {
    auto *geom = new OGRMultiPoint;
    for (auto *ptr : itsPoints)
      geom->addGeometryDirectly(ptr);
    return geom;
  }

  // A generic collection must be used, the types differ too much.
  // The order should be kept the same for the benefit of
  // regression tests: polygons, lines, points

  auto *geom = new OGRGeometryCollection;

  // Add a Polygon or a MultiPolygon
  if (!itsPolygons.empty())
  {
    if (itsPolygons.size() == 1)
      geom->addGeometryDirectly(itsPolygons.front());
    else
    {
      auto *tmp = new OGRMultiPolygon;
      for (auto *ptr : itsPolygons)
        tmp->addGeometryDirectly(ptr);
      geom->addGeometryDirectly(tmp);
    }
  }

  // Add a LineString or a MultiLineString
  if (!itsLines.empty())
  {
    if (itsLines.size() == 1)
      geom->addGeometryDirectly(itsLines.front());
    else
    {
      auto *tmp = new OGRMultiLineString;
      for (auto *ptr : itsLines)
        tmp->addGeometryDirectly(ptr);
      geom->addGeometryDirectly(tmp);
    }
  }

  // Add a Point or a MultiPoint
  if (!itsPoints.empty())
  {
    if (itsPoints.size() == 1)
      geom->addGeometryDirectly(itsPoints.front());
    else
    {
      auto *tmp = new OGRMultiPoint;
      for (auto *ptr : itsPoints)
        tmp->addGeometryDirectly(ptr);
      geom->addGeometryDirectly(tmp);
    }
  }

  return geom;
}

// ----------------------------------------------------------------------
/*!
 * \brief Reverse given segment in a linestring
 */
// ----------------------------------------------------------------------

void reverse_points(OGRLineString *line, int start, int end)
{
  OGRPoint p1;
  OGRPoint p2;
  while (start < end)
  {
    line->getPoint(start, &p1);
    line->getPoint(end, &p2);
    line->setPoint(start, &p2);
    line->setPoint(end, &p1);
    ++start;
    --end;
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Normalize a ring into lexicographic order
 */
// ----------------------------------------------------------------------

void normalize_ring(OGRLinearRing *ring)
{
  if (ring->IsEmpty()) return;

  // Find the "smallest" coordinate

  int best_pos = 0;
  int n = ring->getNumPoints();
  for (int pos = 0; pos < n; ++pos)
  {
    if (ring->getX(pos) < ring->getX(best_pos))
      best_pos = pos;
    else if (ring->getX(pos) == ring->getX(best_pos) && ring->getY(pos) < ring->getY(best_pos))
      best_pos = pos;
  }

  // Quick exit if the ring is already normalized
  if (best_pos == 0) return;

  // Flip hands -algorithm to the part without the
  // duplicate last coordinate at n-1:

  reverse_points(ring, 0, best_pos - 1);
  reverse_points(ring, best_pos, n - 2);
  reverse_points(ring, 0, n - 2);

  // And make sure the ring is valid by duplicating the first coordinate
  // at the end:

  OGRPoint point;
  ring->getPoint(0, &point);
  ring->setPoint(n - 1, &point);
}

// ----------------------------------------------------------------------
/*!
 * \Reverse the lines being built
 *
 * This is used to fix winding rules required by the clipping algorithm
 */
// ----------------------------------------------------------------------

void Fmi::ClipParts::reverseLines()
{
  std::list<OGRLineString *> new_lines;
  for (std::list<OGRLineString *>::reverse_iterator it = itsLines.rbegin(), end = itsLines.rend();
       it != end;
       ++it)
  {
    OGRLineString *line = *it;
    line->reversePoints();
    new_lines.push_back(line);
  }
  itsLines = new_lines;
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
 * 2. Proceed outputting the bbox edges in clockwise orientation
 *    until the ring itself or another linestring is encountered.
 * 3. If the ring became closed by joining the bbox edges, output it and restart at step 1.
 * 4. Attach the new linestring to the ring and jump back to step 2.
 */
// ----------------------------------------------------------------------

void Fmi::ClipParts::reconnectPolygons(const Box &theBox)
{
  // Build the exterior rings first

  std::list<OGRLinearRing *> exterior;

  // If there are no lines, the rectangle must have been
  // inside the exterior ring.

  if (itsLines.empty())
  {
    OGRLinearRing *ring = new OGRLinearRing;
    ring->addPoint(theBox.xmin(), theBox.ymin());
    ring->addPoint(theBox.xmin(), theBox.ymax());
    ring->addPoint(theBox.xmax(), theBox.ymax());
    ring->addPoint(theBox.xmax(), theBox.ymin());
    ring->addPoint(theBox.xmin(), theBox.ymin());
    exterior.push_back(ring);
  }
  else
  {
    // Reconnect all lines into one or more linearrings
    // using box boundaries if necessary

    OGRLinearRing *ring = nullptr;

    while (!itsLines.empty() || ring != nullptr)
    {
      if (ring == nullptr)
      {
        ring = new OGRLinearRing;
        auto *line = itsLines.front();
        itsLines.pop_front();
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
      auto best = itsLines.end();

      if (y1 == theBox.ymin() && x1 > theBox.xmin())
      {
        // On lower edge going left, worst we can do is left corner, closing might be better

        if (ring->getY(0) == y1 && ring->getX(0) < x1)
          x2 = ring->getX(0);
        else
          x2 = theBox.xmin();

        // Look for a better match from the remaining linestrings.

        for (auto iter = itsLines.begin(); iter != itsLines.end(); ++iter)
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
      else if (x1 == theBox.xmin() && y1 < theBox.ymax())
      {
        // On left edge going up, worst we can do is upper conner, closing might be better

        if (ring->getX(0) == x1 && ring->getY(0) > y1)
          y2 = ring->getY(0);
        else
          y2 = theBox.ymax();

        for (auto iter = itsLines.begin(); iter != itsLines.end(); ++iter)
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
      else if (y1 == theBox.ymax() && x1 < theBox.xmax())
      {
        // On top edge going right, worst we can do is right corner, closing might be better

        if (ring->getY(0) == y1 && ring->getX(0) > x1)
          x2 = ring->getX(0);
        else
          x2 = theBox.xmax();

        for (auto iter = itsLines.begin(); iter != itsLines.end(); ++iter)
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
          y2 = theBox.ymin();

        for (auto iter = itsLines.begin(); iter != itsLines.end(); ++iter)
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

      if (best != itsLines.end())
      {
        // Found a matching linestring to continue to from the same
        // edge we were studying. Move to it and continue building.
        ring->addSubLineString(*best);
        delete *best;
        itsLines.erase(best);
      }
      else
      {
        // Couldn't find a matching best line. Either close the ring or move to next corner.
        ring->addPoint(x2, y2);

        if (ring->get_IsClosed())
        {
          normalize_ring(ring);
          exterior.push_back(ring);
          ring = nullptr;
        }
      }
    }
  }

  // Build the result polygons

  std::list<OGRPolygon *> polygons;
  for (auto *ring : exterior)
  {
    OGRPolygon *poly = new OGRPolygon;
    poly->addRingDirectly(ring);
    polygons.push_back(poly);
  }

  // Attach holes to polygons

  for (auto *hole : itsPolygons)
  {
    if (polygons.size() == 1)
      polygons.front()->addRing(hole->getExteriorRing());
    else
    {
      OGRPoint point;
      hole->getExteriorRing()->getPoint(0, &point);
      for (auto *poly : polygons)
      {
        if (poly->getExteriorRing()->isPointInRing(&point, false))
        {
          poly->addRing(hole->getExteriorRing());
          break;
        }
      }
    }
    delete hole;
  }

  clear();
  itsPolygons = polygons;
}
