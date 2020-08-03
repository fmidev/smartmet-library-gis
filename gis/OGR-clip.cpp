/*

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
       - if box inside exterior, box becomes part of new holes ; ADD_BOX
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
#include "ClipParts.h"
#include "OGR.h"
#include <iomanip>
#include <iostream>
#include <list>
#include <stdexcept>

using Fmi::Box;
using Fmi::ClipParts;

// ----------------------------------------------------------------------
/*!
 * \brief Test if all positions were inside or on the edge if the box
 */
// ----------------------------------------------------------------------

bool all_inside(int position)
{
  return (((position & Box::Inside) != 0) && ((position & Box::Outside) == 0));
}

// ----------------------------------------------------------------------
/*!
 * \brief Test if all positions were outside or on the edge of the box
 */
// ----------------------------------------------------------------------

bool all_outside(int position)
{
  return (((position & Box::Outside) != 0) && ((position & Box::Inside) == 0));
}

// ----------------------------------------------------------------------
/*!
 * \brief Test if the box center is inside the given ring
 */
// ----------------------------------------------------------------------

bool box_inside_ring(const Box &theBox, const OGRLinearRing &theRing)
{
  auto x = 0.5 * (theBox.xmin() + theBox.xmax());
  auto y = 0.5 * (theBox.ymin() + theBox.ymax());
  return inside(theRing, 0.5 * theGeom->getExteriorRing(), x, y);
}

// ----------------------------------------------------------------------
/*!
 * \brief Utility function to make code more readable
 */
// ----------------------------------------------------------------------

bool different(double x1, double y1, double x2, double y2) { return !(x1 == x2 && y1 == y2); }
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

void clip_point(const OGRPoint *theGeom, ClipParts &theParts, const Box &theBox, bool keep_inside)
{
  if (theGeom == nullptr) return;

  double x = theGeom->getX();
  double y = theGeom->getY();

  auto pos = theBox.position(x, y);
  if (keep_inside)
  {
    if (pos == Box::Inside) theParts.add(new OGRPoint(x, y));
  }
  else
  {
    if (pos == Box::Outside) theParts.add(new OGRPoint(x, y));
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Clip OGR geometry.
 *
 * Returns OR'ed position for all vertices, from which one can quickly tell if all points
 * were either inside or outside.
 */
// ----------------------------------------------------------------------

int clip_linestring_parts(const OGRLineString *theGeom, ClipParts &theParts, const Box &theBox)
{
  int position = 0;

  int n = theGeom->getNumPoints();

  if (theGeom == nullptr || n < 1) return position;

  // For shorthand code

  const OGRLineString &g = *theGeom;

  // Keep a record of the point where a line segment entered
  // the rectangle. If the boolean is set, we must insert
  // the point to the beginning of the linestring which then
  // continues inside the rectangle.

  double x0 = 0;
  double y0 = 0;
  bool add_start = false;

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

      if (i >= n) return position;

      // Establish new position

      x = g.getX(i);
      y = g.getY(i);
      pos = theBox.position(x, y);

      position |= pos;

      // Handle all possible cases

      x0 = g.getX(i - 1);
      y0 = g.getY(i - 1);

      clip_to_edges(x0, y0, x, y, theBox);

      if (pos == Box::Inside)
      {
        add_start = true;  // x0,y0 must have clipped the rectangle
        // Main loop will enter the Inside/Edge section
      }
      else if (pos == Box::Outside)
      {
        // From Outside to Outside. We need to check whether
        // we created a line segment inside the box. In any
        // case, we will continue the main loop after this
        // which will then enter the Outside section.

        // Clip the other end too
        clip_to_edges(x, y, x0, y0, theBox);

        Box::Position prev_pos = theBox.position(x0, y0);
        pos = theBox.position(x, y);

        if (different(x0, y0, x, y) &&  // discard corners etc
            Box::onEdge(prev_pos) &&    // discard if does not intersect rect
            Box::onEdge(pos) && !Box::onSameEdge(prev_pos, pos)  // discard if travels along edge
        )
        {
          auto *line = new OGRLineString;
          line->addPoint(x0, y0);
          line->addPoint(x, y);
          theParts.add(line);
        }

        // Continue main loop outside the rect
      }
      else
      {
        // From outside to edge. If the edge we hit first when
        // following the line is not the edge we end at, then
        // clearly we must go through the rectangle and hence
        // a start point must be set.

        Box::Position newpos = theBox.position(x0, y0);
        if (!Box::onSameEdge(pos, newpos))
        {
          add_start = true;
        }
        else
        {
          // we ignore the travel along the edge and continue
          // the main loop at the last edge point
        }
      }
    }

    else
    {
      // The point is now stricly inside or on the edge.
      // Keep iterating until the end or the point goes
      // outside. We may have to output partial linestrings
      // while iterating until we go strictly outside

      int start_index = i;  // 1st valid original point
      bool go_outside = false;

      while (!go_outside && ++i < n)
      {
        x = g.getX(i);
        y = g.getY(i);

        Box::Position prev_pos = pos;
        pos = theBox.position(x, y);

        position |= pos;

        if (pos == Box::Inside)
        {
          // Just keep going
        }
        else if (pos == Box::Outside)
        {
          go_outside = true;

          // Clip the outside point to edges
          clip_to_edges(x, y, g.getX(i - 1), g.getY(i - 1), theBox);
          pos = theBox.position(x, y);

          // Does the line exit through the inside of the box?

          bool through_box =
              (different(x, y, g.getX(i), g.getY(i)) && !Box::onSameEdge(prev_pos, pos));

          // Output a LineString if it at least one segment long

          if (start_index < i - 1 || add_start || through_box)
          {
            auto *line = new OGRLineString();
            if (add_start)
            {
              line->addPoint(x0, y0);
              add_start = false;
            }
            line->addSubLineString(&g, start_index, i - 1);

            if (through_box) line->addPoint(x, y);

            theParts.add(line);
          }
          // And continue main loop on the outside
        }
        else
        {
          // on same edge?
          if (Box::onSameEdge(prev_pos, pos))
          {
            // Nothing to output if we haven't been elsewhere
            if (start_index < i - 1 || add_start)
            {
              auto *line = new OGRLineString();
              if (add_start)
              {
                line->addPoint(x0, y0);
                add_start = false;
              }
              line->addSubLineString(&g, start_index, i - 1);
              theParts.add(line);
            }
            start_index = i;
          }
          else
          {
            // On different edge. Must have gone through the box
            // then - keep collecting points that generate inside
            // line segments
          }
        }
      }

      // Was everything in? If so, generate no output but return true in this case only
      if (start_index == 0 && i >= n) return position;

      // Flush the last line segment if data ended and there is something to flush

      if (!go_outside &&                       // meaning data ended
          (start_index < i - 1 || add_start))  // meaning something has to be generated
      {
        auto *line = new OGRLineString();
        if (add_start)
        {
          line->addPoint(x0, y0);
          add_start = false;
        }
        line->addSubLineString(&g, start_index, i - 1);

        theParts.add(line);
      }
    }
  }

  return position;
}

