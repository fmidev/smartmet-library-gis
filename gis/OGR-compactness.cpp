// ======================================================================
/*!
 * \file
 * \brief Isoperimetric compactness 4πA/L² for OGR polygons,
 *        plus a polygon-collection filter that drops "ragged" shapes.
 *
 * The maths is purely geometric and dimensionless, so this works on
 * any planar OGRGeometry. For polygons stored in a geographic CRS
 * (lat/lon) we evaluate area on the WGS84 sphere and perimeter via
 * great-circle arcs — otherwise high-latitude polygons would score
 * far below their true compactness because 1° of longitude shrinks
 * toward the poles.
 */
// ======================================================================

#include "OGR.h"

#include <boost/math/constants/constants.hpp>
#include <macgyver/Exception.h>
#include <ogr_geometry.h>
#include <ogr_spatialref.h>

#include <cmath>
#include <memory>

namespace
{
constexpr double kEarthRadiusMeters = 6378137.0;

// Spherical area of a closed ring on the WGS84 sphere, in m². Mirrors
// the convention used by Fmi::OGR::despeckle's geographic_area helper:
// the formula returns a signed result; we take the absolute value.
double geographic_area(const OGRLineString* line)
{
  if (line == nullptr)
    return 0.0;
  const std::size_t n = line->getNumPoints();
  if (n < 2)
    return 0.0;

  double area = 0.0;
  for (std::size_t i = 0; i + 1 < n; ++i)
  {
    const double x1 = line->getX(i) * boost::math::double_constants::degree;
    const double y1 = line->getY(i) * boost::math::double_constants::degree;
    const double x2 = line->getX(i + 1) * boost::math::double_constants::degree;
    const double y2 = line->getY(i + 1) * boost::math::double_constants::degree;
    area += (x2 - x1) * (2.0 + std::sin(y1) + std::sin(y2));
  }
  area *= kEarthRadiusMeters * kEarthRadiusMeters / 2.0;
  return std::abs(area);
}

// Great-circle perimeter on the WGS84 sphere, in m.
double geographic_length(const OGRLineString* line)
{
  if (line == nullptr)
    return 0.0;
  const std::size_t n = line->getNumPoints();
  if (n < 2)
    return 0.0;

  double total = 0.0;
  for (std::size_t i = 0; i + 1 < n; ++i)
  {
    const double lon1 = line->getX(i) * boost::math::double_constants::degree;
    const double lat1 = line->getY(i) * boost::math::double_constants::degree;
    const double lon2 = line->getX(i + 1) * boost::math::double_constants::degree;
    const double lat2 = line->getY(i + 1) * boost::math::double_constants::degree;
    const double dlon = lon2 - lon1;
    const double dlat = lat2 - lat1;
    const double sdlat2 = std::sin(0.5 * dlat);
    const double sdlon2 = std::sin(0.5 * dlon);
    const double a = sdlat2 * sdlat2 + std::cos(lat1) * std::cos(lat2) * sdlon2 * sdlon2;
    total += 2.0 * kEarthRadiusMeters * std::asin(std::min(1.0, std::sqrt(a)));
  }
  return total;
}

bool spatial_ref_is_geographic(const OGRSpatialReference* sr)
{
  return (sr != nullptr) && (sr->IsGeographic() != 0);
}

double ring_area(const OGRLinearRing* ring, bool geographic)
{
  if (ring == nullptr)
    return 0.0;
  if (geographic)
    return geographic_area(static_cast<const OGRLineString*>(ring));
  return std::abs(ring->get_Area());
}

double ring_length(const OGRLinearRing* ring, bool geographic)
{
  if (ring == nullptr)
    return 0.0;
  if (geographic)
    return geographic_length(static_cast<const OGRLineString*>(ring));
  return ring->get_Length();
}

double polygon_compactness(const OGRPolygon& poly,
                           Fmi::OGR::CompactnessMode mode,
                           bool geographic)
{
  const auto* ext = poly.getExteriorRing();
  if (ext == nullptr)
    return 0.0;

  double area = ring_area(ext, geographic);
  double length = ring_length(ext, geographic);

  if (mode == Fmi::OGR::CompactnessMode::Net)
  {
    const int holes = poly.getNumInteriorRings();
    for (int i = 0; i < holes; ++i)
    {
      const auto* hole = poly.getInteriorRing(i);
      area -= ring_area(hole, geographic);
      length += ring_length(hole, geographic);
    }
  }

  if (length <= 0.0 || area <= 0.0)
    return 0.0;

  return 4.0 * boost::math::double_constants::pi * area / (length * length);
}

double polygon_area(const OGRPolygon& poly, bool geographic)
{
  // For Exterior-mode area filtering we want exterior-only area; net area
  // can include negative contributions from holes. Most callers asking
  // "area >= X" mean the exterior. Match that convention regardless of
  // CompactnessMode: the area filter is independent of the shape filter.
  return ring_area(poly.getExteriorRing(), geographic);
}

bool keep_polygon(const OGRPolygon& poly,
                  double minCompactness,
                  double minAreaNative,
                  Fmi::OGR::CompactnessMode mode,
                  bool geographic)
{
  if (minAreaNative > 0.0 && polygon_area(poly, geographic) < minAreaNative)
    return false;

  if (minCompactness > 0.0 &&
      polygon_compactness(poly, mode, geographic) < minCompactness)
    return false;

  return true;
}

OGRGeometry* filter_geom(const OGRGeometry* g,
                         double minCompactness,
                         double minAreaNative,
                         Fmi::OGR::CompactnessMode mode,
                         bool geographic);

OGRGeometry* filter_polygon(const OGRPolygon* poly,
                            double minCompactness,
                            double minAreaNative,
                            Fmi::OGR::CompactnessMode mode,
                            bool geographic)
{
  if (poly == nullptr)
    return nullptr;
  if (keep_polygon(*poly, minCompactness, minAreaNative, mode, geographic))
    return poly->clone();
  return nullptr;
}

OGRGeometry* filter_multipolygon(const OGRMultiPolygon* mp,
                                 double minCompactness,
                                 double minAreaNative,
                                 Fmi::OGR::CompactnessMode mode,
                                 bool geographic)
{
  if (mp == nullptr)
    return nullptr;
  std::unique_ptr<OGRMultiPolygon> out(new OGRMultiPolygon);
  const int n = mp->getNumGeometries();
  for (int i = 0; i < n; ++i)
  {
    const auto* p = dynamic_cast<const OGRPolygon*>(mp->getGeometryRef(i));
    if (p != nullptr && keep_polygon(*p, minCompactness, minAreaNative, mode, geographic))
      out->addGeometry(p);  // clones
  }
  if (out->getNumGeometries() == 0)
    return nullptr;
  return out.release();
}

OGRGeometry* filter_collection(const OGRGeometryCollection* col,
                               double minCompactness,
                               double minAreaNative,
                               Fmi::OGR::CompactnessMode mode,
                               bool geographic)
{
  if (col == nullptr)
    return nullptr;
  std::unique_ptr<OGRGeometryCollection> out(new OGRGeometryCollection);
  const int n = col->getNumGeometries();
  for (int i = 0; i < n; ++i)
  {
    auto* sub = filter_geom(
        col->getGeometryRef(i), minCompactness, minAreaNative, mode, geographic);
    if (sub != nullptr)
      out->addGeometryDirectly(sub);
  }
  if (out->getNumGeometries() == 0)
    return nullptr;
  return out.release();
}

OGRGeometry* filter_geom(const OGRGeometry* g,
                         double minCompactness,
                         double minAreaNative,
                         Fmi::OGR::CompactnessMode mode,
                         bool geographic)
{
  if (g == nullptr)
    return nullptr;

  switch (wkbFlatten(g->getGeometryType()))
  {
    case wkbPolygon:
      return filter_polygon(dynamic_cast<const OGRPolygon*>(g),
                            minCompactness,
                            minAreaNative,
                            mode,
                            geographic);
    case wkbMultiPolygon:
      return filter_multipolygon(dynamic_cast<const OGRMultiPolygon*>(g),
                                 minCompactness,
                                 minAreaNative,
                                 mode,
                                 geographic);
    case wkbGeometryCollection:
      return filter_collection(dynamic_cast<const OGRGeometryCollection*>(g),
                               minCompactness,
                               minAreaNative,
                               mode,
                               geographic);
    default:
      // Points / lines / etc — pass through unchanged.
      return g->clone();
  }
}

}  // namespace

