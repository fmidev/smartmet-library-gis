#include "OGRSpatialReferenceFactory.h"
#include "CrsRegistry.h"
#include "OGR.h"
#include "ProjInfo.h"
#include <fmt/format.h>
#include <macgyver/Exception.h>
#include <macgyver/Hash.h>
#include <macgyver/StringConversion.h>
#include <macgyver/StaticCleanup.h>
#include <gdal_version.h>
#include <ogr_geometry.h>
#include <ogr_spatialref.h>
#include <algorithm>
#include <atomic>
#include <set>
#include <list>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace Fmi
{
namespace
{
// Spatial references are handed out as *private* objects: every Create() returns
// a fresh clone that the caller owns outright and may modify. Nothing is ever
// shared and nothing is ever handed back, which is what both libraries require -
// PROJ's contract is that a PJ is used by one thread at a time, and GDAL's
// OGRSpatialReference lazily rebuilds internal PROJ/WKT state even from
// logically-const methods. See ../docs/proj-gdal-thread-safety.md.
//
// Cloning is what makes this cheap enough to do unconditionally, but Clone()
// *reads* (and lazily rebuilds) the object it copies, so the source has to be
// private to the cloning thread too. Hence two levels:
//
//   * the *master* store: one object per definition string, parsed once and
//     never handed out. Cloned only while holding
//     OGRSpatialReferenceFactory::mutex(), and only to seed a thread's sample.
//
//   * a *thread-local sample* per definition string, cloned once from the master.
//     Every subsequent Create() clones this, with no lock at all, because no
//     other thread can reach it.
//
// Measured on a 24-core host (test/SpatialReferenceCloneBench): cloning a
// thread-local sample runs at 221k acquisitions/s on one thread and 2.0M on
// eight. Cloning one *shared* sample under a lock manages 220k on one thread and
// only 153k on eight - it goes backwards, which is why the sample is per-thread.

const std::size_t default_cache_size = 1000;

// How many distinct CRSes each thread keeps a sample of. A thread touches only
// the CRSes of the requests it serves, so this is small; exceeding it costs one
// extra clone-under-lock, never an error.
std::atomic<std::size_t> g_sample_store_size{64};

// One entry per definition string: the master object plus, once somebody asks
// for them, the values derived from it. See CrsRegistry.h for why these are one
// structure and not two caches.
struct MasterEntry
{
  std::shared_ptr<OGRSpatialReference> crs;
  std::once_flag derived_once;
  std::shared_ptr<const CrsRegistry::Derived> derived;
};

using MasterCache = Cache::Cache<std::string, std::shared_ptr<MasterEntry>>;
MasterCache& masterCache()
{
  static MasterCache g_masterCache{default_cache_size};
  // The cached objects hold OGRSpatialReference instances backed by GDAL/PROJ
  // global state. Clear them via StaticCleanup::AtExit (from main()) before the
  // unordered static destruction at exit, which otherwise releases them after
  // PROJ teardown and double-frees with some GDAL/PROJ versions.
  static StaticCleanup cleanup([]() { g_masterCache.clear(); });
  return g_masterCache;
}

void release_crs(OGRSpatialReference* ref)
{
  if (ref != nullptr)
    ref->Release();
}

// ---------------------------------------------------------------------------
// Per-thread sample store: a small LRU of this thread's private sample objects.
// Needs no locking of any kind - it is unreachable from other threads.
// ---------------------------------------------------------------------------
class SampleStore
{
 public:
  SampleStore();
  ~SampleStore();

  SampleStore(const SampleStore&) = delete;
  SampleStore& operator=(const SampleStore&) = delete;
  SampleStore(SampleStore&&) = delete;
  SampleStore& operator=(SampleStore&&) = delete;

  // Release every sample this thread holds.
  //
  // Must happen while GDAL and PROJ still have their own thread-local state.
  // ~OGRSpatialReference reassigns a PROJ context to the object before
  // destroying it (GDAL's ogr_srs_api "we need to reassign the PROJ context"
  // path, see ../docs/proj-gdal-thread-safety.md section 3.6), and if GDAL's TLS
  // has already been torn down that call *creates* a fresh PJ_CONTEXT which
  // nothing will ever free. Leaving these objects to ordinary static/TLS
  // destruction order therefore leaks one PROJ context per retained sample.
  void Clear()
  {
    m_index.clear();
    m_entries.clear();
  }

  OGRSpatialReference* Find(const std::string& key)
  {
    auto pos = m_index.find(key);
    if (pos == m_index.end())
      return nullptr;
    // Touch: move to the front. List iterators stay valid across splice, so the
    // iterator held in m_index remains correct.
    m_entries.splice(m_entries.begin(), m_entries, pos->second);
    return pos->second->second.get();
  }

  void Insert(const std::string& key, const std::shared_ptr<OGRSpatialReference>& srs)
  {
    m_entries.emplace_front(key, srs);
    m_index[key] = m_entries.begin();

    const auto maxsize = g_sample_store_size.load();
    while (m_entries.size() > maxsize && m_entries.size() > 1)
    {
      m_index.erase(m_entries.back().first);
      m_entries.pop_back();
    }
  }

 private:
  using Entry = std::pair<std::string, std::shared_ptr<OGRSpatialReference>>;
  std::list<Entry> m_entries;  // most recently used at the front
  std::unordered_map<std::string, std::list<Entry>::iterator> m_index;
};

// Every live SampleStore, so that all of them can be emptied deterministically
// from main() before GDAL/PROJ tear their own globals down. A store is only ever
// *used* by its owning thread, so the registry lock is taken twice per thread
// (once to register, once to unregister) and never on the hot path.
class SampleStoreRegistry
{
 public:
  void Register(SampleStore* store)
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_stores.insert(store);
  }

  void Unregister(SampleStore* store)
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_stores.erase(store);
  }

  void ClearAll()
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto* store : m_stores)
      store->Clear();
  }

 private:
  std::mutex m_mutex;
  std::set<SampleStore*> m_stores;
};

