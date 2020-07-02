#include "Interrupt.h"
#include "OGR.h"
#include "ProjInfo.h"
#include "SpatialReference.h"
#include <boost/math/constants/constants.hpp>
#include <ogr_geometry.h>
#include <ogr_spatialref.h>

namespace Fmi
{
const double epsilon = 1e-6;
const double poleshift = 0.1;  // extend the cut beyond the poles for boolean operations

const double wgs84radius = 6378137.0;

const int default_circle_segments = 360;

// Longitude to -180...180 range
double modlon(double lon)
{
  if (lon > 180) return fmod(lon + 180, 360) - 180;
  if (lon < -180) return -(fmod(-lon + 180, 360) - 180);
  return lon;
}

// Create a circle in WGS84 coordinates. Range may span from -360 to +360

OGRPolygon* make_circle(double lon, double lat, double radius, int segments)
{
  OGRPolygon* poly = new OGRPolygon;
  OGRLinearRing* ring = new OGRLinearRing;

  // We start from -180 instead of zero for southern circles to avoid extra joining work
  const auto angle_offset = (lat >= 0 ? 0.0 : -M_PI);

  const auto lon1 = lon * boost::math::double_constants::degree;
  const auto lat1 = lat * boost::math::double_constants::degree;
  const auto dr = radius / wgs84radius;  // angular distance in radians

  const auto sindr = sin(dr);
  const auto cosdr = cos(dr);

  const auto sinlat1 = sin(lat1);
  const auto coslat1 = cos(lat1);

  for (int i = 0; i <= segments; i++)
  {
    const auto angle = 2 * M_PI * i / segments + angle_offset;

    auto la = asin(sinlat1 * cosdr + coslat1 * sindr * cos(angle));
    auto lo = lon1 + atan2(sin(angle) * sindr * coslat1, cosdr - sinlat1 * sin(la));

    la *= boost::math::double_constants::radian;
    lo *= boost::math::double_constants::radian;

    // Note: No mod 360 math here, we wish to preserve the overflow for clipping

    ring->addPoint(lo, la);
  }

  // Now we need to check if we must add either pole to close the ring

  const auto n = ring->getNumPoints();

  const auto x1 = ring->getX(0);
  const auto y1 = ring->getY(0);
  auto x2 = ring->getX(n - 1);
  auto y2 = ring->getY(n - 1);

  const double step = 10;  // degrees

  if (std::hypot(x1 - x2, y1 - y2) > 1e-3)
  {
    if (lat >= 0)
    {
      // close via north pole
      while (y2 + step < 90)
      {
        y2 += step;
        ring->addPoint(x2, y2);
      }
      while (x2 < x1)
      {
        ring->addPoint(x2, 90);
        x2 += step;
      }
      y2 = 90;
      while (y2 > y1)
      {
        ring->addPoint(x1, y2);
        y2 -= step;
      }
    }
    else
    {
      // close via south pole
      while (y2 - step > -90)
      {
        y2 -= step;
        ring->addPoint(x2, y2);
      }
      while (x2 > x1)
      {
        ring->addPoint(x2, -90);
        x2 -= step;
      }
      y2 = -90;
      while (y2 < y1)
      {
        ring->addPoint(x1, y2);
        y2 += step;
      }
    }
  }

  ring->closeRings();  // close if not already closed
  poly->addRingDirectly(ring);
  return poly;
}

// Create a rect

OGRPolygon* make_rect(double x1, double y1, double x2, double y2)
{
  OGRPolygon* poly = new OGRPolygon;
  OGRLinearRing* ring = new OGRLinearRing;

  ring->addPoint(x1, y1);
  ring->addPoint(x1, y2);
  ring->addPoint(x2, y2);
  ring->addPoint(x2, y1);
  ring->addPoint(x1, y1);

  poly->addRingDirectly(ring);
  return poly;
}

// Create a circle cutgeometry

OGRGeometry* circle_cut(double lon,
                        double lat,
                        double radius,
                        int segments = default_circle_segments)
{
  // One circle
  auto* geom = make_circle(lon, lat, radius, segments);

  // Extract envelope

  OGREnvelope env;
  geom->getEnvelope(&env);

  // Nothing to do if the circle is fully within normal bounds
  if (env.MinX >= -180 && env.MaxX <= 180)
  {
    geom->assignSpatialReference(OGRSpatialReference::GetWGS84SRS());
    return geom;
  }

  // Otherwise we must take at least 2 intersections, maybe 3

  OGRGeometryCollection* result = new OGRGeometryCollection;

  auto* rect = make_rect(-180, -90, 180, 90);
  auto* cut = geom->Intersection(rect);
  if (cut != nullptr && cut->IsEmpty() == 0) result->addGeometryDirectly(cut);
  CPLFree(rect);

  if (env.MinX < -180)
  {
    auto* rect = make_rect(-540, -90, -180, 90);
    auto* cut = geom->Intersection(rect);
    OGR::translate(cut, +360, 0);
    if (cut != nullptr && cut->IsEmpty() == 0) result->addGeometryDirectly(cut);
    CPLFree(rect);
  }

  if (env.MaxX > 180)
  {
    auto* rect = make_rect(180, -90, 540, 90);
    auto* cut = geom->Intersection(rect);
    OGR::translate(cut, -360, 0);
    if (cut != nullptr && cut->IsEmpty() == 0) result->addGeometryDirectly(cut);
    CPLFree(rect);
  }

  CPLFree(geom);

  result->assignSpatialReference(OGRSpatialReference::GetWGS84SRS());
  return result;
}

// Add latitudes at 1 degree intervals as in -90.1 -89 -88 ... 89 90.1

OGRPolygon* vertical_cut(double lon, int lat1, int lat2, int resolution = 1)
{
  OGRPolygon* poly = new OGRPolygon;
  OGRLinearRing* ring = new OGRLinearRing;
  auto x1 = lon - epsilon;
  auto x2 = lon + epsilon;
  auto y1 = std::min(lat1, lat2);
  auto y2 = std::max(lat1, lat2);

  // From lon-eps,y1 up to to lon-eps,y2
  for (int y = y1; y <= y2; y += resolution)
    ring->addPoint(x1, y + (y == -90 ? -poleshift : y == 90 ? +poleshift : 0));

  // From lon+eps,y2 down to lon+eps,y1
  for (int y = y2; y >= y1; y -= resolution)
    ring->addPoint(x2, y + (y == -90 ? -poleshift : y == 90 ? +poleshift : 0));

  ring->closeRings();
  poly->addRingDirectly(ring);
  return poly;
}

Interrupt interruptGeometry(const SpatialReference& theSRS)
{
  Interrupt result;

  // Geographic: cut everything at lon_wrap antimeridian
  if (theSRS.isGeographic())
  {
    const auto opt_lon_wrap = theSRS.projInfo().getDouble("o_wrap");
    const auto lon_wrap = (opt_lon_wrap ? *opt_lon_wrap : 180.0);
    OGRMultiPolygon* mpoly = new OGRMultiPolygon;
    mpoly->assignSpatialReference(OGRSpatialReference::GetWGS84SRS());
    mpoly->addGeometryDirectly(vertical_cut(modlon(lon_wrap - 180), -90, 90));
    if (lon_wrap == 0) mpoly->addGeometryDirectly(vertical_cut(modlon(lon_wrap + 180), -90, 90));

    result.cutGeometry.reset(mpoly);
    return result;
  }

  const auto opt_name = theSRS.projInfo().getString("proj");
  if (!opt_name) return result;

  const auto name = *opt_name;

  const auto opt_lon_0 = theSRS.projInfo().getDouble("lon_0");
  const auto lon_0 = opt_lon_0 ? *opt_lon_0 : 0.0;

  const auto opt_lat_0 = theSRS.projInfo().getDouble("lat_0");
  const auto lat_0 = opt_lat_0 ? *opt_lat_0 : 0.0;

  // Polar projections
  if (name == "aeqd" || name == "stere" || name == "sterea" || name == "ups") return result;

  // Spherical cuts
  if (name == "laea" || name == "airy" || name == "geos" || name == "gnom" || name == "ortho" ||
      name == "tpeqd")
  {
    // We take less than a full 90 degree view to avoid boundary effects
    const auto radius = 90 * wgs84radius * boost::math::double_constants::degree;
    result.andGeometry.reset(circle_cut(lon_0, lat_0, radius));
    return result;
  }

  if (name == "igh")
  {
    // Interrupted Goode Homolosine

    OGRMultiPolygon* mpoly = new OGRMultiPolygon;
    mpoly->assignSpatialReference(OGRSpatialReference::GetWGS84SRS());
    mpoly->addGeometryDirectly(vertical_cut(modlon(lon_0 - 180), -90, 90));
    if (lon_0 == 0) mpoly->addGeometryDirectly(vertical_cut(modlon(lon_0 + 180), -90, 90));
    mpoly->addGeometryDirectly(vertical_cut(modlon(lon_0 - 40), 0, 90));
    mpoly->addGeometryDirectly(vertical_cut(modlon(lon_0 - 100), -90, 0));
    mpoly->addGeometryDirectly(vertical_cut(modlon(lon_0 - 20), -90, 0));
    mpoly->addGeometryDirectly(vertical_cut(modlon(lon_0 + 80), -90, 0));

    result.cutGeometry.reset(mpoly);
    return result;
  }

  if (name == "healpix")
  {
    OGRMultiPolygon* mpoly = new OGRMultiPolygon;
    mpoly->assignSpatialReference(OGRSpatialReference::GetWGS84SRS());
    mpoly->addGeometryDirectly(vertical_cut(modlon(lon_0 - 180), -90, 90));
    if (lon_0 == 0) mpoly->addGeometryDirectly(vertical_cut(modlon(lon_0 + 180), -90, 90));
    mpoly->addGeometryDirectly(vertical_cut(modlon(lon_0 - 90), -90, -45));
    mpoly->addGeometryDirectly(vertical_cut(modlon(lon_0 - 90), 45, 90));
    mpoly->addGeometryDirectly(vertical_cut(modlon(lon_0), -90, -45));
    mpoly->addGeometryDirectly(vertical_cut(modlon(lon_0), 45, 90));
    mpoly->addGeometryDirectly(vertical_cut(modlon(lon_0 + 90), -90, -45));
    mpoly->addGeometryDirectly(vertical_cut(modlon(lon_0 + 90), 45, 90));

    result.cutGeometry.reset(mpoly);
    return result;
  }

  // Regular geometric: cut everything at lon_0+180 antimeridian
  // lon_0 is needed for all remaining geometric projections

  OGRMultiPolygon* mpoly = new OGRMultiPolygon;
  mpoly->assignSpatialReference(OGRSpatialReference::GetWGS84SRS());
  mpoly->addGeometryDirectly(vertical_cut(modlon(lon_0 - 180), -90, 90));
  if (lon_0 == 0) mpoly->addGeometryDirectly(vertical_cut(modlon(lon_0 + 180), -90, 90));

  result.cutGeometry.reset(mpoly);
  return result;
}

OGREnvelope interruptEnvelope(const SpatialReference& theSRS)
{
  // Default answer covers nothing. User should check for this special case.
  OGREnvelope env;
  env.MinY = 0;
  env.MaxY = 0;
  env.MinX = 0;
  env.MaxX = 0;

  if (theSRS.isGeographic())
  {
    // geographic projections are modified by +lon_wrap which defines the wanter center longitude
    const auto opt_lon_wrap = theSRS.projInfo().getDouble("lon_wrap");
    const auto lon_wrap = (opt_lon_wrap ? *opt_lon_wrap : 0);

    env.MinY = -90;
    env.MaxY = +90;
    env.MinX = lon_wrap - 180;
    env.MaxX = lon_wrap + 180;
    return env;
  }

  // For geometric projections the default box is modified by +lon_0
  // whose default value is zero.  Anything not rectilinear must be
  // clipped to produce shorted edges at the antimeridians, or the
  // edges will not curve nicely enough when projected. In practise it
  // seems like only cylindrical projections and pseudocylindrical
  // projections with straight sides such as collg can be handled via
  // envelopes.

  const auto opt_name = theSRS.projInfo().getString("proj");
  if (!opt_name) return env;
  const auto name = *opt_name;

  if (name == "cc" || name == "cea" || name == "collg" || name == "comill" || name == "eqc" ||
      name == "fouc_s" || name == "gall" || name == "merc" || name == "mill" || name == "ocea" ||
      name == "patterson" || name == "webmerc")
  {
    const auto opt_lon_0 = theSRS.projInfo().getDouble("lon_0");
    const auto lon_0 = opt_lon_0 ? *opt_lon_0 : 0.0;

    env.MinY = -90;
    env.MaxY = +90;
    env.MinX = lon_0 - 180;
    env.MaxX = lon_0 + 180;
  }

  return env;
}

}  // namespace Fmi
