#include "Shape_rect.h"
#include "OGR.h"
#include "ShapeClipper.h"

namespace Fmi
{
namespace
{
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

void clip_one_edge(double &x1, double &y1, double x2, double y2, double limit)
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

}  // namespace

// The state of clipping or cutting a linestring
struct Shape_rect::LineState
{
  LineState(const OGRLineString& theGeom, int theSize) : g(theGeom), n(theSize) {}

  const OGRLineString& g;  // For shorthand code
  const int n;
  int i = 0;
  int position = 0;
  int pos = 0;
  double x = 0;
  double y = 0;
  double x0 = 0;
  double y0 = 0;
  bool add_start = false;
  int start_index = 0;
  double start_x = 0;
  double start_y = 0;
};

// ----------------------------------------------------------------------
/*!
 * \brief Skip points outside the rectangle on the same side as the current point
 */
// ----------------------------------------------------------------------

void Shape_rect::skipOutside(LineState &st) const
{
  ++st.i;  // we already know it is outside

  if (st.x < itsXMin)
    while (st.i < st.n && st.g.getX(st.i) < itsXMin)
      ++st.i;

  else if (st.x > itsXMax)
    while (st.i < st.n && st.g.getX(st.i) > itsXMax)
      ++st.i;

  else if (st.y < itsYMin)
    while (st.i < st.n && st.g.getY(st.i) < itsYMin)
      ++st.i;

  else if (st.y > itsYMax)
    while (st.i < st.n && st.g.getY(st.i) > itsYMax)
      ++st.i;
}

// ----------------------------------------------------------------------
/*!
 * \brief Clip the line starting from a point outside the rectangle
 *
 * Returns false if the line ended outside.
 */
// ----------------------------------------------------------------------

bool Shape_rect::clipOutside(LineState& st, ShapeClipper& theClipper, bool exterior) const
{
  // Skip points as fast as possible until something has to be checked
  // in more detail.

  skipOutside(st);

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
  st.pos = getPosition(st.x, st.y);

  st.position |= st.pos;

  // Calculate possible intersection coordinate with the box

  st.x0 = st.g.getX(st.i - 1);
  st.y0 = st.g.getY(st.i - 1);
  clip_to_edges(st.x0, st.y0, st.x, st.y);

  if (st.pos == Position::Inside)  // out-in
  {
    // x0,y0 begings the new linestring
    st.start_index = st.i;
    st.add_start = true;
    // Main loop will enter the Inside/Edge section
  }
  else if (st.pos == Position::Outside)  // out-out
  {
    // Clip the other end too
    clip_to_edges(st.x, st.y, st.x0, st.y0);

    auto prev_pos = getPosition(st.x0, st.y0);
    st.pos = getPosition(st.x, st.y);

    if (different(st.x0, st.y0, st.x, st.y) &&           // discard corners etc
        onEdge(prev_pos) &&                              // discard if does not intersect rect
        onEdge(st.pos) && !onSameEdge(prev_pos, st.pos)  // discard if travels along edge
    )
    {
      st.position |= Position::Inside;  // something is inside
      auto* line = new OGRLineString;
      line->addPoint(st.x0, st.y0);
      line->addPoint(st.x, st.y);
#if 0            
        std::cout << __LINE__ << " adding " << (exterior ? "ext: " : "int: ")
                  << OGR::exportToWkt(*line) << "\n";
#endif
      theClipper.add(line, exterior);
    }

    // Continue main loop outside the rect
  }
  else  // out-edge
  {
    auto prev_pos = getPosition(st.x0, st.y0);

    if (!onSameEdge(st.pos, prev_pos))
    {
      st.position |= Position::Inside;  // something is inside
      auto* line = new OGRLineString;
      line->addPoint(st.x0, st.y0);
      line->addPoint(st.x, st.y);
#if 0            
        std::cout << __LINE__ << " adding " << (exterior ? "ext: " : "int: ")
                  << OGR::exportToWkt(*line) << "\n";
#endif
      theClipper.add(line, exterior);
    }
  }
  return true;
}

// ----------------------------------------------------------------------
/*!
 * \brief Clip the line starting from a point inside the rectangle
 *
 * Returns true if the whole line is inside.
 */
// ----------------------------------------------------------------------

bool Shape_rect::clipInside(LineState& st, ShapeClipper& theClipper, bool exterior) const
{
  while (++st.i < st.n)
  {
    st.x = st.g.getX(st.i);
    st.y = st.g.getY(st.i);

    st.pos = getPosition(st.x, st.y);
    st.position |= st.pos;

    if (st.pos == Position::Inside)  // in-in
    {
    }
    else if (st.pos == Position::Outside)  // in-out
    {
      // Clip the outside point to edges
      clip_to_edges(st.x, st.y, st.g.getX(st.i - 1), st.g.getY(st.i - 1));

      auto* line = new OGRLineString();
      if (st.add_start)
        line->addPoint(st.x0, st.y0);
      st.add_start = false;
      if (st.start_index <= st.i - 1)
        line->addSubLineString(&st.g, st.start_index, st.i - 1);
      line->addPoint(st.x, st.y);
#if 0
        std::cout << "Adding EXT: " << OGR::exportToWkt(*line) << "\n";
#endif
      theClipper.addExterior(line);

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
#if 0
        std::cout << __LINE__ << " adding " << (exterior ? "ext: " : "int: ")
                  << OGR::exportToWkt(*line) << "\n";
#endif
      theClipper.add(line, exterior);

      st.start_index = st.i;  // potentially going in again at this point
      break;                  // going to the edge loop
    }
  }

  // Was everything in? If so, generate no output here, just clone the linestring elsewhere
  if (st.start_index == 0 && st.i >= st.n)
    return true;

  // Flush the last line segment if data ended and there is something to flush
  if (st.pos == Position::Inside && (st.start_index < st.i - 1 || st.add_start))
  {
    auto* line = new OGRLineString();
    if (st.add_start)
    {
      line->addPoint(st.x0, st.y0);
      st.add_start = false;
    }
    line->addSubLineString(&st.g, st.start_index, st.i - 1);
#if 0
      std::cout << __LINE__ << " adding " << (exterior ? "ext: " : "int: ")
                << OGR::exportToWkt(*line) << "\n";
#endif
    // theClipper.add(line, exterior);
    theClipper.addExterior(line);
  }
  return false;
}

// ----------------------------------------------------------------------
/*!
 * \brief Clip the line starting from a point on the edge of the rectangle
 *
 * Returns true if the clipping is done.
 */
// ----------------------------------------------------------------------

bool Shape_rect::clipEdge(LineState& st, ShapeClipper& theClipper, bool exterior) const
{
  while (++st.i < st.n)
  {
    auto prev_pos = st.pos;

    st.x = st.g.getX(st.i);
    st.y = st.g.getY(st.i);

    st.pos = getPosition(st.x, st.y);
    st.position |= st.pos;

    if (st.pos == Position::Inside)  // edge-in
    {
      // Start new linestring
      st.start_index = st.i - 1;
      break;  // And continue main loop on the inside
    }

    if (st.pos == Position::Outside)  // edge-out
    {
      // Clip the outside point to edges
      clip_to_edges(st.x, st.y, st.g.getX(st.i - 1), st.g.getY(st.i - 1));
      st.pos = getPosition(st.x, st.y);

      // Does the line exit through the inside of the box?

      bool through_box = (different(st.x, st.y, st.g.getX(st.i), st.g.getY(st.i)) &&
                          !onSameEdge(prev_pos, st.pos));

      // Output a LineString if it at least one segment long

      if (through_box)
      {
        auto* line = new OGRLineString();
        line->addPoint(st.g.getX(st.i - 1), st.g.getY(st.i - 1));
        line->addPoint(st.x, st.y);
#if 0
          std::cout << "Adding EXT: " << OGR::exportToWkt(*line) << "\n";
#endif
        theClipper.addExterior(line);
      }
      break;  // And continue main loop on the outside
    }

    // edge-edge

    // travel to different edge?
    if (!onSameEdge(prev_pos, st.pos))
    {
      st.position |= Position::Inside;  // passed through
      auto* line = new OGRLineString();
      line->addPoint(st.g.getX(st.i - 1), st.g.getY(st.i - 1));
      line->addPoint(st.x, st.y);
#if 0
          std::cout << "Adding EXT: " << OGR::exportToWkt(*line) << "\n";
#endif
      theClipper.addExterior(line);
      st.start_index = st.i;
    }
  }
  return false;
}

// ----------------------------------------------------------------------
/*!
 * \brief Cut the line starting from a point outside the rectangle
 *
 * Returns false if the line ended outside.
 */
// ----------------------------------------------------------------------

bool Shape_rect::cutOutside(LineState& st, ShapeClipper& theClipper, bool exterior) const
{
  // Skip points as fast as possible until something has to be checked
  // in more detail.

  skipOutside(st);

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
#if 0
        std::cout << __LINE__ << " adding " << (exterior ? "ext: " : "int: ")
                  << OGR::exportToWkt(*line) << "\n";
#endif
      theClipper.add(line, exterior);
    }
    return false;
  }

  // Establish new position

  st.x = st.g.getX(st.i);
  st.y = st.g.getY(st.i);
  st.pos = getPosition(st.x, st.y);

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

  clip_to_edges(st.x0, st.y0, st.x, st.y);

  if (st.pos == Position::Inside)  // out-in
  {
    auto* line = new OGRLineString();

    if (st.add_start)
      line->addPoint(st.start_x, st.start_y);
    st.add_start = false;
    line->addSubLineString(&st.g, st.start_index, st.i - 1);
    line->addPoint(st.x0, st.y0);
#if 0
      std::cout << __LINE__ << " adding " << (exterior ? "ext: " : "int: ")
                << OGR::exportToWkt(*line) << "\n";
#endif
    theClipper.add(line, exterior);
    // Main loop will enter the Inside/Edge section
  }
  else if (st.pos == Position::Outside)  // out-out
  {
    // Clip the other end too
    clip_to_edges(st.x, st.y, st.x0, st.y0);

    auto prev_pos = getPosition(st.x0, st.y0);
    st.pos = getPosition(st.x, st.y);
    st.position |= st.pos;

    if (different(st.x0, st.y0, st.x, st.y) &&  // discard corners etc
        onEdge(prev_pos))                       // discard if does not intersect rect
    {
      // Intersection occurred. Must generate a linestring from start to intersection point
      // then.
      auto* line = new OGRLineString();

      if (st.add_start)
        line->addPoint(st.start_x, st.start_y);
      line->addSubLineString(&st.g, st.start_index, st.i - 1);
      line->addPoint(st.x0, st.y0);
#if 0
        std::cout << __LINE__ << " adding " << (exterior ? "ext: " : "int: ")
                  << OGR::exportToWkt(*line) << "\n";
#endif
      theClipper.add(line, exterior);

      st.position |= Position::Inside;  // mark we visited inside

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
    auto* line = new OGRLineString();
    if (st.add_start)
      line->addPoint(st.start_x, st.start_y);
    line->addSubLineString(&st.g, st.start_index, st.i - 1);
    line->addPoint(st.x0, st.y0);
    if (st.x != st.g.getX(st.i) || st.y != st.g.getY(st.i))
      st.position |= Position::Inside;
#if 0
      std::cout << __LINE__ << " adding " << (exterior ? "ext: " : "int: ")
                << OGR::exportToWkt(*line) << "\n";
#endif
    theClipper.add(line, exterior);

    st.add_start = false;
  }
  return true;
}

