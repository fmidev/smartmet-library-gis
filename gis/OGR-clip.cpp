#include "Box.h"
#include "ClipParts.h"
#include "OGR.h"
#include <iomanip>
#include <list>
#include <stdexcept>

using Fmi::Box;
using Fmi::ClipParts;

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

void clip_point(const OGRPoint *theGeom, ClipParts &theParts, const Box &theBox)
{
  if (theGeom == nullptr)
    return;

  double x = theGeom->getX();
  double y = theGeom->getY();

  if (theBox.position(x, y) == Box::Inside)
    theParts.add(new OGRPoint(x, y));
}

// ----------------------------------------------------------------------
/*!
 * \brief Clip OGR geometry.
 *
 * Returns true if the geometry was fully inside, and does not output
 * anything to ClipParts.
 */
// ----------------------------------------------------------------------

bool clip_linestring_parts(const OGRLineString *theGeom, ClipParts &theParts, const Box &theBox)
{
  int n = theGeom->getNumPoints();

  if (theGeom == nullptr || n < 1)
    return false;

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
        return false;

      // Establish new position

      x = g.getX(i);
      y = g.getY(i);
      pos = theBox.position(x, y);

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

            if (through_box)
              line->addPoint(x, y);

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
      if (start_index == 0 && i >= n)
        return true;

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

  return false;
}

// ----------------------------------------------------------------------
/*!
 * \brief Clip polygon, do not close clipped ones
 */
// ----------------------------------------------------------------------

