#include "ShapeClipper.h"
#include "GeometryBuilder.h"
#include "OGR.h"
#include <boost/functional/hash.hpp>
#include <boost/math/constants/constants.hpp>
#include <cassert>
#include <iostream>
#include <unordered_map>
#include <utility>

namespace Fmi
{
namespace
{
double turn_angle(double angle1, double angle2)
{
  // max turn left > -180 and max turn right < 180
  auto turn = fmod(angle1 - angle2, 360.0);
  if (turn < -180)
    turn += 360;
  else if (turn > 180)
    turn -= 360;
  return turn;
}

using OGRLineStringList = std::list<OGRLineString *>;
using OGRLineStringMatches = std::list<OGRLineStringList::iterator>;

OGRLineStringList::iterator best_match(const OGRLineString &line1,
                                       const OGRLineStringMatches &matches)
{
  auto pos2 = matches.front();

  if (matches.size() > 1)  // only two matches should be possible though
  {
    using boost::math::double_constants::radian;
    const auto n1 = line1.getNumPoints();
    const auto dy1 = line1.getY(n1 - 1) - line1.getY(n1 - 2);
    const auto dx1 = line1.getX(n1 - 1) - line1.getX(n1 - 2);
    const auto angle1 = atan2(dy1, dx1) * radian;

    // Choose rightmost turn to satisfy OGC rules
    double best_turn = -999;
    for (auto &p : matches)
    {
      auto *line = *p;
      const auto dy = line->getY(1) - line->getY(0);
      const auto dx = line->getX(1) - line->getX(0);
      const auto angle2 = atan2(dy, dx) * radian;
      const auto turn = turn_angle(angle1, angle2);

      if (turn > best_turn)
      {
        best_turn = turn;
        pos2 = p;
      }
    }
  }

  return pos2;
}

// Code for detecting holes touching each other or the exterior shell

using Vertex = std::pair<double, double>;
using VertexCounts = std::unordered_map<Vertex, int, boost::hash<Vertex>>;

void add_vertex_counts(VertexCounts &counts, const OGRLineStringList &lines)
{
  for (auto *line : lines)
  {
    const auto n = line->getNumPoints();
    for (auto i = 1; i < n - 1; i++)
    {
      Vertex vertex(line->getX(i), line->getY(i));
      ++counts[vertex];
    }
  }
}

void remove_singles(VertexCounts &counts)
{
  VertexCounts duplicates;
  for (auto &vertex_count : counts)
    if (vertex_count.second > 1)
      duplicates.insert(vertex_count);
  std::swap(counts, duplicates);
}

void split_touches(const VertexCounts &counts, OGRLineStringList &lines)
{
  // Avoid traversal if there are no touches
  if (counts.empty())
    return;

  OGRLineStringList new_lines;
  for (auto *line : lines)
  {
    const auto n = line->getNumPoints();
    for (auto i = 1; i < n - 1; i++)
    {
      Vertex vertex(line->getX(i), line->getY(i));
      if (counts.find(vertex) != counts.end())
      {
        // Extract from start of line to this point
        auto *new_line = new OGRLineString;
        new_line->addSubLineString(line, 0, i);
        new_lines.push_back(new_line);

        // Extract from this point to end of the line
        new_line = new OGRLineString;
        new_line->addSubLineString(line, i, -1);
        delete line;
        line = new_line;
      }
    }
    // Output the remaining line (or all of it if no split occurred)
    new_lines.push_back(line);
  }

  std::swap(lines, new_lines);
}

}  // namespace

ShapeClipper::ShapeClipper(Shape_sptr &theShape, bool keep_inside)
{
  try
  {
    if (theShape == nullptr)
      throw Fmi::Exception(BCP, "The 'theShape' parameter points to NULL!");

    itsShape = theShape;
    itsKeepInsideFlag = keep_inside;
    itsAddShapeFlag = false;
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

Fmi::ShapeClipper::~ShapeClipper()
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
    Fmi::Exception exception(BCP, "Operation failed!", nullptr);
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

void Fmi::ShapeClipper::reconnectLines(std::list<OGRLineString *> &lines, bool exterior)
{
  try
  {
    // Nothing to reconnect if there aren't at least two lines
    if (lines.size() < 2)
      return;

    for (auto pos1 = lines.begin(); pos1 != lines.end();)
    {
      auto *line1 = *pos1;
      int n1 = line1->getNumPoints();

      // Find matching continuations for line1, if any

      OGRLineStringMatches matches;

      for (auto pos2 = lines.begin(); pos2 != lines.end(); ++pos2)
      {
        auto *line2 = *pos2;
        const int n2 = line2->getNumPoints();

        // Find matching continuations
        if (n1 != 0 && n2 != 0 && pos1 != pos2 && line1->getX(n1 - 1) == line2->getX(0) &&
            line1->getY(n1 - 1) == line2->getY(0))
        {
          matches.push_back(pos2);
        }
      }

      if (matches.empty())
      {
        ++pos1;
        continue;
      }

      auto pos2 = best_match(*line1, matches);

      // Join the best match at pos2 to line1
      auto line2 = *pos2;
      auto n2 = line2->getNumPoints();

      line1->addSubLineString(line2, 1, n2 - 1);
      delete line2;
      lines.erase(pos2);

      // The merge may have closed a linearring if the intersections
      // have collapsed to a single point. This can happen if there is
      // a tiny sliver polygon just outside the rectangle, and the
      // intersection coordinates will be identical.

      if (line1->get_IsClosed())
      {
        auto *ring = new OGRLinearRing;
        ring->addSubLineString(line1, 0, -1);
        if (exterior)
          addExterior(ring);
        else
          addInterior(ring);
        delete line1;
        pos1 = lines.erase(pos1);
        if (pos1 == lines.end())
          return;
      }
    }
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

void Fmi::ShapeClipper::reconnect()
{
  try
  {
    // Count how many times points between the end points occur (holes touching the exterior or
    // other holes)
    VertexCounts counts;
    add_vertex_counts(counts, itsExteriorLines);
    add_vertex_counts(counts, itsInteriorLines);
    // And split the lines at points with count > 1 for doing correct reconnections
    remove_singles(counts);
    split_touches(counts, itsExteriorLines);
    split_touches(counts, itsInteriorLines);

    // Then reconnect the lines obeying OGC rules (always turn right)
    reconnectLines(itsExteriorLines, true);
    reconnectLines(itsInteriorLines, false);
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

void Fmi::ShapeClipper::release(GeometryBuilder &theBuilder)
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

void Fmi::ShapeClipper::clear()
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

bool Fmi::ShapeClipper::empty() const
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
 * \brief Add a shape to the result
 */
// ----------------------------------------------------------------------

void Fmi::ShapeClipper::addShape()
{
  try
  {
    itsAddShapeFlag = true;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Add a linestring
 */
// ----------------------------------------------------------------------

void Fmi::ShapeClipper::add(OGRLineString *theLine, bool exterior)
{
  if (exterior)
    addExterior(theLine);
  else
    addInterior(theLine);
}

// ----------------------------------------------------------------------
/*!
 * \brief Add intermediate OGR Polygon
 */
// ----------------------------------------------------------------------

void Fmi::ShapeClipper::addExterior(OGRLinearRing *theRing)
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

void Fmi::ShapeClipper::addExterior(OGRLineString *theLine)
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

void Fmi::ShapeClipper::addInterior(OGRLinearRing *theRing)
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

void Fmi::ShapeClipper::addInterior(OGRLineString *theLine)
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
 * \brief Reconnect lines into polygons along box edges
 */
// ----------------------------------------------------------------------

void Fmi::ShapeClipper::connectLines(std::list<OGRLinearRing *> &theRings,
                                     std::list<OGRLineString *> &theLines,
                                     double theMaximumSegmentLength,
                                     bool keep_inside,
                                     bool exterior)
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
    double x2 = ring->getX(0);
    double y2 = ring->getY(0);

    // printf("RING %f,%f => %f,%f\n",x1,y1,x2,y2);

    // No linestring to move onto found next - meaning we'd
    // either move to the next corner or close the current ring.

    if (!ring->get_IsClosed())
    {
      auto best = (cw ? itsShape->search_cw(ring, theLines, x1, y1, x2, y2)
                      : itsShape->search_ccw(ring, theLines, x1, y1, x2, y2));
      if (best != theLines.end())
      {
        // Found a matching linestring to continue to from the same edge we were studying. Move to
        // it and continue building. The line might continue from the same point, in which case we
        // must skip the first point.

        if (x1 != (*best)->getX(0) || y1 != (*best)->getY(0))
        {
          // printf(" -- connect %f,%f => %f,%f
          // %f,%f\n",x1,y1,x2,y2,(*best)->getX(0),(*best)->getY(0));
          if (cw)
            itsShape->connectPoints_cw(*ring, x1, y1, x2, y2, theMaximumSegmentLength);
          else
            itsShape->connectPoints_ccw(*ring, x1, y1, x2, y2, theMaximumSegmentLength);

          if (x2 != (*best)->getX(0) || y2 != (*best)->getY(0))
            ring->addSubLineString(*best);
          else
            ring->addSubLineString(*best, 1);  // start from 2nd point
        }
        else
        {
          ring->addSubLineString(*best, 1);  // start from 2nd point
        }
        delete *best;
        theLines.erase(best);
      }
      else
      {
        if (x1 != x2 || y1 != y2)
        {
          // printf(" ++ connect %f,%f => %f,%f\n",x1,y1,x2,y2);
          if (cw)
            itsShape->connectPoints_cw(*ring, x1, y1, x2, y2, theMaximumSegmentLength);
          else
            itsShape->connectPoints_ccw(*ring, x1, y1, x2, y2, theMaximumSegmentLength);
        }

        if (x2 == ring->getX(0) && y2 == ring->getY(0))
          ring->closeRings();
      }
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

void Fmi::ShapeClipper::reconnectWithShape(double theMaximumSegmentLength)
{
  try
  {
    // Make exterior ring if necessary

    if (itsKeepInsideFlag && itsAddShapeFlag && itsExteriorLines.empty())
    {
      auto *ring = itsShape->makeRing(theMaximumSegmentLength);
      itsExteriorRings.push_back(ring);
    }

    // Make hole if necessary

    if (!itsKeepInsideFlag && itsAddShapeFlag && itsInteriorLines.empty())
    {
      auto *ring = itsShape->makeHole(theMaximumSegmentLength);
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
        itsExteriorRings, itsExteriorLines, theMaximumSegmentLength, itsKeepInsideFlag, true);
    connectLines(
        itsInteriorRings, itsInteriorLines, theMaximumSegmentLength, itsKeepInsideFlag, false);

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
      if (itsPolygons.size() == 0)
        ;
      else if (itsPolygons.size() == 1)
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
void Fmi::ShapeClipper::reconnectWithoutShape()
{
  try
  {
    // Make exterior circle if necessary

    /*
    std::cout << itsExteriorRings.size() << "\n";
    std::cout << itsExteriorLines.size() << "\n";

    std::cout << itsInteriorRings.size() << "\n";
    std::cout << itsInteriorLines.size() << "\n";
     */

    if (itsKeepInsideFlag && itsAddShapeFlag && itsExteriorLines.empty())
    {
      // auto *ring = make_exterior(itsShape);
      auto *ring = itsShape->makeRing(0);
      itsExteriorRings.push_back(ring);
    }

    // Make hole if necessary

    if (!itsKeepInsideFlag && itsAddShapeFlag && !itsInteriorLines.empty())
    {
      // auto *ring = make_hole(itsShape);
      auto *ring = itsShape->makeHole(0);
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

}  // namespace Fmi
