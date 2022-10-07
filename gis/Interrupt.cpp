#include "Interrupt.h"
#include "OGR.h"
#include "ProjInfo.h"
#include "Shape_circle.h"
#include "Shape_rect.h"
#include "SpatialReference.h"
#include <boost/math/constants/constants.hpp>
#include <macgyver/Exception.h>
#include <ogr_geometry.h>
#include <ogr_spatialref.h>

#include <iostream>

namespace Fmi
{
const double epsilon = 1e-6;

const double wgs84radius = 6378137.0;

const int default_circle_segments = 360;

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

OGRGeometry* circle_cut(double lon,
                        double lat,
                        double radius,
                        int segments = default_circle_segments)
{
  try
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

    auto* result = new OGRGeometryCollection;

    auto* rect = make_rect(-180, -90, 180, 90);
    auto* cut = geom->Intersection(rect);
    if (cut != nullptr && cut->IsEmpty() == 0)
      result->addGeometryDirectly(cut);
    CPLFree(rect);

    if (env.MinX < -180)
    {
      auto* rect = make_rect(-540, -90, -180, 90);
      auto* cut = geom->Intersection(rect);
      OGR::translate(cut, +360, 0);
      if (cut != nullptr && cut->IsEmpty() == 0)
        result->addGeometryDirectly(cut);
      CPLFree(rect);
    }

    if (env.MaxX > 180)
    {
      auto* rect = make_rect(180, -90, 540, 90);
      auto* cut = geom->Intersection(rect);
      OGR::translate(cut, -360, 0);
      if (cut != nullptr && cut->IsEmpty() == 0)
        result->addGeometryDirectly(cut);
      CPLFree(rect);
    }

    CPLFree(geom);

    result->assignSpatialReference(OGRSpatialReference::GetWGS84SRS());
    return result;
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

Interrupt interruptGeometry(const SpatialReference& theSRS)
{
  try
  {
    Interrupt result;

    const auto opt_name = theSRS.projInfo().getString("proj");
    if (!opt_name)
      return result;

    const auto name = *opt_name;

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
      auto opt_lat_p = theSRS.projInfo().getDouble("o_lat_p");
      if (opt_lat_p)
      {
        const auto opt_lon_0 = theSRS.projInfo().getDouble("lon_0");
        const auto lon_0 = (opt_lon_0 ? *opt_lon_0 : 0.0);

        const auto lat_p = *opt_lat_p;

        result.shapeCuts.emplace_back(make_vertical_cut(0, -90, lat_p - 90));
        result.shapeCuts.emplace_back(make_vertical_cut(lon_0, -90, lat_p - 90));
        result.shapeCuts.emplace_back(make_vertical_cut(-lon_0, -90, lat_p - 90));

        result.shapeCuts.emplace_back(make_vertical_cut(lat_p, -90, lat_p - 90));
        result.shapeCuts.emplace_back(make_vertical_cut(-lat_p, -90, lat_p - 90));

        result.shapeCuts.emplace_back(make_horizontal_cut(-lat_p, -180, 180));
        result.shapeCuts.emplace_back(make_horizontal_cut(-90, -180, 180));
        result.shapeCuts.emplace_back(make_horizontal_cut(+90, -180, 180));
        result.shapeCuts.emplace_back(make_vertical_cut(+180, -90, 90));
        result.shapeCuts.emplace_back(make_vertical_cut(-180, -90, 90));
      }
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
      // Poles always project to x-coordinate zero, we need to cut them out too
      // Cannot use epsilon here, the cut would be too small for PROJ.7
      result.shapeClips.push_back(std::make_shared<Shape_rect>(-178, -89.99, 178, 89.99));
      return result;
    }

    if (name == "nicol")
    {
      result.shapeCuts.emplace_back(make_vertical_cut(modlon(lon_0 + 180), -90, 90));
      if (lon_0 == 0)
        result.shapeCuts.emplace_back(make_vertical_cut(modlon(lon_0 - 180), -90, 90));

      // TODO: proj=nicol is hard to handle correctly since the projection seems to wrap
      // around itself around -+90 longitudes
      //
      // Very slow:
      // result.shapeCuts.emplace_back(make_vertical_cut(modlon(lon_0 - 90), -90, 90));
      // result.shapeCuts.emplace_back(make_vertical_cut(modlon(lon_0 + 90), -90, 90));
      return result;
    }

    if (name == "nsper")
    {
      auto opt_h = theSRS.projInfo().getDouble("h");
      auto h = (opt_h ? *opt_h : 3000000.0);  // no idea what PROJ.x default is
      // From a triangle with hypotenuse R+h and side R to the tangent we get cos(theta) = R/(R+h)
      auto radius = acos(wgs84radius / (wgs84radius + h)) * wgs84radius;
      // Then reduce a little to avoid problems at the edges
      radius = 0.999 * radius;
      result.andGeometry.reset(circle_cut(lon_0, lat_0, radius));
      return result;
    }

    if (name == "tcc")
    {
      // TODO: Figure out what's wrong in longitude range 90...130
      result.shapeCuts.emplace_back(std::make_shared<Shape_rect>(90, -90, 130, 90));
      return result;
    }

    if (name == "lcc")
    {
      result.shapeCuts.emplace_back(make_vertical_cut(modlon(lon_0 + 180), -90, 90));
      if (lon_0 == 0)
        result.shapeCuts.emplace_back(make_vertical_cut(modlon(lon_0 - 180), -90, 90));
      result.shapeCuts.emplace_back(make_horizontal_cut(-90, -180, 180));
    }

    if (name == "imw_p")
    {
      // TODO: Slow as hell, disabled for now
      // const auto radius = 80 * wgs84radius * degree;
      // result.andGeometry.reset(circle_cut(lon_0, lat_0, radius));
      return result;
    }

    if (name == "aeqd")
    {
      // TODO: 130 is just an experimental value getting some things right, but
      // this clipping is not even close to correct. Not sure what kind of clipping this needs.
      //
      // Also: The antarctic is missing completely. Probably the fault of the current
      // version of circle cutting.

      const auto radius = 130 * wgs84radius * degree;
      result.andGeometry.reset(circle_cut(lon_0, lat_0, radius));
      return result;
    }

    if (name == "tmerc")
    {
      // TODO: This is just experimental to get something out
      // const auto radius = 90 * wgs84radius * degree;
      // result.andGeometry.reset(circle_cut(lon_0, lat_0, radius));
      return result;
    }
    if (name == "gstmerc")
    {
      // 90 causes errors
      const auto radius = 89.5 * wgs84radius * degree;
      result.andGeometry.reset(circle_cut(lon_0, lat_0, radius));
      return result;
    }

    if (name == "gnom")
    {
      // TODO: Nothing seems to work, result is full of NaN values
      const auto radius = 89 * wgs84radius * degree;
      result.andGeometry.reset(circle_cut(lon_0, lat_0, radius));
      return result;
    }

    if (name == "airy" || name == "ortho")
    {
      const auto radius = 90 * wgs84radius * degree;
      result.andGeometry.reset(circle_cut(lon_0, lat_0, radius));
      return result;
    }

    if (name == "tpers")
    {
      // 50 was found experimentally
      const auto radius = 50 * wgs84radius * degree;
      result.andGeometry.reset(circle_cut(lon_0, lat_0, radius));
      return result;
    }

    if (name == "geos")
    {
      // 80 was found experimentally
      const auto radius = 80 * wgs84radius * degree;
      result.andGeometry.reset(circle_cut(lon_0, lat_0, radius));
      return result;
    }

    if (name == "adams_hemi")
    {
      // TODO: Just something that works on small scales not up to the maximum
      const auto radius = 90 * wgs84radius * degree;
      result.andGeometry.reset(circle_cut(lon_0, lat_0, radius));
      return result;
    }

    if (name == "bertin1953" || name == "peirce_q")
    {
      // TODO: No idea how to fix these
      return result;
    }

    if (name == "tpeqd")
    {
      const auto opt_lon_1 = theSRS.projInfo().getDouble("lon_1");
      const auto lon_1 = opt_lon_1 ? *opt_lon_1 : 0.0;

      const auto opt_lat_1 = theSRS.projInfo().getDouble("lat_1");
      const auto lat_1 = opt_lat_1 ? *opt_lat_1 : 0.0;

      const auto opt_lon_2 = theSRS.projInfo().getDouble("lon_2");
      const auto lon_2 = opt_lon_2 ? *opt_lon_2 : 0.0;

      const auto opt_lat_2 = theSRS.projInfo().getDouble("lat_2");
      const auto lat_2 = opt_lat_2 ? *opt_lat_2 : 0.0;

      // Hack solution: rough estimate on the center
      const auto lon = 0.5 * (lon_1 + lon_2);
      const auto lat = 0.5 * (lat_1 + lat_2);

      const auto radius = 145 * wgs84radius * degree;
      result.andGeometry.reset(circle_cut(lon, lat, radius));
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
      // Interrupted Goode Homolosine (Oseanic)

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

    // Regular geometric: cut everything at lon_0+180 antimeridian
    // lon_0 is needed for all remaining geometric projections

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
    if (!opt_name)
      return env;
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
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

}  // namespace Fmi
