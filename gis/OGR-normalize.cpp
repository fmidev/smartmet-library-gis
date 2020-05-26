#include "OGR.h"

#include <ogr_geometry.h>
#include <stdexcept>

namespace
{
// Forward declaration needed since two functions call each other
OGRGeometry *normalize_winding(const OGRGeometry *theGeom);

// ----------------------------------------------------------------------
/*!
 * \brief Handle Polygon
 */
// ----------------------------------------------------------------------

OGRPolygon *normalize_winding(const OGRPolygon *theGeom)
{
  if (theGeom == nullptr || theGeom->IsEmpty() != 0) return nullptr;

  auto *out = new OGRPolygon;

  auto *exterior = dynamic_cast<OGRLinearRing *>(theGeom->getExteriorRing()->clone());

  if (!exterior->isClockwise()) exterior->reverseWindingOrder();

  out->addRingDirectly(exterior);

  for (int i = 0, n = theGeom->getNumInteriorRings(); i < n; i++)
  {
    auto *geom = dynamic_cast<OGRLinearRing *>(theGeom->getInteriorRing(i)->clone());
    if (geom->isClockwise()) geom->reverseWindingOrder();
    out->addRingDirectly(geom);
  }

  return out;
}

// ----------------------------------------------------------------------
/*!
 * \brief Handle MultiPolygon
 */
// ----------------------------------------------------------------------

OGRMultiPolygon *normalize_winding(const OGRMultiPolygon *theGeom)
{
  if (theGeom == nullptr || theGeom->IsEmpty() != 0) return nullptr;

  auto *out = new OGRMultiPolygon();

  for (int i = 0, n = theGeom->getNumGeometries(); i < n; ++i)
  {
    auto *geom = normalize_winding(dynamic_cast<const OGRPolygon *>(theGeom->getGeometryRef(i)));
    if (geom != nullptr) out->addGeometryDirectly(geom);
  }
  return out;
}

// ----------------------------------------------------------------------
/*!
 * \brief Handle GeometryCollection
 */
// ----------------------------------------------------------------------

OGRGeometryCollection *normalize_winding(const OGRGeometryCollection *theGeom)
{
  if (theGeom == nullptr || theGeom->IsEmpty() != 0) return nullptr;

  auto *out = new OGRGeometryCollection();

  for (int i = 0, n = theGeom->getNumGeometries(); i < n; ++i)
  {
    auto *geom = normalize_winding(theGeom->getGeometryRef(i));
    if (geom != nullptr) out->addGeometryDirectly(geom);
  }
  return out;
}

// ----------------------------------------------------------------------
/*!
 * \brief Normalize winding of the given geometry
 */
// ----------------------------------------------------------------------

OGRGeometry *normalize_winding(const OGRGeometry *theGeom)
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
    case wkbPolygon:
      return normalize_winding(dynamic_cast<const OGRPolygon *>(theGeom));
    case wkbMultiPolygon:
      return normalize_winding(dynamic_cast<const OGRMultiPolygon *>(theGeom));
    case wkbGeometryCollection:
      return normalize_winding(dynamic_cast<const OGRGeometryCollection *>(theGeom));

    case wkbNone:
      throw std::runtime_error(
          "Encountered a 'none' geometry component while changing winding order of an OGR "
          "geometry");
    default:
      throw std::runtime_error(
          "Encountered an unknown geometry component while changing winding order of an OGR "
          "geometry");
  }

  // NOT REACHED
  return nullptr;
}

}  // namespace

// ----------------------------------------------------------------------
/*!
 * \brief Normalize the winding order
 */
// ----------------------------------------------------------------------

OGRGeometry *Fmi::OGR::normalizeWindingOrder(const OGRGeometry &theGeom)
{
  auto *geom = normalize_winding(&theGeom);

  if (geom != nullptr)
    geom->assignSpatialReference(theGeom.getSpatialReference());  // SR is ref. counter

  return geom;
}