// ----------------------------------------------------------------------
/*!
 * \brief Cut the line starting from a point inside the rectangle
 *
 * Returns true if the whole line is inside.
 */
// ----------------------------------------------------------------------

bool Shape_rect::cutInside(LineState& st, ShapeClipper& theClipper, bool exterior) const
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
    st.pos = getPosition(st.x, st.y);

    st.position |= st.pos;

    if (st.pos != Position::Outside)
      continue;

    // Clip the outside point to edges
    clip_to_edges(st.x, st.y, st.g.getX(st.i - 1), st.g.getY(st.i - 1));
    st.pos = getPosition(st.x, st.y);

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

// ----------------------------------------------------------------------
/*!
 * \brief Cut the line starting from a point on the edge of the rectangle
 *
 * Returns true if the whole line is inside.
 */
// ----------------------------------------------------------------------

bool Shape_rect::cutEdge(LineState& st, ShapeClipper& theClipper, bool exterior) const
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
    st.pos = getPosition(st.x, st.y);

    st.position |= st.pos;

    if (st.pos == Position::Inside)
      continue;
    if (st.pos != Position::Outside)  // on edge
    {
      if (!onSameEdge(prev_pos, st.pos))
        st.position |= Position::Inside;  // visited inside
      continue;
    }

    // Clip the outside point to edges
    clip_to_edges(st.x, st.y, st.g.getX(st.i - 1), st.g.getY(st.i - 1));

    if (st.x != st.g.getX(st.i) || st.y != st.g.getY(st.i))
      st.position |= Position::Inside;

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

