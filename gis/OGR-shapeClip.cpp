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

#include "GeometryBuilder.h"
#include "OGR.h"
#include "ShapeClipper.h"
#include "Shape_circle.h"
#include <macgyver/Exception.h>
#include <iomanip>
#include <iostream>
#include <list>

namespace Fmi
{
void do_point(const OGRPoint *theGeom,
              GeometryBuilder &theBuilder,
              const Shape_sptr &theShape,
              bool keep_inside)
{
  try
  {
    if (theGeom == nullptr)
      return;

    double x = theGeom->getX();
    double y = theGeom->getY();

    auto pos = theShape->getPosition(x, y);
    if (keep_inside)
    {
      if (pos == Shape::Inside)
        theBuilder.add(new OGRPoint(x, y));
    }
    else
    {
      if (pos == Shape::Outside)
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
 * \brief Clip or cut with circle
 */
// ----------------------------------------------------------------------
int do_circle(const OGRLineString *theGeom,
              ShapeClipper &theClipper,
              const Shape_sptr &theShape,
              bool keep_inside,
              bool exterior)
{
  try
  {
    int pos = 0;
    if (keep_inside)
      pos = theShape->clip(theGeom, theClipper, exterior);
    else
      pos = theShape->cut(theGeom, theClipper, exterior);

    return pos;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Clip polygon, do not close clipped ones
 */
// ----------------------------------------------------------------------

void do_polygon_to_linestrings(const OGRPolygon *theGeom,
                               GeometryBuilder &theBuilder,
                               Shape_sptr &theShape,
                               bool keep_inside)
{
  try
  {
    if (theGeom == nullptr || theGeom->IsEmpty() != 0)
      return;

    // Clip the exterior first to see what's going on

    ShapeClipper clipper(theShape, keep_inside);

    bool all_exterior = true;

    // If everything was in, just clone the original

    auto position =
        do_circle(theGeom->getExteriorRing(), clipper, theShape, keep_inside, all_exterior);

    if (Shape::all_only_inside(position))
    {
      // CLIP: - if all vertices inside box or on the edges, return input as is
      // CUT:  - if all vertices inside box or on the edges, return empty result
      if (keep_inside)
        theBuilder.add(dynamic_cast<OGRPolygon *>(theGeom->clone()));
      return;
    }

    if (Shape::all_not_inside(position))
    {
      // CLIP:
      // -if circle inside exterior, omit exterior
      // -if circle outside exterior, return empty result
      // CUT:
      // - if circle inside exterior, keep exterior and continue
      // - if circle outside exterior, return input as is

      bool insideRing = theShape->isInsideRing(*theGeom->getExteriorRing());

      if (keep_inside)
      {
        if (!insideRing)
          return;
      }
      else
      {
        if (!insideRing)
        {
          theBuilder.add(dynamic_cast<OGRPolygon *>(theGeom->clone()));
          return;
        }

        clipper.addExterior(dynamic_cast<OGRLinearRing *>(theGeom->getExteriorRing()->clone()));
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
      const auto *hole = theGeom->getInteriorRing(i);
      auto holeposition = do_circle(hole, clipper, theShape, keep_inside, all_exterior);

      if (Shape::all_only_inside(holeposition))
      {
        if (keep_inside)
          clipper.addExterior(dynamic_cast<OGRLinearRing *>(hole->clone()));
      }
      else if (Shape::all_not_inside(holeposition))
      {
        bool insideHole = theShape->isInsideRing(*hole);

        if (insideHole)
        {
          // If the box clip is inside a hole the result is empty
          // Otherwise we can keep the original input

          if (!keep_inside)
            theBuilder.add(dynamic_cast<OGRPolygon *>(theGeom->clone()));

          return;
        }

        // Otherwise the hole is outside the box
        if (!keep_inside)
          clipper.addExterior(dynamic_cast<OGRLinearRing *>(hole->clone()));
      }
    }

    clipper.reconnect();
    clipper.reconnectWithoutShape();
    clipper.release(theBuilder);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
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
                            Shape_sptr &theShape,
                            double max_length,
                            bool keep_inside)
{
  try
  {
    if (theGeom == nullptr || theGeom->IsEmpty() != 0)
      return;

    // Clip the exterior first to see what's going on

    ShapeClipper clipper(theShape, keep_inside);

    auto position = do_circle(theGeom->getExteriorRing(), clipper, theShape, keep_inside, true);

    if (Shape::all_not_outside(position))
    {
      // CLIP: - if all vertices inside the circle or on the edges, return input as is
      // CUT:  - if all vertices inside the circle or on the edges, return empty result
      if (keep_inside)
      {
        auto *poly = dynamic_cast<OGRPolygon *>(theGeom->clone());
        OGR::normalize(*poly);
        theBuilder.add(poly);
      }

      return;
    }

    if (Shape::all_not_inside(position))
    {
      // CLIP:
      // -if the circle inside exterior, the circle becomes part of new exterior ; ADD_CIRCLE
      // -if the circle outside exterior, return empty result
      // CUT:
      // - if the circle inside exterior, the circle becomes part of new holes ; ADD_CIRCLE
      // - if the circle outside exterior, return input as is

      bool insideRing = theShape->isInsideRing(*theGeom->getExteriorRing());

      if (keep_inside)
      {
        if (!insideRing)
          return;

        clipper.addShape();
      }
      else
      {
        if (!insideRing)
        {
          theBuilder.add(dynamic_cast<OGRPolygon *>(theGeom->clone()));
          return;
        }
        // the circle is inside exterior, must keep exterior
        clipper.addExterior(dynamic_cast<OGRLinearRing *>(theGeom->getExteriorRing()->clone()));
        clipper.addShape();
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
      const auto *hole = theGeom->getInteriorRing(i);

      auto holeposition = do_circle(hole, clipper, theShape, keep_inside, false);

      if (Shape::all_only_inside(holeposition))
      {
        if (keep_inside)
          clipper.addInterior(dynamic_cast<OGRLinearRing *>(hole->clone()));
      }
      else if (Shape::all_not_inside(holeposition))
      {
        bool insideHole = theShape->isInsideRing(*hole);

        if (insideHole)
        {
          // If the box clip is inside a hole the result is empty
          // Otherwise we can keep the original input
          if (!keep_inside)
            theBuilder.add(dynamic_cast<OGRPolygon *>(theGeom->clone()));
          return;
        }

        // Otherwise the hole is outside the circle
        if (!keep_inside)
          clipper.addInterior(dynamic_cast<OGRLinearRing *>(hole->clone()));
      }
    }

    clipper.reconnect();                     // trivial reconnect of endpoints
    clipper.reconnectWithShape(max_length);  // reconnect along circle boundaries
    clipper.release(theBuilder);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

void do_polygon(const OGRPolygon *theGeom,
                GeometryBuilder &theBuilder,
                Shape_sptr &theShape,
                double max_length,
                bool keep_polygons,
                bool keep_inside)
{
  try
  {
    if (keep_polygons)
      do_polygon_to_polygons(theGeom, theBuilder, theShape, max_length, keep_inside);
    else
      do_polygon_to_linestrings(theGeom, theBuilder, theShape, keep_inside);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

void do_linestring(const OGRLineString *theGeom,
                   GeometryBuilder &theBuilder,
                   Shape_sptr &theShape,
                   bool keep_inside)
{
  try
  {
    if (theGeom == nullptr || theGeom->IsEmpty() != 0)
      return;

    // If everything was in, just clone the original when clipping

    ShapeClipper clipper(theShape, keep_inside);
    auto position = do_circle(theGeom, clipper, theShape, keep_inside, true);

    if (Shape::all_only_inside(position))
    {
      if (keep_inside)
        theBuilder.add(dynamic_cast<OGRLineString *>(theGeom->clone()));
    }
    else if (Shape::all_only_outside(position))
    {
      if (!keep_inside)
        theBuilder.add(dynamic_cast<OGRLineString *>(theGeom->clone()));
    }
    else
    {
      clipper.reconnect();
      clipper.reconnectWithoutShape();
      clipper.release(theBuilder);
    }
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

void do_multipoint(const OGRMultiPoint *theGeom,
                   GeometryBuilder &theBuilder,
                   Shape_sptr &theShape,
                   bool keep_inside)
{
  try
  {
    if (theGeom == nullptr || theGeom->IsEmpty() != 0)
      return;

    for (int i = 0, n = theGeom->getNumGeometries(); i < n; ++i)
    {
      do_point(dynamic_cast<const OGRPoint *>(theGeom->getGeometryRef(i)),
               theBuilder,
               theShape,
               keep_inside);
    }
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

void do_multilinestring(const OGRMultiLineString *theGeom,
                        GeometryBuilder &theBuilder,
                        Shape_sptr &theShape,
                        bool keep_inside)
{
  try
  {
    if (theGeom == nullptr || theGeom->IsEmpty() != 0)
      return;

    for (int i = 0, n = theGeom->getNumGeometries(); i < n; ++i)
    {
      do_linestring(dynamic_cast<const OGRLineString *>(theGeom->getGeometryRef(i)),
                    theBuilder,
                    theShape,
                    keep_inside);
    }
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

void do_multipolygon(const OGRMultiPolygon *theGeom,
                     GeometryBuilder &theBuilder,
                     Shape_sptr &theShape,
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
      do_polygon(dynamic_cast<const OGRPolygon *>(theGeom->getGeometryRef(i)),
                 theBuilder,
                 theShape,
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

// Needed since two functions call each other
void do_geom(const OGRGeometry *theGeom,
             GeometryBuilder &theBuilder,
             Shape_sptr &theShape,
             double max_length,
             bool keep_polygons,
             bool keep_inside);

void do_geometrycollection(const OGRGeometryCollection *theGeom,
                           GeometryBuilder &theBuilder,
                           Shape_sptr &theShape,
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
          theGeom->getGeometryRef(i), theBuilder, theShape, max_length, keep_polygons, keep_inside);
    }
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

void do_geom(const OGRGeometry *theGeom,
             GeometryBuilder &theBuilder,
             Shape_sptr &theShape,
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
        return do_point(dynamic_cast<const OGRPoint *>(theGeom), theBuilder, theShape, keep_inside);
      case wkbLineString:
        return do_linestring(
            dynamic_cast<const OGRLineString *>(theGeom), theBuilder, theShape, keep_inside);
      case wkbPolygon:
        return do_polygon(dynamic_cast<const OGRPolygon *>(theGeom),
                          theBuilder,
                          theShape,
                          max_length,
                          keep_polygons,
                          keep_inside);
      case wkbMultiPoint:
        return do_multipoint(
            dynamic_cast<const OGRMultiPoint *>(theGeom), theBuilder, theShape, keep_inside);
      case wkbMultiLineString:
        return do_multilinestring(
            dynamic_cast<const OGRMultiLineString *>(theGeom), theBuilder, theShape, keep_inside);
      case wkbMultiPolygon:
        return do_multipolygon(dynamic_cast<const OGRMultiPolygon *>(theGeom),
                               theBuilder,
                               theShape,
                               max_length,
                               keep_polygons,
                               keep_inside);
      case wkbGeometryCollection:
        return do_geometrycollection(dynamic_cast<const OGRGeometryCollection *>(theGeom),
                                     theBuilder,
                                     theShape,
                                     max_length,
                                     keep_polygons,
                                     keep_inside);
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

OGRGeometry *OGR::lineclip(const OGRGeometry &theGeom, Shape_sptr &theShape)
{
  try
  {
    bool keep_polygons = false;
    bool keep_inside = true;

    GeometryBuilder builder;
    do_geom(&theGeom, builder, theShape, 0, keep_polygons, keep_inside);

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

void OGR::polyclip(GeometryBuilder &builder,
                   const OGRGeometry &theGeom,
                   Shape_sptr &theShape,
                   double theMaxSegmentLength)
{
  try
  {
    bool keep_polygons = true;
    bool keep_inside = true;

    do_geom(&theGeom, builder, theShape, theMaxSegmentLength, keep_polygons, keep_inside);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

OGRGeometry *OGR::polyclip(const OGRGeometry &theGeom,
                           Shape_sptr &theShape,
                           double theMaxSegmentLength)
{
  try
  {
    GeometryBuilder builder;
    polyclip(builder, theGeom, theShape, theMaxSegmentLength);

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

OGRGeometry *OGR::linecut(const OGRGeometry &theGeom, Shape_sptr &theShape)
{
  try
  {
    bool keep_polygons = false;
    bool keep_inside = false;

    GeometryBuilder builder;
    do_geom(&theGeom, builder, theShape, 0, keep_polygons, keep_inside);

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

void OGR::polycut(GeometryBuilder &builder,
                  const OGRGeometry &theGeom,
                  Shape_sptr &theShape,
                  double theMaxSegmentLength)
{
  try
  {
    bool keep_polygons = true;
    bool keep_inside = false;

    do_geom(&theGeom, builder, theShape, theMaxSegmentLength, keep_polygons, keep_inside);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

OGRGeometry *OGR::polycut(const OGRGeometry &theGeom,
                          Shape_sptr &theShape,
                          double theMaxSegmentLength)
{
  try
  {
    GeometryBuilder builder;
    polycut(builder, theGeom, theShape, theMaxSegmentLength);

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
