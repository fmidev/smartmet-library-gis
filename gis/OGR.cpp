#include "OGR.h"

#include "CoordinateTransformation.h"
#include "GEOS.h"
#include "SpatialReference.h"
#include <boost/math/constants/constants.hpp>
#include <boost/scoped_ptr.hpp>
#include <fmt/format.h>
#include <macgyver/Exception.h>
#include <cmath>
#include <ogr_geometry.h>

// ----------------------------------------------------------------------
/*!
 * \brief Export OGR spatial reference to WKT
 */
// ----------------------------------------------------------------------

std::string Fmi::OGR::exportToWkt(const OGRSpatialReference& theSRS)
{
  try
  {
    char* out;
    theSRS.exportToWkt(&out);
    std::string ret = out;
    CPLFree(out);
    return ret;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Export OGR spatial reference to WKT1_SIMPLE format
 */
// ----------------------------------------------------------------------

std::string Fmi::OGR::exportToSimpleWkt(const OGRSpatialReference& theSRS)
{
  try
  {
    const char* const options[] = {"FORMAT=WKT1_SIMPLE",
                                   nullptr};  // NOLINT(modernize-avoid-c-arrays)
    char* out;
    theSRS.exportToWkt(&out, options);
    std::string ret = out;
    CPLFree(out);
    return ret;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Export OGR spatial reference to WKT
 */
// ----------------------------------------------------------------------

std::string Fmi::OGR::exportToPrettyWkt(const OGRSpatialReference& theSRS)
{
  try
  {
    char* out;
    theSRS.exportToPrettyWkt(&out);
    std::string ret = out;
    CPLFree(out);
    return ret;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Export OGR spatial reference to PROJ
 */
// ----------------------------------------------------------------------

std::string Fmi::OGR::exportToProj(const OGRSpatialReference& theSRS)
{
  try
  {
    char* out;
    theSRS.exportToProj4(&out);
    std::string ret = out;
    CPLFree(out);
    return ret;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
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
  try
  {
    char* out;
    theGeom.exportToWkt(&out);
    std::string ret = out;
    CPLFree(out);
    return ret;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

std::string Fmi::OGR::exportToWkt(const OGRGeometry& theGeom, int precision)
{
  try
  {
    OGRWktOptions options;
    options.precision = precision;
    return theGeom.exportToWkt(options, nullptr);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Convert GEOS geometry to OGR
 *
 * The spatial reference may be nullptr for createFromWkb.
 */
// ----------------------------------------------------------------------

OGRGeometry* Fmi::OGR::importFromGeos(const geos::geom::Geometry& theGeom,
                                      OGRSpatialReference* theSRS)
{
  try
  {
    auto wkb = GEOS::exportToWkb(theGeom);

    // Read to OGR using as hideous casts as is required

    auto* cwkb = reinterpret_cast<unsigned char*>(const_cast<char*>(wkb.c_str()));

    OGRGeometry* ogeom = nullptr;
    OGRErr err = OGRGeometryFactory::createFromWkb(cwkb, theSRS, &ogeom);

    if (err != OGRERR_NONE)
      throw Fmi::Exception::Trace(BCP, "Failed to convert GEOS geometry to OGR geometry");

    // Return the result

    return ogeom;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
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
  try
  {
    OGRGeometry* geom = nullptr;
    OGRErr err = OGRGeometryFactory::createFromWkt(wktString.c_str(), nullptr, &geom);
    if (err != OGRERR_NONE)
    {
      std::string errStr = "Failed to create OGRGeometry from WKT " + wktString + "";
      if (err == OGRERR_NOT_ENOUGH_DATA)
        errStr += " OGRErr: OGRERR_NOT_ENOUGH_DATA";
      if (err == OGRERR_UNSUPPORTED_GEOMETRY_TYPE)
        errStr += " OGRErr: OGRERR_UNSUPPORTED_GEOMETRY_TYPE";
      if (err == OGRERR_CORRUPT_DATA)
        errStr += " OGRErr: OGRERR_CORRUPT_DATA";

      throw Fmi::Exception::Trace(BCP, errStr);
    }

    if (theEPSGNumber > 0)
    {
      OGRSpatialReference srs;
      srs.importFromEPSGA(theEPSGNumber);
      std::shared_ptr<OGRSpatialReference> tmp(srs.Clone(),
          [](OGRSpatialReference* sr) { sr->Release(); });
      geom->assignSpatialReference(tmp.get());
    }

    return geom;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
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
                                         int theGeometryType,
                                         unsigned int theEPSGNumber)
{
  try
  {
    std::string wkt;

    OGRPoint ogrPoint;
    OGRLineString ogrLineString;
    OGRPolygon ogrPolygon;
    OGRGeometry* ogrGeom = nullptr;

    auto geomtype = static_cast<OGRwkbGeometryType>(theGeometryType);

    if (geomtype == wkbPoint)
    {
      wkt += "POINT(";
      ogrGeom = &ogrPoint;
    }
    else if (geomtype == wkbLineString || geomtype == wkbLinearRing)
    {
      wkt += "LINESTRING(";
      ogrGeom = &ogrLineString;
    }
    else if (geomtype == wkbPolygon)
    {
      wkt += "POLYGON((";
      ogrGeom = &ogrPolygon;
    }
    else
      return ogrGeom;

    for (auto iter = theCoordinates.begin(); iter != theCoordinates.end(); iter++)
    {
      if (iter != theCoordinates.begin())
        wkt += ", ";
      wkt += fmt::format("%f %f", iter->first, iter->second);
    }
    wkt += (geomtype == wkbPolygon ? "))" : ")");

    const char* pBuff = wkt.c_str();

    ogrGeom->importFromWkt(&pBuff);

    OGRSpatialReference srs;
    srs.importFromEPSGA(theEPSGNumber);
    std::shared_ptr<OGRSpatialReference> tmp(srs.Clone(),
        [](OGRSpatialReference* sr) { sr->Release(); });
    ogrGeom->assignSpatialReference(tmp.get());

    return ogrGeom->clone();
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

static OGRGeometry* expandGeometry(const OGRGeometry* theGeom, double theRadiusInMeters)
{
  try
  {
    OGRGeometry* ret = nullptr;

    boost::scoped_ptr<OGRGeometry> tmp_geom;
    tmp_geom.reset(theGeom->clone());

    OGRwkbGeometryType type(tmp_geom->getGeometryType());

    OGRSpatialReference* pSR = tmp_geom->getSpatialReference();

    OGRSpatialReference SR;

    if (pSR != nullptr)
      SR = *pSR;
    else
    {
      // if no spatial reference, use EPSG:4326
      OGRErr err = SR.importFromEPSGA(4326);
      if (err != OGRERR_NONE)
        throw Fmi::Exception::Trace(BCP, "EPSG:4326 is unknown!");

      tmp_geom->assignSpatialReference(&SR);
    }

    Fmi::SpatialReference sourceCR(SR);
    Fmi::SpatialReference targetCR("EPSGA:3395");
    Fmi::CoordinateTransformation CT(sourceCR, targetCR);

    // transform to EPSG:3395 geometry
    if (tmp_geom->transform(CT.get()) != OGRERR_NONE)
      throw Fmi::Exception::Trace(BCP, "OGRGeometry::transform() function call failed");

    Fmi::CoordinateTransformation CT2(targetCR, sourceCR);

    unsigned int radius =
        lround(type == wkbLineString || type == wkbMultiLineString ? theRadiusInMeters
                                                                   : theRadiusInMeters * 2);
    // make the buffer
    boost::scoped_ptr<OGRPolygon> polygon(dynamic_cast<OGRPolygon*>(tmp_geom->Buffer(radius, 20)));

    if (polygon == nullptr)
    {
      if (type != wkbMultiLineString)
        throw Fmi::Exception::Trace(BCP, "OGRGeometry::Buffer() function call failed!");

      // Iterare OGRLineStrings inside OGRMultiLineString  and expand one by one
      boost::scoped_ptr<OGRMultiLineString> multilinestring;
      multilinestring.reset(dynamic_cast<OGRMultiLineString*>(tmp_geom->clone()));
      int n_geometries(multilinestring->getNumGeometries());

      // Expanded geometries are put inside OGRMultiPolygon
      OGRMultiPolygon mpoly;
      for (int i = 0; i < n_geometries; i++)
      {
        boost::scoped_ptr<OGRGeometry> linestring;
        linestring.reset(multilinestring->getGeometryRef(i)->Buffer(radius, 20));
        mpoly.addGeometry(linestring.get());
      }
      // Convert back to original geometry
      if (mpoly.transform(CT2.get()) != OGRERR_NONE)
        throw Fmi::Exception::Trace(BCP, "OGRMultiPolygon::transform() function call failed");

      return mpoly.clone();
    }

    // get exterior ring of polygon
    // the returned ring pointer is to an internal data object of the OGRPolygon.
    // It should not be modified or deleted by the application
    OGRLinearRing* exring(polygon->getExteriorRing());

    // Convert back to original geometry
    if (exring->transform(CT2.get()) != OGRERR_NONE)
      throw Fmi::Exception::Trace(BCP, "OGRLinearRing::transform() function call failed");

    OGRPolygon poly;
    poly.addRing(exring);

    // polygon is simplified to reduce amount of points
    if (exring->getNumPoints() > 1000)
      ret = poly.SimplifyPreserveTopology(0.001);

    if (ret == nullptr)
      ret = poly.clone();

    return ret;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
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
  try
  {
    OGRGeometry* ret = nullptr;

    if (theGeom == nullptr)
    {
      throw Fmi::Exception::Trace(BCP, "ExpandGeometry failed: theGeom is nullptr!");
      return ret;
    }

    if (theRadiusInMeters <= 0.0)
      return theGeom->clone();

    // in case of  multipolygon, expand each polygon separately
    if (theGeom->getGeometryType() == wkbMultiPolygon)
    {
      boost::scoped_ptr<OGRMultiPolygon> multipoly;
      multipoly.reset(dynamic_cast<OGRMultiPolygon*>(theGeom->clone()));

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
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
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
 */
// ----------------------------------------------------------------------

boost::optional<double> Fmi::OGR::gridNorth(const CoordinateTransformation& theTransformation,
                                            double theLon,
                                            double theLat)
{
  try
  {
    // Actual coordinate
    double x1 = theLon;
    double y1 = theLat;

    // Move slightly to the north
    double x2 = theLon;
    double y2 = theLat + 0.0001;

    // Swap orientation if necessary near the north pole
    if (y2 >= 90)
    {
      y2 = theLat;
      y1 = theLat - 0.0001;
    }

    if (!theTransformation.transform(x1, y1) || !theTransformation.transform(x2, y2))
      return {};

    // Calculate the azimuth. Note that for us angle 0 is up and not to increasing x
    // as in normal math, hence we have rotated the system by swapping dx and dy in atan2

    return atan2(x2 - x1, y2 - y1) * boost::math::double_constants::radian;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Translation visitor
 */
// ----------------------------------------------------------------------

class TranslateVisitor : public OGRDefaultGeometryVisitor
{
 public:
  TranslateVisitor(double dx, double dy) : m_dx(dx), m_dy(dy) {}

  using OGRDefaultGeometryVisitor::visit;

  void visit(OGRPoint* point) override
  {
    try
    {
      if (point != nullptr)
      {
        point->setX(point->getX() + m_dx);
        point->setY(point->getY() + m_dy);
      }
    }
    catch (...)
    {
      throw Fmi::Exception::Trace(BCP, "Operation failed!");
    }
  }

 private:
  double m_dx = 0;
  double m_dy = 0;
};

// ----------------------------------------------------------------------
/*!
 * \brief Translate a geometry by a fixed amount
 */
// ----------------------------------------------------------------------

void Fmi::OGR::translate(OGRGeometry& theGeom, double dx, double dy)
{
  try
  {
    TranslateVisitor visitor(dx, dy);
    theGeom.accept(&visitor);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

void Fmi::OGR::translate(OGRGeometry* theGeom, double dx, double dy)
{
  try
  {
    if (theGeom != nullptr)
      translate(*theGeom, dx, dy);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}
