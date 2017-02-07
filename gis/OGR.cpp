#include "OGR.h"
#include "GEOS.h"
#include <stdexcept>
#include <sstream>
#include <boost/scoped_ptr.hpp>

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
 * The spatial reference may be NULL for createFromWkb.
 */
// ----------------------------------------------------------------------

OGRGeometry* Fmi::OGR::importFromGeos(const geos::geom::Geometry& theGeom,
                                      OGRSpatialReference* theSR)
{
  auto wkb = GEOS::exportToWkb(theGeom);

  // Read to OGR using as hideous casts as is required

  unsigned char* cwkb = reinterpret_cast<unsigned char*>(const_cast<char*>(wkb.c_str()));

  OGRGeometry* ogeom = NULL;
  OGRErr err = OGRGeometryFactory::createFromWkb(cwkb, theSR, &ogeom);

  if (err != OGRERR_NONE)
    throw std::runtime_error("Failed to convert GEOS geometry to OGR geometry");

  // Return the result

  return ogeom;
}

OGRGeometry* Fmi::OGR::constructGeometry(const CoordinatePoints& theCoordinates,
                                         OGRwkbGeometryType theGeometryType,
                                         unsigned int theEPSGNumber)
{
  std::stringstream ss_wkt;

  OGRPoint ogrPoint;
  OGRLineString ogrLineString;
  OGRPolygon ogrPolygon;
  OGRGeometry* ogrGeom = 0;

  if (theGeometryType == wkbPoint)
  {
    ss_wkt << "POINT(";
    ogrGeom = &ogrPoint;
  }
  else if (theGeometryType == wkbLineString || theGeometryType == wkbLinearRing)
  {
    ss_wkt << "LINESTRING(";
    ogrGeom = &ogrLineString;
  }
  else if (theGeometryType == wkbPolygon)
  {
    ss_wkt << "POLYGON((";
    ogrGeom = &ogrPolygon;
  }
  else
    return ogrGeom;

  for (CoordinatePoints::const_iterator iter = theCoordinates.begin(); iter != theCoordinates.end();
       iter++)
  {
    if (iter != theCoordinates.begin()) ss_wkt << ", ";
    ss_wkt << iter->first << " " << iter->second;
  }
  ss_wkt << (theGeometryType == wkbPolygon ? "))" : ")");

  std::string str(ss_wkt.str());
  char* pBuff(const_cast<char*>(str.c_str()));

  ogrGeom->importFromWkt(&pBuff);

  OGRSpatialReference srs;
  srs.importFromEPSGA(theEPSGNumber);
  ogrGeom->assignSpatialReference(srs.Clone());

  return ogrGeom->clone();
}

static OGRGeometry* expandGeometry(const OGRGeometry* theGeom, double theRadiusInMeters)
{
  OGRGeometry* ret(0);

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

  if (pCT == NULL)
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

OGRGeometry* Fmi::OGR::expandGeometry(const OGRGeometry* theGeom, double theRadiusInMeters)
{
  OGRGeometry* ret(0);

  if (!theGeom)
  {
    throw std::runtime_error("ExpandGeometry failed: theGeom is NULL!");
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
