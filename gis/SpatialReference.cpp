#include "SpatialReference.h"
#include "OGR.h"
#include "OGRSpatialReferenceFactory.h"
#include "ProjInfo.h"
#include <fmt/format.h>
#include <macgyver/Exception.h>
#include <macgyver/Hash.h>
#include <macgyver/StringConversion.h>
#include <macgyver/StaticCleanup.h>
#include <ogr_geometry.h>

namespace Fmi
{
// Cache variables
namespace
{
// Data members separated to a separate structure for caching purposes.
struct ImplData
{
  std::size_t hashvalue = 0;
  std::shared_ptr<OGRSpatialReference> crs;
  bool is_geographic = false;
  bool is_axis_swapped = false;
  bool epsg_treats_as_lat_long = false;
  ProjInfo projinfo;
};

const std::size_t default_cache_size = 10000;
using ImplDataCache = Cache::Cache<std::string, std::shared_ptr<ImplData>>;

ImplDataCache &get_cache()
{
  static ImplDataCache g_ImplDataCache{default_cache_size};
  static StaticCleanup cleanup([]() { g_ImplDataCache.clear(); });
  return g_ImplDataCache;
}

bool is_axis_swapped(const OGRSpatialReference &crs)
{
  try
  {
#if GDAL_VERSION_MAJOR > 1
    auto strategy = crs.GetAxisMappingStrategy();
    if (strategy == OAMS_TRADITIONAL_GIS_ORDER)
      return false;
    if (strategy == OAMS_CUSTOM)
      return false;  // Don't really know what to do in this case
    if (strategy != OAMS_AUTHORITY_COMPLIANT)
      return false;  // Unknown case

    return (crs.EPSGTreatsAsLatLong() || crs.EPSGTreatsAsNorthingEasting());
#else
    // GDAL1 does not seem to obey EPSGA flags at all
    return false;
#endif
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

}  // namespace

// Implementation details
class SpatialReference::Impl
{
 public:
  std::shared_ptr<ImplData> m_data;

  ~Impl() = default;

  Impl(const Impl &other) = default;

  explicit Impl(const SpatialReference &other) : m_data(other.impl->m_data) {}

  explicit Impl(const OGRSpatialReference &other) { init(other); }

  Impl &operator=(const Impl &other) = delete;

  explicit Impl(OGRSpatialReference &other)
  {
    try
    {
      init(const_cast<const OGRSpatialReference &>(other));
    }
    catch (...)
    {
      throw Fmi::Exception::Trace(BCP, "Operation failed!");
    }
  }

  explicit Impl(const std::shared_ptr<OGRSpatialReference> &other)
  {
    try
    {
      if (!other)
        throw Fmi::Exception::Trace(
            BCP, "Initialization of SpatialReference from empty shared ptr not allowed");

      init(*other);
    }
    catch (...)
    {
      throw Fmi::Exception::Trace(BCP, "Operation failed!");
    }
  }

  explicit Impl(const std::string &theCRS) { init(theCRS); }

  explicit Impl(int epsg)
  {
    try
    {
      init(fmt::format("EPSG:{}", epsg));
    }
    catch (...)
    {
      throw Fmi::Exception::Trace(BCP, "Operation failed!");
    }
  }

  void init(const std::string &theCRS)
  {
    try
    {
      auto obj = get_cache().find(theCRS);
      if (obj)
        m_data = *obj;
      else
      {
        m_data = std::make_shared<ImplData>();
        m_data->crs = OGRSpatialReferenceFactory::Create(theCRS);

        try
        {
          // exportToProj may lose the original +type=crs setting, hence we try direct parsing first
          m_data->projinfo = ProjInfo(theCRS);
        }
        catch (...)
        {
          m_data->projinfo = ProjInfo(OGR::exportToProj(*m_data->crs));
        }
        m_data->hashvalue = Fmi::hash_value(m_data->projinfo.projStr());
        m_data->is_geographic = (m_data->crs->IsGeographic() != 0);
        m_data->is_axis_swapped = is_axis_swapped(*m_data->crs);
        m_data->epsg_treats_as_lat_long = (m_data->crs->EPSGTreatsAsLatLong() != 0);

        get_cache().insert(theCRS, m_data);
      }
    }
    catch (...)
    {
      throw Fmi::Exception::Trace(BCP, "Operation failed!");
    }
  }

  void init(const OGRSpatialReference &other)
  {
    try
    {
      std::shared_ptr<OGRSpatialReference> tmp(other.Clone(),
                                               [](OGRSpatialReference *ref) { ref->Release(); });

      m_data = std::make_shared<ImplData>();
      m_data->crs = tmp;
      m_data->projinfo = ProjInfo(OGR::exportToProj(*m_data->crs));
      m_data->hashvalue = Fmi::hash_value(m_data->projinfo.projStr());
      m_data->is_geographic = (m_data->crs->IsGeographic() != 0);
      m_data->is_axis_swapped = is_axis_swapped(*m_data->crs);
      m_data->epsg_treats_as_lat_long = (m_data->crs->EPSGTreatsAsLatLong() != 0);
    }
    catch (...)
    {
      throw Fmi::Exception::Trace(BCP, "Operation failed!");
    }
  }

};  // class Impl

SpatialReference::~SpatialReference() = default;

SpatialReference::SpatialReference(const SpatialReference &other) : impl(new Impl(*other.impl)) {}

SpatialReference::SpatialReference(SpatialReference &&other) noexcept : impl(std::move(other.impl))
{
}

SpatialReference::SpatialReference(const OGRSpatialReference &other) : impl(new Impl(other)) {}

SpatialReference::SpatialReference(OGRSpatialReference &other) : impl(new Impl(other)) {}

SpatialReference::SpatialReference(const std::shared_ptr<OGRSpatialReference> &other)
    : impl(new Impl(other))
{
}

SpatialReference::SpatialReference(const char *theDesc) : impl(new Impl(std::string(theDesc))) {}

SpatialReference::SpatialReference(const std::string &theDesc) : impl(new Impl(theDesc)) {}

SpatialReference::SpatialReference(int epsg) : impl(new Impl(epsg)) {}

bool SpatialReference::isGeographic() const
{
  try
  {
    return impl->m_data->is_geographic;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

bool SpatialReference::isAxisSwapped() const
{
  try
  {
    return impl->m_data->is_axis_swapped;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

bool SpatialReference::EPSGTreatsAsLatLong() const
{
  try
  {
    return impl->m_data->epsg_treats_as_lat_long;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

std::size_t SpatialReference::hashValue() const
{
  try
  {
    return impl->m_data->hashvalue;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

const OGRSpatialReference &SpatialReference::operator*() const
{
  try
  {
    return *impl->m_data->crs;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

OGRSpatialReference *SpatialReference::get() const
{
  try
  {
    return impl->m_data->crs.get();
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

SpatialReference::operator OGRSpatialReference &() const
{
  try
  {
    return *impl->m_data->crs;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

SpatialReference::operator OGRSpatialReference *() const
{
  try
  {
    return impl->m_data->crs.get();
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

const ProjInfo &SpatialReference::projInfo() const
{
  try
  {
    return impl->m_data->projinfo;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

const std::string &SpatialReference::projStr() const
{
  try
  {
    return impl->m_data->projinfo.projStr();
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

std::optional<int> SpatialReference::getEPSG() const
{
  const auto &crs = *impl->m_data->crs;

  const auto *root = crs.GetRoot();
  std::string prefix = root->GetValue();

  const std::string authority = "AUTHORITY";

  if (prefix != "PROJCS" && prefix != "GEOGCS")
    return {};

  for (int i = 0; i < root->GetChildCount(); i++)
  {
    const auto *node = root->GetChild(i);
    if (node != nullptr)
    {
      const auto *name = node->GetValue();
      if (name != nullptr && authority == name)
      {
        if (node->GetChildCount() != 2)
          return {};
        const auto *value = node->GetChild(1);
        if (value == nullptr)
          return {};
        return Fmi::stoi(value->GetValue());
      }
    }
  }
  return {};
}

void SpatialReference::setCacheSize(std::size_t newMaxSize)
{
  try
  {
    get_cache().resize(newMaxSize);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

Cache::CacheStats SpatialReference::getCacheStats()
{
  try
  {
    return get_cache().statistics();
  }

  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

}  // namespace Fmi
