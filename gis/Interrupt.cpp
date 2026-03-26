#include "Interrupt.h"
#include "OGR.h"
#include "ProjInfo.h"
#include "Shape_circle.h"
#include "Shape_rect.h"
#include "SpatialReference.h"
#include <boost/math/constants/constants.hpp>
#include <macgyver/Exception.h>
#include <memory>
#include <ogr_geometry.h>
#include <ogr_spatialref.h>

namespace Fmi
{
const double epsilon = 1e-6;

const double wgs84radius = 6378137.0;

const int default_circle_segments = 360;

namespace
{
std::shared_ptr<OGRGeometry> make_geometry_ptr(OGRGeometry* geometry)
{
  return {geometry, [](OGRGeometry* geometry) { OGRGeometryFactory::destroyGeometry(geometry); }};
}

// Longitude to -180...180 range
double modlon(double lon)
{
  try
  {
    if (lon > 180)
      return fmod(lon + 180, 360) - 180;
    if (lon < -180)
      return -(fmod(-lon + 180, 360) - 180);
    return lon;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// Create a circle in WGS84 coordinates. Range may span from -360 to +360

OGRPolygon* make_circle(double lon, double lat, double radius, int segments)
{
  try
  {
    auto* poly = new OGRPolygon;
    auto* ring = new OGRLinearRing;

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
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// Create a rect

OGRPolygon* make_rect(double x1, double y1, double x2, double y2)
{
  try
  {
    auto* poly = new OGRPolygon;
    auto* ring = new OGRLinearRing;

    ring->addPoint(x1, y1);
    ring->addPoint(x1, y2);
    ring->addPoint(x2, y2);
    ring->addPoint(x2, y1);
    ring->addPoint(x1, y1);

    poly->addRingDirectly(ring);
    return poly;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// Create a circle cutgeometry

std::shared_ptr<OGRGeometry> circle_cut(double lon,
                                        double lat,
                                        double radius,
                                        int segments = default_circle_segments)
{
  try
  {
    // One circle
    auto geom = make_geometry_ptr(make_circle(lon, lat, radius, segments));

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

    auto* result = new OGRGeometryCollection;

    auto rect = make_geometry_ptr(make_rect(-180, -90, 180, 90));
    auto* cut = geom->Intersection(rect.get());
    if (cut != nullptr && cut->IsEmpty() == 0)
      result->addGeometryDirectly(cut);

    if (env.MinX < -180)
    {
      rect = make_geometry_ptr(make_rect(-540, -90, -180, 90));
      cut = geom->Intersection(rect.get());
      OGR::translate(cut, +360, 0);
      if (cut != nullptr && cut->IsEmpty() == 0)
        result->addGeometryDirectly(cut);
    }

    if (env.MaxX > 180)
    {
      rect = make_geometry_ptr(make_rect(180, -90, 540, 90));
      cut = geom->Intersection(rect.get());
      OGR::translate(cut, -360, 0);
      if (cut != nullptr && cut->IsEmpty() == 0)
        result->addGeometryDirectly(cut);
    }

    result->assignSpatialReference(OGRSpatialReference::GetWGS84SRS());
    return make_geometry_ptr(result);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

Shape_sptr make_vertical_cut(double lon, double lat1, double lat2)
{
  try
  {
    return std::make_shared<Shape_rect>(
        lon - epsilon, std::min(lat1, lat2), lon + epsilon, std::max(lat1, lat2));
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

Shape_sptr make_horizontal_cut(double lat, double lon1, double lon2)
{
  try
  {
    return std::make_shared<Shape_rect>(
        std::min(lon1, lon2), lat - epsilon, std::max(lon1, lon2), lat + epsilon);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

}  // anonymous namespace

Interrupt interruptGeometry(const SpatialReference& theSRS)
{
  try
  {
    Interrupt result;

    const auto opt_name = theSRS.projInfo().getString("proj");
    if (!opt_name)
      return result;

    const auto& name = *opt_name;

    const auto opt_lon_0 = theSRS.projInfo().getDouble("lon_0");
    const auto lon_0 = opt_lon_0 ? *opt_lon_0 : 0.0;

    const auto opt_lat_0 = theSRS.projInfo().getDouble("lat_0");
    const auto lat_0 = opt_lat_0 ? *opt_lat_0 : 0.0;

    using boost::math::double_constants::degree;

    // If general oblique transformation such as rotated latlon, cut the Antarctic in half at the
    // central meridian. -60 is large enough to make the cut, since Drake passage is below
    // that latitude. In reality, the cut should be made for any polygon which spans the south pole,
    // and the cut should be made for that polygon only. Hence this code is not generic enough.
    // Similar logic would be needed for the north pole should there be a polygon covering it.
    //
    // The Interrupt struct should thus contain conditional cuts for individual polygons based
    // on the envelope of the individual polygon. The current implementation does not support this.
    //
    // The code commented out shows various tests used to find out how a nonzero lon_0 should be
    // handled, but the (random) experimental approach failed.

    if (theSRS.projInfo().getString("proj") == std::string("ob_tran"))
    {
      const auto opt_lat_p = theSRS.projInfo().getDouble("o_lat_p");
      const auto lat_p = opt_lat_p ? *opt_lat_p : 0.0;

      if (std::abs(lat_p - 90.0) < 0.01)
      {
        // Transverse aspect (o_lat_p ≈ 90°): the oblique transform is a pure
        // longitude rotation by o_lon_p.  The natural seam in geographic
        // coordinates is the meridian at modlon(o_lon_p + 180°) — the
        // antimeridian of the rotated frame — where PROJ's normalisation wraps
        // both sides of the input to the same projected longitude, causing
        // world-spanning polygons to degenerate.
        const auto opt_o_lon_p = theSRS.projInfo().getDouble("o_lon_p");
        const auto o_lon_p = opt_o_lon_p ? *opt_o_lon_p : 0.0;
        const auto seam_lon = modlon(o_lon_p + 180.0);

        result.shapeCuts.emplace_back(make_vertical_cut(seam_lon, -90, 90));
        // When the seam lands on ±180° also add the other side (same meridian,
        // opposite sign convention) — matching the default antimeridian case.
        if (std::abs(std::abs(seam_lon) - 180.0) < 0.01)
          result.shapeCuts.emplace_back(make_vertical_cut(-seam_lon, -90, 90));
        return result;
      }

      // General oblique (o_lat_p ≠ 90°): the natural seam is a great circle in
      // geographic coordinates, not a simple meridian.  Exact great-circle
      // cutting is not yet implemented; fall through to the default antimeridian
      // cut below, which is a reasonable approximation for mildly oblique cases.
    }

    // Geographic: cut everything at lon_wrap (default=Greenwich) antimeridians
    if (theSRS.isGeographic())
    {
      const auto opt_lon_wrap = theSRS.projInfo().getDouble("lon_wrap");
      const auto lon_wrap = (opt_lon_wrap ? *opt_lon_wrap : 0.0);

      result.shapeCuts.emplace_back(make_vertical_cut(modlon(lon_wrap + 180), -90, 90));
      if (lon_wrap == 0)
        result.shapeCuts.emplace_back(make_vertical_cut(modlon(lon_wrap - 180), -90, 90));

      return result;
    }

    if (name == "laea")
    {
      // Lambert Azimuthal Equal-Area: valid over a full hemisphere (180° from centre).
      // The antipodal point (lon_0+180°, −lat_0) maps to a circle of infinite radius
      // and must be excluded.
      //
      // circle_cut() cannot be used for a near-full-hemisphere clip (radius ≈ 180°)
      // because the geodesic ring degenerates near the poles and the interior
      // orientation of the resulting polygon becomes wrong for radii > 90°.
      //
      // Instead we cut away a small disc (radius 1°) centred on the antipodal point.
      // This correctly handles polar, oblique, and equatorial aspects; the previous
      // ±178° rectangular clip only worked for the north-polar aspect (lat_0 = 90°).
      const auto antiLon = modlon(lon_0 + 180.0);
      const auto antiLat = -lat_0;
      const auto antiRadius = 1.0 * wgs84radius * degree;
      result.cutGeometry = circle_cut(antiLon, antiLat, antiRadius);
      return result;
    }

    if (name == "nicol")
    {
      // Nicolosi Globular: cut at the anti-meridian like most geometric projections.
      // The projection wraps around itself near lon_0 ± 90°, which would require
      // additional cuts at those meridians for correct handling — but those cuts
      // are very slow in practice and are left disabled for now.
      result.shapeCuts.emplace_back(make_vertical_cut(modlon(lon_0 + 180), -90, 90));
      if (lon_0 == 0)
        result.shapeCuts.emplace_back(make_vertical_cut(modlon(lon_0 - 180), -90, 90));
      return result;
    }

    if (name == "nsper")
    {
      // Near-Side Perspective: only points within line-of-sight of the satellite at
      // height h above the surface are visible.  The visible radius is the angle to
      // the horizon: cos(θ) = R / (R+h).  Reduced by 0.1 % to avoid projection
      // failures right at the horizon edge.
      auto opt_h = theSRS.projInfo().getDouble("h");
      auto h = (opt_h ? *opt_h : 3000000.0);
      auto radius = acos(wgs84radius / (wgs84radius + h)) * wgs84radius;
      radius = 0.999 * radius;
      result.andGeometry = circle_cut(lon_0, lat_0, radius);
      return result;
    }

    if (name == "tcc")
    {
      // Transverse Central Cylindrical: PROJ produces artefacts in the longitude
      // range 90…130° for unknown reasons; clip that band out as a workaround.
      result.shapeCuts.emplace_back(std::make_shared<Shape_rect>(90, -90, 130, 90));
      return result;
    }

    if (name == "lcc")
    {
      // Lambert Conformal Conic: cut at the anti-meridian and at the south pole
      // latitude.  The conic wraps around the pole on the far side; without the
      // horizontal cut the south-polar region projects to a huge swept arc that
      // jumps across the box.
      result.shapeCuts.emplace_back(make_vertical_cut(modlon(lon_0 + 180), -90, 90));
      if (lon_0 == 0)
        result.shapeCuts.emplace_back(make_vertical_cut(modlon(lon_0 - 180), -90, 90));
      result.shapeCuts.emplace_back(make_horizontal_cut(-90, -180, 180));
      return result;
    }

    if (name == "imw_p")
    {
      // International Map of the World Polyconic: no reliable clip has been found
      // (circle clipping was too slow).  Return an empty interrupt and let the
      // projection produce whatever it can.
      return result;
    }

    if (name == "aeqd")
    {
      // Azimuthal Equidistant: valid up to 180° from centre but the antipodal region
      // is heavily distorted.  130° was found experimentally to give reasonable
      // results; a proper hemisphere clip would require correctly handling the
      // antarctic, which the current circle-cut implementation does not do.
      const auto radius = 130 * wgs84radius * degree;
      result.andGeometry = circle_cut(lon_0, lat_0, radius);
      return result;
    }

    if (name == "tmerc")
    {
      // Transverse Mercator (tmerc) and Gauss-Schreiber Transverse Mercator (gstmerc):
      // both project the hemisphere within ±90° of the central meridian.  Beyond that
      // boundary the projection diverges.  89.9° was found experimentally to avoid
      // projection failures right at the ±90° horizon without clipping legitimate data.
      const auto radius = 89.9 * wgs84radius * degree;
      result.andGeometry = circle_cut(lon_0, lat_0, radius);
      return result;
    }

    if (name == "gstmerc")
    {
      // Transverse Mercator (tmerc) and Gauss-Schreiber Transverse Mercator (gstmerc):
      // both project the hemisphere within ±90° of the central meridian.  Beyond that
      // boundary the projection diverges.  89.5° was found experimentally to avoid
      // projection failures right at the ±90° horizon without clipping legitimate data.
      const auto radius = 89.5 * wgs84radius * degree;
      result.andGeometry = circle_cut(lon_0, lat_0, radius);
      return result;
    }

    if (name == "gnom")
    {
      // Gnomonic: only the hemisphere facing the centre is valid.  89° avoids the
      // NaN/infinity values that PROJ produces right at the 90° horizon.
      const auto radius = 89 * wgs84radius * degree;
      result.andGeometry = circle_cut(lon_0, lat_0, radius);
      return result;
    }

    if (name == "airy" || name == "ortho")
    {
      // Clip to just inside the hemisphere boundary.  Exactly 90° passes through
      // both poles, which project to a single point in azimuthal projections —
      // this collapses the north/south edges of the clip polygon to zero length,
      // producing run endpoints interior to the box that RectClipper cannot close.
      const auto radius = 0.999 * 90 * wgs84radius * degree;
      result.andGeometry = circle_cut(lon_0, lat_0, radius);
      return result;
    }

    if (name == "tpers")
    {
      // Tilted Perspective (satellite view): the visible disk has the same geometric
      // horizon as nsper — cos(θ) = R/(R+h).  The tilt and azimuth parameters rotate
      // the view but do not change the outer boundary of the visible hemisphere.
      // Reduced by 0.1 % to avoid projection failures right at the horizon edge.
      // Falls back to 50° (≈ h = 5 500 km) when h is absent.
      auto opt_h = theSRS.projInfo().getDouble("h");
      auto h = (opt_h ? *opt_h : 5500000.0);
      auto radius = acos(wgs84radius / (wgs84radius + h)) * wgs84radius;
      radius = 0.999 * radius;
      result.andGeometry = circle_cut(lon_0, lat_0, radius);
      return result;
    }

    if (name == "geos")
    {
      // Geostationary Satellite View: visible horizon is the same geometric formula
      // as nsper — cos(θ) = R/(R+h).  Standard GEO orbit h ≈ 35 786 km gives
      // θ ≈ 81.3°; the formula handles any non-standard h correctly.
      // Reduced by 0.1 % to avoid projection failures right at the horizon edge.
      // Falls back to the standard GEO orbit height when h is absent.
      auto opt_h = theSRS.projInfo().getDouble("h");
      auto h = (opt_h ? *opt_h : 35786000.0);
      auto radius = acos(wgs84radius / (wgs84radius + h)) * wgs84radius;
      radius = 0.999 * radius;
      result.andGeometry = circle_cut(lon_0, lat_0, radius);
      return result;
    }

    if (name == "adams_hemi")
    {
      // Adams Hemisphere-in-a-Square: valid domain is exactly one hemisphere (90°
      // from centre).

      if (lon_0 > -90 && lon_0 < 90)
      {
        result.shapeCuts.emplace_back(std::make_shared<Shape_rect>(-180, -90, lon_0 - 90, 90));
        result.shapeCuts.emplace_back(std::make_shared<Shape_rect>(lon_0 + 90, -90, 180, 90));
      }
      else if (lon_0 <= -90)
      {
        result.shapeCuts.emplace_back(
            std::make_shared<Shape_rect>(lon_0 + 90, -90, lon_0 + 270, 90));
      }
      else if (lon_0 >= 90)
      {
        result.shapeCuts.emplace_back(
            std::make_shared<Shape_rect>(lon_0 - 270, -90, lon_0 - 90, 90));
      }

      return result;
    }

    if (name == "bertin1953" || name == "peirce_q")
    {
      // Bertin 1953 and Peirce Quincuncial: these projections have irregular
      // singularity patterns that do not align with simple meridian cuts or circle
      // clips.  No satisfactory interrupt geometry has been found; return empty and
      // accept that artefacts may appear near the singularities.
      return result;
    }

    if (name == "tpeqd")
    {
      // Two-Point Equidistant: the projection centre is the midpoint of the two
      // control points.  145° gives enough coverage without hitting the antipodal
      // singularity region.  This is an approximation; the true valid domain depends
      // on the geometry of the two control points.
      //
      // The midpoint longitude must be computed wrap-aware: a simple arithmetic
      // mean of lon_1 and lon_2 gives the wrong answer when the two points straddle
      // the anti-meridian (e.g. lon_1=170, lon_2=-170 → mean=0 instead of ±180).
      // We normalise the angular difference to (−180, +180] before halving.
      const auto opt_lon_1 = theSRS.projInfo().getDouble("lon_1");
      const auto lon_1 = opt_lon_1 ? *opt_lon_1 : 0.0;

      const auto opt_lat_1 = theSRS.projInfo().getDouble("lat_1");
      const auto lat_1 = opt_lat_1 ? *opt_lat_1 : 0.0;

      const auto opt_lon_2 = theSRS.projInfo().getDouble("lon_2");
      const auto lon_2 = opt_lon_2 ? *opt_lon_2 : 0.0;

      const auto opt_lat_2 = theSRS.projInfo().getDouble("lat_2");
      const auto lat_2 = opt_lat_2 ? *opt_lat_2 : 0.0;

      // Wrap-aware longitude midpoint: normalise diff to (−180, +180], then add half.
      double dlon = lon_2 - lon_1;
      if (dlon > 180.0)
        dlon -= 360.0;
      else if (dlon <= -180.0)
        dlon += 360.0;
      const auto lon = modlon(lon_1 + 0.5 * dlon);
      const auto lat = 0.5 * (lat_1 + lat_2);

      const auto radius = 145 * wgs84radius * degree;
      result.andGeometry = circle_cut(lon, lat, radius);
      return result;
    }

    if (name == "igh")
    {
      // Interrupted Goode Homolosine
      result.shapeCuts.emplace_back(make_vertical_cut(modlon(lon_0 + 180), -90, 90));
      if (lon_0 == 0)
        result.shapeCuts.emplace_back(make_vertical_cut(modlon(lon_0 - 180), -90, 90));

      result.shapeCuts.emplace_back(make_vertical_cut(modlon(lon_0 - 40), 0, 90));
      result.shapeCuts.emplace_back(make_vertical_cut(modlon(lon_0 - 100), -90, 0));
      result.shapeCuts.emplace_back(make_vertical_cut(modlon(lon_0 - 20), -90, 0));
      result.shapeCuts.emplace_back(make_vertical_cut(modlon(lon_0 + 80), -90, 0));

      return result;
    }

    if (name == "igh_o")
    {
      // Interrupted Goode Homolosine (Oceanic)

      result.shapeCuts.emplace_back(make_vertical_cut(modlon(lon_0 + 180), -90, 90));
      if (lon_0 == 0)
        result.shapeCuts.emplace_back(make_vertical_cut(modlon(lon_0 - 180), -90, 90));
      result.shapeCuts.emplace_back(make_vertical_cut(modlon(lon_0 - 90), 0, 90));
      result.shapeCuts.emplace_back(make_vertical_cut(modlon(lon_0 + 60), 0, 90));
      result.shapeCuts.emplace_back(make_vertical_cut(modlon(lon_0 - 60), -90, 0));
      result.shapeCuts.emplace_back(make_vertical_cut(modlon(lon_0 + 90), -90, 0));
      return result;
    }

    if (name == "healpix")
    {
      result.shapeCuts.emplace_back(make_vertical_cut(modlon(lon_0 + 180), -90, 90));
      if (lon_0 == 0)
        result.shapeCuts.emplace_back(make_vertical_cut(modlon(lon_0 - 180), -90, 90));

      result.shapeCuts.emplace_back(make_vertical_cut(modlon(lon_0 - 90), -90, -45));
      result.shapeCuts.emplace_back(make_vertical_cut(modlon(lon_0 - 90), 45, 90));
      result.shapeCuts.emplace_back(make_vertical_cut(modlon(lon_0), -90, -45));
      result.shapeCuts.emplace_back(make_vertical_cut(modlon(lon_0), 45, 90));
      result.shapeCuts.emplace_back(make_vertical_cut(modlon(lon_0 + 90), -90, -45));
      result.shapeCuts.emplace_back(make_vertical_cut(modlon(lon_0 + 90), 45, 90));

      return result;
    }

    if (name == "isea")
    {
      // Icosahedral Snyder Equal Area.
      // TODO: PROJ.7 implementation seems to have the cuts at odd locations, perhaps a bug?

      result.shapeCuts.emplace_back(make_vertical_cut(modlon(lon_0 + 180), -90, 90));
      if (lon_0 == 0)
        result.shapeCuts.emplace_back(make_vertical_cut(modlon(lon_0 - 180), -90, 90));

      // result.shapeCuts.emplace_back(make_vertical_cut(modlon(lon_0 - 108), 30, 90));
      // result.shapeCuts.emplace_back(make_vertical_cut(modlon(lon_0 - 36), 30, 90));
      // result.shapeCuts.emplace_back(make_vertical_cut(modlon(lon_0 + 36), 30, 90));
      // result.shapeCuts.emplace_back(make_vertical_cut(modlon(lon_0 + 108), 30, 90));
      //
      // result.shapeCuts.emplace_back(make_vertical_cut(modlon(lon_0 - 144), -90, -30));
      // result.shapeCuts.emplace_back(make_vertical_cut(modlon(lon_0 - 72), -90, -30));
      // result.shapeCuts.emplace_back(make_vertical_cut(modlon(lon_0 + 0), -90, -30));
      // result.shapeCuts.emplace_back(make_vertical_cut(modlon(lon_0 + 72), -90, -30));
      // result.shapeCuts.emplace_back(make_vertical_cut(modlon(lon_0 + 144), -90, -30));

      return result;
    }

    // Default: cut the input polygon at the anti-meridian (lon_0 ± 180°).
    // This is the correct pre-processing for all remaining geometric projections
    // whose domain boundary aligns with the anti-meridian (conic, azimuthal, etc.).
    // Without this cut, a polygon that straddles the anti-meridian would project to
    // two widely-separated halves connected by a huge "jump" segment.

    result.shapeCuts.emplace_back(make_vertical_cut(modlon(lon_0 + 180), -90, 90));
    if (lon_0 == 0)
      result.shapeCuts.emplace_back(make_vertical_cut(modlon(lon_0 - 180), -90, 90));

    return result;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

OGREnvelope interruptEnvelope(const SpatialReference& theSRS)
{
  try
  {
    // Default answer covers nothing. User should check for this special case.
    OGREnvelope env;
    env.MinY = 0;
    env.MaxY = 0;
    env.MinX = 0;
    env.MaxX = 0;

    if (theSRS.isGeographic())
    {
      // geographic projections are modified by +lon_wrap which defines the wanted center longitude
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
    if (!opt_name)
      return env;
    const auto& name = *opt_name;

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
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

}  // namespace Fmi
