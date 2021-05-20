/*

  When a polylon or polyline is clipped with a rectangle, the possible cases
  when handling an individual line segment based on the location of its end points are:

  1. in-in
     - continue clip
  2. in-edge
     - terminate clip
  3. in-out
     - terminate clip at intersection point
  4. edge-in
     - begin new linestring
  5. edge-edge
     a) from edge 1 to edge 1
        - ignore segment for travelling on the edges only
     b) from edge 1 to edge 2
        - produce segment between endpoints
  6. edge-out
     - if passes inside the rectangle, produce a linesegment
     - ignore segment if goes straight out
  7. out-in
     - begin new linestring at intersection point
  8. out-edge
     - if passes inside the rectangle, produce linestring from the intersection points
     - ignore otherwise
  9. out-out
     a) ignore if both points completely below, above, left or right of rectangle
     b) calculate possible intersection.
        - produce line between intersection points are of non zero length
          and are not on the same edge (segment is colinear with the edge)

  When cutting a rectangle out:

  1. in-in
     - ignore segment
  2. in-edge
     - ignore segment
  3. in-out
     - begin new linestring at intersection point
  4. edge-in
     - ignore segment
  5. edge-edge
     - ignore segment
  6. edge-out
     - if passes inside the rectangle, start linestring at the exit intersection point
     - otherwise begin new linestring at start point
  7. out-in
     - end current linestring at intersection point
  8. out-edge
     - if passes inside the rectangle, terminate current linestring at the intersection
     - otherwise terminate at the end point
  9. out-out
     a) keep if both points completely below, above, left or right of rectangle
     b) calculate possible intersection.
        - if line between intersections points is of non zero length, end current
          linestring at the first intersection and begin a new one at the second intersection
        - otherwise keep growing the current linestring with the segment

  When a polygon is clipped or cut, the possible outcomes for individual rings are as follows:

  Clipping
  * exterior is not clipped
    - if all vertices inside box or on the edges, return input as is
    - if all vertices outside box or on the edges
       - if box inside exterior, box becomes part of new exterior ; ADD_BOX
       - if box outside exterior, return empty result
  * exterior is clipped, and becomes part of new exterior ; ADD_EXTERIOR
  * hole is not clipped
    - if all vertices inside box or on the edges, hole is part of new interior ; ADD_INTERIOR
    - if all vertices outside box
       - if box inside hole, return empty result
       - if box outside hole, omit box
  * hole is clipped
    - becomes part of new exterior ; ADD_EXTERIOR

  Cutting
  * exterior is not cut
    - if all vertices inside box or on the edges, return empty result
    - if all vertices outside box or on the edges
       - if box inside exterior, box becomes part of new holes ; ADD_EXTERIOR + ADD_BOX
       - if box outside exterior, return input as is
  * exterior is cut, and becomes part of new exterior ; ADD_EXTERIOR
  * hole is not cut
    - if all vertices inside box or on the edges, hole is omitted
    - if all vertices outside box or on the edges
       - if box inside hole, return input as is
       - if box outside hole, omit hole
  * hole is cut
    - becomes part of new interior ; ADD_INTERIOR

  The algorithm structure is identical, the only thing that changes is which part
  goes where in the final result.

  a) A ring may be exterior or a hole
  b) A linestring may be part of exterior or a hole
  c) There may be multiple exteriors built from a/b, to which the build holes have to be assigned to

  So:
  1. Gather exterior and interior rings
  2. Gather exterior and interior linestrings
  3. Flag if the box should be exterior or hole if nothing intersects it
  4. Build exterior rings
  5. Build interior rings
  6. Assign rings to exterior rings to get polygons
  7. Build multipolygon if necessary

 */

#include "Box.h"
#include "GeometryBuilder.h"
#include "OGR.h"
#include "RectClipper.h"
#include <iomanip>
#include <iostream>
#include <list>
#include <stdexcept>

