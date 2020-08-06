#include "SpatialReference.h"
#include "OGR.h"
#include "OGRSpatialReferenceFactory.h"
#include "ProjInfo.h"
#include <boost/functional/hash.hpp>
#include <ogr_geometry.h>

namespace Fmi
{
class SpatialReference::Impl
{
 public:
  ~Impl() = default;

  Impl(const Impl &other) = default;

  explicit Impl(const OGRSpatialReference &other)
      : m_crs(other.Clone()), m_projInfo(OGR::exportToProj(*m_crs))
  {
    m_crs->SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
  }

  explicit Impl(OGRSpatialReference &other)
      : m_crs(other.Clone()), m_projInfo(OGR::exportToProj(*m_crs))
  {
    m_crs->SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
  }

  explicit Impl(const std::string &theCRS)
  {
    m_crs = OGRSpatialReferenceFactory::Create(theCRS);
    m_crs->SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
    m_projInfo = ProjInfo(OGR::exportToProj(*m_crs));
  }

  explicit Impl(int epsg)
  {
    m_crs = OGRSpatialReferenceFactory::Create(epsg);
    m_crs->SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
    m_projInfo = ProjInfo(OGR::exportToProj(*m_crs));
  }

  Impl &operator=(const Impl &) = delete;

  std::shared_ptr<OGRSpatialReference> m_crs;
  ProjInfo m_projInfo;

};  // class Impl

SpatialReference::~SpatialReference() = default;

SpatialReference::SpatialReference(const SpatialReference &other) : impl(new Impl(*other.impl)) {}

SpatialReference::SpatialReference(const OGRSpatialReference &other) : impl(new Impl(other)) {}

SpatialReference::SpatialReference(OGRSpatialReference &other) : impl(new Impl(other)) {}

SpatialReference::SpatialReference(const char *theCRS) : impl(new Impl(std::string(theCRS))) {}

SpatialReference::SpatialReference(const std::string &theCRS) : impl(new Impl(theCRS)) {}

SpatialReference::SpatialReference(int epsg) : impl(new Impl(epsg)) {}

bool SpatialReference::isGeographic() const { return impl->m_crs->IsGeographic() != 0; }

bool SpatialReference::isAxisSwapped() const
{
#if GDAL_VERSION_MAJOR > 1
  auto strategy = impl->m_crs->GetAxisMappingStrategy();
  if (strategy == OAMS_TRADITIONAL_GIS_ORDER) return false;
  if (strategy == OAMS_CUSTOM) return false;  // Don't really know what to do in this case
  if (strategy != OAMS_AUTHORITY_COMPLIANT) return false;  // Unknown case

  return (impl->m_crs->EPSGTreatsAsLatLong() || impl->m_crs->EPSGTreatsAsNorthingEasting());
#else
  // GDAL1 does not seem to obey EPSGA flags at all
  return false;
#endif
}

std::size_t SpatialReference::hashValue() const
{
  auto wkt = OGR::exportToWkt(*impl->m_crs);
  return boost::hash_value(wkt);
}

const OGRSpatialReference &SpatialReference::operator*() const { return *impl->m_crs; }
OGRSpatialReference *SpatialReference::get() const { return impl->m_crs.get(); }

SpatialReference::operator OGRSpatialReference &() const { return *impl->m_crs; }
SpatialReference::operator OGRSpatialReference *() const { return impl->m_crs.get(); }

const ProjInfo &SpatialReference::projInfo() const { return impl->m_projInfo; }
const std::string &SpatialReference::projStr() const { return impl->m_projInfo.projStr(); }

}  // namespace Fmi
