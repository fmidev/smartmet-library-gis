#include "SpatialReference.h"
#include "CrsRegistry.h"
#include "OGRSpatialReferenceFactory.h"
#include "ProjInfo.h"
#include <fmt/format.h>
#include <macgyver/Exception.h>
#include <ogr_spatialref.h>
#include <mutex>

namespace Fmi
{
// Implementation details
class SpatialReference::Impl
{
 public:
  // Shared, immutable, derived once per definition string.
  std::shared_ptr<const CrsRegistry::Derived> m_data;

  // The definition string, when there is one. It is what an instance clones its
  // own private OGRSpatialReference from, on demand.
  std::string m_desc;

  // Set instead of m_desc when the CRS came from a caller-supplied
  // OGRSpatialReference: there is no key to clone a fresh copy from, so the
  // private clone made at construction is kept and shared by copies.
  std::shared_ptr<OGRSpatialReference> m_owned;

  ~Impl() = default;

  // Copies share the derived values but NOT the object: each instance clones its
  // own the first time one is actually asked for. This is what makes copying a
  // SpatialReference cheap while keeping every instance's object private.
  Impl(const Impl &other)
      : m_data(other.m_data), m_desc(other.m_desc), m_owned(other.m_owned)
  {
  }

  explicit Impl(const SpatialReference &other)
      : m_data(other.impl->m_data), m_desc(other.impl->m_desc), m_owned(other.impl->m_owned)
  {
  }

  // The private OGRSpatialReference belonging to this instance. Cloned lazily:
  // most callers only ever read the derived values (WKT, PROJ string, EPSG code,
  // axis flags), which are precomputed, and never need an object at all.
  OGRSpatialReference *object() const
  {
    if (m_owned)
      return m_owned.get();

    std::call_once(m_once, [this]() { m_own = OGRSpatialReferenceFactory::Create(m_desc); });
    return m_own.get();
  }

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
      // One lookup, one store: CrsRegistry keeps the master object and these
      // derived values under the same key, and derives them only when asked.
      m_desc = theCRS;
      m_data = CrsRegistry::derived(theCRS);
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
      // No definition string, so nothing can be shared or cached: this is the
      // expensive path (see CrsRegistry::derive). Derive from our own clone
      // rather than from the caller's object, which may be shared and which
      // exportToWkt() would mutate PROJ-side caches inside.
      std::shared_ptr<OGRSpatialReference> tmp(other.Clone(),
                                               [](OGRSpatialReference *ref) { ref->Release(); });
      if (!tmp)
        throw Fmi::Exception(BCP, "Failed to clone spatial reference");

      m_owned = tmp;
      m_data = CrsRegistry::derive(*tmp);
    }
    catch (...)
    {
      throw Fmi::Exception::Trace(BCP, "Operation failed!");
    }
  }

  mutable std::once_flag m_once;
  mutable std::shared_ptr<OGRSpatialReference> m_own;
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
    return *impl->object();
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
    return impl->object();
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
    return *impl->object();
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
    return impl->object();
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

const std::string &SpatialReference::WKT() const
{
  try
  {
    return impl->m_data->wkt;
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
  try
  {
    return impl->m_data->epsg;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

void SpatialReference::setCacheSize(std::size_t newMaxSize)
{
  try
  {
    CrsRegistry::SetCacheSize(newMaxSize);
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
    return CrsRegistry::getCacheStats();
  }

  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

}  // namespace Fmi