// ----------------------------------------------------------------------
/*!
 * \brief Cut OGR geometry.
 *
 */
// ----------------------------------------------------------------------

int cut_linestring_parts(const OGRLineString *theGeom, ClipParts &theParts, const Box &theBox)
{
  int position = 0;

  int n = theGeom->getNumPoints();

  if (theGeom == nullptr || n < 1) return position;

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

    std::cout << "Enter\t" << i << "\t" << x << "," << y << "\t" << static_cast<int>(pos) << "\n";

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
        std::cout << "Terminating\nadd_start = " << add_start << "\nstart_x=" << start_x
                  << "\nstart_index=" << start_index << "\nstart_y =" << start_y << "\nn=" << n
                  << "\n";
        if (start_index == 0) return false;  // everything was out

        if (start_index > 0 && start_index < n)
        {
          auto *line = new OGRLineString();
          if (add_start) line->addPoint(start_x, start_y);
          line->addSubLineString(&g, start_index, n - 1);
          std::cout << "Adding line from " << start_index << " to " << n - 1 << "\n";
          std::cout << "\t = " << Fmi::OGR::exportToWkt(*line) << "\n";
          theParts.add(line);
        }
        return false;
      }

      // Establish new position

      x = g.getX(i);
      y = g.getY(i);
      pos = theBox.position(x, y);

      position |= pos;

      std::cout << "Out:\t" << i << "\t" << x << "," << y << "\t" << static_cast<int>(pos) << "\n";

      // Handle all possible cases

      x0 = g.getX(i - 1);
      y0 = g.getY(i - 1);

      clip_to_edges(x0, y0, x, y, theBox);

      if (pos == Box::Inside)
      {
        auto *line = new OGRLineString();

        if (add_start) line->addPoint(start_x, start_y);
        add_start = false;
        line->addSubLineString(&g, start_index, i - 1);
        line->addPoint(x0, y0);
        std::cout << "\t + " << Fmi::OGR::exportToWkt(*line) << "\n";
        theParts.add(line);
        // Main loop will enter the Inside/Edge section
      }
      else if (pos == Box::Outside)
      {
        // From Outside to Outside. We need to check whether
        // the line segment crossed the box. In any
        // case, we will continue the main loop after this
        // which will then enter the Outside section again.

        // Clip the other end too
        clip_to_edges(x, y, x0, y0, theBox);

        Box::Position prev_pos = theBox.position(x0, y0);
        pos = theBox.position(x, y);
        position |= pos;

#if 0        
        if (different(x0, y0, x, y) &&  // discard corners etc
            Box::onEdge(prev_pos) &&    // discard if does not intersect rect
            Box::onEdge(pos) && !Box::onSameEdge(prev_pos, pos))  // discard if travels along edge
#else
        if (different(x0, y0, x, y) &&  // discard corners etc
            Box::onEdge(prev_pos))      // discard if does not intersect rect
#endif
        {
          std::cout << "Intersected out-out!\n";

          // Intersection occurred. Must generate a linestring from start to intersection point
          // then.
          auto *line = new OGRLineString();

          if (add_start) line->addPoint(start_x, start_y);
          line->addSubLineString(&g, start_index, i - 1);
          line->addPoint(x0, y0);
          std::cout << "\t ++ " << Fmi::OGR::exportToWkt(*line) << "\n";
          theParts.add(line);

          // Start next line from the end intersection point
          start_index = i;
          start_x = x;
          start_y = y;
          add_start = true;
          std::cout << "\t" << start_index << "\t" << start_x << "\t" << start_y << "\t" << x0
                    << "\t" << y0 << "\n";
        }

        // Continue main loop outside the rect
      }
      else
      {
        auto *line = new OGRLineString();
        if (add_start) line->addPoint(start_x, start_y);
        line->addSubLineString(&g, start_index, i - 1);
        line->addPoint(x0, y0);
        std::cout << "\t +++ " << Fmi::OGR::exportToWkt(*line) << "\n";
        theParts.add(line);

        add_start = false;
      }
    }

    else
    {
      // The point is now stricly inside or on the edge.
      // Keep iterating until the end or the point goes
      // outside.

      while (++i < n)
      {
        x = g.getX(i);
        y = g.getY(i);
        pos = theBox.position(x, y);

        position |= pos;

        std::cout << "In:\t" << i << "\t" << x << "," << y << "\t" << static_cast<int>(pos) << "\n";
        if (pos == Box::Inside) continue;
        if (pos != Box::Outside)
          continue;  // on same edge or from edge to another edge inside the box

        std::cout << "Going out\n";

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
      if (start_index == 0 && i >= n) return position;
    }
  }

  if (add_start)
  {
    auto *line = new OGRLineString();

    line->addPoint(start_x, start_y);
    line->addSubLineString(&g, start_index, n - 1);
    theParts.add(line);
  }

  theParts.reconnect();

  return position;
}