SampleStoreRegistry& sampleStoreRegistry()
{
  static SampleStoreRegistry g_registry;
  static StaticCleanup cleanup([]() { g_registry.ClearAll(); });
  return g_registry;
}

SampleStore::SampleStore()
{
  // Make GDAL create this thread's PROJ context BEFORE this object finishes
  // constructing.
  //
  // thread_local objects are destroyed in reverse order of the completion of
  // their construction. GDAL keeps each thread's PJ_CONTEXT in a thread_local of
  // its own, and if that one is destroyed first, our samples' destructors make
  // GDAL create a replacement context that nothing will ever free - one leaked
  // PROJ context per thread. Touching GDAL here puts its thread_local ahead of
  // ours in the construction order, so ours is torn down first, while the
  // context it needs is still alive.
  //
  // Costs one proj.db lookup per thread, serialised like every other parse.
  {
    std::lock_guard<std::mutex> lock(OGRSpatialReferenceFactory::mutex());
    OGRSpatialReference probe;
    probe.importFromEPSG(4326);
  }

  sampleStoreRegistry().Register(this);
}

SampleStore::~SampleStore()
{
  sampleStoreRegistry().Unregister(this);

  // Clear explicitly rather than relying on member destruction order relative to
  // GDAL's own thread-local teardown, which is unspecified.
  Clear();
}

SampleStore& sampleStore()
{
  static thread_local SampleStore g_store;
  return g_store;
}

// Known datums : those listed in PROJ.4 pj_datums.c

