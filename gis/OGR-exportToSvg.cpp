#include "Box.h"
#include "OGR.h"
#include <fmt/format.h>
#include <fmt/printf.h>
#include <macgyver/Exception.h>
#include <ogr_geometry.h>

// Not in use on SmartMet Editor yet

#ifdef UNIX
#include <double-conversion/double-conversion.h>
#endif

using Fmi::Box;

namespace
{
// ----------------------------------------------------------------------
/*!
 * \brief Format a number to a local buffer and append it to string thus avoiding intermediate
 * std::string creation
 */
// ----------------------------------------------------------------------

#ifdef UNIX
void append_number(std::string &out, double num, const char * /* format */, int decimals)
{
  try
  {
    using namespace double_conversion;

    const int kBufferSize = 168;
    char buffer[kBufferSize];
    StringBuilder builder(buffer, kBufferSize);
    int flags = DoubleToStringConverter::UNIQUE_ZERO;

    // Not available in RHEL7 yet:
    // int flags = DoubleToStringConverter::UNIQUE_ZERO |
    // DoubleToStringConverter::NO_TRAILING_ZEROS;

    DoubleToStringConverter dc(flags, "Infinity", "NaN", 'e', 0, 0, 0, 0);

    if (!dc.ToFixed(num, decimals, &builder))
      out.append("NaN");

    // Remove trailing zeros and decimal point if possible. This can be removed
    // when NO_TRAILING_ZEROS becomes available.

    auto n = builder.position();  // must be called before next row!
    builder.Finalize();           // required to avoid asserts in debug mode

    auto pos = n;
    while (pos > 0 && buffer[--pos] == '0')
    {
    }
    if (buffer[pos] == ',' || buffer[pos] == '.')
      n = pos;

    out.append(buffer, n);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

#else

// The above version is about 13 times faster since fmt has to parse the format string again and
// again

void append_number(std::string &out, double num, const char *format, int /* decimals */)
{
  try
  {
    char buffer[30]{};  // zero initialized!
    if (strcmp(format, "%.0f") == 0)
      fmt::format_to(buffer, "%d", std::round(num));
    else
    {
      fmt::format_to(buffer, format, num);

      // Remove trailing zeros and decimal point if possible
      auto pos = strlen(buffer);
      while (pos > 0 && buffer[--pos] == '0')
      {
      }
      if (buffer[pos] == ',' || buffer[pos] == '.')
        buffer[pos] = '\0';
      // Convert -0 to 0
      if (strcmp(buffer, "-0") == 0)
      {
        out += '0';
        return;
      }
    }
    out.append(buffer);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}
#endif

}  // namespace

// Forward declaration needed since two functions call each other

void writeSVG(std::string &out,
              const OGRGeometry *geom,
              const Box &box,
              double rfactor,
              const char *format,
              int decimals);

// ----------------------------------------------------------------------
/*!
 * \brief Handle a Point
 */
// ----------------------------------------------------------------------

void writePointSVG(
    std::string &out, const OGRPoint *geom, const Box &box, const char *format, int decimals)
{
  try
  {
    if (geom != nullptr)
    {
      double x = geom->getX();
      double y = geom->getY();
      box.transform(x, y);
      out += 'M';
      append_number(out, x, format, decimals);
      out += ' ';
      append_number(out, y, format, decimals);
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

void writeLinearRingSVG(std::string &out,
                        const OGRLinearRing *geom,
                        const Box &box,
                        double rfactor,
                        const char *format,
                        int decimals)
{
  try
  {
    if (geom == nullptr || geom->IsEmpty() != 0)
      return;

    // Convert the numbers to rounded form

    const int n = geom->getNumPoints();

    std::vector<double> xx(n, 0.0);
    std::vector<double> yy(n, 0.0);
    for (int i = 0; i < n; ++i)
    {
      double x = geom->getX(i);
      double y = geom->getY(i);
      box.transform(x, y);
      xx[i] = std::round(x * rfactor) / rfactor;
      yy[i] = std::round(y * rfactor) / rfactor;
    }

    // Output the first point immediately so we don't have to test
    // for i==0 in the inner loop

    double prev_x = xx[0];
    double prev_y = yy[0];

    out += 'M';
    append_number(out, prev_x, format, decimals);
    out += ' ';
    append_number(out, prev_y, format, decimals);

    for (int i = 1; i < n - 1; ++i)
    {
      double new_x = xx[i];
      double new_y = yy[i];

      if (new_x != prev_x || new_y != prev_y)
      {
        out += ' ';
        append_number(out, new_x, format, decimals);
        out += ' ';
        append_number(out, new_y, format, decimals);
        prev_x = new_x;
        prev_y = new_y;
      }
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

void writeLineStringSVG(std::string &out,
                        const OGRLineString *geom,
                        const Box &box,
                        double rfactor,
                        const char *format,
                        int decimals)
{
  try
  {
    if (geom == nullptr || geom->IsEmpty() != 0)
      return;

    // Convert the numbers to rounded form

    const int n = geom->getNumPoints();

    std::vector<double> xx(n, 0.0);
    std::vector<double> yy(n, 0.0);
    for (int i = 0; i < n; ++i)
    {
      double x = geom->getX(i);
      double y = geom->getY(i);
      box.transform(x, y);
      xx[i] = std::round(x * rfactor) / rfactor;
      yy[i] = std::round(y * rfactor) / rfactor;
    }

    // Output the first point immediately so we don't have to test
    // for i==0 in the inner loop

    double prev_x = xx[0];
    double prev_y = yy[0];

    out += 'M';
    append_number(out, prev_x, format, decimals);
    out += ' ';
    append_number(out, prev_y, format, decimals);

    for (int i = 1; i < n; ++i)
    {
      double new_x = xx[i];
      double new_y = yy[i];

      if (new_x != prev_x || new_y != prev_y)
      {
        out += ' ';
        append_number(out, new_x, format, decimals);
        out += ' ';
        append_number(out, new_y, format, decimals);
        prev_x = new_x;
        prev_y = new_y;
      }
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

void writePolygonSVG(std::string &out,
                     const OGRPolygon *geom,
                     const Box &box,
                     double rfactor,
                     const char *format,
                     int decimals)
{
  try
  {
    if (geom == nullptr || geom->IsEmpty() != 0)
      return;

    writeLinearRingSVG(out, geom->getExteriorRing(), box, rfactor, format, decimals);
    for (int i = 0, n = geom->getNumInteriorRings(); i < n; ++i)
    {
      writeLinearRingSVG(out, geom->getInteriorRing(i), box, rfactor, format, decimals);
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

void writeMultiPointSVG(
    std::string &out, const OGRMultiPoint *geom, const Box &box, const char *format, int decimals)
{
  try
  {
    if (geom == nullptr || geom->IsEmpty() != 0)
      return;

    for (int i = 0, n = geom->getNumGeometries(); i < n; ++i)
    {
      writePointSVG(
          out, dynamic_cast<const OGRPoint *>(geom->getGeometryRef(i)), box, format, decimals);
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

void writeMultiLineStringSVG(std::string &out,
                             const OGRMultiLineString *geom,
                             const Box &box,
                             double rfactor,
                             const char *format,
                             int decimals)
{
  try
  {
    if (geom == nullptr || geom->IsEmpty() != 0)
      return;

    for (int i = 0, n = geom->getNumGeometries(); i < n; ++i)
      writeLineStringSVG(out,
                         dynamic_cast<const OGRLineString *>(geom->getGeometryRef(i)),
                         box,
                         rfactor,
                         format,
                         decimals);
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

void writeMultiPolygonSVG(std::string &out,
                          const OGRMultiPolygon *geom,
                          const Box &box,
                          double rfactor,
                          const char *format,
                          int decimals)
{
  try
  {
    if (geom == nullptr || geom->IsEmpty() != 0)
      return;

    for (int i = 0, n = geom->getNumGeometries(); i < n; ++i)
      writePolygonSVG(out,
                      dynamic_cast<const OGRPolygon *>(geom->getGeometryRef(i)),
                      box,
                      rfactor,
                      format,
                      decimals);
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

void writeGeometryCollectionSVG(std::string &out,
                                const OGRGeometryCollection *geom,
                                const Box &box,
                                double rfactor,
                                const char *format,
                                int decimals)
{
  try
  {
    if (geom == nullptr || geom->IsEmpty() != 0)
      return;

    for (int i = 0, n = geom->getNumGeometries(); i < n; ++i)
      writeSVG(out, geom->getGeometryRef(i), box, rfactor, format, decimals);
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

void writeSVG(std::string &out,
              const OGRGeometry *geom,
              const Box &box,
              double rfactor,
              const char *format,
              int decimals)
{
  try
  {
    OGRwkbGeometryType id = geom->getGeometryType();

    switch (id)
    {
      case wkbPoint:
      case wkbPoint25D:
        return writePointSVG(out, dynamic_cast<const OGRPoint *>(geom), box, format, decimals);
      case wkbLineString:
      case wkbLineString25D:
        return writeLineStringSVG(
            out, dynamic_cast<const OGRLineString *>(geom), box, rfactor, format, decimals);
      case wkbLinearRing:
        return writeLinearRingSVG(
            out, dynamic_cast<const OGRLinearRing *>(geom), box, rfactor, format, decimals);
      case wkbPolygon:
      case wkbPolygon25D:
        return writePolygonSVG(
            out, dynamic_cast<const OGRPolygon *>(geom), box, rfactor, format, decimals);
      case wkbMultiPoint:
      case wkbMultiPoint25D:
        return writeMultiPointSVG(
            out, dynamic_cast<const OGRMultiPoint *>(geom), box, format, decimals);
      case wkbMultiLineString:
      case wkbMultiLineString25D:
        return writeMultiLineStringSVG(
            out, dynamic_cast<const OGRMultiLineString *>(geom), box, rfactor, format, decimals);
      case wkbMultiPolygon:
      case wkbMultiPolygon25D:
        return writeMultiPolygonSVG(
            out, dynamic_cast<const OGRMultiPolygon *>(geom), box, rfactor, format, decimals);
      case wkbGeometryCollection:
      case wkbGeometryCollection25D:
        return writeGeometryCollectionSVG(
            out, dynamic_cast<const OGRGeometryCollection *>(geom), box, rfactor, format, decimals);
      default:
        throw Fmi::Exception::Trace(
            BCP, "Encountered an unknown geometry component in OGR to SVG conversion");
    }
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

std::string Fmi::OGR::exportToSvg(const OGRGeometry &theGeom,
                                  const Box &theBox,
                                  double thePrecision)
{
  try
  {
    // For backwards compatibility
    const double precision = std::max(0.0, thePrecision);

    const int decimals = std::ceil(precision);
    const double rfactor = pow(10.0, precision);
    const std::string format = "{:." + fmt::sprintf("%d", decimals) + "f}";

    std::string out;
    writeSVG(out, &theGeom, theBox, rfactor, format.c_str(), decimals);
    return out;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}
