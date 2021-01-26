#include "SpatialReference.h"

#include "OGR.h"
#include "OGRSpatialReferenceFactory.h"
#include "ProjInfo.h"

#include <boost/functional/hash.hpp>
#include <fmt/format.h>
#include <macgyver/Cache.h>

#include <ogr_geometry.h>

namespace Fmi
{
// Cache variables
namespace
{
// Data members separated to a separate structure for caching purposes.
struct ImplData
{
  std::size_t hashvalue;
  std::shared_ptr<OGRSpatialReference> crs;
  ProjInfo projinfo;
};

const std::size_t default_cache_size = 10000;
using ImplDataCache = Cache::Cache<std::string, std::shared_ptr<ImplData>>;
ImplDataCache g_ImplDataCache{default_cache_size};

}  // namespace

// Implementation details
class SpatialReference::Impl
{
 public:
  ~Impl() = default;

  Impl(const Impl &other) = default;

  explicit Impl(const SpatialReference &other) : m_data(other.impl->m_data) {}

  explicit Impl(const OGRSpatialReference &other) { init(other); }

  explicit Impl(OGRSpatialReference &other)
  {
    init(const_cast<const OGRSpatialReference &>(other));
  }

  explicit Impl(const std::string &theCRS) { init(theCRS); }

  explicit Impl(int epsg) { init(fmt::format("EPSG:{}", epsg)); }

  void init(const std::string &theCRS)
  {
    auto obj = g_ImplDataCache.find(theCRS);
    if (obj)
      m_data = *obj;
    else
    {
      m_data = std::make_shared<ImplData>();
      m_data->crs = OGRSpatialReferenceFactory::Create(theCRS);
      m_data->projinfo = ProjInfo(OGR::exportToProj(*m_data->crs));
      m_data->hashvalue = boost::hash_value(m_data->projinfo.projStr());
      g_ImplDataCache.insert(theCRS, m_data);
    }
  }

  void init(const OGRSpatialReference &other)
  {
    m_data = std::make_shared<ImplData>();
    m_data->crs.reset(other.Clone());
    m_data->projinfo = ProjInfo(OGR::exportToProj(*m_data->crs));
    m_data->hashvalue = boost::hash_value(m_data->projinfo.projStr());
  }

  Impl &operator=(const Impl &) = delete;

  std::shared_ptr<ImplData> m_data;

};  // class Impl

SpatialReference::~SpatialReference() = default;

SpatialReference::SpatialReference(const SpatialReference &other) : impl(new Impl(*other.impl)) {}

SpatialReference::SpatialReference(const OGRSpatialReference &other) : impl(new Impl(other)) {}

SpatialReference::SpatialReference(OGRSpatialReference &other) : impl(new Impl(other)) {}

SpatialReference::SpatialReference(const char *theCRS) : impl(new Impl(std::string(theCRS))) {}

SpatialReference::SpatialReference(const std::string &theCRS) : impl(new Impl(theCRS)) {}

SpatialReference::SpatialReference(int epsg) : impl(new Impl(epsg)) {}

bool SpatialReference::isGeographic() const { return impl->m_data->crs->IsGeographic() != 0; }

bool SpatialReference::isAxisSwapped() const
{
#if GDAL_VERSION_MAJOR > 1
  auto strategy = impl->m_data->crs->GetAxisMappingStrategy();
  if (strategy == OAMS_TRADITIONAL_GIS_ORDER) return false;
  if (strategy == OAMS_CUSTOM) return false;  // Don't really know what to do in this case
  if (strategy != OAMS_AUTHORITY_COMPLIANT) return false;  // Unknown case

  return (impl->m_data->crs->EPSGTreatsAsLatLong() ||
          impl->m_data->crs->EPSGTreatsAsNorthingEasting());
#else
  // GDAL1 does not seem to obey EPSGA flags at all
  return false;
#endif
}

std::size_t SpatialReference::hashValue() const { return impl->m_data->hashvalue; }

const OGRSpatialReference &SpatialReference::operator*() const { return *impl->m_data->crs; }
OGRSpatialReference *SpatialReference::get() const { return impl->m_data->crs.get(); }

SpatialReference::operator OGRSpatialReference &() const { return *impl->m_data->crs; }
SpatialReference::operator OGRSpatialReference *() const { return impl->m_data->crs.get(); }

const ProjInfo &SpatialReference::projInfo() const { return impl->m_data->projinfo; }
const std::string &SpatialReference::projStr() const { return impl->m_data->projinfo.projStr(); }

void SpatialReference::setCacheSize(std::size_t newMaxSize) { g_ImplDataCache.resize(newMaxSize); }

}  // namespace Fmi
