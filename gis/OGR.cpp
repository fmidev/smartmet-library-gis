#include "OGR.h"
#include "GEOS.h"
#include <boost/math/constants/constants.hpp>
#include <boost/scoped_ptr.hpp>
#include <fmt/format.h>
#include <cmath>
#include <stdexcept>

// ----------------------------------------------------------------------
/*!
 * \brief Export OGR spatial reference to WKT
 */
// ----------------------------------------------------------------------

std::string Fmi::OGR::exportToWkt(const OGRSpatialReference& theSRS)
{
  char* out;
  theSRS.exportToWkt(&out);
  std::string ret = out;
  OGRFree(out);
  return ret;
}

// ----------------------------------------------------------------------
/*!
 * \brief Export OGR geometry to WKT
 *
 * This is a convenience method since the OGR API is not being
 * convenient for getting WKT out.
 */
// ----------------------------------------------------------------------

std::string Fmi::OGR::exportToWkt(const OGRGeometry& theGeom)
{
  char* out;
  theGeom.exportToWkt(&out);
  std::string ret = out;
  OGRFree(out);
  return ret;
}

// ----------------------------------------------------------------------
/*!
 * \brief Convert GEOS geometry to OGR
 *
 * The spatial reference may be nullptr for createFromWkb.
 */
// ----------------------------------------------------------------------

OGRGeometry* Fmi::OGR::importFromGeos(const geos::geom::Geometry& theGeom,
                                      OGRSpatialReference* theSR)
{
  auto wkb = GEOS::exportToWkb(theGeom);

  // Read to OGR using as hideous casts as is required

  unsigned char* cwkb = reinterpret_cast<unsigned char*>(const_cast<char*>(wkb.c_str()));

  OGRGeometry* ogeom = nullptr;
  OGRErr err = OGRGeometryFactory::createFromWkb(cwkb, theSR, &ogeom);

  if (err != OGRERR_NONE)
    throw std::runtime_error("Failed to convert GEOS geometry to OGR geometry");

  // Return the result

  return ogeom;
}

// ----------------------------------------------------------------------
/*!
 * \brief Create OGRGeometry from WKT-string
 * \param wktString String of WKT-format
 * \param theEPSGNumber EPSG code
 *
 *  The spatial reference is assigned to geometry if theEPSGNumber > 0
 *
 */
// ----------------------------------------------------------------------

OGRGeometry* Fmi::OGR::createFromWkt(const std::string& wktString,
                                     unsigned int theEPSGNumber /* = 0 */)
{
  OGRGeometry* geom = nullptr;
  char* pszWKT(const_cast<char*>(wktString.c_str()));
  OGRErr err = OGRGeometryFactory::createFromWkt(&pszWKT, NULL, &geom);
  if (err != OGRERR_NONE)
  {
    std::string errStr = "Failed to create OGRGeometry from WKT " + wktString + "";
    if (err == OGRERR_NOT_ENOUGH_DATA) errStr += " OGRErr: OGRERR_NOT_ENOUGH_DATA";
    if (err == OGRERR_UNSUPPORTED_GEOMETRY_TYPE)
      errStr += " OGRErr: OGRERR_UNSUPPORTED_GEOMETRY_TYPE";
    if (err == OGRERR_CORRUPT_DATA) errStr += " OGRErr: OGRERR_CORRUPT_DATA";

    throw std::runtime_error(errStr);
  }

  if (theEPSGNumber > 0)
  {
    OGRSpatialReference srs;
    srs.importFromEPSGA(theEPSGNumber);
    geom->assignSpatialReference(srs.Clone());
  }

  return geom;
}

// ----------------------------------------------------------------------
/*!
 * \brief Create OGRGeometry from list of coordinate points
 * \param theCoordinates List of coordinate points
 * \param theGeometryType Geometry type to be created from coordinate points
 * \param theEPSGNumber EPSG code
 *
 *  POINT, LINESTRING and POLYGON geometries are supported
 *
 */
// ----------------------------------------------------------------------

OGRGeometry* Fmi::OGR::constructGeometry(const CoordinatePoints& theCoordinates,
                                         OGRwkbGeometryType theGeometryType,
                                         unsigned int theEPSGNumber)
{
  std::string wkt;

  OGRPoint ogrPoint;
  OGRLineString ogrLineString;
  OGRPolygon ogrPolygon;
  OGRGeometry* ogrGeom = 0;

  if (theGeometryType == wkbPoint)
  {
    wkt += "POINT(";
    ogrGeom = &ogrPoint;
  }
  else if (theGeometryType == wkbLineString || theGeometryType == wkbLinearRing)
  {
    wkt += "LINESTRING(";
    ogrGeom = &ogrLineString;
  }
  else if (theGeometryType == wkbPolygon)
  {
    wkt += "POLYGON((";
    ogrGeom = &ogrPolygon;
  }
  else
    return ogrGeom;

  for (CoordinatePoints::const_iterator iter = theCoordinates.begin(); iter != theCoordinates.end();
       iter++)
  {
    if (iter != theCoordinates.begin()) wkt += ", ";
    wkt += fmt::format("%f %f", iter->first, iter->second);
  }
  wkt += (theGeometryType == wkbPolygon ? "))" : ")");

  char* pBuff(const_cast<char*>(wkt.c_str()));

  ogrGeom->importFromWkt(&pBuff);

  OGRSpatialReference srs;
  srs.importFromEPSGA(theEPSGNumber);
  ogrGeom->assignSpatialReference(srs.Clone());

  return ogrGeom->clone();
}