double Fmi::OGR::compactness(const OGRPolygon& thePoly, CompactnessMode theMode)
{
  try
  {
    const bool geographic = spatial_ref_is_geographic(thePoly.getSpatialReference());
    return polygon_compactness(thePoly, theMode, geographic);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

double Fmi::OGR::compactness(double theArea, double thePerimeter)
{
  if (theArea <= 0.0 || thePerimeter <= 0.0)
    return 0.0;
  return 4.0 * boost::math::double_constants::pi * theArea / (thePerimeter * thePerimeter);
}

OGRGeometry* Fmi::OGR::filterByCompactness(const OGRGeometry& theGeom,
                                            double theMinCompactness,
                                            double theMinArea,
                                            CompactnessMode theMode)
{
  try
  {
    const bool geographic = spatial_ref_is_geographic(theGeom.getSpatialReference());

    // theMinArea is in km²; convert to native units (m² for geographic CRS,
    // CRS-units² for projected — matching Fmi::OGR::despeckle's convention).
    const double minAreaNative = theMinArea * 1000.0 * 1000.0;

    auto* result =
        filter_geom(&theGeom, theMinCompactness, minAreaNative, theMode, geographic);

    if (result != nullptr)
      result->assignSpatialReference(theGeom.getSpatialReference());

    return result;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}