std::map<std::string, std::string> known_datums = {
    {"FMI", "+R=6371229 +towgs84=0,0,0"},
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

// Parse a definition string into a brand new, thread-private object.
//
// Must be called while holding OGRSpatialReferenceFactory::mutex(). Parsing calls
// SetFromUserInput, which for named datums/ellipsoids queries proj.db through
// SQLite; running many such parses concurrently serialised worker threads on
// PROJ's SQLite mutex and could deadlock them.
std::shared_ptr<OGRSpatialReference> parse_crs(const std::string& theDesc)
{
  // Substitute for known datums/ellipsoids
  auto desc = theDesc;
  auto pos = known_datums.find(desc);
  if (pos != known_datums.end())
    desc = std::string("+proj=longlat ") + pos->second;
  else
  {
    pos = known_ellipsoids.find(desc);
    if (pos != known_ellipsoids.end())
      desc = std::string("+proj=longlat ") + pos->second;
  }

  std::shared_ptr<OGRSpatialReference> sr(new OGRSpatialReference, release_crs);

  auto err = sr->SetFromUserInput(desc.c_str());
  if (err != OGRERR_NONE)
  {
    if (theDesc == desc)
      throw Fmi::Exception(BCP, "Failed to create spatial reference: " + theDesc);

    throw Fmi::Exception(BCP,
                         "Failed to create spatial reference: " + theDesc + " (" + desc + ")");
  }

  // This is done here instead of SpatialReference constructors to make the modification
  // thread safe. Note that changing the strategy leads to calling proj_destroy
  // on the projection, and recreating it.

  sr->SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);

  return sr;
}

// Fetch (or build) the never-handed-out template for this definition string.
// Fetch (or build) the never-handed-out master entry for this definition string.
std::shared_ptr<MasterEntry> get_master_entry(const std::string& theDesc)
{
  // Fast path: warm cache hit, no serialization
  auto cached = masterCache().find(theDesc);
  if (cached)
    return *cached;

  std::lock_guard<std::mutex> parseLock(OGRSpatialReferenceFactory::mutex());

  // Re-check under the lock: another thread may have parsed the same key while
  // we waited, in which case we reuse its result instead of parsing again.
  cached = masterCache().find(theDesc);
  if (cached)
    return *cached;

  auto entry = std::make_shared<MasterEntry>();
  entry->crs = parse_crs(theDesc);
  masterCache().insert(theDesc, entry);
  return entry;
}

std::shared_ptr<OGRSpatialReference> get_master(const std::string& theDesc)
{
  return get_master_entry(theDesc)->crs;
}

// This thread's private sample for a definition string, seeded on first use.
OGRSpatialReference* get_sample(const std::string& theDesc)
{
  if (auto* sample = sampleStore().Find(theDesc))
    return sample;

  auto master = get_master(theDesc);

  // The master is shared, so cloning it must be serialised against every other
  // use of it. This happens once per thread per CRS.
  std::shared_ptr<OGRSpatialReference> mine;
  {
    std::lock_guard<std::mutex> lock(OGRSpatialReferenceFactory::mutex());
    auto* copy = master->Clone();
    if (copy == nullptr)
      throw Fmi::Exception(BCP, "Failed to clone spatial reference: " + theDesc);
    mine.reset(copy, release_crs);
  }

  sampleStore().Insert(theDesc, mine);
  return mine.get();
}

