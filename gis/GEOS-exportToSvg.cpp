#include "GEOS.h"

// GEOS does not seem to include <memory> as it should for Point.h
#include <memory>

#include <fmt/format.h>
#include <fmt/printf.h>
#include <geos/geom/Coordinate.h>
#include <geos/geom/CoordinateSequence.h>
#include <geos/geom/LineString.h>
#include <geos/geom/LinearRing.h>
#include <geos/geom/MultiLineString.h>
#include <geos/geom/MultiPoint.h>
#include <geos/geom/MultiPolygon.h>
#include <geos/geom/Point.h>
#include <geos/geom/Polygon.h>
#include <geos/geom/PrecisionModel.h>
#include <geos/version.h>
#include <macgyver/Exception.h>
#include <macgyver/NumericCast.h>

#define GEOS_VERSION_ID (100*GEOS_VERSION_MAJOR + GEOS_VERSION_MINOR)

//using geos::geom::Coordinate;
using geos::geom::Geometry;
using geos::geom::GeometryCollection;
using geos::geom::LinearRing;
using geos::geom::LineString;
using geos::geom::MultiLineString;
using geos::geom::MultiPoint;
using geos::geom::MultiPolygon;
using geos::geom::Point;
using geos::geom::Polygon;

#if GEOS_VERSION_ID >= 312
using Coordinate = geos::geom::CoordinateXY;
#else
using Coordinate = geos::geom::Coordinate;
#endif

