#include "Interrupt.h"
#include "ProjInfo.h"
#include "SpatialReference.h"
#include <ogr_geometry.h>
#include <ogr_spatialref.h>

namespace Fmi
{
const double epsilon = 1e-6;
const double poleshift = 0.1;  // extend the cut beyond the poles for boolean operations

double modlon(double lon)
{
  if (lon > 180) return fmod(lon + 180, 360) - 180;
  if (lon < -180) return -(fmod(-lon + 180, 360) - 180);
  return lon;
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

Interrupt::~Interrupt()
{
  CPLFree(andGeometry);
  CPLFree(cutGeometry);
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

    result.cutGeometry = mpoly;
    return result;
  }

  const auto opt_name = theSRS.projInfo().getString("proj");
  if (!opt_name) return result;

  const auto name = *opt_name;

  // Polar projections

  if (name == "aeqd" || name == "laea" || name == "stere" || name == "sterea" || name == "ups")
    return result;

  // These need isocircle cuts

  if (name == "airy") return result;
  if (name == "geos") return result;
  if (name == "gnom") return result;
  if (name == "ortho") return result;
  if (name == "tpeqd") return result;

  const auto opt_lon_0 = theSRS.projInfo().getDouble("lon_0");
  const auto lon_0 = opt_lon_0 ? *opt_lon_0 : 0.0;

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

    result.cutGeometry = mpoly;
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

    result.cutGeometry = mpoly;
    return result;
  }

  // Regular geometric: cut everything at lon_0+180 antimeridian
  // lon_0 is needed for all remaining geometric projections

  OGRMultiPolygon* mpoly = new OGRMultiPolygon;
  mpoly->assignSpatialReference(OGRSpatialReference::GetWGS84SRS());
  mpoly->addGeometryDirectly(vertical_cut(modlon(lon_0 - 180), -90, 90));
  if (lon_0 == 0) mpoly->addGeometryDirectly(vertical_cut(modlon(lon_0 + 180), -90, 90));

  result.cutGeometry = mpoly;
  return result;
}

}  // namespace Fmi
