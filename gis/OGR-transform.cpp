#include "Box.h"
#include "OGR.h"

#include <ogr_geometry.h>
#include <stdexcept>

using Fmi::Box;

// Note: At the time of writing OGRDefaultGeometryVisitor was not available in RHEL7

namespace
{
// Forward declaration needed since two functions call each other

void tr(OGRGeometry *geom, const Box &box);

// ----------------------------------------------------------------------
/*!
 * \brief Handle a Point
 */
// ----------------------------------------------------------------------

void tr(OGRPoint *geom, const Box &box)
{
  if (geom != nullptr)
  {
    double x = geom->getX();
    double y = geom->getY();
    box.transform(x, y);
    geom->setX(x);
    geom->setY(y);
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Handle LinearRing
 */
// ----------------------------------------------------------------------

void tr(OGRLinearRing *geom, const Box &box)
{
  if (geom == nullptr || geom->IsEmpty() != 0) return;

  const int n = geom->getNumPoints();

  for (int i = 0; i < n; ++i)
  {
    double x = geom->getX(i);
    double y = geom->getY(i);
    box.transform(x, y);
    geom->setPoint(i, x, y);
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Handle LineString
 */
// ----------------------------------------------------------------------

void tr(OGRLineString *geom, const Box &box)
{
  if (geom == nullptr || geom->IsEmpty() != 0) return;

  const int n = geom->getNumPoints();

  for (int i = 0; i < n; ++i)
  {
    double x = geom->getX(i);
    double y = geom->getY(i);
    box.transform(x, y);
    geom->setPoint(i, x, y);
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Handle Polygon
 */
// ----------------------------------------------------------------------

void tr(OGRPolygon *geom, const Box &box)
{
  if (geom == nullptr || geom->IsEmpty() != 0) return;

  tr(geom->getExteriorRing(), box);
  for (int i = 0, n = geom->getNumInteriorRings(); i < n; ++i)
    tr(geom->getInteriorRing(i), box);
}

// ----------------------------------------------------------------------
/*!
 * \brief Handle MultiPoint
 */
// ----------------------------------------------------------------------

void tr(OGRMultiPoint *geom, const Box &box)
{
  if (geom == nullptr || geom->IsEmpty() != 0) return;

  for (int i = 0, n = geom->getNumGeometries(); i < n; ++i)
    tr(dynamic_cast<OGRPoint *>(geom->getGeometryRef(i)), box);
}

// ----------------------------------------------------------------------
/*!
 * \brief Handle MultiLineString
 */
// ----------------------------------------------------------------------

void tr(OGRMultiLineString *geom, const Box &box)
{
  if (geom == nullptr || geom->IsEmpty() != 0) return;
  for (int i = 0, n = geom->getNumGeometries(); i < n; ++i)
    tr(dynamic_cast<OGRLineString *>(geom->getGeometryRef(i)), box);
}

// ----------------------------------------------------------------------
/*!
 * \brief Handle MultiPolygon
 */
// ----------------------------------------------------------------------

void tr(OGRMultiPolygon *geom, const Box &box)
{
  if (geom == nullptr || geom->IsEmpty() != 0) return;
  for (int i = 0, n = geom->getNumGeometries(); i < n; ++i)
    tr(dynamic_cast<OGRPolygon *>(geom->getGeometryRef(i)), box);
}

// ----------------------------------------------------------------------
/*!
 * \brief Handle GeometryCollection
 */
// ----------------------------------------------------------------------

void tr(OGRGeometryCollection *geom, const Box &box)
{
  if (geom == nullptr || geom->IsEmpty() != 0) return;
  for (int i = 0, n = geom->getNumGeometries(); i < n; ++i)
    tr(geom->getGeometryRef(i), box);
}

// ----------------------------------------------------------------------
/*!
 * \brief Write the geometry as a SVG path
 *
 * This could be more efficiently done if the geometries supported
 * a visitor.
 */
// ----------------------------------------------------------------------

void tr(OGRGeometry *geom, const Box &box)
{
  OGRwkbGeometryType id = geom->getGeometryType();

  switch (id)
  {
    case wkbPoint:
      return tr(dynamic_cast<OGRPoint *>(geom), box);
    case wkbLineString:
      return tr(dynamic_cast<OGRLineString *>(geom), box);
    case wkbLinearRing:
      return tr(dynamic_cast<OGRLinearRing *>(geom), box);
    case wkbPolygon:
      return tr(dynamic_cast<OGRPolygon *>(geom), box);
    case wkbMultiPoint:
      return tr(dynamic_cast<OGRMultiPoint *>(geom), box);
    case wkbMultiLineString:
      return tr(dynamic_cast<OGRMultiLineString *>(geom), box);
    case wkbMultiPolygon:
      return tr(dynamic_cast<OGRMultiPolygon *>(geom), box);
    case wkbGeometryCollection:
      return tr(dynamic_cast<OGRGeometryCollection *>(geom), box);
    default:
      throw std::runtime_error(
          "Encountered an unknown geometry component in OGRGeometry transform call");
  }
}

}  // namespace

// ----------------------------------------------------------------------
/*!
 * \brief Convert the geometry to a SVG string
 */
// ----------------------------------------------------------------------

namespace Fmi
{
namespace OGR
{
void transform(OGRGeometry &theGeom, const Box &theBox) { tr(&theGeom, theBox); }
}  // namespace OGR
}  // namespace Fmi
