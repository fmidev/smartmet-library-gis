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
#include <macgyver/Exception.h>

namespace Fmi
{

namespace
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
  try
  {
    return (OGR::inside(theRing, theBox.xmin(), theBox.ymin()) &&
            OGR::inside(theRing, theBox.xmin(), theBox.ymax()) &&
            OGR::inside(theRing, theBox.xmax(), theBox.ymin()) &&
            OGR::inside(theRing, theBox.xmax(), theBox.ymax()));
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Utility function to make code more readable
 */
// ----------------------------------------------------------------------

bool different(double x1, double y1, double x2, double y2)
{
  return (x1 != x2 || y1 != y2);
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
  try
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
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
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
  try
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
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
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

#if 0
bool inside(const OGRLinearRing *theGeom, const Box &theBox)
{
  try
  {
    if (theGeom == nullptr)
      return false;

    int n = theGeom->getNumPoints();

    if (n < 1)
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
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}
#endif

// The state of iterating a linestring while clipping or cutting it with a rectangle
struct RectLineState
{
  RectLineState(const OGRLineString& theGeom, int theSize) : g(theGeom), n(theSize) {}

  const OGRLineString& g;  // For shorthand code
  const int n;
  int i = 0;
  int position = 0;
  Box::Position pos = Box::Inside;
  double x = 0;
  double y = 0;
  double x0 = 0;
  double y0 = 0;
  bool add_start = false;
  int start_index = 0;
  double start_x = 0;
  double start_y = 0;
};

// Skip points outside the rectangle on the same side as the current point
void skip_outside(RectLineState& st, const Box& theBox)
{
  ++st.i;  // we already know it is outside

  if (st.x < theBox.xmin())
    while (st.i < st.n && st.g.getX(st.i) < theBox.xmin())
      ++st.i;

  else if (st.x > theBox.xmax())
    while (st.i < st.n && st.g.getX(st.i) > theBox.xmax())
      ++st.i;

  else if (st.y < theBox.ymin())
    while (st.i < st.n && st.g.getY(st.i) < theBox.ymin())
      ++st.i;

  else if (st.y > theBox.ymax())
    while (st.i < st.n && st.g.getY(st.i) > theBox.ymax())
      ++st.i;
}

// Add a line to the exterior or interior lines of the clipper
void add_rect_line(RectClipper& theRect, OGRLineString* line, bool exterior)
{
  if (exterior)
    theRect.addExterior(line);
  else
    theRect.addInterior(line);
}

// The line from the start of the current linestring to the point x0,y0 where the rectangle
// was entered
OGRLineString* line_to_entry(const RectLineState& st)
{
  auto* line = new OGRLineString();
  if (st.add_start)
    line->addPoint(st.start_x, st.start_y);
  line->addSubLineString(&st.g, st.start_index, st.i - 1);
  line->addPoint(st.x0, st.y0);
  return line;
}

// Terminate the clipped line at the point where it exits the rectangle
void clip_rect_exit(RectLineState& st, RectClipper& theRect, const Box& theBox)
{
  // Clip the outside point to edges
  clip_to_edges(st.x, st.y, st.g.getX(st.i - 1), st.g.getY(st.i - 1), theBox);

  // Note that if there is a spike that enters and exits the box
  // at the same coordinate, we must discard the spike.

  const bool spike =
      (st.add_start && st.x0 == st.x && st.y0 == st.y && st.i - st.start_index < 2);

  if (!spike)
  {
    auto* line = new OGRLineString();
    if (st.add_start)
      line->addPoint(st.x0, st.y0);
    if (st.start_index <= st.i - 1)
      line->addSubLineString(&st.g, st.start_index, st.i - 1);
    line->addPoint(st.x, st.y);
    theRect.addExterior(line);
  }
  st.add_start = false;
}

// Clip the line starting from a point outside the rectangle, false if the line ended outside
bool clip_rect_outside(RectLineState& st, RectClipper& theRect, const Box& theBox, bool exterior)
{
  // Skip points as fast as possible until something has to be checked
  // in more detail.

  skip_outside(st, theBox);

  if (st.i >= st.n)
    return false;

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

  st.x = st.g.getX(st.i);
  st.y = st.g.getY(st.i);
  st.pos = theBox.position(st.x, st.y);

  st.position |= st.pos;

  // Calculate possible intersection coordinate with the box

  st.x0 = st.g.getX(st.i - 1);
  st.y0 = st.g.getY(st.i - 1);
  clip_to_edges(st.x0, st.y0, st.x, st.y, theBox);

  if (st.pos == Box::Inside)  // out-in
  {
    // x0,y0 begings the new linestring
    st.start_index = st.i;
    st.add_start = true;
    // Main loop will enter the Inside/Edge section
  }
  else if (st.pos == Box::Outside)  // out-out
  {
    // Clip the other end too
    clip_to_edges(st.x, st.y, st.x0, st.y0, theBox);

    auto prev_pos = theBox.position(st.x0, st.y0);
    st.pos = theBox.position(st.x, st.y);

    if (different(st.x0, st.y0, st.x, st.y) &&  // discard corners etc
        Box::onEdge(prev_pos) &&                // discard if does not intersect rect
        Box::onEdge(st.pos) && !Box::onSameEdge(prev_pos, st.pos)  // discard if travels along edge
    )
    {
      st.position |= Box::Inside;  // something is inside
      auto* line = new OGRLineString;
      line->addPoint(st.x0, st.y0);
      line->addPoint(st.x, st.y);
      add_rect_line(theRect, line, exterior);
    }

    // Continue main loop outside the rect
  }
  else  // out-edge
  {
    auto prev_pos = theBox.position(st.x0, st.y0);

    if (!Box::onSameEdge(st.pos, prev_pos))
    {
      st.position |= Box::Inside;  // something is inside
      auto* line = new OGRLineString;
      line->addPoint(st.x0, st.y0);
      line->addPoint(st.x, st.y);
      add_rect_line(theRect, line, exterior);
    }
  }
  return true;
}

// Clip the line starting from a point inside the rectangle, true if the whole line is inside
bool clip_rect_inside(RectLineState& st, RectClipper& theRect, const Box& theBox, bool exterior)
{
  /*
   *  1. in-in
   *     - continue clip
   *  2. in-edge
   *     - terminate clip
   *  3. in-out
   *     - terminate clip at intersection point
   */

  while (++st.i < st.n)
  {
    st.x = st.g.getX(st.i);
    st.y = st.g.getY(st.i);

    st.pos = theBox.position(st.x, st.y);
    st.position |= st.pos;

    if (st.pos == Box::Inside)  // in-in
    {
    }
    else if (st.pos == Box::Outside)  // in-out
    {
      clip_rect_exit(st, theRect, theBox);
      break;  // going to outside the loop
    }
    else  // in-edge
    {
      if (st.start_index == 0 && st.i == st.n - 1)
        return true;  // All inside?

      auto* line = new OGRLineString();
      if (st.add_start)
        line->addPoint(st.x0, st.y0);
      st.add_start = false;
      line->addSubLineString(&st.g, st.start_index, st.i);
      add_rect_line(theRect, line, exterior);

      st.start_index = st.i;  // potentially going in again at this point
      break;                  // going to the edge loop
    }
  }

  // Was everything in? If so, generate no output here, just clone the linestring elsewhere
  if (st.start_index == 0 && st.i >= st.n)
    return true;

  // Flush the last line segment if data ended and there is something to flush
  if (st.pos == Box::Inside && (st.start_index < st.i - 1 || st.add_start))
  {
    auto* line = new OGRLineString();
    if (st.add_start)
    {
      line->addPoint(st.x0, st.y0);
      st.add_start = false;
    }
    line->addSubLineString(&st.g, st.start_index, st.i - 1);
    add_rect_line(theRect, line, exterior);
  }
  return false;
}

// Clip the line starting from a point on the edge of the rectangle, true if the processing is done
bool clip_rect_edge(RectLineState& st, RectClipper& theRect, const Box& theBox, bool exterior)
{
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

  while (++st.i < st.n)
  {
    auto prev_pos = st.pos;

    st.x = st.g.getX(st.i);
    st.y = st.g.getY(st.i);

    st.pos = theBox.position(st.x, st.y);
    st.position |= st.pos;

    if (st.pos == Box::Inside)  // edge-in
    {
      // Start new linestring
      st.start_index = st.i - 1;
      break;  // And continue main loop on the inside
    }

    if (st.pos == Box::Outside)  // edge-out
    {
      // Clip the outside point to edges
      clip_to_edges(st.x, st.y, st.g.getX(st.i - 1), st.g.getY(st.i - 1), theBox);
      st.pos = theBox.position(st.x, st.y);

      // Does the line exit through the inside of the box?

      bool through_box = (different(st.x, st.y, st.g.getX(st.i), st.g.getY(st.i)) &&
                          !Box::onSameEdge(prev_pos, st.pos));

      // Output a LineString if it at least one segment long

      if (through_box)
      {
        auto* line = new OGRLineString();
        line->addPoint(st.g.getX(st.i - 1), st.g.getY(st.i - 1));
        line->addPoint(st.x, st.y);
        theRect.addExterior(line);
      }
      break;  // And continue main loop on the outside
    }

    // edge-edge

    // travel to different edge?
    if (!Box::onSameEdge(prev_pos, st.pos))
    {
      st.position |= Box::Inside;  // passed through
      auto* line = new OGRLineString();
      line->addPoint(st.g.getX(st.i - 1), st.g.getY(st.i - 1));
      line->addPoint(st.x, st.y);
      theRect.addExterior(line);
      st.start_index = st.i;
    }
  }
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

int clip_rect(const OGRLineString* theGeom, RectClipper& theRect, const Box& theBox, bool exterior)
{
  try
  {
    int position = 0;

    if (theGeom == nullptr)
      return position;

    int n = theGeom->getNumPoints();

    if (n < 1)
      return position;

    // Keep a record of the point where a line segment entered
    // the rectangle. If the boolean is set, we must insert
    // the point to the beginning of the linestring which then
    // continues inside the rectangle.

    RectLineState st(*theGeom, n);

    // Start iterating

    while (st.i < n)
    {
      // Establish initial position

      st.x = st.g.getX(st.i);
      st.y = st.g.getY(st.i);
      st.pos = theBox.position(st.x, st.y);

      // Update global status
      st.position |= st.pos;

      if (st.pos == Box::Outside)
      {
        if (!clip_rect_outside(st, theRect, theBox, exterior))
          return st.position;
      }
      else if (st.pos == Box::Inside)
      {
        if (clip_rect_inside(st, theRect, theBox, exterior))
          return Box::Inside;
      }
      else if (clip_rect_edge(st, theRect, theBox, exterior))
        return st.position;
    }
    return st.position;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// Cut the line starting from a point outside the rectangle, false if the line ended outside
bool cut_rect_outside(RectLineState& st, RectClipper& theRect, const Box& theBox, bool exterior)
{
  // Skip points as fast as possible until something has to be checked
  // in more detail.

  skip_outside(st, theBox);

  if (st.i >= st.n)
  {
    if (st.start_index == 0)
      return false;  // everything was out

    if (st.start_index > 0 && st.start_index < st.n)
    {
      auto* line = new OGRLineString();
      if (st.add_start)
        line->addPoint(st.start_x, st.start_y);
      line->addSubLineString(&st.g, st.start_index, st.n - 1);
      add_rect_line(theRect, line, exterior);
    }
    return false;
  }

  // Establish new position

  st.x = st.g.getX(st.i);
  st.y = st.g.getY(st.i);
  st.pos = theBox.position(st.x, st.y);

  st.position |= st.pos;

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

  st.x0 = st.g.getX(st.i - 1);
  st.y0 = st.g.getY(st.i - 1);

  clip_to_edges(st.x0, st.y0, st.x, st.y, theBox);

  if (st.pos == Box::Inside)  // out-in
  {
    auto* line = line_to_entry(st);
    st.add_start = false;
    add_rect_line(theRect, line, exterior);
    // Main loop will enter the Inside/Edge section
  }
  else if (st.pos == Box::Outside)  // out-out
  {
    // Clip the other end too
    clip_to_edges(st.x, st.y, st.x0, st.y0, theBox);

    Box::Position prev_pos = theBox.position(st.x0, st.y0);
    st.pos = theBox.position(st.x, st.y);
    st.position |= st.pos;

    if (different(st.x0, st.y0, st.x, st.y) &&  // discard corners etc
        Box::onEdge(prev_pos))                  // discard if does not intersect rect
    {
      // Intersection occurred. Must generate a linestring from start to intersection point
      // then.
      add_rect_line(theRect, line_to_entry(st), exterior);

      st.position |= Box::Inside;  // mark we visited inside

      // Start next line from the end intersection point
      st.start_index = st.i;
      st.start_x = st.x;
      st.start_y = st.y;
      st.add_start = true;
    }

    // Continue main loop outside the rect
  }
  else
  {
    auto* line = line_to_entry(st);
    if (st.x != st.g.getX(st.i) || st.y != st.g.getY(st.i))
      st.position |= Box::Inside;
    add_rect_line(theRect, line, exterior);

    st.add_start = false;
  }
  return true;
}

// Cut the line starting from a point inside the rectangle, true if the whole line is inside
bool cut_rect_inside(RectLineState& st, RectClipper& theRect, const Box& theBox, bool exterior)
{
  /*
   *  1. in-in
   *     - ignore segment
   *  2. in-edge
   *     - ignore segment
   *  3. in-out
   *     - begin new linestring at intersection point
   */

  while (++st.i < st.n)
  {
    st.x = st.g.getX(st.i);
    st.y = st.g.getY(st.i);
    st.pos = theBox.position(st.x, st.y);

    st.position |= st.pos;

    if (st.pos != Box::Outside)
      continue;

    // Clip the outside point to edges
    clip_to_edges(st.x, st.y, st.g.getX(st.i - 1), st.g.getY(st.i - 1), theBox);
    st.pos = theBox.position(st.x, st.y);

    st.position |= st.pos;

    st.start_index = st.i;
    st.add_start = true;
    st.start_x = st.x;
    st.start_y = st.y;
    break;
  }

  // Was everything in? If so, generate no output
  if (st.start_index == 0 && st.i >= st.n)
    return true;
  return false;
}

// Cut the line starting from a point on the edge of the rectangle, true if the processing is done
bool cut_rect_edge(RectLineState& st, RectClipper& theRect, const Box& theBox, bool exterior)
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

  while (++st.i < st.n)
  {
    auto prev_pos = st.pos;
    st.x = st.g.getX(st.i);
    st.y = st.g.getY(st.i);
    st.pos = theBox.position(st.x, st.y);

    st.position |= st.pos;

    if (st.pos == Box::Inside)
      continue;
    if (st.pos != Box::Outside)  // on edge
    {
      if (!Box::onSameEdge(prev_pos, st.pos))
        st.position |= Box::Inside;  // visited inside
      continue;
    }

    // Clip the outside point to edges
    clip_to_edges(st.x, st.y, st.g.getX(st.i - 1), st.g.getY(st.i - 1), theBox);

    if (st.x != st.g.getX(st.i) || st.y != st.g.getY(st.i))
      st.position |= Box::Inside;

    st.start_index = st.i;
    st.add_start = true;
    st.start_x = st.x;
    st.start_y = st.y;
    break;
  }

  // Was everything in? If so, generate no output
  if (st.start_index == 0 && st.i >= st.n)
    return true;
  return false;
}

// ----------------------------------------------------------------------
/*!
 * \brief Cut OGR geometry.
 */
// ----------------------------------------------------------------------

int cut_rect(const OGRLineString* theGeom, RectClipper& theRect, const Box& theBox, bool exterior)
{
  try
  {
    int position = 0;

    if (theGeom == nullptr)
      return position;

    int n = theGeom->getNumPoints();

    if (n < 1)
      return position;

    // Keep a record of the point where a line segment entered
    // the rectangle. If the boolean is set, we must insert
    // the point to the end of the linestring which began outside the rectangle.

    RectLineState st(*theGeom, n);

    // Start iterating

    while (st.i < n)
    {
      // Establish initial position

      st.x = st.g.getX(st.i);
      st.y = st.g.getY(st.i);
      st.pos = theBox.position(st.x, st.y);

      st.position |= st.pos;

      if (st.pos == Box::Outside)
      {
        if (!cut_rect_outside(st, theRect, theBox, exterior))
          return st.position;
      }
      else if (st.pos == Box::Inside)
      {
        if (cut_rect_inside(st, theRect, theBox, exterior))
          return st.position;
      }
      else if (cut_rect_edge(st, theRect, theBox, exterior))
        return st.position;
    }

    if (st.add_start)
    {
      auto* line = new OGRLineString();

      line->addPoint(st.start_x, st.start_y);
      line->addSubLineString(&st.g, st.start_index, n - 1);
      add_rect_line(theRect, line, exterior);
    }

    return st.position;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
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
  try
  {
    if (keep_inside)
      return clip_rect(theGeom, theRect, theBox, exterior);

    return cut_rect(theGeom, theRect, theBox, exterior);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Handle an exterior with no vertices inside the box when clipping to linestrings
 *
 * Returns false if the result is complete.
 */
// ----------------------------------------------------------------------

bool linestrings_exterior_not_inside(const OGRPolygon *theGeom,
                                     GeometryBuilder &theBuilder,
                                     const Box &theBox,
                                     RectClipper &rect,
                                     bool keep_inside)
{
  // CLIP:
  // -if box inside exterior, omit exterior
  // -if box outside exterior, return empty result
  // CUT:
  // - if box inside exterior, keep exterior and continue
  // - if box outside exterior, return input as is

  const auto *exterior = theGeom->getExteriorRing();
  if (exterior == nullptr)
    return false;
  bool box_inside = box_inside_ring(theBox, *exterior);

  if (keep_inside)
    return box_inside;

  if (!box_inside)
  {
    theBuilder.add(theGeom->clone());
    return false;
  }
  rect.addExterior(theGeom->getExteriorRing()->clone());
  return true;
}

// ----------------------------------------------------------------------
/*!
 * \brief Handle a hole of a polygon when clipping to linestrings
 *
 * Returns false if the result is complete.
 */
// ----------------------------------------------------------------------

bool linestrings_hole(const OGRPolygon *theGeom,
                      const OGRLinearRing *hole,
                      int holeposition,
                      GeometryBuilder &theBuilder,
                      const Box &theBox,
                      RectClipper &rect,
                      bool keep_inside)
{
  if (all_only_inside(holeposition))
  {
    if (keep_inside)
      rect.addExterior(hole->clone());
  }
  else if (all_not_inside(holeposition))
  {
    bool box_inside_hole = box_inside_ring(theBox, *hole);

    if (box_inside_hole)
    {
      // If the box clip is inside a hole the result is empty
      // Otherwise we can keep the original input
      if (!keep_inside)
        theBuilder.add(theGeom->clone());
      return false;
    }

    // Otherwise the hole is outside the box
    if (!keep_inside)
      rect.addExterior(hole->clone());
  }
  return true;
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
  try
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
        theBuilder.add(theGeom->clone());
      return;
    }

    // If no clipped lines were generated and no vertex is inside...
    if (all_not_inside(position) && rect.empty() &&
        !linestrings_exterior_not_inside(theGeom, theBuilder, theBox, rect, keep_inside))
      return;

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
      const auto *hole = theGeom->getInteriorRing(i);
      auto holeposition = do_rect(hole, rect, theBox, keep_inside, all_exterior);

      if (!linestrings_hole(theGeom, hole, holeposition, theBuilder, theBox, rect, keep_inside))
        return;
    }

    rect.reconnect();
    rect.reconnectWithoutBox();
    rect.release(theBuilder);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Handle an exterior with no vertices inside the box when clipping to polygons
 *
 * Returns false if the result is complete.
 */
// ----------------------------------------------------------------------

bool polygons_exterior_not_inside(const OGRPolygon *theGeom,
                                  GeometryBuilder &theBuilder,
                                  const Box &theBox,
                                  RectClipper &rect,
                                  bool keep_inside)
{
  // std::cerr << "all_not_inside true\n";

  // CLIP:
  // -if box inside exterior, box becomes part of new exterior ; ADD_BOX
  // -if box outside exterior, return empty result
  // CUT:
  // - if box inside exterior, box becomes part of new holes ; ADD_BOX
  // - if box outside exterior, return input as is

  const auto *exterior = theGeom->getExteriorRing();
  if (exterior == nullptr)
    return false;
  bool box_inside = box_inside_ring(theBox, *exterior);

  if (keep_inside)
  {
    if (!box_inside)
    {
      // std::cerr << "Returning\n";
      return false;
    }
    rect.addBox();
    return true;
  }

  if (!box_inside)
  {
    theBuilder.add(theGeom->clone());
    // std::cerr << "Returning clone\n";
    return false;
  }
  // box is inside exterior, must keep exterior
  rect.addExterior(theGeom->getExteriorRing()->clone());
  rect.addBox();
  return true;
}

// ----------------------------------------------------------------------
/*!
 * \brief Handle a hole of a polygon when clipping to polygons
 *
 * Returns false if the result is complete.
 */
// ----------------------------------------------------------------------

bool polygons_hole(const OGRPolygon *theGeom,
                   const OGRLinearRing *hole,
                   int holeposition,
                   GeometryBuilder &theBuilder,
                   const Box &theBox,
                   RectClipper &rect,
                   bool keep_inside)
{
  if (all_only_inside(holeposition))
  {
    if (keep_inside)
      rect.addInterior(hole->clone());
  }
  else if (all_not_inside(holeposition))
  {
    bool box_inside_hole = box_inside_ring(theBox, *hole);

    if (box_inside_hole)
    {
      // If the box clip is inside a hole the result is empty
      // Otherwise we can keep the original input
      if (!keep_inside)
        theBuilder.add(theGeom->clone());
      return false;
    }

    // Otherwise the hole is outside the box.
    // If the hole has edge contact with the box, cut_rect already generated interior
    // lines for it that will be connected along the box boundary. Adding the hole ring
    // directly too would create duplicate rings.
    bool has_edge_contact = (holeposition & (Box::Left | Box::Top | Box::Right | Box::Bottom)) != 0;
    if (!keep_inside && !has_edge_contact)
      rect.addInterior(hole->clone());
  }
  return true;
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
  try
  {
    if (theGeom == nullptr || theGeom->IsEmpty() != 0)
      return;

    // Clip the exterior first to see what's going on

    RectClipper rect(theBox, keep_inside);

    // std::cerr << "do_rect\n";
    auto position = do_rect(theGeom->getExteriorRing(), rect, theBox, keep_inside, true);

    if (all_not_outside(position))
    {
      // std::cerr << "all_not_outside true\n";

      // CLIP: - if all vertices inside box or on the edges, return input as is
      // CUT:  - if all vertices inside box or on the edges, return empty result

      if (keep_inside)
      {
        auto *poly = theGeom->clone();
        OGR::normalize(*poly);
        theBuilder.add(poly);
      }
      return;
    }

    // If clipping produced no lines and nothing is inside...
    if (all_not_inside(position) && rect.empty() &&
        !polygons_exterior_not_inside(theGeom, theBuilder, theBox, rect, keep_inside))
      return;

    // Clipping:
    // If the exterior does not intersect the bounding box at all, it must
    // be surrounding the bbox. The exterior is thus shrunk into the bbox.
    // Any hole in it remains a hole, except when it intersects the bbox, in
    // which case it becomes part of the exterior.

    // Cutting: If the exterior does not intersect the bounding box at
    // all, it must be surrounding the bbox. The exterior is thus kept
    // as it is. The bbox becomes a hole. Any hole intersecting the hole
    // is unioned with it.

    // Force exterior to be counter clockwise
    // if (ext_clockwise) rect.reverseLines();
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

    // std::cerr << "Prosessing holes\n";

    for (int i = 0, n = theGeom->getNumInteriorRings(); i < n; ++i)
    {
      const auto *hole = theGeom->getInteriorRing(i);

      auto holeposition = do_rect(hole, rect, theBox, keep_inside, false);

      if (!polygons_hole(theGeom, hole, holeposition, theBuilder, theBox, rect, keep_inside))
        return;
    }

    // std::cerr << "Calling reconnect\n";
    rect.reconnect();  // trivial reconnect of endpoints
    // std::cerr << "Calling reconnectWithBox\n";
    rect.reconnectWithBox(max_length);  // reconnect along box boundaries
    rect.release(theBuilder);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
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
  try
  {
    if (keep_polygons)
      do_polygon_to_polygons(theGeom, theBuilder, theBox, max_length, keep_inside);
    else
      do_polygon_to_linestrings(theGeom, theBuilder, theBox, keep_inside);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
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
  try
  {
    if (theGeom == nullptr || theGeom->IsEmpty() != 0)
      return;

    // If everything was in, just clone the original when clipping

    RectClipper rect(theBox, keep_inside);
    auto position = do_rect(theGeom, rect, theBox, keep_inside, true);

    if (all_only_inside(position))
    {
      if (keep_inside)
        theBuilder.add(theGeom->clone());
    }
    else if (all_only_outside(position))
    {
      if (!keep_inside)
        theBuilder.add(theGeom->clone());
    }
    else
    {
      rect.reconnect();
      rect.reconnectWithoutBox();
      rect.release(theBuilder);
    }
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
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
  try
  {
    if (theGeom == nullptr || theGeom->IsEmpty() != 0)
      return;

    for (int i = 0, n = theGeom->getNumGeometries(); i < n; ++i)
    {
      do_point(theGeom->getGeometryRef(i), theBuilder, theBox, keep_inside);
    }
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
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
  try
  {
    if (theGeom == nullptr || theGeom->IsEmpty() != 0)
      return;

    for (int i = 0, n = theGeom->getNumGeometries(); i < n; ++i)
    {
      do_linestring(theGeom->getGeometryRef(i), theBuilder, theBox, keep_inside);
    }
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
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
  try
  {
    if (theGeom == nullptr || theGeom->IsEmpty() != 0)
      return;

    for (int i = 0, n = theGeom->getNumGeometries(); i < n; ++i)
    {
      do_polygon(theGeom->getGeometryRef(i),
                 theBuilder,
                 theBox,
                 max_length,
                 keep_polygons,
                 keep_inside);
    }
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
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
  try
  {
    if (theGeom == nullptr || theGeom->IsEmpty() != 0)
      return;

    for (int i = 0, n = theGeom->getNumGeometries(); i < n; ++i)
    {
      do_geom(
          theGeom->getGeometryRef(i), theBuilder, theBox, max_length, keep_polygons, keep_inside);
    }
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
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
  try
  {
    OGRwkbGeometryType id = theGeom->getGeometryType();

    switch (id)
    {
      case wkbPoint:
      {
        do_point(dynamic_cast<const OGRPoint *>(theGeom), theBuilder, theBox, keep_inside);
        break;
      }
      case wkbLineString:
      {
        do_linestring(
            dynamic_cast<const OGRLineString *>(theGeom), theBuilder, theBox, keep_inside);
        break;
      }
      case wkbPolygon:
      {
        do_polygon(dynamic_cast<const OGRPolygon *>(theGeom),
                   theBuilder,
                   theBox,
                   max_length,
                   keep_polygons,
                   keep_inside);
        break;
      }
      case wkbMultiPoint:
      {
        do_multipoint(
            dynamic_cast<const OGRMultiPoint *>(theGeom), theBuilder, theBox, keep_inside);
        break;
      }
      case wkbMultiLineString:
      {
        do_multilinestring(
            dynamic_cast<const OGRMultiLineString *>(theGeom), theBuilder, theBox, keep_inside);
        break;
      }
      case wkbMultiPolygon:
      {
        do_multipolygon(dynamic_cast<const OGRMultiPolygon *>(theGeom),
                        theBuilder,
                        theBox,
                        max_length,
                        keep_polygons,
                        keep_inside);
        break;
      }
      case wkbGeometryCollection:
      {
        do_geometrycollection(dynamic_cast<const OGRGeometryCollection *>(theGeom),
                              theBuilder,
                              theBox,
                              max_length,
                              keep_polygons,
                              keep_inside);
        break;
      }
      case wkbLinearRing:
        throw Fmi::Exception::Trace(BCP, "Direct clipping of LinearRings is not supported");
      default:
        throw Fmi::Exception::Trace(
            BCP, "Encountered an unknown geometry component when clipping polygons");
    }
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

}  // namespace

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
  try
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
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
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
                           double theMaxSegmentLength)
{
  try
  {
    bool keep_polygons = true;
    bool keep_inside = true;

    GeometryBuilder builder;
    do_geom(&theGeom, builder, theBox, theMaxSegmentLength, keep_polygons, keep_inside);

    OGRGeometry *geom = builder.build();

    if (geom != nullptr)
      geom->assignSpatialReference(theGeom.getSpatialReference());

    return geom;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
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
  try
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
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Cut a geometry so that polygons are preserved
 *
 * \return Empty GeometryCollection if the result is empty
 */
// ----------------------------------------------------------------------

OGRGeometry *OGR::polycut(const OGRGeometry &theGeom, const Box &theBox, double theMaxSegmentLength)
{
  try
  {
    bool keep_polygons = true;
    bool keep_inside = false;

    GeometryBuilder builder;
    do_geom(&theGeom, builder, theBox, theMaxSegmentLength, keep_polygons, keep_inside);

    OGRGeometry *geom = builder.build();
    if (geom != nullptr)
      geom->assignSpatialReference(theGeom.getSpatialReference());

    return geom;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

}  // namespace Fmi
