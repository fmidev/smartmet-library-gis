#include "OGR.h"

#include <ogr_geometry.h>
#include <ogr_spatialref.h>

namespace Fmi
{
namespace OGR
{
const double epsilon = 1e-6;
const double poleshift = 90.1;  // extend the cut beyond the poles for boolean operations

double modlon(double lon)
{
  if (lon > 180) return fmod(lon + 180, 360) - 180;
  if (lon < -180) return -(fmod(-lon + 180, 360) - 180);
  return lon;
}

OGRPolygon* vertical_cut(double lon, double lat1, double lat2)
{
  OGRPolygon* poly = new OGRPolygon;
  OGRLinearRing* ring = new OGRLinearRing;
  ring->addPoint(lon - epsilon, std::min(lat1, lat2));
  ring->addPoint(lon - epsilon, std::max(lat1, lat2));
  ring->addPoint(lon + epsilon, std::max(lat1, lat2));
  ring->addPoint(lon + epsilon, std::min(lat1, lat2));
  ring->closeRings();
  poly->addRingDirectly(ring);
  return poly;
}

OGRMultiPolygon* interruptGeometry(const OGRSpatialReference& theSRS)
{
  // May happen for regular longlat data?
  if (theSRS.IsEmpty()) return nullptr;

  const char* nameptr = theSRS.GetAttrValue("PROJECTION");
  if (nameptr == nullptr) return nullptr;
  const std::string name = nameptr;

  // Geographic: cut everything at lon_wrap antimeridian
  if (theSRS.IsGeographic())
  {
    const double lon_wrap = theSRS.GetNormProjParm("TODO", 180.0);
    OGRMultiPolygon* mpoly = new OGRMultiPolygon;
    mpoly->assignSpatialReference(OGRSpatialReference::GetWGS84SRS());
    mpoly->addGeometryDirectly(vertical_cut(modlon(lon_wrap - 180), -poleshift, poleshift));
    mpoly->addGeometryDirectly(vertical_cut(modlon(lon_wrap + 180), -poleshift, poleshift));
    return mpoly;
  }

  // Polar projections

  if (name == SRS_PT_AZIMUTHAL_EQUIDISTANT || name == SRS_PT_LAMBERT_AZIMUTHAL_EQUAL_AREA ||
      name == SRS_PT_OBLIQUE_STEREOGRAPHIC || name == SRS_PT_POLAR_STEREOGRAPHIC ||
      name == SRS_PT_STEREOGRAPHIC)
    return nullptr;

  // These need isocircle cuts

  if (name == SRS_PT_GEOSTATIONARY_SATELLITE) return nullptr;
  if (name == SRS_PT_GNOMONIC) return nullptr;
  if (name == SRS_PT_ORTHOGRAPHIC) return nullptr;
  if (name == SRS_PT_TWO_POINT_EQUIDISTANT) return nullptr;

  // lon_0 is now needed for all geometric projections

  const double lon_0 = theSRS.GetNormProjParm(SRS_PP_CENTRAL_MERIDIAN, 0.0);

  if (name == SRS_PT_IGH)
  {
    // Regular land cut Goode homolosine, no other variants are supported in GDAL30/PROJ.7
    OGRMultiPolygon* mpoly = new OGRMultiPolygon;
    mpoly->assignSpatialReference(OGRSpatialReference::GetWGS84SRS());
    mpoly->addGeometryDirectly(vertical_cut(modlon(lon_0 - 180), -poleshift, poleshift));
    mpoly->addGeometryDirectly(vertical_cut(modlon(lon_0 + 180), -poleshift, poleshift));
    mpoly->addGeometryDirectly(vertical_cut(modlon(lon_0 - 40), 0, poleshift));
    mpoly->addGeometryDirectly(vertical_cut(modlon(lon_0 - 100), -poleshift, 0));
    mpoly->addGeometryDirectly(vertical_cut(modlon(lon_0 - 20), -poleshift, 0));
    mpoly->addGeometryDirectly(vertical_cut(modlon(lon_0 + 80), -poleshift, 0));
    return mpoly;
  }

  // Regular geometric: cut everything at lon_0+180 antimeridian
  OGRMultiPolygon* mpoly = new OGRMultiPolygon;
  mpoly->assignSpatialReference(OGRSpatialReference::GetWGS84SRS());
  mpoly->addGeometryDirectly(vertical_cut(modlon(lon_0 - 180), -poleshift, poleshift));
  mpoly->addGeometryDirectly(vertical_cut(modlon(lon_0 + 180), -poleshift, poleshift));
  return mpoly;
}

}  // namespace OGR
}  // namespace Fmi
