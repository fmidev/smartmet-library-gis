#include "OGR.h"
#include <ogr_geometry.h>
#include <stdexcept>

namespace
{
// Forward declaration needed since two functions call each other
OGRGeometry *renormalize_winding(const OGRGeometry *theGeom);

// ----------------------------------------------------------------------
/*!
 * \brief Handle Polygon
 */
// ----------------------------------------------------------------------

OGRPolygon *renormalize_winding(const OGRPolygon *theGeom)
{
  if (theGeom == nullptr || theGeom->IsEmpty() != 0) return nullptr;

  auto *exterior = dynamic_cast<const OGRLinearRing *>(theGeom->getExteriorRing());

  bool is_cw = exterior->isClockwise();

  if (is_cw) return dynamic_cast<OGRPolygon *>(theGeom->clone());

  // Now we must reverse the exterior and make it the interior of a larger envelope.
  // Any holes are simply dropped, they do not belong to the polygon formed
  // by the envelope and the old exterior.

  OGREnvelope env;
  exterior->getEnvelope(&env);

  // Increment bbox by its size in each direction
  const auto width = env.MaxX - env.MinX;
  const auto height = env.MaxY - env.MinY;
  const auto x1 = env.MinX - width;
  const auto x2 = env.MaxX + width;
  const auto y1 = env.MinY - height;
  const auto y2 = env.MaxY + height;

  // Make the new polygon exterior

  auto *out = new OGRPolygon();
  auto *newexterior = new OGRLinearRing();
  newexterior->addPoint(x1, y1);
  newexterior->addPoint(x1, y2);
  newexterior->addPoint(x2, y2);
  newexterior->addPoint(x2, y1);
  newexterior->addPoint(x1, y1);
  out->addRingDirectly(newexterior);

  // Add the old CCW exterior as a hole

  out->addRingDirectly(dynamic_cast<OGRLinearRing *>(theGeom->getExteriorRing()->clone()));

  return out;
}

// ----------------------------------------------------------------------
/*!
 * \brief Handle MultiPolygon
 */
// ----------------------------------------------------------------------

OGRMultiPolygon *renormalize_winding(const OGRMultiPolygon *theGeom)
{
  if (theGeom == nullptr || theGeom->IsEmpty() != 0) return nullptr;

  auto *out = new OGRMultiPolygon();

  for (int i = 0, n = theGeom->getNumGeometries(); i < n; ++i)
  {
    auto *geom = renormalize_winding(dynamic_cast<const OGRPolygon *>(theGeom->getGeometryRef(i)));
    if (geom != nullptr) out->addGeometryDirectly(geom);
  }
  return out;
}

// ----------------------------------------------------------------------
/*!
 * \brief Handle GeometryCollection
 */
// ----------------------------------------------------------------------

OGRGeometryCollection *renormalize_winding(const OGRGeometryCollection *theGeom)
{
  if (theGeom == nullptr || theGeom->IsEmpty() != 0) return nullptr;

  auto *out = new OGRGeometryCollection();

  for (int i = 0, n = theGeom->getNumGeometries(); i < n; ++i)
  {
    auto *geom = renormalize_winding(theGeom->getGeometryRef(i));
    if (geom != nullptr) out->addGeometryDirectly(geom);
  }
  return out;
}

// ----------------------------------------------------------------------
/*!
 * \brief Renormalize winding of the given geometry
 */
// ----------------------------------------------------------------------

OGRGeometry *renormalize_winding(const OGRGeometry *theGeom)
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
      return renormalize_winding(dynamic_cast<const OGRPolygon *>(theGeom));
    case wkbMultiPolygon:
      return renormalize_winding(dynamic_cast<const OGRMultiPolygon *>(theGeom));
    case wkbGeometryCollection:
      return renormalize_winding(dynamic_cast<const OGRGeometryCollection *>(theGeom));

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
 * \brief Renormalize the winding order
 *
 * Typically used to change the winding order of normalized GEOS
 * geometries, which have clockwise shells. OGC requires counter-clockwise
 * shells.
 */
// ----------------------------------------------------------------------

OGRGeometry *Fmi::OGR::renormalizeWindingOrder(const OGRGeometry &theGeom)
{
  auto *geom = renormalize_winding(&theGeom);

  if (geom != nullptr)
    geom->assignSpatialReference(theGeom.getSpatialReference());  // SR is ref. counter

  return geom;
}