static OGRGeometry* expandGeometry(const OGRGeometry* theGeom, double theRadiusInMeters)
{
  OGRGeometry* ret = nullptr;

  boost::scoped_ptr<OGRGeometry> tmp_geom;
  tmp_geom.reset(theGeom->clone());

  OGRwkbGeometryType type(tmp_geom->getGeometryType());

  OGRSpatialReference* pSR = tmp_geom->getSpatialReference();

  OGRSpatialReference sourceSR;

  if (pSR != 0)
  {
    sourceSR = *pSR;
  }
  else
  {
    // if no spatial reference, use EPSG:4326
    OGRErr err = sourceSR.importFromEPSGA(4326);
    if (err != OGRERR_NONE) throw std::runtime_error("EPSG:4326 is unknown!");

    tmp_geom->assignSpatialReference(&sourceSR);
  }

  OGRSpatialReference targetSR;

  // wgs-84-world-mercator7
  OGRErr err = targetSR.importFromEPSGA(3395);

  if (err != OGRERR_NONE) throw std::runtime_error("EPSG:3395 is unknown!");

  OGRCoordinateTransformation* pCT = OGRCreateCoordinateTransformation(&sourceSR, &targetSR);

  if (pCT == nullptr)
    throw std::runtime_error("OGRCreateCoordinateTransformation function call failed");

  // transform to EPSG:3395 geometry
  if (tmp_geom->transform(pCT) != OGRERR_NONE) return theGeom->clone();

  delete pCT;

  unsigned int radius =
      (type == wkbLineString || type == wkbMultiLineString ? theRadiusInMeters
                                                           : theRadiusInMeters * 2);

  // make the buffer
  boost::scoped_ptr<OGRPolygon> polygon(static_cast<OGRPolygon*>(tmp_geom->Buffer(radius, 20)));

  // get exterior ring of polygon
  // the returned ring pointer is to an internal data object of the OGRPolygon.
  // It should not be modified or deleted by the application
  OGRLinearRing* exring(polygon->getExteriorRing());

  pCT = OGRCreateCoordinateTransformation(&targetSR, &sourceSR);

  if (!pCT) throw std::runtime_error("OGRCreateCoordinateTransformation function call failed");

  // convert back to original geometry
  exring->transform(pCT);

  delete pCT;

  OGRPolygon poly;
  poly.addRing(exring);

  // polygon is simplified to reduce amount of points
  if (exring->getNumPoints() > 1000) ret = poly.SimplifyPreserveTopology(0.001);

  if (!ret) ret = poly.clone();

  return ret;
}

// ----------------------------------------------------------------------
/*!
 * \brief Expand the geometry by theRadiusInMeters meters
 * \param theGeom The geometry to be expanded
 * \param theRadiusInMeters Amount of meters to expand the geometry
 * \return The expanded geometry
 *
 */
// ----------------------------------------------------------------------

OGRGeometry* Fmi::OGR::expandGeometry(const OGRGeometry* theGeom, double theRadiusInMeters)
{
  OGRGeometry* ret = nullptr;

  if (!theGeom)
  {
    throw std::runtime_error("ExpandGeometry failed: theGeom is nullptr!");
    return ret;
  }

  if (theRadiusInMeters <= 0.0) return theGeom->clone();

  // in case of  multipolygon, expand each polygon separately
  if (theGeom->getGeometryType() == wkbMultiPolygon)
  {
    boost::scoped_ptr<OGRMultiPolygon> multipoly;
    multipoly.reset(static_cast<OGRMultiPolygon*>(theGeom->clone()));

    OGRMultiPolygon mpoly;
    int n_geometries(multipoly->getNumGeometries());

    for (int i = 0; i < n_geometries; i++)
    {
      // addGeometry makes a copy, so poly must be deleted
      boost::scoped_ptr<OGRGeometry> poly;
      poly.reset(::expandGeometry(multipoly->getGeometryRef(i), theRadiusInMeters));
      mpoly.addGeometry(poly.get());
    }
    ret = mpoly.clone();
  }
  else
    ret = ::expandGeometry(theGeom, theRadiusInMeters);

  return ret;
}

// ----------------------------------------------------------------------
/*!
 * \brief Direction of north in the spatial reference
 * \param theTransformation Transformation from WGS84 to the desired spatial reference
 * \param theLon The longitude
 * \param theLat The latitude
 * \return Direction of north in degrees if it can be calculated
 *
 * Also called true north azimuth. The difference between in angles
 * between true north and the meridian at the given point. The direction
 * where Y increases in the projection.
 *
 * GDAL does not seem to provide direct support for this calculation.
 * We approximate the result by moving a small distance to the north,
 * and by calculating the resulting azimuth angle. This is not entirely
 * accurate, but accurate enough for most purposes.
 *
 * Note: OGRCoordinateTransformation::Transform is not const
 */
// ----------------------------------------------------------------------

boost::optional<double> Fmi::OGR::gridNorth(OGRCoordinateTransformation& theTransformation,
                                            double theLon,
                                            double theLat)
{
  // Actual coordinate
  double x1 = theLon;
  double y1 = theLat;
  if (!theTransformation.Transform(1, &x1, &y1)) return {};

  // Move slightly to the north
  double x2 = theLon;
  double y2 = theLat + 0.0001;
  if (y2 < 90)
  {
    if (!theTransformation.Transform(1, &x2, &y2)) return {};
  }
  else
  {
    // move south instead and swap orientation
    y2 = theLat - 0.0001;
    if (!theTransformation.Transform(1, &x2, &y2)) return {};
    std::swap(x1, x2);
    std::swap(y1, y2);
  }

  // Calculate the azimuth. Note that for us angle 0 is up and not to increasing x
  // as in normal math, hence we have rotated the system by swapping dx and dy in atan2
  return atan2(x2 - x1, y2 - y1) * boost::math::double_constants::radian;
}
