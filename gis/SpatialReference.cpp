#include "SpatialReference.h"
#include <gdal_version.h>
#include <ogr_geometry.h>

namespace Fmi
{
namespace
{
// Known datums : those listed in PROJ.4 pj_datums.c

std::map<std::string, std::string> known_datums = {
    {"FMI", "+R=6371229 +towgs84=0,0,0"},
    {"WGS84", "+a=6378137 +rf=298.257223563 +towgs84=0,0,0"},
    {"GGRS87", "+a=6378137 +rf=298.257222101 +towgs84=-199.87,74.79,246.62"},
    {"NAD83", "+a=6378137 +rf=298.257222101 +towgs84=0,0,0"},
    {"NAD27", "+a=6378206.4 +b=6356583.8 +nadgrids=@conus,@alaska,@ntv2_0.gsb,@ntv1_can.dat"},
    {"potsdam", "+a=6377397.155 +rf=299.1528128 +nadgrids=@BETA2007.gsb"},
    {"carthage", "+a=6378249.2 +rf=293.4660212936269 +towgs84=-263.0,6.0,431.0"},
    {"hermannskogel",
     "+a=6377397.155 +rf=299.1528128 +towgs84=577.326,90.129,463.919,5.137,1.474,5.297,2.4232"},
    {"ire65",
     "+a=6377340.189 +b=6356034.446 +towgs84=482.530,-130.596,564.557,-1.042,-0.214,-0.631,8.15"},
    {"nzgd49", "+a=6378388 +rf=297. +towgs84=59.47,-5.04,187.44,0.47,-0.1,1.024,-4.5993"},
    {"OSGB36",
     "+a=6377563.396 +b=6356256.910 "
     "+towgs84=446.448,-125.157,542.060,0.1502,0.2470,0.8421,-20.4894"}};

// Known reference ellipsoids : those listed in PROJ.4 pj_ellps.c

std::map<std::string, std::string> known_ellipsoids = {
    {"MERIT", "+a=6378137 +rf=298.257"},
    {"SGS85", "+a=6378136 +rf=298.257"},
    {"GRS80", "+a=6378137 +rf=298.257222101"},
    {"IAU76", "+a=6378140 +rf=298.257"},
    {"airy", "+a=6377563.396 +b=6356256.910"},
    {"APL4.9", "+a=6378137.0. +rf=298.25"},
    {"NWL9D", "+a=6378145.0. +rf=298.25"},
    {"mod_airy", "+a=6377340.189 +b=6356034.446"},
    {"andrae", "+a=6377104.43 +rf=300.0"},
    {"aust_SA", "+a=6378160 +rf=298.25"},
    {"GRS67", "+a=6378160 +rf=298.2471674270"},
    {"bessel", "+a=6377397.155 +rf=299.1528128"},
    {"bess_nam", "+a=6377483.865 +rf=299.1528128"},
    {"clrk66", "+a=6378206.4 +b=6356583.8"},
    {"clrk80", "+a=6378249.145 +rf=293.4663"},
    {"clrk80ign", "+a=6378249.2 +rf=293.4660212936269"},
    {"CPM", "+a=6375738.7 +rf=334.29"},
    {"delmbr", "+a=6376428. +rf=311.5"},
    {"engelis", "+a=6378136.05 +rf=298.2566"},
    {"evrst30", "+a=6377276.345 +rf=300.8017"},
    {"evrst48", "+a=6377304.063 +rf=300.8017"},
    {"evrst56", "+a=6377301.243 +rf=300.8017"},
    {"evrst69", "+a=6377295.664 +rf=300.8017"},
    {"evrstSS", "+a=6377298.556 +rf=300.8017"},
    {"fschr60", "+a=6378166. +rf=298.3"},
    {"fschr60m", "+a=6378155. +rf=298.3"},
    {"fschr68", "+a=6378150. +rf=298.3"},
    {"helmert", "+a=6378200. +rf=298.3"},
    {"hough", "+a=6378270 +rf=297."},
    {"intl", "+a=6378388 +rf=297."},
    {"krass", "+a=6378245 +rf=298.3"},
    {"kaula", "+a=6378163. +rf=298.24"},
    {"lerch", "+a=6378139. +rf=298.257"},
    {"mprts", "+a=6397300. +rf=191."},
    {"new_intl", "+a=6378157.5 +b=6356772.2"},
    {"plessis", "+a=6376523 +b=6355863"},
    {"SEasia", "+a=6378155 +b=6356773.3205"},
    {"walbeck", "+a=6376896 +b=6355834.8467"},
    {"WGS60", "+a=6378165 +rf=298.3"},
    {"WGS66", "+a=6378145 +rf=298.25"},
    {"WGS72", "+a=6378135 +rf=298.26"},
    {"WGS84", "+a=6378137 +rf=298.257223563"},
    {"sphere", "+a=6370997 +b=6370997"}};

// Utility function for creating spatial references

std::unique_ptr<OGRSpatialReference> make_sr(std::string theDesc)
{
  if (theDesc.empty())
    throw std::runtime_error("Cannot create spatial reference from empty string");

  // Substitute for known datums/ellipsoids

  auto desc = theDesc;
  auto pos = known_datums.find(desc);
  if (pos != known_datums.end())
    desc = std::string("+proj=longlat ") + pos->second;
  else
  {
    pos = known_ellipsoids.find(desc);
    if (pos != known_ellipsoids.end()) desc = std::string("+proj=longlat ") + pos->second;
  }

  std::unique_ptr<OGRSpatialReference> sr(new OGRSpatialReference);
  auto err = sr->SetFromUserInput(desc.c_str());

  if (err == OGRERR_NONE) return sr;

  if (theDesc == desc)
    throw std::runtime_error("Failed to create spatial reference from '" + theDesc + "'");
  throw std::runtime_error("Failed to create spatial reference from '" + theDesc + "' ('" + desc +
                           "')");
}

}  // namespace

SpatialReference::~SpatialReference() { delete itsSR; }

SpatialReference::SpatialReference(const SpatialReference &other)
    : itsProjStr(other.itsProjStr), itsSR(other.itsSR->Clone())
{
}

SpatialReference::SpatialReference(const OGRSpatialReference &other)
    : itsProjStr(), itsSR(other.Clone())
{
}

SpatialReference::SpatialReference(OGRSpatialReference &other) : itsProjStr(), itsSR(other.Clone())
{
}

SpatialReference::SpatialReference(const char *theSR) { init(theSR); }

SpatialReference::SpatialReference(const std::string &theSR) { init(theSR); }

void SpatialReference::init(const std::string &theSR)
{
  itsProjStr = theSR;
  itsSR = make_sr(theSR).release();
}

bool SpatialReference::IsGeographic() const { return itsSR->IsGeographic() != 0; }

bool SpatialReference::IsAxisSwapped() const
{
#if GDAL_VERSION_MAJOR > 1
  return (itsSR->EPSGTreatsAsLatLong() || itsSR->EPSGTreatsAsNorthingEasting());
#else
  // GDAL1 does not seem to obey EPSGA flags at all
  return false;
#endif
}

}  // namespace Fmi