// ----------------------------------------------------------------------
/*!
 * \brief Clip or cut with rectangle
 */
// ----------------------------------------------------------------------

int handle_linestring_parts(const OGRLineString *theGeom,
                            ClipParts &theParts,
                            const Box &theBox,
                            bool keep_inside)
{
  if (keep_inside) return clip_linestring_parts(theGeom, theParts, theBox);
  return cut_linestring_parts(theGeom, theParts, theBox);
}

// ----------------------------------------------------------------------
/*!
 * \brief Clip polygon, do not close clipped ones
 */
// ----------------------------------------------------------------------

void clip_polygon_to_linestrings(const OGRPolygon *theGeom,
                                 Fmi::ClipParts &theParts,
                                 const Box &theBox,
                                 bool keep_inside)
{
  if (theGeom == nullptr || theGeom->IsEmpty() != 0) return;

  // Clip the exterior first to see what's going on

  Fmi::ClipParts parts(keep_inside);

  // If everything was in, just clone the original

  auto position = handle_linestring_parts(theGeom->getExteriorRing(), parts, theBox, keep_inside);

  if (all_inside(position))
  {
    if (keep_inside)
    {
      theParts.add(dynamic_cast<OGRPolygon *>(theGeom->clone()));
      return;
    }

    // Keep only the exterior for now since cutting away a rectangle may remove holes
    auto *poly = new OGRPolygon;
    auto *ring = const_cast<OGRLinearRing *>(theGeom->getExteriorRing());
    poly->addRing(ring);
    parts.add(poly);
  }

  // Now, if parts is empty, our rectangle may be inside the polygon
  // If not, holes are outside too

  if (parts.empty())
  {
    std::cout << __LINE__ << std::endl;
    // We could now check whether the rectangle is inside the outer
    // ring to avoid checking the holes. However, if holes are much
    // smaller than the exterior ring just checking the holes
    // separately could be faster.

    if (keep_inside)
      if (theGeom->getNumInteriorRings() == 0) return;

#if 0
    if(!inside((xmin+xmax)/2,(ymin+ymax)/2,theGeom->getExteriorRing()))
      return;
#endif
    // exterior surrounds the rectangle, holes must be
    // checked separately. There is no harm in omitting
    // the above check, it is for speed only.
  }
  else
  {
    // The exterior must have been clipped into linestrings.
    // Move them to the actual parts collector, clearing parts.

    parts.reconnect();
    parts.release(theParts);
  }

  std::cout << "Handling holes\n";

  // Clipping:
  // Handle the holes now:
  // - Clipped ones become linestrings
  // - Intact ones become new polygons without holes

  // Cutting:
  // Handle the holes now:
  // - Clipped ones become linestrings
  // - Intact ones become new polygons without holes

  theParts.dump();

  for (int i = 0, n = theGeom->getNumInteriorRings(); i < n; ++i)
  {
    auto *hole = theGeom->getInteriorRing(i);
    if (handle_linestring_parts(hole, parts, theBox, keep_inside))
    {
      std::cout << "Case 1\n";
      // becomes new exterior
      auto *poly = new OGRPolygon;
      poly->addRing(const_cast<OGRLinearRing *>(hole));  // clones
      theParts.add(poly);
    }
    else if (!parts.empty())
    {
      std::cout << "Case 2\n";
      parts.reconnect();
      parts.release(theParts);
    }
    else
    {
      std::cout << "Case 3\n";
    }
  }

  parts.reconnectPolygons(theBox, false);

  theParts.dump();
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

bool inside(const OGRLinearRing *theGeom, const Fmi::Box &theBox)
{
  int n = theGeom->getNumPoints();

  if (theGeom == nullptr || n < 1) return false;

  for (int i = 0; i < n; ++i)
  {
    double x = theGeom->getX(i);
    double y = theGeom->getY(i);

    Box::Position pos = theBox.position(x, y);

    if (pos == Box::Outside) return false;
    if (pos == Box::Inside) return true;
  }

  // Indeterminate answer, should be impossible. We discard the hole
  // by saying it is not inside.

  return false;
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

void clip_polygon_to_polygons(const OGRPolygon *theGeom,
                              Fmi::ClipParts &theParts,
                              const Box &theBox,
                              bool keep_inside)
{
  if (theGeom == nullptr || theGeom->IsEmpty() != 0) return;

  // Clip the exterior first to see what's going on

  Fmi::ClipParts parts(keep_inside);

  auto position = handle_linestring_parts(theGeom->getExteriorRing(), parts, theBox, keep_inside);

  std::cout << "position in = " << position << "\n";
  if (all_inside(position))
  {
    // CLIP: - if all vertices inside box or on the edges, return input as is
    // CUT:  - if all vertices inside box or on the edges, return empty result
    if (keep_inside) theParts.add(dynamic_cast<OGRPolygon *>(theGeom->clone()));
    return;
  }

  if (all_outside(position))
  {
    // CLIP:
    // -if box inside exterior, box becomes part of new exterior ; ADD_BOX
    // -if if box outside exterior, return empty result
    // CUT:
    // - if box inside exterior, box becomes part of new holes ; ADD_BOX
    // - if box outside exterior, return input as is

    bool box_inside = box_inside_ring(*theGeom.getExteriorRing, theBox);

    if (keep_inside)
    {
    }
    else
    {
    }
  }

  // Establish whether the exterior was intersected
  bool exterior_intersected = !parts.empty();

  // If there were no intersections and the bbox is outside the exterior

  bool box_corner_inside_exterior =
      Fmi::OGR::inside(*theGeom->getExteriorRing(), theBox.xmin(), theBox.ymin());

  // If exterior was not intersected and the bbox corner is outside, then the result depends on
  // whether the bbox surrounds the exterior or not
  //
  //         surrounds      does not
  // CLIP    full input     empty         ==> !surrounds xor clipping
  // CUT      empty        full input

  if (!exterior_intersected && !box_corner_inside_exterior)
  {
    // if an exterior point is inside the bbox then the bbox surrounds the geometry
    bool surrounds = (Box::Inside == theBox.position(theGeom->getExteriorRing()->getX(0),
                                                     theGeom->getExteriorRing()->getY(0)));

    if (!surrounds ^ keep_inside) theParts.add(dynamic_cast<OGRPolygon *>(theGeom->clone()));
    return;
  }

  // Bbox becomes new hole if it its fully inside the exterior when cutting
  bool box_is_hole = false;

  if (!keep_inside && !exterior_intersected && box_corner_inside_exterior)
  {
    std::cout << "\tAdding exterior\n";
    auto *poly = new OGRPolygon;
    auto *ring = const_cast<OGRLinearRing *>(theGeom->getExteriorRing());
    poly->addRing(ring);
    parts.add(poly);

    // bbox is now known to be a hole
    box_is_hole = true;
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

  bool ext_clockwise = (theGeom->getExteriorRing()->isClockwise() == 1);

  // Force exterior to be clockwise
  if (!ext_clockwise) parts.reverseLines();

  // Must do this to make sure all end points are on the edges

  parts.reconnect();

  // Clipping:
  // Handle the holes now:
  // - Clipped ones become part of the exterior
  // - Intact ones become holes in new polygons formed by exterior parts
  //   if they are completely inside the clipping box, otherwise they
  //   must be completely outside the clipping box
  // However, if we find a hole surrounding the clipping box, there is
  // no output at all.

  // Cutting:
  // Handle the holes now:
  // - Clipped ones are merged with the bbox hole
  // - Intact ones are retained as is
  // - If there are no merges, bbox becomes a new hole

  int holes_clipped = 0;
  int holes_inside = 0;
  int holes_outside = 0;

  std::cout << "\tPROCESSING " << theGeom->getNumInteriorRings() << " HOLES\n";
  Fmi::ClipParts holeparts(keep_inside);

  for (int i = 0, n = theGeom->getNumInteriorRings(); i < n; ++i)
  {
    auto *hole = theGeom->getInteriorRing(i);
    bool hole_clockwise = (hole->isClockwise() == 1);

    bool hole_fully_in = handle_linestring_parts(hole, holeparts, theBox, keep_inside);

    if (hole_fully_in)
    {
      std::cout << "\tCase 1\n";
      ++holes_inside;
      if (keep_inside)
      {
        auto *poly = new OGRPolygon;

        // Force holes to be counter clockwise
        if (hole_clockwise)
        {
          auto *ring = dynamic_cast<OGRLinearRing *>(hole->clone());
          ring->reverseWindingOrder();
          poly->addRingDirectly(ring);  // transfers ownership
        }
        else
          poly->addRing(const_cast<OGRLinearRing *>(hole));  // clones
        holeparts.add(poly);
      }
    }
    else
    {
      if (!holeparts.empty())
      {
        std::cout << "\tCase 2\n";
        ++holes_clipped;
        // Clipped holes change orientation when connecting the edges, cut holes preserve
        // orientation
        if (keep_inside && hole_clockwise) holeparts.reverseLines();
      }
      else
      {
        std::cout << "Case 3\n";
        // No intersections with the outside hole, but we may be inside it!
        ++holes_outside;
        if (Fmi::OGR::inside(*theGeom->getInteriorRing(i), theBox.xmin(), theBox.ymin()))
        {
          // Box is completely inside the hole, the intersection must be empty
          if (keep_inside) return;

          // Cutting a hole from a hole does nothing
          box_is_hole = false;

          // NEW STUFF
          auto *poly = new OGRPolygon;

          // Force holes to be counter clockwise
          if (hole_clockwise)
          {
            auto *ring = dynamic_cast<OGRLinearRing *>(hole->clone());
            ring->reverseWindingOrder();
            poly->addRingDirectly(ring);  // transfers ownership
          }
          else
            poly->addRing(const_cast<OGRLinearRing *>(hole));  // clones
          holeparts.add(poly);
        }
      }
    }
  }

  // Add the bbox as a new hole if necessary

  holeparts.reconnect();
  holeparts.release(parts);

  bool add_bbox = false;

  if (keep_inside)
  {
    // Must now add the bbox as the new exterior if neither the exterior nor any
    // of the holes intersected the box, and the holes need an exterior.
    add_bbox = (!exterior_intersected && holes_clipped == 0);
  }
  else
  {
    // If we know the bbox is inside the exterior it should be a separate hole
    // unless it was merged with earlier holes

    if (box_is_hole && holes_clipped == 0) add_bbox = true;
  }

#if 1
  std::cout << "Rebuilding:\n"
            << "\tkeep_inside = " << keep_inside << "\n"
            << "\text_intersects = " << exterior_intersected << "\n"
            << "\text cw = " << ext_clockwise << "\n"
            << "\tbox_corner_inside = " << box_corner_inside_exterior << "\n"
            << "\tbox is hole = " << box_is_hole << "\n"
            << "\tholes inside = " << holes_inside << "\n"
            << "\tholes outside = " << holes_outside << "\n"
            << "\tholes clipped = " << holes_clipped << "\n"
            << "\tadd_bbox = " << add_bbox << "\n";
#endif

  parts.reconnectPolygons(theBox, add_bbox);
  parts.release(theParts);
}

// ----------------------------------------------------------------------
/*!
 * \brief Clip OGR geometry
 */
// ----------------------------------------------------------------------

void clip_polygon(const OGRPolygon *theGeom,
                  Fmi::ClipParts &theParts,
                  const Box &theBox,
                  bool keep_polygons,
                  bool keep_inside)
{
  if (keep_polygons)
    clip_polygon_to_polygons(theGeom, theParts, theBox, keep_inside);
  else
    clip_polygon_to_linestrings(theGeom, theParts, theBox, keep_inside);
}

// ----------------------------------------------------------------------
/*!
 * \brief Clip OGR geometry
 */
// ----------------------------------------------------------------------

void clip_linestring(const OGRLineString *theGeom,
                     Fmi::ClipParts &theParts,
                     const Box &theBox,
                     bool keep_inside)
{
  if (theGeom == nullptr || theGeom->IsEmpty() != 0) return;

  // If everything was in, just clone the original when clipping

  bool everything_in = handle_linestring_parts(theGeom, theParts, theBox, keep_inside);

  std::cout << "everything_in = " << everything_in << "\n";

  if (everything_in)
  {
    if (keep_inside) theParts.add(dynamic_cast<OGRLineString *>(theGeom->clone()));
  }
  else if (theParts.empty())
  {
    if (!keep_inside) theParts.add(dynamic_cast<OGRLineString *>(theGeom->clone()));
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Clip OGR geometry
 */
// ----------------------------------------------------------------------

void clip_multipoint(const OGRMultiPoint *theGeom,
                     ClipParts &theParts,
                     const Box &theBox,
                     bool keep_inside)
{
  if (theGeom == nullptr || theGeom->IsEmpty() != 0) return;
  for (int i = 0, n = theGeom->getNumGeometries(); i < n; ++i)
  {
    clip_point(
        dynamic_cast<const OGRPoint *>(theGeom->getGeometryRef(i)), theParts, theBox, keep_inside);
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Clip OGR geometry
 */
// ----------------------------------------------------------------------

void clip_multilinestring(const OGRMultiLineString *theGeom,
                          ClipParts &theParts,
                          const Box &theBox,
                          bool keep_inside)
{
  if (theGeom == nullptr || theGeom->IsEmpty() != 0) return;

  for (int i = 0, n = theGeom->getNumGeometries(); i < n; ++i)
  {
    clip_linestring(dynamic_cast<const OGRLineString *>(theGeom->getGeometryRef(i)),
                    theParts,
                    theBox,
                    keep_inside);
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Clip OGR geometry
 */
// ----------------------------------------------------------------------

void clip_multipolygon(const OGRMultiPolygon *theGeom,
                       Fmi::ClipParts &theParts,
                       const Box &theBox,
                       bool keep_polygons,
                       bool keep_inside)
{
  if (theGeom == nullptr || theGeom->IsEmpty() != 0) return;

  for (int i = 0, n = theGeom->getNumGeometries(); i < n; ++i)
  {
    clip_polygon(dynamic_cast<const OGRPolygon *>(theGeom->getGeometryRef(i)),
                 theParts,
                 theBox,
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

void clip_geom(const OGRGeometry *theGeom,
               ClipParts &theParts,
               const Box &theBox,
               bool keep_polygons,
               bool keep_inside);

void clip_geometrycollection(const OGRGeometryCollection *theGeom,
                             ClipParts &theParts,
                             const Box &theBox,
                             bool keep_polygons,
                             bool keep_inside)
{
  if (theGeom == nullptr || theGeom->IsEmpty() != 0) return;

  for (int i = 0, n = theGeom->getNumGeometries(); i < n; ++i)
  {
    clip_geom(theGeom->getGeometryRef(i), theParts, theBox, keep_polygons, keep_inside);
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Clip OGR geometry to output geometry
 */
// ----------------------------------------------------------------------

void clip_geom(const OGRGeometry *theGeom,
               ClipParts &theParts,
               const Box &theBox,
               bool keep_polygons,
               bool keep_inside)
{
  OGRwkbGeometryType id = theGeom->getGeometryType();

  switch (id)
  {
    case wkbPoint:
      return clip_point(dynamic_cast<const OGRPoint *>(theGeom), theParts, theBox, keep_inside);
    case wkbLineString:
      return clip_linestring(
          dynamic_cast<const OGRLineString *>(theGeom), theParts, theBox, keep_inside);
    case wkbPolygon:
      return clip_polygon(
          dynamic_cast<const OGRPolygon *>(theGeom), theParts, theBox, keep_polygons, keep_inside);
    case wkbMultiPoint:
      return clip_multipoint(
          dynamic_cast<const OGRMultiPoint *>(theGeom), theParts, theBox, keep_inside);
    case wkbMultiLineString:
      return clip_multilinestring(
          dynamic_cast<const OGRMultiLineString *>(theGeom), theParts, theBox, keep_inside);
    case wkbMultiPolygon:
      return clip_multipolygon(dynamic_cast<const OGRMultiPolygon *>(theGeom),
                               theParts,
                               theBox,
                               keep_polygons,
                               keep_inside);
    case wkbGeometryCollection:
      return clip_geometrycollection(dynamic_cast<const OGRGeometryCollection *>(theGeom),
                                     theParts,
                                     theBox,
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

OGRGeometry *Fmi::OGR::lineclip(const OGRGeometry &theGeom, const Box &theBox)
{
  bool keep_polygons = false;
  bool keep_inside = true;

  ClipParts parts(keep_inside);
  clip_geom(&theGeom, parts, theBox, keep_polygons, keep_inside);

  OGRGeometry *geom = parts.build();
  if (geom != nullptr) geom->assignSpatialReference(theGeom.getSpatialReference());
  return geom;
}

// ----------------------------------------------------------------------
/*!
 * \brief Clip a geometry so that polygons are preserved
 *
 * \return Empty GeometryCollection if the result is empty
 */
// ----------------------------------------------------------------------

OGRGeometry *Fmi::OGR::polyclip(const OGRGeometry &theGeom, const Box &theBox)
{
  bool keep_polygons = true;
  bool keep_inside = true;

  ClipParts parts(keep_inside);
  clip_geom(&theGeom, parts, theBox, keep_polygons, keep_inside);

  OGRGeometry *geom = parts.build();
  if (geom != nullptr) geom->assignSpatialReference(theGeom.getSpatialReference());

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

OGRGeometry *Fmi::OGR::linecut(const OGRGeometry &theGeom, const Box &theBox)
{
  bool keep_polygons = false;
  bool keep_inside = false;

  ClipParts parts(keep_inside);
  clip_geom(&theGeom, parts, theBox, keep_polygons, keep_inside);

  OGRGeometry *geom = parts.build();
  if (geom != nullptr) geom->assignSpatialReference(theGeom.getSpatialReference());
  return geom;
}

// ----------------------------------------------------------------------
/*!
 * \brief Cut a geometry so that polygons are preserved
 *
 * \return Empty GeometryCollection if the result is empty
 */
// ----------------------------------------------------------------------

OGRGeometry *Fmi::OGR::polycut(const OGRGeometry &theGeom, const Box &theBox)
{
  bool keep_polygons = true;
  bool keep_inside = false;

  ClipParts parts(keep_inside);
  clip_geom(&theGeom, parts, theBox, keep_polygons, keep_inside);

  OGRGeometry *geom = parts.build();
  if (geom != nullptr) geom->assignSpatialReference(theGeom.getSpatialReference());

  return geom;
}