Shape_rect::~Shape_rect() = default;

int Shape_rect::clip(const OGRLineString* theGeom, ShapeClipper& theClipper, bool exterior) const
{
  try
  {
    int position = 0;

    if (theGeom == nullptr)
      return position;

    int n = theGeom->getNumPoints();

    if (n < 1)
      return position;

    // For shorthand code

    // Keep a record of the point where a line segment entered
    // the rectangle. If the boolean is set, we must insert
    // the point to the beginning of the linestring which then
    // continues inside the rectangle.

    LineState st(*theGeom, n);

    // Start iterating

    while (st.i < n)
    {
      // Establish initial position

      st.x = st.g.getX(st.i);
      st.y = st.g.getY(st.i);
      st.pos = getPosition(st.x, st.y);

      // Update global status
      st.position |= st.pos;

      if (st.pos == Position::Outside)
      {
        if (!clipOutside(st, theClipper, exterior))
          return st.position;
      }
      else if (st.pos == Position::Inside)
      {
        if (clipInside(st, theClipper, exterior))
          return Position::Inside;
      }
      else if (clipEdge(st, theClipper, exterior))
        return st.position;
    }
    return st.position;
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

    if (theGeom == nullptr)
      return position;

    int n = theGeom->getNumPoints();

    if (n < 1)
      return position;

    // For shorthand code

    // Keep a record of the point where a line segment entered
    // the rectangle. If the boolean is set, we must insert
    // the point to the end of the linestring which began outside the rectangle.

    LineState st(*theGeom, n);

    // Start iterating

    while (st.i < n)
    {
      // Establish initial position

      st.x = st.g.getX(st.i);
      st.y = st.g.getY(st.i);
      st.pos = getPosition(st.x, st.y);

      st.position |= st.pos;

      if (st.pos == Position::Outside)
      {
        if (!cutOutside(st, theClipper, exterior))
          return st.position;
      }
      else if (st.pos == Position::Inside)
      {
        if (cutInside(st, theClipper, exterior))
          return st.position;
      }
      else if (cutEdge(st, theClipper, exterior))
        return st.position;
    }

    if (st.add_start)
    {
      auto *line = new OGRLineString();

      line->addPoint(st.start_x, st.start_y);
      line->addSubLineString(&st.g, st.start_index, n - 1);
#if 0
      std::cout << __LINE__ << " adding " << (exterior ? "ext: " : "int: ")
                << OGR::exportToWkt(*line) << "\n";
#endif
      theClipper.add(line, exterior);
    }

    return st.position;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

bool Shape_rect::connectPoints_cw(OGRLinearRing &ring,
                                  double x1,
                                  double y1,
                                  double x2,
                                  double y2,
                                  double theMaximumSegmentLength) const
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

bool Shape_rect::connectPoints_ccw(OGRLinearRing &ring,
                                   double x1,
                                   double y1,
                                   double x2,
                                   double y2,
                                   double theMaximumSegmentLength) const
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

int Shape_rect::getLineIntersectionPoints(double /* aX */,
                                          double /* aY */,
                                          double /* bX */,
                                          double /* bY */,
                                          double & /* pX1 */,
                                          double & /* pY1 */,
                                          double & /* pX2 */,
                                          double & /* pY2 */)
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
    return (OGR::inside(theRing, itsXMin, itsYMin) && OGR::inside(theRing, itsXMin, itsYMax) &&
            OGR::inside(theRing, itsXMax, itsYMin) && OGR::inside(theRing, itsXMax, itsYMax));
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

bool Shape_rect::isRingInside(const OGRLinearRing &theRing) const
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

OGRLinearRing *Shape_rect::makeRing(double theMaximumSegmentLength) const
{
  try
  {
    auto *ring = new OGRLinearRing;
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

OGRLinearRing *Shape_rect::makeHole(double theMaximumSegmentLength) const
{
  try
  {
    auto *ring = new OGRLinearRing;
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

namespace
{
// cw search along the lower edge
LineIterator cw_lower_edge(OGRLinearRing* ring,
                           std::list<OGRLineString*>& lines,
                           double x1,
                           double y1,
                           double& x2,
                           double& y2,
                           double itsXMin,
                           double itsYMin,
                           double itsXMax,
                           double itsYMax)
{
  auto best = lines.end();

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
  return best;
}

// cw search along the left edge
LineIterator cw_left_edge(OGRLinearRing* ring,
                          std::list<OGRLineString*>& lines,
                          double x1,
                          double y1,
                          double& x2,
                          double& y2,
                          double itsXMin,
                          double itsYMin,
                          double itsXMax,
                          double itsYMax)
{
  auto best = lines.end();

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
  return best;
}

// cw search along the top edge
LineIterator cw_top_edge(OGRLinearRing* ring,
                         std::list<OGRLineString*>& lines,
                         double x1,
                         double y1,
                         double& x2,
                         double& y2,
                         double itsXMin,
                         double itsYMin,
                         double itsXMax,
                         double itsYMax)
{
  auto best = lines.end();

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
  return best;
}

// cw search along the right edge
LineIterator cw_right_edge(OGRLinearRing* ring,
                           std::list<OGRLineString*>& lines,
                           double x1,
                           double y1,
                           double& x2,
                           double& y2,
                           double itsXMin,
                           double itsYMin,
                           double itsXMax,
                           double itsYMax)
{
  auto best = lines.end();

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
  return best;
}

// ccw search along the lower edge
LineIterator ccw_lower_edge(OGRLinearRing* ring,
                            std::list<OGRLineString*>& lines,
                            double x1,
                            double y1,
                            double& x2,
                            double& y2,
                            double itsXMin,
                            double itsYMin,
                            double itsXMax,
                            double itsYMax)
{
  auto best = lines.end();

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
  return best;
}

// ccw search along the left edge
LineIterator ccw_left_edge(OGRLinearRing* ring,
                           std::list<OGRLineString*>& lines,
                           double x1,
                           double y1,
                           double& x2,
                           double& y2,
                           double itsXMin,
                           double itsYMin,
                           double itsXMax,
                           double itsYMax)
{
  auto best = lines.end();

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
  return best;
}

// ccw search along the top edge
LineIterator ccw_top_edge(OGRLinearRing* ring,
                          std::list<OGRLineString*>& lines,
                          double x1,
                          double y1,
                          double& x2,
                          double& y2,
                          double itsXMin,
                          double itsYMin,
                          double itsXMax,
                          double itsYMax)
{
  auto best = lines.end();

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
  return best;
}

// ccw search along the right edge
LineIterator ccw_right_edge(OGRLinearRing* ring,
                            std::list<OGRLineString*>& lines,
                            double x1,
                            double y1,
                            double& x2,
                            double& y2,
                            double itsXMin,
                            double itsYMin,
                            double itsXMax,
                            double itsYMax)
{
  auto best = lines.end();

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
  return best;
}

}  // namespace

LineIterator Shape_rect::search_cw(OGRLinearRing* ring,
                                   std::list<OGRLineString*>& lines,
                                   double x1,
                                   double y1,
                                   double& x2,
                                   double& y2) const
{
  try
  {
    x2 = x1;
    y2 = y1;
    if (y1 == itsYMin && x1 > itsXMin)
      return cw_lower_edge(ring, lines, x1, y1, x2, y2, itsXMin, itsYMin, itsXMax, itsYMax);
    if (x1 == itsXMin && y1 < itsYMax)
      return cw_left_edge(ring, lines, x1, y1, x2, y2, itsXMin, itsYMin, itsXMax, itsYMax);
    if (y1 == itsYMax && x1 < itsXMax)
      return cw_top_edge(ring, lines, x1, y1, x2, y2, itsXMin, itsYMin, itsXMax, itsYMax);
    return cw_right_edge(ring, lines, x1, y1, x2, y2, itsXMin, itsYMin, itsXMax, itsYMax);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

LineIterator Shape_rect::search_ccw(OGRLinearRing *ring,
                                    std::list<OGRLineString *> &lines,
                                    double x1,
                                    double y1,
                                    double &x2,
                                    double &y2) const
{
  try
  {
    x2 = x1;
    y2 = y1;
    if (y1 == itsYMin && x1 < itsXMax)
      return ccw_lower_edge(ring, lines, x1, y1, x2, y2, itsXMin, itsYMin, itsXMax, itsYMax);
    if (x1 == itsXMin && y1 > itsYMin)
      return ccw_left_edge(ring, lines, x1, y1, x2, y2, itsXMin, itsYMin, itsXMax, itsYMax);
    if (y1 == itsYMax && x1 > itsXMin)
      return ccw_top_edge(ring, lines, x1, y1, x2, y2, itsXMin, itsYMin, itsXMax, itsYMax);
    return ccw_right_edge(ring, lines, x1, y1, x2, y2, itsXMin, itsYMin, itsXMax, itsYMax);
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
  return (x1 != x2 || y1 != y2);
}

void Shape_rect::reorientLines(std::list<OGRLineString *> &lines) const
{
  for (auto *line : lines)
  {
    int n = line->getNumPoints();
    if (n < 2)
      continue;
    double x1 = line->getX(0);
    double y1 = line->getY(0);
    double x2 = line->getX(n - 1);
    double y2 = line->getY(n - 1);
    // Same-edge: endpoints on same edge going against CCW direction
    const bool same_edge_wrong_dir =
        (x1 == itsXMin && x2 == itsXMin && y1 < y2) ||  // left edge going up
        (x1 == itsXMax && x2 == itsXMax && y1 > y2) ||  // right edge going down
        (y1 == itsYMin && y2 == itsYMin && x1 > x2) ||  // bottom edge going left
        (y1 == itsYMax && y2 == itsYMax && x1 < x2);    // top edge going right
    // Adjacent-edge corner cases: CCW from end reaches start via short path through a corner.
    // Reversing the line makes CCW take the long (correct) path that incorporates the box.
    const bool adjacent_edge_wrong_dir =
        (x2 == itsXMin && y1 == itsYMin) ||  // end on left, start on bottom -> BL corner
        (y2 == itsYMin && x1 == itsXMax) ||  // end on bottom, start on right -> BR corner
        (x2 == itsXMax && y1 == itsYMax) ||  // end on right, start on top -> TR corner
        (y2 == itsYMax && x1 == itsXMin);    // end on top, start on left -> TL corner
    if (same_edge_wrong_dir || adjacent_edge_wrong_dir)
      line->reversePoints();
  }
}

void Shape_rect::print(std::ostream &stream)
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
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

}  // namespace Fmi