namespace
{
// ----------------------------------------------------------------------
/*!
 * \brief Pretty print a number
 */
// ----------------------------------------------------------------------

std::string pretty(double num, const char* format)
{
  try
  {
    if (strcmp(format, "%.0f") == 0)
      return fmt::sprintf("%d", static_cast<long>(round(num)));

    std::string ret = fmt::sprintf(format, num);
    std::size_t pos = ret.size();
    while (pos > 0 && ret[--pos] == '0')
    {
    }

    if (ret[pos] != ',' && ret[pos] != '.')
      return ret;

    ret.resize(pos);

    if (ret != "-0")
      return ret;
    return "0";
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}
}  // namespace

// Needed since two functions call each other
void writeSVG(std::string& out, const Geometry* geom, const char* format);
// ----------------------------------------------------------------------
/*!
 * \brief Handle a coordinate
 */
// ----------------------------------------------------------------------

void writeCoordinateSVG(std::string& out, const Coordinate& geom, const char* format)
{
  try
  {
    out += pretty(geom.x, format);
    out += ' ';
    out += pretty(geom.y, format);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Handle a Point
 */
// ----------------------------------------------------------------------

void writePointSVG(std::string& out, const Coordinate* geom, const char* format)
{
  try
  {
    if (geom != nullptr)
    {
      out += 'M';
      out += pretty(geom->x, format);
      out += ' ';
      out += pretty(geom->y, format);
    }
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Handle LinearRing
 */
// ----------------------------------------------------------------------

void writeLinearRingSVG(std::string& out, const LinearRing* geom, const char* format)
{
  try
  {
    if (geom == nullptr || geom->isEmpty())
      return;
    // Bizarre: n is unsigned long but getting coordinate uses int
    for (unsigned long i = 0, n = geom->getNumPoints(); i < n - 1; ++i)
    {
      if (i == 0)
        out += 'M';
      else
        out += ' ';  // No need to write an 'L', lineto is implicit for i>0
      writeCoordinateSVG(out, geom->getCoordinateN(Fmi::numeric_cast<int>(i)), format);
    }
    out += 'Z';
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Handle LineString
 */
// ----------------------------------------------------------------------

void writeLineStringSVG(std::string& out, const LineString* geom, const char* format)
{
  try
  {
    if (geom == nullptr || geom->isEmpty())
      return;

    unsigned long n = geom->getNumPoints();
    for (unsigned long i = 0; i < n - 1; ++i)
    {
      if (i == 0)
        out += 'M';
      else
        out += ' ';  // No need to write an 'L', lineto is implicit for i>0
      writeCoordinateSVG(out, geom->getCoordinateN(Fmi::numeric_cast<int>(i)), format);
    }

    if (geom->isClosed())
      out += 'Z';
    else
    {
      out += (n == 1 ? 'M' : ' ');
      writeCoordinateSVG(out, geom->getCoordinateN(Fmi::numeric_cast<int>(n - 1)), format);
    }
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

void writePolygonSVG(std::string& out, const Polygon* geom, const char* format)
{
  try
  {
    if (geom == nullptr || geom->isEmpty())
      return;

    writeLineStringSVG(out, geom->getExteriorRing(), format);
    for (size_t i = 0, n = geom->getNumInteriorRing(); i < n; ++i)
    {
      writeLineStringSVG(out, geom->getInteriorRingN(i), format);
    }
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Handle MultiPoint
 */
// ----------------------------------------------------------------------

void writeMultiPointSVG(std::string& out, const MultiPoint* geom, const char* format)
{
  try
  {
    if (geom == nullptr || geom->isEmpty())
      return;

    for (size_t i = 0, n = geom->getNumGeometries(); i < n; ++i)
    {
      out += 'M';
      writeCoordinateSVG(out, *geom->getGeometryN(i)->getCoordinate(), format);
    }
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Handle MultiLineString
 */
// ----------------------------------------------------------------------

void writeMultiLineStringSVG(std::string& out, const MultiLineString* geom, const char* format)
{
  try
  {
    if (geom == nullptr || geom->isEmpty())
      return;
    for (size_t i = 0, n = geom->getNumGeometries(); i < n; ++i)
      writeLineStringSVG(out, dynamic_cast<const LineString*>(geom->getGeometryN(i)), format);
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

void writeMultiPolygonSVG(std::string& out, const MultiPolygon* geom, const char* format)
{
  try
  {
    if (geom == nullptr || geom->isEmpty())
      return;
    for (size_t i = 0, n = geom->getNumGeometries(); i < n; ++i)
      writePolygonSVG(out, dynamic_cast<const Polygon*>(geom->getGeometryN(i)), format);
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

void writeGeometryCollectionSVG(std::string& out,
                                const GeometryCollection* geom,
                                const char* format)
{
  try
  {
    if (geom == nullptr || geom->isEmpty())
      return;
    for (size_t i = 0, n = geom->getNumGeometries(); i < n; ++i)
      writeSVG(out, geom->getGeometryN(i), format);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Write the geometry as a SVG path
 *
 * This could be more efficiently done if the geometries supported
 * a visitor.
 */
// ----------------------------------------------------------------------

void writeSVG(std::string& out, const Geometry* geom, const char* format)
{
  try
  {
    if (const auto* point = dynamic_cast<const Point*>(geom))
      writePointSVG(out, point->getCoordinate(), format);
    else if (const auto* lr = dynamic_cast<const LinearRing*>(geom))
      writeLinearRingSVG(out, lr, format);
    else if (const auto* ls = dynamic_cast<const LineString*>(geom))
      writeLineStringSVG(out, ls, format);
    else if (const auto* p = dynamic_cast<const Polygon*>(geom))
      writePolygonSVG(out, p, format);
    else if (const auto* mp = dynamic_cast<const MultiPoint*>(geom))
      writeMultiPointSVG(out, mp, format);
    else if (const auto* ml = dynamic_cast<const MultiLineString*>(geom))
      writeMultiLineStringSVG(out, ml, format);
    else if (const auto* mpg = dynamic_cast<const MultiPolygon*>(geom))
      writeMultiPolygonSVG(out, mpg, format);
    else if (const auto* g = dynamic_cast<const GeometryCollection*>(geom))
      writeGeometryCollectionSVG(out, g, format);
    else
      throw Fmi::Exception::Trace(BCP, "Encountered an unsupported GEOS geometry component");
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Convert the geometry to a SVG string
 */
// ----------------------------------------------------------------------

std::string Fmi::GEOS::exportToSvg(const Geometry& theGeom, int thePrecision)
{
  try
  {
    // Actual number of decimals to use is automatically selected if
    // no precision is given or it is negative. The code here is modeled
    // after geos::io::WKTWriter

    int decimals = (thePrecision < 0 ? theGeom.getPrecisionModel()->getMaximumSignificantDigits()
                                     : thePrecision);

    std::string format = "%." + fmt::sprintf("%d", decimals) + "f";

    std::string out;
    writeSVG(out, &theGeom, format.c_str());
    return out;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}
