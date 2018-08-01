#include "OGR.h"
#include <boost/math/constants/constants.hpp>
#include <stdexcept>

// ----------------------------------------------------------------------
/*!
 * \brief Calculate area in m^2 for a geography
 *
 * Refs: Some algorithms for polygons on a sphere
 *       http://hdl.handle.net/2014/40409
 *
 *       https://trac.osgeo.org/openlayers/browser/trunk/openlayers/lib/OpenLayers/Geometry/LinearRing.js
 *
 * Note: The algorithm returns a positive number for clockwise rings.
 */
// ----------------------------------------------------------------------

double geographic_area(const OGRLinearRing *theGeom)
{
  double area = 0;

  std::size_t npoints = theGeom->getNumPoints();

  for (std::size_t i = 0; i < npoints - 1; i++)
  {
    double x1 = theGeom->getX(i) * boost::math::double_constants::degree;
    double y1 = theGeom->getY(i) * boost::math::double_constants::degree;
    double x2 = theGeom->getX(i + 1) * boost::math::double_constants::degree;
    double y2 = theGeom->getY(i + 1) * boost::math::double_constants::degree;

    area += (x2 - x1) * (2 + sin(y1) + sin(y2));
  }
  area *= 6378137.0 * 6378137.0 / 2.0;

  return std::abs(area);
}

// ----------------------------------------------------------------------
/*!
 * \brief Despeckle OGR geometry
 */
// ----------------------------------------------------------------------

OGRPolygon *despeckle_polygon(const OGRPolygon *theGeom, double theLimit, bool theGeogFlag)
{
  // Quick exit if the exterior is too small

  auto *exterior = theGeom->getExteriorRing();
  double area = (theGeogFlag ? geographic_area(exterior) : exterior->get_Area());

  if (area < theLimit) return nullptr;

  // We have at least a valid exterior

  OGRPolygon *out = new OGRPolygon;
  out->addRingDirectly(static_cast<OGRLinearRing *>(exterior->clone()));

  // Remove too small holes too

  for (int i = 0, n = theGeom->getNumInteriorRings(); i < n; ++i)
  {
    auto *hole = theGeom->getInteriorRing(i);
    area = (theGeogFlag ? geographic_area(hole) : hole->get_Area());
    if (area >= theLimit) out->addRingDirectly(static_cast<OGRLinearRing *>(hole->clone()));
  }

  return out;
}

// ----------------------------------------------------------------------
/*!
 * \brief Despeckle OGR geometry
 */
// ----------------------------------------------------------------------

OGRLineString *despeckle_linestring(const OGRLineString *theGeom, double theLimit, bool theGeogFlag)
{
  if (theGeom == nullptr || theGeom->IsEmpty()) return nullptr;

  return static_cast<OGRLineString *>(theGeom->clone());
}

// ----------------------------------------------------------------------
/*!
 * \brief Despeckle OGR geometry
 */
// ----------------------------------------------------------------------

OGRPoint *despeckle_point(const OGRPoint *theGeom, double theLimit, bool theGeogFlag)
{
  if (theGeom == nullptr || theGeom->IsEmpty()) return nullptr;

  return static_cast<OGRPoint *>(theGeom->clone());
}

// ----------------------------------------------------------------------
/*!
 * \brief Despeckle OGR geometry
 */
// ----------------------------------------------------------------------

OGRMultiPoint *despeckle_multipoint(const OGRMultiPoint *theGeom, double theLimit, bool theGeogFlag)
{
  if (theGeom == nullptr || theGeom->IsEmpty()) return nullptr;

  return static_cast<OGRMultiPoint *>(theGeom->clone());
}

// ----------------------------------------------------------------------
/*!
 * \brief Despeckle OGR geometry
 */
// ----------------------------------------------------------------------

OGRMultiLineString *despeckle_multilinestring(const OGRMultiLineString *theGeom,
                                              double theLimit,
                                              bool theGeogFlag)
{
  if (theGeom == nullptr || theGeom->IsEmpty()) return nullptr;
  ;

  return static_cast<OGRMultiLineString *>(theGeom->clone());
}

// ----------------------------------------------------------------------
/*!
 * \brief Despeckle OGR geometry
 */
// ----------------------------------------------------------------------

