#include "Box.h"
#include "OGR.h"
#include <macgyver/Exception.h>
#include <ogr_geometry.h>

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
  try
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
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Handle LinearRing
 */
// ----------------------------------------------------------------------

void tr(OGRLinearRing *geom, const Box &box)
{
  try
  {
    if (geom == nullptr || geom->IsEmpty() != 0)
      return;

    const int n = geom->getNumPoints();

    for (int i = 0; i < n; ++i)
    {
      double x = geom->getX(i);
      double y = geom->getY(i);
      box.transform(x, y);
      geom->setPoint(i, x, y);
    }
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Handle LineString
 */
// ----------------------------------------------------------------------

void tr(OGRLineString *geom, const Box &box)
{
  try
  {
    if (geom == nullptr || geom->IsEmpty() != 0)
      return;

    const int n = geom->getNumPoints();

    for (int i = 0; i < n; ++i)
    {
      double x = geom->getX(i);
      double y = geom->getY(i);
      box.transform(x, y);
      geom->setPoint(i, x, y);
    }
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Handle Polygon
 */
// ----------------------------------------------------------------------

void tr(OGRPolygon *geom, const Box &box)
{
  try
  {
    if (geom == nullptr || geom->IsEmpty() != 0)
      return;

    tr(geom->getExteriorRing(), box);
    for (int i = 0, n = geom->getNumInteriorRings(); i < n; ++i)
      tr(geom->getInteriorRing(i), box);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Handle MultiPoint
 */
// ----------------------------------------------------------------------

void tr(OGRMultiPoint *geom, const Box &box)
{
  try
  {
    if (geom == nullptr || geom->IsEmpty() != 0)
      return;

    for (int i = 0, n = geom->getNumGeometries(); i < n; ++i)
      tr(dynamic_cast<OGRPoint *>(geom->getGeometryRef(i)), box);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Handle MultiLineString
 */
// ----------------------------------------------------------------------

void tr(OGRMultiLineString *geom, const Box &box)
{
  try
  {
    if (geom == nullptr || geom->IsEmpty() != 0)
      return;

    for (int i = 0, n = geom->getNumGeometries(); i < n; ++i)
      tr(dynamic_cast<OGRLineString *>(geom->getGeometryRef(i)), box);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Handle MultiPolygon
 */
// ----------------------------------------------------------------------

void tr(OGRMultiPolygon *geom, const Box &box)
{
  try
  {
    if (geom == nullptr || geom->IsEmpty() != 0)
      return;

    for (int i = 0, n = geom->getNumGeometries(); i < n; ++i)
      tr(dynamic_cast<OGRPolygon *>(geom->getGeometryRef(i)), box);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Handle GeometryCollection
 */
// ----------------------------------------------------------------------

void tr(OGRGeometryCollection *geom, const Box &box)
{
  try
  {
    if (geom == nullptr || geom->IsEmpty() != 0)
      return;

    for (int i = 0, n = geom->getNumGeometries(); i < n; ++i)
      tr(geom->getGeometryRef(i), box);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
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
  try
  {
    OGRwkbGeometryType id = geom->getGeometryType();

    switch (id)
    {
      case wkbPoint:
      {
        tr(dynamic_cast<OGRPoint *>(geom), box);
        return;
      }
      case wkbLineString:
      {
        tr(dynamic_cast<OGRLineString *>(geom), box);
        return;
      }
      case wkbLinearRing:
      {
        tr(dynamic_cast<OGRLinearRing *>(geom), box);
        return;
      }
      case wkbPolygon:
      {
        tr(dynamic_cast<OGRPolygon *>(geom), box);
        return;
      }
      case wkbMultiPoint:
      {
        tr(dynamic_cast<OGRMultiPoint *>(geom), box);
        return;
      }
      case wkbMultiLineString:
      {
        tr(dynamic_cast<OGRMultiLineString *>(geom), box);
        return;
      }
      case wkbMultiPolygon:
      {
        tr(dynamic_cast<OGRMultiPolygon *>(geom), box);
        return;
      }
      case wkbGeometryCollection:
      {
        tr(dynamic_cast<OGRGeometryCollection *>(geom), box);
        return;
      }
      default:
        throw Fmi::Exception::Trace(
            BCP, "Encountered an unknown geometry component in OGRGeometry transform call");
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
 * \brief Convert the geometry to a SVG string
 */
// ----------------------------------------------------------------------

namespace Fmi
{
namespace OGR
{
void transform(OGRGeometry &theGeom, const Box &theBox)
{
  try
  {
    tr(&theGeom, theBox);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}
}  // namespace OGR
}  // namespace Fmi
