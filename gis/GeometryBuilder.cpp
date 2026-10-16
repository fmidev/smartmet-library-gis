#include "GeometryBuilder.h"

#include <macgyver/Exception.h>
#include <iostream>

namespace Fmi
{
// ----------------------------------------------------------------------
/*!
 * \brief The destructor cleans up the data just in case build() was not called
 */
// ----------------------------------------------------------------------

GeometryBuilder::~GeometryBuilder()
{
  try
  {
    for (auto *ptr : itsPolygons)
      delete ptr;
    for (auto *ptr : itsLines)
      delete ptr;
    for (auto *ptr : itsPoints)
      delete ptr;
  }
  catch (...)
  {
    Fmi::Exception exception(BCP, "Destructor failed", nullptr);
    exception.printError();
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Add polygon to the final geometry
 */
// ----------------------------------------------------------------------

void GeometryBuilder::add(OGRPolygon *theGeom)
{
  try
  {
    itsPolygons.push_back(theGeom);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Add linestring to the final geometry
 */
// ----------------------------------------------------------------------
void GeometryBuilder::add(OGRLineString *theGeom)
{
  try
  {
    auto n = theGeom->getNumPoints();
    if (n == 0)
    {
      // Should not happen, but handling if anyway
      delete theGeom;
    }
    else if (n == 1)
    {
      // This may happen when a line touches an an edge and then turns away again
      auto x = theGeom->getX(0);
      auto y = theGeom->getY(0);
      delete theGeom;
      auto *point = new OGRPoint(x, y);
      add(point);
    }
    else
      itsLines.push_back(theGeom);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Add point to the final geometry
 */
// ----------------------------------------------------------------------
void GeometryBuilder::add(OGRPoint *theGeom)
{
  try
  {
    itsPoints.push_back(theGeom);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

namespace
{
// Take the only element out of the container
template <typename Container>
OGRGeometry *take_front(Container &items)
{
  auto *ptr = items.front();
  items.clear();
  return ptr;
}

// Move all the elements of the container into a new multi-geometry
template <typename Multi, typename Container>
Multi *take_multi(Container &items)
{
  auto *geom = new Multi;
  for (auto *ptr : items)
    geom->addGeometryDirectly(ptr);
  items.clear();
  return geom;
}

// Move the elements of the container into the collection, as a multi-geometry if there are
// several of them
template <typename Multi, typename Container>
void add_single_or_multi(OGRGeometryCollection &geom, Container &items)
{
  if (items.empty())
    return;

  if (items.size() == 1)
  {
    geom.addGeometryDirectly(items.front());
    items.clear();
  }
  else
    geom.addGeometryDirectly(take_multi<Multi>(items));
}
}  // namespace

// ----------------------------------------------------------------------
/*!
 * \brief Build final geometry from the elements
 */
// ----------------------------------------------------------------------

OGRGeometry *GeometryBuilder::build()
{
  try
  {
    // Total number of objects

    std::size_t n = itsPolygons.size() + itsLines.size() + itsPoints.size();

    // We wish to avoid nullptr pointers due to more prone segfault mistakes

    if (n == 0)
      return new OGRGeometryCollection;

    // Simplify to LineString, Polygon or Point if possible
    if (n == 1)
    {
      if (itsPolygons.size() == 1)
        return take_front(itsPolygons);
      if (itsLines.size() == 1)
        return take_front(itsLines);
      return take_front(itsPoints);
    }

    // Simplify to MultiLineString, MultiPolygon or MultiPoint if possible

    if (!itsPolygons.empty() && itsLines.empty() && itsPoints.empty())
      return take_multi<OGRMultiPolygon>(itsPolygons);

    if (!itsLines.empty() && itsPolygons.empty() && itsPoints.empty())
      return take_multi<OGRMultiLineString>(itsLines);

    if (!itsPoints.empty() && itsLines.empty() && itsPolygons.empty())
      return take_multi<OGRMultiPoint>(itsPoints);

    // A generic collection must be used, the types differ too much.
    // The order should be kept the same for the benefit of
    // regression tests: polygons, lines, points

    auto *geom = new OGRGeometryCollection;

    // Add a Polygon or a MultiPolygon
    add_single_or_multi<OGRMultiPolygon>(*geom, itsPolygons);

    // Add a LineString or a MultiLineString
    add_single_or_multi<OGRMultiLineString>(*geom, itsLines);

    // Add a Point or a MultiPoint
    add_single_or_multi<OGRMultiPoint>(*geom, itsPoints);

    return geom;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

}  // namespace Fmi
