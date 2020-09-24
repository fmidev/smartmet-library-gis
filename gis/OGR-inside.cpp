#include "OGR.h"
#include <ogr_geometry.h>
#include <stdexcept>

// Forward declaration needed since two functions call each other
bool inside(const OGRGeometry *theGeom, double theX, double theY);

// ----------------------------------------------------------------------
/*!
 * \brief Handle LinearRing
 */
// ----------------------------------------------------------------------

bool inside(const OGRLinearRing *theGeom, double theX, double theY)
{
  if (theGeom == nullptr || theGeom->IsEmpty() != 0) return false;

  OGRPoint pt(theX, theY);
  return (theGeom->isPointInRing(&pt, 0) != 0);
}

// ----------------------------------------------------------------------
/*!
 * \brief Handle Polygon
 */
// ----------------------------------------------------------------------

bool inside(const OGRPolygon *theGeom, double theX, double theY)
{
  if (theGeom == nullptr || theGeom->IsEmpty() != 0) return false;

  return Fmi::OGR::inside(*theGeom, theX, theY);
}

// ----------------------------------------------------------------------
/*!
 * \brief Handle MultiPolygon
 */
// ----------------------------------------------------------------------

bool inside(const OGRMultiPolygon *theGeom, double theX, double theY)
{
  if (theGeom == nullptr || theGeom->IsEmpty() != 0) return false;
  for (int i = 0, n = theGeom->getNumGeometries(); i < n; ++i)
  {
    if (inside(dynamic_cast<const OGRPolygon *>(theGeom->getGeometryRef(i)), theX, theY))
      return true;
  }
  return false;
}

// ----------------------------------------------------------------------
/*!
 * \brief Handle GeometryCollection
 */
// ----------------------------------------------------------------------

bool inside(const OGRGeometryCollection *theGeom, double theX, double theY)
{
  if (theGeom == nullptr || theGeom->IsEmpty() != 0) return false;
  for (int i = 0, n = theGeom->getNumGeometries(); i < n; ++i)
  {
    if (inside(theGeom->getGeometryRef(i), theX, theY)) return true;
  }
  return false;
}

// ----------------------------------------------------------------------
/*!
 * \brief Handle Polygon
 */
// ----------------------------------------------------------------------

bool Fmi::OGR::inside(const OGRPolygon &theGeom, double theX, double theY)
{
  // Outside if outside exterior ring
  if (!inside(theGeom.getExteriorRing(), theX, theY)) return false;

  for (int i = 0, n = theGeom.getNumInteriorRings(); i < n; ++i)
  {
    // outside if inside hole
    if (inside(theGeom.getInteriorRing(i), theX, theY)) return false;
  }

  return true;
}

// ----------------------------------------------------------------------
/*!
 * \brief Is the given coordinate inside the geometry?
 */
// ----------------------------------------------------------------------

bool Fmi::OGR::inside(const OGRGeometry &theGeom, double theX, double theY)
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
      if (strcmp(theGeom.getGeometryName(), "LINEARRING") != 0) return false;
    // Fall through
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
    case wkbUnknown:
      throw std::runtime_error(
          "Encountered an unknown geometry component in OGR to SVG conversion");
    case wkbNone:
      throw std::runtime_error("Encountered a 'none' geometry component in OGR to SVG conversion");
  }

  // NOT REACHED
  return false;
}

// ----------------------------------------------------------------------
/*!
 * \brief Is the given coordinate inside the geometry?
 */
// ----------------------------------------------------------------------

bool inside(const OGRGeometry *theGeom, double theX, double theY)
{
  if (theGeom == nullptr || theGeom->IsEmpty() != 0) return false;

  return Fmi::OGR::inside(*theGeom, theX, theY);
}