// Hand the caller its own spatial reference.
//
// The returned object is a fresh clone belonging to the caller alone: it may be
// read, mutated, attached to a geometry with assignSpatialReference(), or kept
// for as long as the caller likes, with no effect on anybody else. It is simply
// destroyed when the last reference to it goes away - there is nothing to give
// back and no shared state to corrupt.
std::shared_ptr<OGRSpatialReference> make_crs(std::string theDesc)
{
  try
  {
    if (theDesc.empty())
      throw Fmi::Exception::Trace(BCP, "Cannot create spatial reference from empty string");

    // For some reason getEPSG test fails unless this conversion is done
    if (theDesc == "WGS84")
      theDesc = "EPSG:4326";

    // Cloning this thread's own sample needs no lock: nothing else can touch it.
    auto* copy = get_sample(theDesc)->Clone();
    if (copy == nullptr)
      throw Fmi::Exception(BCP, "Failed to clone spatial reference: " + theDesc);

    return {copy, release_crs};
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ---------------------------------------------------------------------------
// Derivation of the cached CRS properties.
//
// Every one of these runs on an object private to the calling thread, which
// matters: several are not the pure reads their const signatures suggest.
// GetRoot() builds the OGR_SRSNode tree on demand, and EPSGTreatsAsLatLong() /
// EPSGTreatsAsNorthingEasting() call demoteFromBoundCRS() /
// undoDemoteFromBoundCRS() on every call, swapping the underlying PJ* out and
// briefly nulling the node tree pointer. Any CRS with +towgs84 or +nadgrids is a
// BoundCRS, which covers most of the known_datums table above.
// ---------------------------------------------------------------------------

bool derive_axis_swapped(const OGRSpatialReference &crs)
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

CrsRegistry::Derived derive_from(const OGRSpatialReference& crs, const std::string& desc)
{
  CrsRegistry::Derived d;

  d.wkt = OGR::exportToWkt(crs);

  if (!desc.empty())
  {
    try
    {
      // exportToProj may lose the original +type=crs setting, hence we try direct parsing first
      d.projinfo = ProjInfo(desc);
    }
    catch (...)
    {
      d.projinfo = ProjInfo(OGR::exportToProj(crs));
    }
  }
  else
  {
    d.projinfo = ProjInfo(OGR::exportToProj(crs));
  }

  d.hashvalue = Fmi::hash_value(d.wkt);  // WKT is more reliable than PROJ strings
  d.is_geographic = (crs.IsGeographic() != 0);
  d.is_axis_swapped = derive_axis_swapped(crs);
  d.epsg_treats_as_lat_long = (crs.EPSGTreatsAsLatLong() != 0);
  d.epsg = get_epsg(crs);

  return d;
}

}  // namespace

namespace OGRSpatialReferenceFactory
{
std::mutex& mutex()
{
  static std::mutex g_mutex;
  return g_mutex;
}

std::shared_ptr<OGRSpatialReference> Create(const std::string& theDesc)
{
  try
  {
    return make_crs(theDesc);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

std::shared_ptr<OGRSpatialReference> Create(int epsg)
{
  try
  {
    auto desc = fmt::format("EPSG:{}", epsg);
    return make_crs(desc);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

void SetCacheSize(std::size_t newMaxSize)
{
  try
  {
    masterCache().resize(newMaxSize);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

void SetSampleStoreSize(std::size_t newMaxSize)
{
  g_sample_store_size.store(newMaxSize > 0 ? newMaxSize : 1);
}

std::size_t getSampleStoreSize()
{
  return g_sample_store_size.load();
}

Cache::CacheStats getCacheStats()
{
  return masterCache().statistics();
}

void ReleaseThreadSamples()
{
  sampleStore().Clear();
}

}  // namespace OGRSpatialReferenceFactory

namespace CrsRegistry
{
std::shared_ptr<const Derived> derived(const std::string& desc)
{
  try
  {
    auto entry = get_master_entry(desc == "WGS84" ? std::string("EPSG:4326") : desc);

    std::call_once(entry->derived_once,
                   [&entry, &desc]()
                   {
                     // Derive from a PRIVATE clone, never from the master: the
                     // calls involved mutate the object they read.
                     auto own = make_crs(desc);
                     entry->derived =
                         std::make_shared<const Derived>(derive_from(*own, desc));
                   });

    return entry->derived;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

std::shared_ptr<const Derived> derive(const OGRSpatialReference& crs)
{
  try
  {
    return std::make_shared<const Derived>(derive_from(crs, std::string()));
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

Cache::CacheStats getCacheStats()
{
  return masterCache().statistics();
}

void SetCacheSize(std::size_t newMaxSize)
{
  try
  {
    masterCache().resize(newMaxSize);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

}  // namespace CrsRegistry
}  // namespace Fmi
