#include "GeometryBuilder.h"

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
  for (auto *ptr : itsPolygons)
    delete ptr;
  for (auto *ptr : itsLines)
    delete ptr;
  for (auto *ptr : itsPoints)
    delete ptr;
}

// ----------------------------------------------------------------------
/*!
 * \brief Add polygon to the final geometry
 */
// ----------------------------------------------------------------------

void GeometryBuilder::add(OGRPolygon *theGeom) { itsPolygons.push_back(theGeom); }

// ----------------------------------------------------------------------
/*!
 * \brief Add linestring to the final geometry
 */
// ----------------------------------------------------------------------
void GeometryBuilder::add(OGRLineString *theGeom)
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

// ----------------------------------------------------------------------
/*!
 * \brief Add point to the final geometry
 */
// ----------------------------------------------------------------------
void GeometryBuilder::add(OGRPoint *theGeom) { itsPoints.push_back(theGeom); }

// ----------------------------------------------------------------------
/*!
 * \brief Build final geometry from the elements
 */
// ----------------------------------------------------------------------

OGRGeometry *GeometryBuilder::build()
{
  // Total number of objects

  std::size_t n = itsPolygons.size() + itsLines.size() + itsPoints.size();

  // We wish to avoid nullptr pointers due to more prone segfault mistakes

  if (n == 0) return new OGRGeometryCollection;

  // Simplify to LineString, Polygon or Point if possible
  if (n == 1)
  {
    if (itsPolygons.size() == 1)
    {
      auto *ptr = itsPolygons.front();
      itsPolygons.clear();
      return ptr;
    }
    if (itsLines.size() == 1)
    {
      auto *ptr = itsLines.front();
      itsLines.clear();
      return ptr;
    }
    auto *ptr = itsPoints.front();
    itsPoints.clear();
    return ptr;
  }

  // Simplify to MultiLineString, MultiPolygon or MultiPoint if possible

  if (!itsPolygons.empty() && itsLines.empty() && itsPoints.empty())
  {
    auto *geom = new OGRMultiPolygon;
    for (auto *ptr : itsPolygons)
      geom->addGeometryDirectly(ptr);
    itsPolygons.clear();
    return geom;
  }

  if (!itsLines.empty() && itsPolygons.empty() && itsPoints.empty())
  {
    auto *geom = new OGRMultiLineString;
    for (auto *ptr : itsLines)
      geom->addGeometryDirectly(ptr);
    itsLines.clear();
    return geom;
  }

  if (!itsPoints.empty() && itsLines.empty() && itsPolygons.empty())
  {
    auto *geom = new OGRMultiPoint;
    for (auto *ptr : itsPoints)
      geom->addGeometryDirectly(ptr);
    itsPoints.clear();
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
    itsPolygons.clear();
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
    itsLines.clear();
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
    itsPoints.clear();
  }

  return geom;
}

}  // namespace Fmi
