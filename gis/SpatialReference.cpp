#include "SpatialReference.h"
#include "OGR.h"
#include "OGRSpatialReferenceFactory.h"
#include "ProjInfo.h"
#include <fmt/format.h>
#include <macgyver/Exception.h>
#include <macgyver/Hash.h>
#include <macgyver/StaticCleanup.h>
#include <macgyver/StringConversion.h>
#include <ogr_geometry.h>
#include <mutex>

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
  std::optional<int> epsg;
  std::string wkt;
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

// Extract the EPSG code from the WKT node tree of the CRS.
//
// This must only be called while the CRS is still private to the calling thread
// (see derive_mutex() below), never on the shared object returned by
// OGRSpatialReferenceFactory. GetRoot() builds GDAL's OGR_SRSNode tree lazily on
// first use and stores it in the OGRSpatialReference, and the raw node pointers
// we walk here remain owned by it. Two threads doing this at the same time
// therefore both build a tree and both assign it, and a third can be walking the
// one that was replaced.
std::optional<int> get_epsg(const OGRSpatialReference &crs)
{
  try
  {
    const auto *root = crs.GetRoot();
    if (root == nullptr)
      return {};

    const std::string prefix = root->GetValue();
    if (prefix != "PROJCS" && prefix != "GEOGCS")
      return {};

    const std::string authority = "AUTHORITY";

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
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// Serializes deriving the cached properties of a CRS.
//
// Everything Impl::init() reads from the OGRSpatialReference is derived exactly
// once per CRS and then served from ImplData, but the object it reads from is the
// process-wide shared one owned by OGRSpatialReferenceFactory. Concurrent first
// uses of the same CRS would run those GDAL calls on it in parallel, and several
// of them are not the pure reads their const signatures suggest:
//
//   - GetRoot() builds the OGR_SRSNode tree on demand (see get_epsg above).
//   - EPSGTreatsAsLatLong() and EPSGTreatsAsNorthingEasting() call
//     demoteFromBoundCRS()/undoDemoteFromBoundCRS() on every call, which swap
//     the underlying PJ* out and temporarily set the node tree pointer to null.
//     Any CRS with +towgs84 or +nadgrids is a BoundCRS, which includes most of
//     the entries in OGRSpatialReferenceFactory's known_datums table.
//
// GDAL only guards those paths when the OGRSpatialReference was created with
// AssignAndSetThreadSafe(), which these are not. So serialize the derivation
// here. Same reasoning and same cost as the parse mutex in
// OGRSpatialReferenceFactory: this is a cold-miss-only path, warm lookups return
// from the cache above without reaching it.
std::mutex &derive_mutex()
{
  static std::mutex g_deriveMutex;
  return g_deriveMutex;
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
      // Fast path: warm cache hit, no serialization
      auto obj = get_cache().find(theCRS);
      if (obj)
        m_data = *obj;
      else
      {
        // Cold miss. The properties below are derived from the shared CRS object
        // with GDAL calls that are not safe to run concurrently on it.
        std::lock_guard<std::mutex> deriveLock(derive_mutex());

        // Re-check under the lock: another thread may have derived the same key
        // while we waited, in which case we reuse its result.
        obj = get_cache().find(theCRS);
        if (obj)
        {
          m_data = *obj;
          return;
        }

        m_data = std::make_shared<ImplData>();
        m_data->crs = OGRSpatialReferenceFactory::Create(theCRS);

        // Generate WKT only once, and cache spatial references for better speed
        m_data->wkt = OGR::exportToWkt(*m_data->crs);

        try
        {
          // exportToProj may lose the original +type=crs setting, hence we try direct parsing first
          m_data->projinfo = ProjInfo(theCRS);
        }
        catch (...)
        {
          m_data->projinfo = ProjInfo(OGR::exportToProj(*m_data->crs));
        }
        m_data->hashvalue = Fmi::hash_value(m_data->wkt);  // WKT is more reliable than PROJ strings

        m_data->is_geographic = (m_data->crs->IsGeographic() != 0);
        m_data->is_axis_swapped = is_axis_swapped(*m_data->crs);
        m_data->epsg_treats_as_lat_long = (m_data->crs->EPSGTreatsAsLatLong() != 0);
        m_data->epsg = get_epsg(*m_data->crs);

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
      m_data->wkt = OGR::exportToWkt(other);
      m_data->projinfo = ProjInfo(OGR::exportToProj(*m_data->crs));
      m_data->hashvalue = Fmi::hash_value(m_data->wkt);
      m_data->is_geographic = (m_data->crs->IsGeographic() != 0);
      m_data->is_axis_swapped = is_axis_swapped(*m_data->crs);
      m_data->epsg_treats_as_lat_long = (m_data->crs->EPSGTreatsAsLatLong() != 0);
      m_data->epsg = get_epsg(*m_data->crs);
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