OGRMultiPolygon *despeckle_multipolygon(const OGRMultiPolygon *theGeom,
                                        double theLimit,
                                        bool theGeogFlag)
{
  if (theGeom == nullptr || theGeom->IsEmpty()) return nullptr;

  OGRMultiPolygon *out = new OGRMultiPolygon();

  for (int i = 0, n = theGeom->getNumGeometries(); i < n; ++i)
  {
    auto *geom = despeckle_polygon(
        static_cast<const OGRPolygon *>(theGeom->getGeometryRef(i)), theLimit, theGeogFlag);
    if (geom) out->addGeometryDirectly(geom);
  }

  if (!out->IsEmpty()) return out;

  delete out;
  return nullptr;
}

// ----------------------------------------------------------------------
/*!
 * \brief Despeckle OGR geometry
 */
// ----------------------------------------------------------------------

// Needed since two functions call each other

OGRGeometry *despeckle_geom(const OGRGeometry *theGeom, double theLimit, bool theGeogFlag);

OGRGeometryCollection *despeckle_geometrycollection(const OGRGeometryCollection *theGeom,
                                                    double theLimit,
                                                    bool theGeogFlag)
{
  if (theGeom == nullptr || theGeom->IsEmpty()) return nullptr;

  OGRGeometryCollection *out = new OGRGeometryCollection;

  for (int i = 0, n = theGeom->getNumGeometries(); i < n; ++i)
  {
    auto *geom = despeckle_geom(theGeom->getGeometryRef(i), theLimit, theGeogFlag);
    if (geom != nullptr) out->addGeometryDirectly(geom);
  }

  if (!out->IsEmpty()) return out;

  delete out;
  return nullptr;
}

// ----------------------------------------------------------------------
/*!
 * \brief Despeckle OGR geometry to output geometry
 */
// ----------------------------------------------------------------------

OGRGeometry *despeckle_geom(const OGRGeometry *theGeom, double theLimit, bool theGeogFlag)
{
  OGRwkbGeometryType id = theGeom->getGeometryType();

  switch (id)
  {
    case wkbPoint:
      return despeckle_point(static_cast<const OGRPoint *>(theGeom), theLimit, theGeogFlag);
    case wkbLineString:
      return despeckle_linestring(
          static_cast<const OGRLineString *>(theGeom), theLimit, theGeogFlag);
    case wkbPolygon:
      return despeckle_polygon(static_cast<const OGRPolygon *>(theGeom), theLimit, theGeogFlag);
    case wkbMultiPoint:
      return despeckle_multipoint(
          static_cast<const OGRMultiPoint *>(theGeom), theLimit, theGeogFlag);
    case wkbMultiLineString:
      return despeckle_multilinestring(
          static_cast<const OGRMultiLineString *>(theGeom), theLimit, theGeogFlag);
    case wkbMultiPolygon:
      return despeckle_multipolygon(
          static_cast<const OGRMultiPolygon *>(theGeom), theLimit, theGeogFlag);
    case wkbGeometryCollection:
      return despeckle_geometrycollection(
          static_cast<const OGRGeometryCollection *>(theGeom), theLimit, theGeogFlag);
    case wkbLinearRing:
      throw std::runtime_error("Direct despeckling of LinearRings is not supported");
    case wkbNone:
      throw std::runtime_error(
          "Encountered a 'none' geometry component when despeckling a geometry");
    case wkbUnknown:
    case wkbPoint25D:
    case wkbLineString25D:
    case wkbPolygon25D:
    case wkbMultiLineString25D:
    case wkbMultiPoint25D:
    case wkbMultiPolygon25D:
    case wkbGeometryCollection25D:
      throw std::runtime_error("Encountered an unknown geometry component when clipping polygons");
  }

  // NOT REACHED
  return nullptr;
}

// ----------------------------------------------------------------------
/*!
 * \brief Despeckle a geometry so that small polygons are removed
 *
 * \return Empty GeometryCollection if the result is empty
 */
// ----------------------------------------------------------------------

OGRGeometry *Fmi::OGR::despeckle(const OGRGeometry &theGeom, double theAreaLimit)
{
  // Area calculations for geographies must be done by ourselves, OGR
  // does it in the native system and hence would produce square degrees.

  OGRSpatialReference *crs = theGeom.getSpatialReference();
  bool geographic = (crs ? crs->IsGeographic() : false);

  // Actual despeckling

  auto *geom =
      despeckle_geom(&theGeom, theAreaLimit * 1000 * 1000, geographic);  // from m^2 to km^2

  if (geom != nullptr)
    geom->assignSpatialReference(theGeom.getSpatialReference());  // SR is ref. counted
  return geom;
}