namespace Fmi
{
// ----------------------------------------------------------------------
/*!
 * \brief Test if all positions were outside the box or on the edges
 */
// ----------------------------------------------------------------------

bool all_not_inside(int position)
{
  return ((position & Box::Inside) == 0);
}

// ----------------------------------------------------------------------
/*!
 * \brief Test if all positions were on the edges or inside the box
 */
// ----------------------------------------------------------------------

bool all_not_outside(int position)
{
  return ((position & Box::Outside) == 0);
}

// ----------------------------------------------------------------------
/*!
 * \brief Test if all positions were inside the box
 */
// ----------------------------------------------------------------------

bool all_only_inside(int position)
{
  return (position & Box::Inside) != 0 && !(position & (position - 1));
}

// ----------------------------------------------------------------------
/*!
 * \brief Test if all positions were outside the box
 */
// ----------------------------------------------------------------------

bool all_only_outside(int position)
{
  return (position & Box::Outside) != 0 && !(position & (position - 1));
}

// ----------------------------------------------------------------------
/*!
 * \brief Test if the box is inside the given ring given that it is either inside or outside
 */
// ----------------------------------------------------------------------

bool box_inside_ring(const Box &theBox, const OGRLinearRing &theRing)
{
  return (OGR::inside(theRing, theBox.xmin(), theBox.ymin()) &&
          OGR::inside(theRing, theBox.xmin(), theBox.ymax()) &&
          OGR::inside(theRing, theBox.xmax(), theBox.ymin()) &&
          OGR::inside(theRing, theBox.xmax(), theBox.ymax()));
}

// ----------------------------------------------------------------------
/*!
 * \brief Utility function to make code more readable
 */
// ----------------------------------------------------------------------

bool different(double x1, double y1, double x2, double y2)
{
  return !(x1 == x2 && y1 == y2);
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

inline void clip_one_edge(double &x1, double &y1, double x2, double y2, double limit)
{
  if (x1 != x2)
  {
    y1 += (y2 - y1) * (limit - x1) / (x2 - x1);
    x1 = limit;
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

void clip_to_edges(double &x1, double &y1, double x2, double y2, const Box &theBox)
{
  if (x1 < theBox.xmin())
    clip_one_edge(x1, y1, x2, y2, theBox.xmin());
  else if (x1 > theBox.xmax())
    clip_one_edge(x1, y1, x2, y2, theBox.xmax());

  if (y1 < theBox.ymin())
    clip_one_edge(y1, x1, y2, x2, theBox.ymin());
  else if (y1 > theBox.ymax())
    clip_one_edge(y1, x1, y2, x2, theBox.ymax());
}

// ----------------------------------------------------------------------
/*!
 * \brief Clip OGR geometry
 *
 * Here outGeom may also be a OGRMultiPoint
 */
// ----------------------------------------------------------------------

void do_point(const OGRPoint *theGeom,
              GeometryBuilder &theBuilder,
              const Box &theBox,
              bool keep_inside)
{
  if (theGeom == nullptr)
    return;

  double x = theGeom->getX();
  double y = theGeom->getY();

  auto pos = theBox.position(x, y);
  if (keep_inside)
  {
    if (pos == Box::Inside)
      theBuilder.add(new OGRPoint(x, y));
  }
  else
  {
    if (pos == Box::Outside)
      theBuilder.add(new OGRPoint(x, y));
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Test whether a hole is inside or outside the clipping box.
 *
 * At this point we know the hole does not intersect the clipping box
 * in a way that would generate any lines. However, it may still touch
 * the box at a vertex. It is then sufficient to find a point which
 * is not on the edge of the clipping box to determine whether the hole
 * is outside or inside the box.
 */
// ----------------------------------------------------------------------

bool inside(const OGRLinearRing *theGeom, const Box &theBox)
{
  int n = theGeom->getNumPoints();

  if (theGeom == nullptr || n < 1)
    return false;

  for (int i = 0; i < n; ++i)
  {
    double x = theGeom->getX(i);
    double y = theGeom->getY(i);

    Box::Position pos = theBox.position(x, y);

    if (pos == Box::Outside)
      return false;
    if (pos == Box::Inside)
      return true;
  }

  // Indeterminate answer, should be impossible. We discard the hole
  // by saying it is not inside.

  return false;
}

// ----------------------------------------------------------------------
/*!
 * \brief Clip OGR geometry.
 *
 * Returns OR'ed position for all vertices, from which one can quickly tell if all points
 * were either inside or outside.
 */
// ----------------------------------------------------------------------

int clip_rect(const OGRLineString *theGeom, RectClipper &theRect, const Box &theBox, bool exterior)
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
    Box::Position pos = theBox.position(x, y);

    // Update global status
    position |= pos;

    if (pos == Box::Outside)
    {
      // Skip points as fast as possible until something has to be checked
      // in more detail.

      ++i;  // we already know it is outside

      if (x < theBox.xmin())
        while (i < n && g.getX(i) < theBox.xmin())
          ++i;

      else if (x > theBox.xmax())
        while (i < n && g.getX(i) > theBox.xmax())
          ++i;

      else if (y < theBox.ymin())
        while (i < n && g.getY(i) < theBox.ymin())
          ++i;

      else if (y > theBox.ymax())
        while (i < n && g.getY(i) > theBox.ymax())
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
      pos = theBox.position(x, y);

      position |= pos;

      // Calculate possible intersection coordinate with the box

      x0 = g.getX(i - 1);
      y0 = g.getY(i - 1);
      clip_to_edges(x0, y0, x, y, theBox);

      if (pos == Box::Inside)  // out-in
      {
        // x0,y0 begings the new linestring
        start_index = i;
        add_start = true;
        // Main loop will enter the Inside/Edge section
      }
      else if (pos == Box::Outside)  // out-out
      {
        // Clip the other end too
        clip_to_edges(x, y, x0, y0, theBox);

        auto prev_pos = theBox.position(x0, y0);
        pos = theBox.position(x, y);

        if (different(x0, y0, x, y) &&  // discard corners etc
            Box::onEdge(prev_pos) &&    // discard if does not intersect rect
            Box::onEdge(pos) && !Box::onSameEdge(prev_pos, pos)  // discard if travels along edge
        )
        {
          position |= Box::Inside;  // something is inside
          auto *line = new OGRLineString;
          line->addPoint(x0, y0);
          line->addPoint(x, y);
          if (exterior)
            theRect.addExterior(line);
          else
            theRect.addInterior(line);
        }

        // Continue main loop outside the rect
      }
      else  // out-edge
      {
        auto prev_pos = theBox.position(x0, y0);

        if (!Box::onSameEdge(pos, prev_pos))
        {
          position |= Box::Inside;  // something is inside
          auto *line = new OGRLineString;
          line->addPoint(x0, y0);
          line->addPoint(x, y);
          if (exterior)
            theRect.addExterior(line);
          else
            theRect.addInterior(line);
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

    else if (pos == Box::Inside)
    {
      while (++i < n)
      {
        x = g.getX(i);
        y = g.getY(i);

        pos = theBox.position(x, y);
        position |= pos;

        if (pos == Box::Inside)  // in-in
        {
        }
        else if (pos == Box::Outside)  // in-out
        {
          // Clip the outside point to edges
          clip_to_edges(x, y, g.getX(i - 1), g.getY(i - 1), theBox);

          // Note that if there is a spike that enters and exits the box
          // at the same coordinate, we must discard the spike.

          const bool spike = (add_start && x0 == x && y0 == y && i - start_index < 2);

          if (!spike)
          {
            auto *line = new OGRLineString();
            if (add_start)
              line->addPoint(x0, y0);
            if (start_index <= i - 1)
              line->addSubLineString(&g, start_index, i - 1);
            line->addPoint(x, y);
            theRect.addExterior(line);
          }
          add_start = false;

          break;  // going to outside the loop
        }
        else  // in-edge
        {
          if (start_index == 0 && i == n - 1)
            return Box::Inside;  // All inside?

          auto *line = new OGRLineString();
          if (add_start)
            line->addPoint(x0, y0);
          add_start = false;
          line->addSubLineString(&g, start_index, i);
          if (exterior)
            theRect.addExterior(line);
          else
            theRect.addInterior(line);

          start_index = i;  // potentially going in again at this point
          break;            // going to the edge loop
        }
      }

      // Was everything in? If so, generate no output here, just clone the linestring elsewhere
      if (start_index == 0 && i >= n)
        return Box::Inside;

      // Flush the last line segment if data ended and there is something to flush
      if (pos == Box::Inside && (start_index < i - 1 || add_start))
      {
        auto *line = new OGRLineString();
        if (add_start)
        {
          line->addPoint(x0, y0);
          add_start = false;
        }
        line->addSubLineString(&g, start_index, i - 1);
        if (exterior)
          theRect.addExterior(line);
        else
          theRect.addInterior(line);
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

        pos = theBox.position(x, y);
        position |= pos;

        if (pos == Box::Inside)  // edge-in
        {
          // Start new linestring
          start_index = i - 1;
          break;  // And continue main loop on the inside
        }
        else if (pos == Box::Outside)  // edge-out
        {
          // Clip the outside point to edges
          clip_to_edges(x, y, g.getX(i - 1), g.getY(i - 1), theBox);
          pos = theBox.position(x, y);

          // Does the line exit through the inside of the box?

          bool through_box =
              (different(x, y, g.getX(i), g.getY(i)) && !Box::onSameEdge(prev_pos, pos));

          // Output a LineString if it at least one segment long

          if (through_box)
          {
            auto *line = new OGRLineString();
            line->addPoint(g.getX(i - 1), g.getY(i - 1));
            line->addPoint(x, y);
            theRect.addExterior(line);
          }
          break;  // And continue main loop on the outside
        }
        else  // edge-edge
        {
          // travel to different edge?
          if (!Box::onSameEdge(prev_pos, pos))
          {
            position |= Box::Inside;  // passed through
            auto *line = new OGRLineString();
            line->addPoint(g.getX(i - 1), g.getY(i - 1));
            line->addPoint(x, y);
            theRect.addExterior(line);
            start_index = i;
          }
        }
      }
    }
  }
  return position;
}

// ----------------------------------------------------------------------
/*!
 * \brief Cut OGR geometry.
 */
// ----------------------------------------------------------------------

int cut_rect(const OGRLineString *theGeom, RectClipper &theRect, const Box &theBox, bool exterior)
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
    Box::Position pos = theBox.position(x, y);

    position |= pos;

    if (pos == Box::Outside)
    {
      // Skip points as fast as possible until something has to be checked
      // in more detail.

      ++i;  // we already know it is outside

      if (x < theBox.xmin())
        while (i < n && g.getX(i) < theBox.xmin())
          ++i;

      else if (x > theBox.xmax())
        while (i < n && g.getX(i) > theBox.xmax())
          ++i;

      else if (y < theBox.ymin())
        while (i < n && g.getY(i) < theBox.ymin())
          ++i;

      else if (y > theBox.ymax())
        while (i < n && g.getY(i) > theBox.ymax())
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
            theRect.addExterior(line);
          else
            theRect.addInterior(line);
        }
        return position;
      }

      // Establish new position

      x = g.getX(i);
      y = g.getY(i);
      pos = theBox.position(x, y);

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

      clip_to_edges(x0, y0, x, y, theBox);

      if (pos == Box::Inside)  // out-in
      {
        auto *line = new OGRLineString();

        if (add_start)
          line->addPoint(start_x, start_y);
        add_start = false;
        line->addSubLineString(&g, start_index, i - 1);
        line->addPoint(x0, y0);
        if (exterior)
          theRect.addExterior(line);
        else
          theRect.addInterior(line);
        // Main loop will enter the Inside/Edge section
      }
      else if (pos == Box::Outside)  // out-out
      {
        // Clip the other end too
        clip_to_edges(x, y, x0, y0, theBox);

        Box::Position prev_pos = theBox.position(x0, y0);
        pos = theBox.position(x, y);
        position |= pos;

        if (different(x0, y0, x, y) &&  // discard corners etc
            Box::onEdge(prev_pos))      // discard if does not intersect rect
        {
          // Intersection occurred. Must generate a linestring from start to intersection point
          // then.
          auto *line = new OGRLineString();

          if (add_start)
            line->addPoint(start_x, start_y);
          line->addSubLineString(&g, start_index, i - 1);
          line->addPoint(x0, y0);
          if (exterior)
            theRect.addExterior(line);
          else
            theRect.addInterior(line);

          position |= Box::Inside;  // mark we visited inside

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
          position |= Box::Inside;
        if (exterior)
          theRect.addExterior(line);
        else
          theRect.addInterior(line);

        add_start = false;
      }
    }

    else if (pos == Box::Inside)
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
        pos = theBox.position(x, y);

        position |= pos;

        if (pos != Box::Outside)
          continue;

        // Clip the outside point to edges
        clip_to_edges(x, y, g.getX(i - 1), g.getY(i - 1), theBox);
        pos = theBox.position(x, y);

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
        pos = theBox.position(x, y);

        position |= pos;

        if (pos == Box::Inside)
          continue;
        if (pos != Box::Outside)  // on edge
        {
          if (!Box::onSameEdge(prev_pos, pos))
            position |= Box::Inside;  // visited inside
          continue;
        }

        // Clip the outside point to edges
        clip_to_edges(x, y, g.getX(i - 1), g.getY(i - 1), theBox);

        if (x != g.getX(i) || y != g.getY(i))
          position |= Box::Inside;

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
      theRect.addExterior(line);
    else
      theRect.addInterior(line);
  }

  return position;
}

// ----------------------------------------------------------------------
/*!
 * \brief Clip or cut with rectangle
 */
// ----------------------------------------------------------------------

int do_rect(const OGRLineString *theGeom,
            RectClipper &theRect,
            const Box &theBox,
            bool keep_inside,
            bool exterior)
{
  if (keep_inside)
    return clip_rect(theGeom, theRect, theBox, exterior);
  return cut_rect(theGeom, theRect, theBox, exterior);
}

// ----------------------------------------------------------------------
/*!
 * \brief Clip polygon, do not close clipped ones
 */
// ----------------------------------------------------------------------

void do_polygon_to_linestrings(const OGRPolygon *theGeom,
                               GeometryBuilder &theBuilder,
                               const Box &theBox,
                               bool keep_inside)
{
  if (theGeom == nullptr || theGeom->IsEmpty() != 0)
    return;

  // Clip the exterior first to see what's going on

  RectClipper rect(theBox, keep_inside);

  bool all_exterior = true;

  // If everything was in, just clone the original

  auto position = do_rect(theGeom->getExteriorRing(), rect, theBox, keep_inside, all_exterior);

  if (all_only_inside(position))
  {
    // CLIP: - if all vertices inside box or on the edges, return input as is
    // CUT:  - if all vertices inside box or on the edges, return empty result
    if (keep_inside)
      theBuilder.add(dynamic_cast<OGRPolygon *>(theGeom->clone()));
    return;
  }

  if (all_not_inside(position))
  {
    // CLIP:
    // -if box inside exterior, omit exterior
    // -if box outside exterior, return empty result
    // CUT:
    // - if box inside exterior, keep exterior and continue
    // - if box outside exterior, return input as is

    bool box_inside = box_inside_ring(theBox, *theGeom->getExteriorRing());

    if (keep_inside)
    {
      if (!box_inside)
        return;
    }
    else
    {
      if (!box_inside)
      {
        theBuilder.add(dynamic_cast<OGRPolygon *>(theGeom->clone()));
        return;
      }
      else
      {
        rect.addExterior(dynamic_cast<OGRLinearRing *>(theGeom->getExteriorRing()->clone()));
      }
    }
  }

  // Clipping:
  // Handle the holes now:
  // - Clipped ones become linestrings
  // - Intact ones become new polygons without holes

  // Cutting:
  // Handle the holes now:
  // - Clipped ones become linestrings
  // - Intact ones become new polygons without holes

  // Note that we add the holes as exterior parts

  for (int i = 0, n = theGeom->getNumInteriorRings(); i < n; ++i)
  {
    auto *hole = theGeom->getInteriorRing(i);
    auto holeposition = do_rect(hole, rect, theBox, keep_inside, all_exterior);

    if (all_only_inside(holeposition))
    {
      if (keep_inside)
        rect.addExterior(dynamic_cast<OGRLinearRing *>(hole->clone()));
    }
    else if (all_not_inside(holeposition))
    {
      bool box_inside_hole = box_inside_ring(theBox, *hole);

      if (box_inside_hole)
      {
        // If the box clip is inside a hole the result is empty
        // Otherwise we can keep the original input
        if (!keep_inside)
          theBuilder.add(dynamic_cast<OGRPolygon *>(theGeom->clone()));
        return;
      }

      // Otherwise the hole is outside the box
      if (!keep_inside)
        rect.addExterior(dynamic_cast<OGRLinearRing *>(hole->clone()));
    }
  }

  rect.reconnect();
  rect.reconnectWithoutBox();
  rect.release(theBuilder);
}

// ----------------------------------------------------------------------
/*!
 * \brief Clip polygon, close clipped ones
 *
 * Note: If we enforce exterior to be CW and holes to be CCW
 *
 *   - clipped exterior is forced to be CW
 *   - clipped holes are forced to be CW
 *   - untouched holes are forced to be CCW, unless discarded for being outside
 */
// ----------------------------------------------------------------------

void do_polygon_to_polygons(const OGRPolygon *theGeom,
                            GeometryBuilder &theBuilder,
                            const Box &theBox,
                            double max_length,
                            bool keep_inside)
{
  if (theGeom == nullptr || theGeom->IsEmpty() != 0)
    return;

  // Clip the exterior first to see what's going on

  RectClipper rect(theBox, keep_inside);

  auto position = do_rect(theGeom->getExteriorRing(), rect, theBox, keep_inside, true);

  if (all_not_outside(position))
  {
    // CLIP: - if all vertices inside box or on the edges, return input as is
    // CUT:  - if all vertices inside box or on the edges, return empty result

    if (keep_inside)
    {
      auto *poly = dynamic_cast<OGRPolygon *>(theGeom->clone());
      OGR::normalize(*poly);
      theBuilder.add(poly);
    }
    return;
  }

  if (all_not_inside(position))
  {
    // CLIP:
    // -if box inside exterior, box becomes part of new exterior ; ADD_BOX
    // -if if box outside exterior, return empty result
    // CUT:
    // - if box inside exterior, box becomes part of new holes ; ADD_BOX
    // - if box outside exterior, return input as is

    bool box_inside = box_inside_ring(theBox, *theGeom->getExteriorRing());

    if (keep_inside)
    {
      if (!box_inside)
        return;
      rect.addBox();
    }
    else
    {
      if (!box_inside)
      {
        theBuilder.add(dynamic_cast<OGRPolygon *>(theGeom->clone()));
        return;
      }
      // box is inside exterior, must keep exterior
      rect.addExterior(dynamic_cast<OGRLinearRing *>(theGeom->getExteriorRing()->clone()));
      rect.addBox();
    }
  }

  // Clipping:
  // If the exterior does not intersect the bounding box at all, it must
  // be surrounding the bbox. The exterior is thus shrunk into the bbox.
  // Any hole in it remains a hole, except when it intersects the bbox, in
  // which case it becomes part of the exterior.

  // Cutting: If the exterior does not intersect the bounding box at
  // all, it must be surrounding the bbox. The exterior is thus kept
  // as it is. The bbox becomes a hole. Any hole intersecting the hole
  // is unioned with it.

  // Force exterior to be clockwise
  // if (!ext_clockwise) rect.reverseLines();
  // Must do this to make sure all end points are on the edges
  // rect.reconnect();

  // Clipping:
  // Handle the holes now:
  // - Clipped ones become part of the exterior
  // - Intact ones become holes in new polygons formed by exterior rect
  //   if they are completely inside the clipping box, otherwise they
  //   must be completely outside the clipping box
  // However, if we find a hole surrounding the clipping box, there is
  // no output at all.

  // Cutting:
  // Handle the holes now:
  // - Clipped ones are merged with the bbox hole
  // - Intact ones are retained as is
  // - If there are no merges, bbox becomes a new hole

  for (int i = 0, n = theGeom->getNumInteriorRings(); i < n; ++i)
  {
    auto *hole = theGeom->getInteriorRing(i);

    auto holeposition = do_rect(hole, rect, theBox, keep_inside, false);

    if (all_only_inside(holeposition))
    {
      if (keep_inside)
        rect.addInterior(dynamic_cast<OGRLinearRing *>(hole->clone()));
    }
    else if (all_not_inside(holeposition))
    {
      bool box_inside_hole = box_inside_ring(theBox, *hole);

      if (box_inside_hole)
      {
        // If the box clip is inside a hole the result is empty
        // Otherwise we can keep the original input
        if (!keep_inside)
          theBuilder.add(dynamic_cast<OGRPolygon *>(theGeom->clone()));
        return;
      }

      // Otherwise the hole is outside the box
      if (!keep_inside)
        rect.addInterior(dynamic_cast<OGRLinearRing *>(hole->clone()));
    }
  }

  rect.reconnect();                   // trivial reconnect of endpoints
  rect.reconnectWithBox(max_length);  // reconnect along box boundaries
  rect.release(theBuilder);
}

// ----------------------------------------------------------------------
/*!
 * \brief Clip OGR geometry
 */
// ----------------------------------------------------------------------

void do_polygon(const OGRPolygon *theGeom,
                GeometryBuilder &theBuilder,
                const Box &theBox,
                double max_length,
                bool keep_polygons,
                bool keep_inside)
{
  if (keep_polygons)
    do_polygon_to_polygons(theGeom, theBuilder, theBox, max_length, keep_inside);
  else
    do_polygon_to_linestrings(theGeom, theBuilder, theBox, keep_inside);
}

// ----------------------------------------------------------------------
/*!
 * \brief Clip OGR geometry
 */
// ----------------------------------------------------------------------

void do_linestring(const OGRLineString *theGeom,
                   GeometryBuilder &theBuilder,
                   const Box &theBox,
                   bool keep_inside)
{
  if (theGeom == nullptr || theGeom->IsEmpty() != 0)
    return;

  // If everything was in, just clone the original when clipping

  RectClipper rect(theBox, keep_inside);
  auto position = do_rect(theGeom, rect, theBox, keep_inside, true);

  if (all_only_inside(position))
  {
    if (keep_inside)
      theBuilder.add(dynamic_cast<OGRLineString *>(theGeom->clone()));
  }
  else if (all_only_outside(position))
  {
    if (!keep_inside)
      theBuilder.add(dynamic_cast<OGRLineString *>(theGeom->clone()));
  }
  else
  {
    rect.reconnect();
    rect.reconnectWithoutBox();
    rect.release(theBuilder);
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Clip OGR geometry
 */
// ----------------------------------------------------------------------

void do_multipoint(const OGRMultiPoint *theGeom,
                   GeometryBuilder &theBuilder,
                   const Box &theBox,
                   bool keep_inside)
{
  if (theGeom == nullptr || theGeom->IsEmpty() != 0)
    return;

  for (int i = 0, n = theGeom->getNumGeometries(); i < n; ++i)
  {
    do_point(dynamic_cast<const OGRPoint *>(theGeom->getGeometryRef(i)),
             theBuilder,
             theBox,
             keep_inside);
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Clip OGR geometry
 */
// ----------------------------------------------------------------------

void do_multilinestring(const OGRMultiLineString *theGeom,
                        GeometryBuilder &theBuilder,
                        const Box &theBox,
                        bool keep_inside)
{
  if (theGeom == nullptr || theGeom->IsEmpty() != 0)
    return;

  for (int i = 0, n = theGeom->getNumGeometries(); i < n; ++i)
  {
    do_linestring(dynamic_cast<const OGRLineString *>(theGeom->getGeometryRef(i)),
                  theBuilder,
                  theBox,
                  keep_inside);
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Clip OGR geometry
 */
// ----------------------------------------------------------------------

void do_multipolygon(const OGRMultiPolygon *theGeom,
                     GeometryBuilder &theBuilder,
                     const Box &theBox,
                     double max_length,
                     bool keep_polygons,
                     bool keep_inside)
{
  if (theGeom == nullptr || theGeom->IsEmpty() != 0)
    return;

  for (int i = 0, n = theGeom->getNumGeometries(); i < n; ++i)
  {
    do_polygon(dynamic_cast<const OGRPolygon *>(theGeom->getGeometryRef(i)),
               theBuilder,
               theBox,
               max_length,
               keep_polygons,
               keep_inside);
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Clip OGR geometry
 */
// ----------------------------------------------------------------------

// Needed since two functions call each other

void do_geom(const OGRGeometry *theGeom,
             GeometryBuilder &theBuilder,
             const Box &theBox,
             double max_length,
             bool keep_polygons,
             bool keep_inside);

void do_geometrycollection(const OGRGeometryCollection *theGeom,
                           GeometryBuilder &theBuilder,
                           const Box &theBox,
                           double max_length,
                           bool keep_polygons,
                           bool keep_inside)
{
  if (theGeom == nullptr || theGeom->IsEmpty() != 0)
    return;

  for (int i = 0, n = theGeom->getNumGeometries(); i < n; ++i)
  {
    do_geom(theGeom->getGeometryRef(i), theBuilder, theBox, max_length, keep_polygons, keep_inside);
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Clip OGR geometry to output geometry
 */
// ----------------------------------------------------------------------

void do_geom(const OGRGeometry *theGeom,
             GeometryBuilder &theBuilder,
             const Box &theBox,
             double max_length,
             bool keep_polygons,
             bool keep_inside)
{
  OGRwkbGeometryType id = theGeom->getGeometryType();

  switch (id)
  {
    case wkbPoint:
      return do_point(dynamic_cast<const OGRPoint *>(theGeom), theBuilder, theBox, keep_inside);
    case wkbLineString:
      return do_linestring(
          dynamic_cast<const OGRLineString *>(theGeom), theBuilder, theBox, keep_inside);
    case wkbPolygon:
      return do_polygon(dynamic_cast<const OGRPolygon *>(theGeom),
                        theBuilder,
                        theBox,
                        max_length,
                        keep_polygons,
                        keep_inside);
    case wkbMultiPoint:
      return do_multipoint(
          dynamic_cast<const OGRMultiPoint *>(theGeom), theBuilder, theBox, keep_inside);
    case wkbMultiLineString:
      return do_multilinestring(
          dynamic_cast<const OGRMultiLineString *>(theGeom), theBuilder, theBox, keep_inside);
    case wkbMultiPolygon:
      return do_multipolygon(dynamic_cast<const OGRMultiPolygon *>(theGeom),
                             theBuilder,
                             theBox,
                             max_length,
                             keep_polygons,
                             keep_inside);
    case wkbGeometryCollection:
      return do_geometrycollection(dynamic_cast<const OGRGeometryCollection *>(theGeom),
                                   theBuilder,
                                   theBox,
                                   max_length,
                                   keep_polygons,
                                   keep_inside);
    case wkbLinearRing:
      throw std::runtime_error("Direct clipping of LinearRings is not supported");
    default:
      throw std::runtime_error("Encountered an unknown geometry component when clipping polygons");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Clip a geometry so that polygons may not be preserved
 *
 * Any polygon which intersects the rectangle will be converted to
 * a polyline or a multipolyline - including the holes.
 *
 * \return Empty GeometryCollection if the result is empty
 */
// ----------------------------------------------------------------------

OGRGeometry *OGR::lineclip(const OGRGeometry &theGeom, const Box &theBox)
{
  bool keep_polygons = false;
  bool keep_inside = true;

  GeometryBuilder builder;
  do_geom(&theGeom, builder, theBox, 0, keep_polygons, keep_inside);

  OGRGeometry *geom = builder.build();
  if (geom != nullptr)
    geom->assignSpatialReference(theGeom.getSpatialReference());
  return geom;
}

// ----------------------------------------------------------------------
/*!
 * \brief Clip a geometry so that polygons are preserved
 *
 * \return Empty GeometryCollection if the result is empty
 */
// ----------------------------------------------------------------------

OGRGeometry *OGR::polyclip(const OGRGeometry &theGeom,
                           const Box &theBox,
                           double theMaximumSegmentLength)
{
  bool keep_polygons = true;
  bool keep_inside = true;

  GeometryBuilder builder;
  do_geom(&theGeom, builder, theBox, theMaximumSegmentLength, keep_polygons, keep_inside);

  OGRGeometry *geom = builder.build();

  if (geom != nullptr)
    geom->assignSpatialReference(theGeom.getSpatialReference());

  return geom;
}

// ----------------------------------------------------------------------
/*!
 * \brief Cut a geometry so that polygons may not be preserved
 *
 * Any polygon which intersects the rectangle will be converted to
 * a polyline or a multipolyline - including the holes.
 *
 * \return Empty GeometryCollection if the result is empty
 */
// ----------------------------------------------------------------------

OGRGeometry *OGR::linecut(const OGRGeometry &theGeom, const Box &theBox)
{
  bool keep_polygons = false;
  bool keep_inside = false;

  GeometryBuilder builder;
  do_geom(&theGeom, builder, theBox, 0, keep_polygons, keep_inside);

  OGRGeometry *geom = builder.build();
  if (geom != nullptr)
    geom->assignSpatialReference(theGeom.getSpatialReference());
  return geom;
}

// ----------------------------------------------------------------------
/*!
 * \brief Cut a geometry so that polygons are preserved
 *
 * \return Empty GeometryCollection if the result is empty
 */
// ----------------------------------------------------------------------

OGRGeometry *OGR::polycut(const OGRGeometry &theGeom,
                          const Box &theBox,
                          double theMaximumSegmentLength)
{
  bool keep_polygons = true;
  bool keep_inside = false;

  GeometryBuilder builder;
  do_geom(&theGeom, builder, theBox, theMaximumSegmentLength, keep_polygons, keep_inside);

  OGRGeometry *geom = builder.build();
  if (geom != nullptr)
    geom->assignSpatialReference(theGeom.getSpatialReference());

  return geom;
}

}  // namespace Fmi