void clip_polygon_to_linestrings(const OGRPolygon *theGeom,
                                 Fmi::ClipParts &theParts,
                                 const Box &theBox)
{
  if (theGeom == nullptr || theGeom->IsEmpty() != 0)
    return;

  // Clip the exterior first to see what's going on

  Fmi::ClipParts parts;

  // If everything was in, just clone the original

  if (clip_linestring_parts(theGeom->getExteriorRing(), parts, theBox))
  {
    theParts.add(dynamic_cast<OGRPolygon *>(theGeom->clone()));
    return;
  }

  // Now, if parts is empty, our rectangle may be inside the polygon
  // If not, holes are outside too

  if (parts.empty())
  {
    // We could now check whether the rectangle is inside the outer
    // ring to avoid checking the holes. However, if holes are much
    // smaller than the exterior ring just checking the holes
    // separately could be faster.

    if (theGeom->getNumInteriorRings() == 0)
      return;

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

  // Handle the holes now:
  // - Clipped ones become linestrings
  // - Intact ones become new polygons without holes

  for (int i = 0, n = theGeom->getNumInteriorRings(); i < n; ++i)
  {
    auto *hole = theGeom->getInteriorRing(i);
    if (clip_linestring_parts(hole, parts, theBox))
    {
      // becomes exterior
      auto *poly = new OGRPolygon;
      poly->addRing(const_cast<OGRLinearRing *>(hole));  // clones
      theParts.add(poly);
    }
    else if (!parts.empty())
    {
      parts.reconnect();
      parts.release(theParts);
    }
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

bool inside(const OGRLinearRing *theGeom, const Fmi::Box &theBox)
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
                              const Box &theBox)
{
  if (theGeom == nullptr || theGeom->IsEmpty() != 0)
    return;

  // Clip the exterior first to see what's going on

  Fmi::ClipParts parts;

  // If everything was in, just clone the original

  if (clip_linestring_parts(theGeom->getExteriorRing(), parts, theBox))
  {
    theParts.add(dynamic_cast<OGRPolygon *>(theGeom->clone()));
    return;
  }

  // If there were no intersections, the outer ring might be
  // completely outside.

  if (parts.empty() && !Fmi::OGR::inside(*theGeom->getExteriorRing(), theBox.xmin(), theBox.ymin()))
  {
    return;
  }

  // If the exterior does not intersect the bounding box at all, it must
  // be surrounding the bbox. The exterior is thus shrunk into the bbox.
  // Any hole in it remains a hole, except when it intersects the bbox, in
  // which case it becomes part of the exterior.

  bool exterior_clipped = !parts.empty();

  bool ext_clockwise = (theGeom->getExteriorRing()->isClockwise() == 1);

  // Force exterior to be clockwise
  if (!ext_clockwise)
    parts.reverseLines();

  // Must do this to make sure all end points are on the edges

  parts.reconnect();

  // Handle the holes now:
  // - Clipped ones become part of the exterior
  // - Intact ones become holes in new polygons formed by exterior parts
  //   if they are completely inside the clipping box, otherwise they
  //   must be completely outside the clipping box
  // However, if we find a hole surrounding the clipping box, there is
  // no output at all.

  int holes_clipped = 0;
  int holes_inside = 0;
  int holes_outside = 0;

  for (int i = 0, n = theGeom->getNumInteriorRings(); i < n; ++i)
  {
    Fmi::ClipParts holeparts;
    auto *hole = theGeom->getInteriorRing(i);
    bool hole_clockwise = (hole->isClockwise() == 1);

    if (clip_linestring_parts(hole, holeparts, theBox))
    {
      ++holes_inside;
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
      parts.add(poly);
    }
    else
    {
      if (!holeparts.empty())
      {
        ++holes_clipped;
        // Clipped holes change orientation when connecting the edges!!
        if (hole_clockwise)
          holeparts.reverseLines();
        holeparts.reconnect();
        holeparts.release(parts);
      }
      else
      {
        // No intersections with the outside hole, but we may be inside it!
        ++holes_outside;
        if (Fmi::OGR::inside(*theGeom->getInteriorRing(i), theBox.xmin(), theBox.ymin()))
        {
          // Box is completely inside the hole, the intersection must be empty
          return;
        }
      }
    }
  }

#if 0
  std::cout << "Rebuilding:\n"
            << "\text_intersects = " << exterior_clipped << "\n"
            << "\text cw = " << ext_clockwise << "\n"
            << "\tinside = " << holes_inside << "\n"
            << "\toutside = " << holes_outside << "\n"
            << "\tclipped = " << holes_clipped << std::endl;
#endif

  // Must now add the bbox as the new exterior if neither the exterior nor any
  // of the holes intersected the box, and the holes need an exterior.
  bool add_exterior = (!exterior_clipped && holes_clipped == 0);

  parts.reconnectPolygons(theBox, add_exterior);
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
                  bool keep_polygons)
{
  if (keep_polygons)
    clip_polygon_to_polygons(theGeom, theParts, theBox);
  else
    clip_polygon_to_linestrings(theGeom, theParts, theBox);
}

// ----------------------------------------------------------------------
/*!
 * \brief Clip OGR geometry
 */
// ----------------------------------------------------------------------

void clip_linestring(const OGRLineString *theGeom, Fmi::ClipParts &theParts, const Box &theBox)
{
  if (theGeom == nullptr || theGeom->IsEmpty() != 0)
    return;

  // If everything was in, just clone the original

  if (clip_linestring_parts(theGeom, theParts, theBox))
    theParts.add(dynamic_cast<OGRLineString *>(theGeom->clone()));
}

// ----------------------------------------------------------------------
/*!
 * \brief Clip OGR geometry
 */
// ----------------------------------------------------------------------

void clip_multipoint(const OGRMultiPoint *theGeom, ClipParts &theParts, const Box &theBox)
{
  if (theGeom == nullptr || theGeom->IsEmpty() != 0)
    return;
  for (int i = 0, n = theGeom->getNumGeometries(); i < n; ++i)
  {
    clip_point(dynamic_cast<const OGRPoint *>(theGeom->getGeometryRef(i)), theParts, theBox);
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Clip OGR geometry
 */
// ----------------------------------------------------------------------

void clip_multilinestring(const OGRMultiLineString *theGeom, ClipParts &theParts, const Box &theBox)
{
  if (theGeom == nullptr || theGeom->IsEmpty() != 0)
    return;

  for (int i = 0, n = theGeom->getNumGeometries(); i < n; ++i)
  {
    clip_linestring(
        dynamic_cast<const OGRLineString *>(theGeom->getGeometryRef(i)), theParts, theBox);
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
                       bool keep_polygons)
{
  if (theGeom == nullptr || theGeom->IsEmpty() != 0)
    return;

  for (int i = 0, n = theGeom->getNumGeometries(); i < n; ++i)
  {
    clip_polygon(dynamic_cast<const OGRPolygon *>(theGeom->getGeometryRef(i)),
                 theParts,
                 theBox,
                 keep_polygons);
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
               bool keep_polygons);

void clip_geometrycollection(const OGRGeometryCollection *theGeom,
                             ClipParts &theParts,
                             const Box &theBox,
                             bool keep_polygons)
{
  if (theGeom == nullptr || theGeom->IsEmpty() != 0)
    return;

  for (int i = 0, n = theGeom->getNumGeometries(); i < n; ++i)
  {
    clip_geom(theGeom->getGeometryRef(i), theParts, theBox, keep_polygons);
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
               bool keep_polygons)
{
  OGRwkbGeometryType id = theGeom->getGeometryType();

  switch (id)
  {
    case wkbPoint:
      return clip_point(dynamic_cast<const OGRPoint *>(theGeom), theParts, theBox);
    case wkbLineString:
      return clip_linestring(dynamic_cast<const OGRLineString *>(theGeom), theParts, theBox);
    case wkbPolygon:
      return clip_polygon(
          dynamic_cast<const OGRPolygon *>(theGeom), theParts, theBox, keep_polygons);
    case wkbMultiPoint:
      return clip_multipoint(dynamic_cast<const OGRMultiPoint *>(theGeom), theParts, theBox);
    case wkbMultiLineString:
      return clip_multilinestring(
          dynamic_cast<const OGRMultiLineString *>(theGeom), theParts, theBox);
    case wkbMultiPolygon:
      return clip_multipolygon(
          dynamic_cast<const OGRMultiPolygon *>(theGeom), theParts, theBox, keep_polygons);
    case wkbGeometryCollection:
      return clip_geometrycollection(
          dynamic_cast<const OGRGeometryCollection *>(theGeom), theParts, theBox, keep_polygons);
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
  ClipParts parts;

  bool keep_polygons = false;
  clip_geom(&theGeom, parts, theBox, keep_polygons);

  OGRGeometry *geom = parts.build();
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

OGRGeometry *Fmi::OGR::polyclip(const OGRGeometry &theGeom, const Box &theBox)
{
  ClipParts parts;

  bool keep_polygons = true;
  clip_geom(&theGeom, parts, theBox, keep_polygons);

  OGRGeometry *geom = parts.build();
  if (geom != nullptr)
    geom->assignSpatialReference(theGeom.getSpatialReference());

  return geom;
}
