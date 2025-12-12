#include "OGR.h"
#include <macgyver/Exception.h>
#include <ogr_geometry.h>

namespace
{
}  // namespace

// Forward declaration needed since two functions call each other
bool inside(const OGRGeometry *theGeom, double theX, double theY);

// ----------------------------------------------------------------------
/*!
 * \brief Handle LinearRing
 */
// ----------------------------------------------------------------------

bool inside(const OGRLinearRing *theGeom, double theX, double theY)
{
  try
  {
    if (theGeom == nullptr || theGeom->IsEmpty() != 0)
      return false;

    OGRPoint pt(theX, theY);
    return (theGeom->isPointInRing(&pt, 0) != 0);
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

bool inside(const OGRPolygon *theGeom, double theX, double theY)
{
  try
  {
    if (theGeom == nullptr || theGeom->IsEmpty() != 0)
      return false;

    return Fmi::OGR::inside(*theGeom, theX, theY);
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

bool inside(const OGRMultiPolygon *theGeom, double theX, double theY)
{
  try
  {
    if (theGeom == nullptr || theGeom->IsEmpty() != 0)
      return false;

    for (int i = 0, n = theGeom->getNumGeometries(); i < n; ++i)
    {
      if (inside(dynamic_cast<const OGRPolygon *>(theGeom->getGeometryRef(i)), theX, theY))
        return true;
    }
    return false;
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

bool inside(const OGRGeometryCollection *theGeom, double theX, double theY)
{
  try
  {
    if (theGeom == nullptr || theGeom->IsEmpty() != 0)
      return false;

    for (int i = 0, n = theGeom->getNumGeometries(); i < n; ++i)
    {
      if (inside(theGeom->getGeometryRef(i), theX, theY))
        return true;
    }
    return false;
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

bool Fmi::OGR::inside(const OGRPolygon &theGeom, double theX, double theY)
{
  try
  {
    // Outside if outside exterior ring
    if (!inside(theGeom.getExteriorRing(), theX, theY))
      return false;

    for (int i = 0, n = theGeom.getNumInteriorRings(); i < n; ++i)
    {
      // outside if inside hole
      if (inside(theGeom.getInteriorRing(i), theX, theY))
        return false;
    }

    return true;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Is the given coordinate inside the geometry?
 */
// ----------------------------------------------------------------------

bool Fmi::OGR::inside(const OGRGeometry &theGeom, double theX, double theY)
{
  try
  {
    OGRwkbGeometryType id = theGeom.getGeometryType();

    switch (id)
    {
      case wkbPoint:
      case wkbPoint25D:
      case wkbMultiPoint:
      case wkbMultiPoint25D:
      case wkbMultiLineString:
      case wkbMultiLineString25D:
        return false;
      case wkbLineString:
      case wkbLineString25D:
        if (strcmp(theGeom.getGeometryName(), "LINEARRING") != 0)
          return false;
        [[fallthrough]];
      case wkbLinearRing:
        return inside(dynamic_cast<const OGRLinearRing *>(&theGeom), theX, theY);
      case wkbPolygon:
      case wkbPolygon25D:
        return inside(dynamic_cast<const OGRPolygon *>(&theGeom), theX, theY);
      case wkbMultiPolygon:
      case wkbMultiPolygon25D:
        return inside(dynamic_cast<const OGRMultiPolygon *>(&theGeom), theX, theY);
      case wkbGeometryCollection:
      case wkbGeometryCollection25D:
        return inside(dynamic_cast<const OGRGeometryCollection *>(&theGeom), theX, theY);
      default:
        throw Fmi::Exception::Trace(
            BCP, "Encountered an unknown geometry component in OGR to SVG conversion");
    }
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Is the given coordinate inside the geometry?
 */
// ----------------------------------------------------------------------

bool inside(const OGRGeometry *theGeom, double theX, double theY)
{
  try
  {
    if (theGeom == nullptr || theGeom->IsEmpty() != 0)
      return false;

    return Fmi::OGR::inside(*theGeom, theX, theY);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}
