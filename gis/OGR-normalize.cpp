#include "OGR.h"
#include <ogr_geometry.h>
#include <stdexcept>

namespace
{
void normalize_winding(OGRGeometry *theGeom);

// ----------------------------------------------------------------------
/*!
 * \brief Handle LinearRing
 */
// ----------------------------------------------------------------------

void normalize_winding(OGRLinearRing *theGeom, bool cw)
{
  if (theGeom == nullptr || theGeom->IsEmpty() != 0) return;

  bool is_cw = theGeom->isClockwise();
  if (cw ^ is_cw) theGeom->reverseWindingOrder();
}

// ----------------------------------------------------------------------
/*!
 * \brief Handle Polygon
 */
// ----------------------------------------------------------------------

void normalize_winding(OGRPolygon *theGeom)
{
  if (theGeom == nullptr || theGeom->IsEmpty() != 0) return;

  normalize_winding(theGeom->getExteriorRing(), true);

  for (int i = 0, n = theGeom->getNumInteriorRings(); i < n; ++i)
    normalize_winding(theGeom->getInteriorRing(i), false);
}

// ----------------------------------------------------------------------
/*!
 * \brief Handle MultiPolygon
 */
// ----------------------------------------------------------------------

void normalize_winding(OGRMultiPolygon *theGeom)
{
  if (theGeom == nullptr || theGeom->IsEmpty() != 0) return;

  for (int i = 0, n = theGeom->getNumGeometries(); i < n; ++i)
    normalize_winding(theGeom->getGeometryRef(i));
}

// ----------------------------------------------------------------------
/*!
 * \brief Handle GeometryCollection
 */
// ----------------------------------------------------------------------

void normalize_winding(OGRGeometryCollection *theGeom)
{
  if (theGeom == nullptr || theGeom->IsEmpty() != 0) return;

  for (int i = 0, n = theGeom->getNumGeometries(); i < n; ++i)
    normalize_winding(theGeom->getGeometryRef(i));
}

// ----------------------------------------------------------------------
/*!
 * \brief Normalize winding of the given geometry
 */
// ----------------------------------------------------------------------

void normalize_winding(OGRGeometry *theGeom)
{
  if (theGeom == nullptr) return;

  OGRwkbGeometryType id = theGeom->getGeometryType();

  switch (id)
  {
    case wkbPoint:
    case wkbMultiPoint:
    case wkbMultiLineString:
    case wkbLineString:
      break;
    case wkbLinearRing:
      return normalize_winding(dynamic_cast<OGRLinearRing *>(theGeom));
    case wkbPolygon:
      return normalize_winding(dynamic_cast<OGRPolygon *>(theGeom));
    case wkbMultiPolygon:
      return normalize_winding(dynamic_cast<OGRMultiPolygon *>(theGeom));
    case wkbGeometryCollection:
      return normalize_winding(dynamic_cast<OGRGeometryCollection *>(theGeom));

    case wkbNone:
      throw std::runtime_error(
          "Encountered a 'none' geometry component while normalizing winding order of an OGR "
          "geometry");
    default:
      throw std::runtime_error(
          "Encountered an unknown geometry component while normalizing winding order of an OGR "
          "geometry");
  }
}

}  // namespace

// ----------------------------------------------------------------------
/*!
 * \brief Normalize winding order to exterior=CW, interior=CCW
 */
// ----------------------------------------------------------------------

void Fmi::OGR::normalizeWindingOrder(OGRGeometry &theGeom) { normalize_winding(&theGeom); }
