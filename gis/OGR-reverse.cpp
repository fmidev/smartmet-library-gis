#include "OGR.h"
#include <gdal/ogr_geometry.h>
#include <stdexcept>

// Unfortunately reversing OGR rings cannot be done directly in the
// object, a copy must be made. Tried it before reading the manual
// and lost all holes.

namespace
{
// Forward declaration needed since two functions call each other
OGRGeometry *reverse_winding(const OGRGeometry *theGeom);

// ----------------------------------------------------------------------
/*!
 * \brief Handle LinearRing
 */
// ----------------------------------------------------------------------

OGRLinearRing *reverse_winding(const OGRLinearRing *theGeom)
{
  if (theGeom == nullptr || theGeom->IsEmpty()) return nullptr;

  auto *geom = static_cast<OGRLinearRing *>(theGeom->clone());
  geom->reverseWindingOrder();
  return geom;
}

// ----------------------------------------------------------------------
/*!
 * \brief Handle MultiPolygon
 */
// ----------------------------------------------------------------------

OGRMultiPolygon *reverse_winding(const OGRMultiPolygon *theGeom)
{
  if (theGeom == nullptr || theGeom->IsEmpty()) return nullptr;

  OGRMultiPolygon *out = new OGRMultiPolygon();

  for (int i = 0, n = theGeom->getNumGeometries(); i < n; ++i)
  {
    auto *geom = reverse_winding(static_cast<const OGRPolygon *>(theGeom->getGeometryRef(i)));
    if (geom) out->addGeometryDirectly(geom);
  }
  return out;
}

// ----------------------------------------------------------------------
/*!
 * \brief Handle GeometryCollection
 */
// ----------------------------------------------------------------------

OGRGeometryCollection *reverse_winding(const OGRGeometryCollection *theGeom)
{
  if (theGeom == nullptr || theGeom->IsEmpty()) return nullptr;

  OGRGeometryCollection *out = new OGRGeometryCollection();

  for (int i = 0, n = theGeom->getNumGeometries(); i < n; ++i)
  {
    auto *geom = reverse_winding(theGeom->getGeometryRef(i));
    if (geom) out->addGeometryDirectly(geom);
  }
  return out;
}

// ----------------------------------------------------------------------
/*!
 * \brief Handle Polygon
 */
// ----------------------------------------------------------------------

OGRPolygon *reverse_winding(const OGRPolygon *theGeom)
{
  if (theGeom == nullptr || theGeom->IsEmpty()) return nullptr;

  OGRPolygon *out = new OGRPolygon();

  auto *exterior = static_cast<OGRLinearRing *>(theGeom->getExteriorRing()->clone());
  exterior->reverseWindingOrder();
  out->addRingDirectly(exterior);

  for (int i = 0, n = theGeom->getNumInteriorRings(); i < n; ++i)
  {
    auto *hole = static_cast<OGRLinearRing *>(theGeom->getInteriorRing(i)->clone());
    hole->reverseWindingOrder();
    out->addRingDirectly(hole);
  }
  return out;
}

// ----------------------------------------------------------------------
/*!
 * \brief Reverse winding of the given geometry
 */
// ----------------------------------------------------------------------

OGRGeometry *reverse_winding(const OGRGeometry *theGeom)
{
  if (theGeom == nullptr) return nullptr;

  OGRwkbGeometryType id = theGeom->getGeometryType();

  switch (id)
  {
    case wkbPoint:
    case wkbMultiPoint:
    case wkbMultiLineString:
    case wkbLineString:
      return theGeom->clone();
    case wkbLinearRing:
      return reverse_winding(static_cast<const OGRLinearRing *>(theGeom));
    case wkbPolygon:
      return reverse_winding(static_cast<const OGRPolygon *>(theGeom));
    case wkbMultiPolygon:
      return reverse_winding(static_cast<const OGRMultiPolygon *>(theGeom));
    case wkbGeometryCollection:
      return reverse_winding(static_cast<const OGRGeometryCollection *>(theGeom));

    case wkbPoint25D:
    case wkbMultiPoint25D:
    case wkbMultiLineString25D:
    case wkbMultiPolygon25D:
    case wkbLineString25D:
    case wkbGeometryCollection25D:
    case wkbPolygon25D:
      throw std::runtime_error(
          "25D components not supported when reversing OGR geometry winding orders");
    case wkbUnknown:
      throw std::runtime_error(
          "Encountered an unknown geometry component while changing winding order of an OGR "
          "geometry");
    case wkbNone:
      throw std::runtime_error(
          "Encountered a 'none' geometry component while changing winding order of an OGR "
          "geometry");
  }

  // NOT REACHED
  return nullptr;
}

}  // namespace

// ----------------------------------------------------------------------
/*!
 * \brief Reverse the winding order
 *
 * Typically used to change the winding order of normalized GEOS
 * geometries, which have clockwise shells. OGC requires counter-clockwise
 * shells.
 */
// ----------------------------------------------------------------------

OGRGeometry *Fmi::OGR::reverseWindingOrder(const OGRGeometry &theGeom)
{
  auto *geom = reverse_winding(&theGeom);

  if (geom != nullptr)
    geom->assignSpatialReference(theGeom.getSpatialReference());  // SR is ref. counter

  return geom;
}