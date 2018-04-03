#include "Box.h"
#include "OGR.h"
#include <fmt/format.h>
#include <gdal/ogr_geometry.h>
#include <stdexcept>

using Fmi::Box;

namespace
{
// ----------------------------------------------------------------------
/*!
 * \brief Pretty print a number
 */
// ----------------------------------------------------------------------

std::string pretty(double num, const char *format)
{
  if (strcmp(format, "%.0f") == 0) return fmt::sprintf("%d", static_cast<long>(round(num)));

  std::string ret = fmt::sprintf(format, num);
  std::size_t pos = ret.size();
  while (pos > 0 && ret[--pos] == '0')
  {
  }

  if (ret[pos] != ',' && ret[pos] != '.') return ret;

  ret.resize(pos);

  if (ret != "-0")
    return ret;
  else
    return "0";
}
}  // namespace

// Forward declaration needed since two functions call each other

void writeSVG(std::string &string, const OGRGeometry *geom, const Box &box, const char *format);

// ----------------------------------------------------------------------
/*!
 * \brief Handle a Point
 */
// ----------------------------------------------------------------------

void writePointSVG(std::string &out, const OGRPoint *geom, const Box &box, const char *format)
{
  if (geom != NULL)
  {
    double x = geom->getX();
    double y = geom->getY();
    box.transform(x, y);
    out += 'M' + pretty(x, format) + ' ' + pretty(y, format);
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
                        const char *format)
{
  if (geom == NULL || geom->IsEmpty()) return;

  // Note: Loop terminates before last, OGR rings are always explicitly closed
  //       by duplicating the coordinates but we can just use 'Z'

  // Output the first point immediately so we don't have to test
  // for i==0 in the inner loop

  double x = geom->getX(0);
  double y = geom->getY(0);
  box.transform(x, y);

  std::string previous_x = pretty(x, format);
  std::string previous_y = pretty(y, format);

  out += 'M' + previous_x + ' ' + previous_y;

  int n = geom->getNumPoints();

  for (int i = 1; i < n - 1; ++i)
  {
    x = geom->getX(i);
    y = geom->getY(i);
    box.transform(x, y);
    std::string sx = pretty(x, format);
    std::string sy = pretty(y, format);

    if (sx != previous_x || sy != previous_y)
    {
      out += ' ' + sx + ' ' + sy;
      previous_x = sx;
      previous_y = sy;
    }
  }

  out += 'Z';
}

// ----------------------------------------------------------------------
/*!
 * \brief Handle LineString
 */
// ----------------------------------------------------------------------

void writeLineStringSVG(std::string &out,
                        const OGRLineString *geom,
                        const Box &box,
                        const char *format)
{
  if (geom == NULL || geom->IsEmpty()) return;

  // Output the first point immediately so we don't have to test
  // for i==0 in the inner loop

  double x = geom->getX(0);
  double y = geom->getY(0);
  box.transform(x, y);

  std::string previous_x = pretty(x, format);
  std::string previous_y = pretty(y, format);

  out += 'M' + previous_x + ' ' + previous_y;

  // Save the first point for closure tests
  double x0 = geom->getX(0);
  double y0 = geom->getY(0);

  int n = geom->getNumPoints();

  for (int i = 1; i < n; ++i)
  {
    double x = geom->getX(i);
    double y = geom->getY(i);
    box.transform(x, y);

    // Close with Z if possible. In the extreme case we might get "M<num> <num>Z"
    if (i == n - 1 && x == x0 && y == y0)
      out += 'Z';
    else
    {
      std::string sx = pretty(x, format);
      std::string sy = pretty(y, format);

      if (sx != previous_x || sy != previous_y)
      {
        out += ' ' + sx + ' ' + sy;
        previous_x = sx;
        previous_y = sy;
      }
    }
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Handle Polygon
 */
// ----------------------------------------------------------------------

void writePolygonSVG(std::string &out, const OGRPolygon *geom, const Box &box, const char *format)
{
  if (geom == NULL || geom->IsEmpty()) return;

  writeLinearRingSVG(out, geom->getExteriorRing(), box, format);
  for (int i = 0, n = geom->getNumInteriorRings(); i < n; ++i)
  {
    writeLinearRingSVG(out, geom->getInteriorRing(i), box, format);
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Handle MultiPoint
 */
// ----------------------------------------------------------------------

void writeMultiPointSVG(std::string &out,
                        const OGRMultiPoint *geom,
                        const Box &box,
                        const char *format)
{
  if (geom == NULL || geom->IsEmpty()) return;

  for (int i = 0, n = geom->getNumGeometries(); i < n; ++i)
  {
    writePointSVG(out, static_cast<const OGRPoint *>(geom->getGeometryRef(i)), box, format);
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
                             const char *format)
{
  if (geom == NULL || geom->IsEmpty()) return;
  for (int i = 0, n = geom->getNumGeometries(); i < n; ++i)
    writeLineStringSVG(
        out, static_cast<const OGRLineString *>(geom->getGeometryRef(i)), box, format);
}

// ----------------------------------------------------------------------
/*!
 * \brief Handle MultiPolygon
 */
// ----------------------------------------------------------------------

void writeMultiPolygonSVG(std::string &out,
                          const OGRMultiPolygon *geom,
                          const Box &box,
                          const char *format)
{
  if (geom == NULL || geom->IsEmpty()) return;
  for (int i = 0, n = geom->getNumGeometries(); i < n; ++i)
    writePolygonSVG(out, static_cast<const OGRPolygon *>(geom->getGeometryRef(i)), box, format);
}

// ----------------------------------------------------------------------
/*!
 * \brief Handle GeometryCollection
 */
// ----------------------------------------------------------------------

void writeGeometryCollectionSVG(std::string &out,
                                const OGRGeometryCollection *geom,
                                const Box &box,
                                const char *format)
{
  if (geom == NULL || geom->IsEmpty()) return;
  for (int i = 0, n = geom->getNumGeometries(); i < n; ++i)
    writeSVG(out, geom->getGeometryRef(i), box, format);
}

// ----------------------------------------------------------------------
/*!
 * \brief Write the geometry as a SVG path
 *
 * This could be more efficiently done if the geometries supported
 * a visitor.
 */
// ----------------------------------------------------------------------

void writeSVG(std::string &out, const OGRGeometry *geom, const Box &box, const char *format)
{
  OGRwkbGeometryType id = geom->getGeometryType();

  switch (id)
  {
    case wkbPoint:
    case wkbPoint25D:
      return writePointSVG(out, static_cast<const OGRPoint *>(geom), box, format);
    case wkbLineString:
    case wkbLineString25D:
      return writeLineStringSVG(out, static_cast<const OGRLineString *>(geom), box, format);
    case wkbLinearRing:
      return writeLinearRingSVG(out, static_cast<const OGRLinearRing *>(geom), box, format);
    case wkbPolygon:
    case wkbPolygon25D:
      return writePolygonSVG(out, static_cast<const OGRPolygon *>(geom), box, format);
    case wkbMultiPoint:
    case wkbMultiPoint25D:
      return writeMultiPointSVG(out, static_cast<const OGRMultiPoint *>(geom), box, format);
    case wkbMultiLineString:
    case wkbMultiLineString25D:
      return writeMultiLineStringSVG(
          out, static_cast<const OGRMultiLineString *>(geom), box, format);
    case wkbMultiPolygon:
    case wkbMultiPolygon25D:
      return writeMultiPolygonSVG(out, static_cast<const OGRMultiPolygon *>(geom), box, format);
    case wkbGeometryCollection:
    case wkbGeometryCollection25D:
      return writeGeometryCollectionSVG(
          out, static_cast<const OGRGeometryCollection *>(geom), box, format);
    case wkbUnknown:
      throw std::runtime_error(
          "Encountered an unknown geometry component in OGR to SVG conversion");
    case wkbNone:
      throw std::runtime_error("Encountered a 'none' geometry component in OGR to SVG conversion");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Convert the geometry to a SVG string
 */
// ----------------------------------------------------------------------

std::string Fmi::OGR::exportToSvg(const OGRGeometry &theGeom, const Box &theBox, int thePrecision)
{
  // Actual number of decimals to use is automatically selected if
  // no precision is given or it is negative.

  int decimals = (thePrecision < 0 ? 1 : thePrecision);

  std::string format = "%." + fmt::sprintf("%d", decimals) + "f";

  std::string out;
  writeSVG(out, &theGeom, theBox, format.c_str());
  return out;
}
