// Concurrency battery for the spatial-reference ownership design.
//
// SpatialReferenceOwnershipTest.cpp asserts the ownership *contract* with small,
// mostly deterministic tests. This file is the adversarial half: it attacks
// every concurrency surface of the design as hard as a unit test reasonably can,
// and it verifies *values*, not just liveness - a thread-safety defect in this
// code historically produced silently wrong coordinates, not crashes.
//
// Surfaces covered, each in its own section below:
//
//   1. Sustained correctness   - derived values and transforms under contention,
//                                compared bit-exactly against single-threaded
//                                baselines computed in the same process
//   2. Cold-start races        - many threads racing the FIRST use of a key:
//                                master parse, call_once derivation, seeding of
//                                per-thread samples, transformation construction
//   3. Facade semantics        - Fmi::SpatialReference shared/copied across
//                                threads, the lazy-clone call_once race
//   4. Mutation storms         - callers mutating their objects while others
//                                verify; nothing may leak between callers
//   5. Transformation pool     - exclusivity, recycling exactness, tiny-pool churn
//   6. Cache pressure          - eviction storms, resizing mid-flight, held
//                                objects surviving eviction
//   7. Lifecycle               - thread churn, cross-thread destruction, objects
//                                outliving their creator thread, ReleaseThreadSamples
//   8. Error paths             - invalid definitions under contention
//
// Thread counts deliberately exceed typical CI core counts: oversubscription
// widens race windows. Every multi-thread test starts its threads on a barrier
// so they hit the tested window together instead of trickling in.
//
// GIS_THREAD_STRESS=<n> multiplies iteration counts for soak runs, e.g.
//   GIS_THREAD_STRESS=20 ./SpatialReferenceConcurrencyTest
// The default (1) keeps the whole battery in tens of seconds for CI.
//
// Run it under sanitizers with the LIBRARY instrumented too, or GDAL/PROJ-side
// accesses stay invisible (see ../docs/proj-gdal-thread-safety.md):
//   make clean && make TSAN=yes && make -C test TSAN=yes SpatialReferenceConcurrencyTest
//   make clean && make ASAN=yes && make -C test ASAN=yes SpatialReferenceConcurrencyTest

#include "CoordinateTransformation.h"
#include "OGRCoordinateTransformationFactory.h"
#include "OGRSpatialReferenceFactory.h"
#include "ProjInfo.h"
#include "SpatialReference.h"

#include <cpl_conv.h>
#include <cpl_error.h>
#include <ogr_geometry.h>
#include <ogr_spatialref.h>

#include <gtest/gtest.h>

#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace
{
// ---------------------------------------------------------------------------
// Infrastructure
// ---------------------------------------------------------------------------

int stress()
{
  static const int value = []
  {
    const char* env = std::getenv("GIS_THREAD_STRESS");
    if (env == nullptr)
      return 1;
    const int n = std::atoi(env);
    return n > 0 ? n : 1;
  }();
  return value;
}

int scaled(int n)
{
  return n * stress();
}

// C++17 has no std::barrier; a reusable one so every thread enters the tested
// window at the same instant.
class Barrier
{
 public:
  explicit Barrier(int count) : m_threshold(count), m_count(count) {}

  void wait()
  {
    std::unique_lock<std::mutex> lock(m_mutex);
    const auto generation = m_generation;
    if (--m_count == 0)
    {
      ++m_generation;
      m_count = m_threshold;
      m_cv.notify_all();
    }
    else
      m_cv.wait(lock, [this, generation] { return generation != m_generation; });
  }

 private:
  std::mutex m_mutex;
  std::condition_variable m_cv;
  const int m_threshold;
  int m_count;
  long m_generation = 0;
};

// Run nthreads copies of body, barrier-synchronised at the start. An exception
// escaping a thread is reported as a test failure, never as a terminate().
void run_threads(int nthreads, const std::function<void(int)>& body)
{
  Barrier barrier(nthreads);
  std::vector<std::string> errors(nthreads);
  std::vector<std::thread> threads;
  threads.reserve(nthreads);

  for (int i = 0; i < nthreads; i++)
  {
    threads.emplace_back(
        [&, i]
        {
          barrier.wait();
          try
          {
            body(i);
          }
          catch (const std::exception& e)
          {
            errors[i] = e.what();
          }
          catch (...)
          {
            errors[i] = "unknown exception";
          }
        });
  }
  for (auto& t : threads)
    t.join();

  for (int i = 0; i < nthreads; i++)
    EXPECT_TRUE(errors[i].empty()) << "thread " << i << " threw: " << errors[i];
}

std::string wkt_of(const OGRSpatialReference& srs)
{
  char* wkt = nullptr;
  if (srs.exportToWkt(&wkt) != OGRERR_NONE || wkt == nullptr)
    return {};
  const std::string result(wkt);
  CPLFree(wkt);
  return result;
}

// A definition string this process has never parsed before: varying the false
// easting yields a new, valid CRS every call. Used to make cold-start windows
// reproducible without restarting the process.
std::string fresh_key()
{
  static std::atomic<long> counter{0};
  const long n = ++counter;
  return "+proj=tmerc +lat_0=0 +lon_0=25 +k=0.9996 +x_0=" + std::to_string(500000 + n) +
         " +y_0=0 +ellps=GRS80 +units=m +no_defs +type=crs";
}

// ---------------------------------------------------------------------------
// The CRS matrix and its single-threaded baselines.
//
// Deliberately heterogeneous: plain geographic, datum-shifted projected (KKJ is
// a BoundCRS via +towgs84, which exercises the demote/undemote path inside
// EPSGTreatsAsLatLong), web mercator, LAEA, the WGS84 alias, the FMI sphere
// datum, and a raw PROJ string that never touches proj.db.
// ---------------------------------------------------------------------------

const std::vector<std::string>& crs_matrix()
{
  static const std::vector<std::string> matrix = {
      "EPSG:4326",
      "EPSG:4258",
      "EPSG:2393",
      "EPSG:3067",
      "EPSG:3857",
      "EPSG:3035",
      "WGS84",
      "FMI",
      "+proj=stere +lat_0=90 +lat_ts=60 +lon_0=25 +R=6371229 +units=m +no_defs +type=crs"};
  return matrix;
}

// Test points inside the validity area of every CRS in the matrix.
const std::vector<std::pair<double, double>>& test_points()
{
  static const std::vector<std::pair<double, double>> points = {
      {25.0, 60.0}, {21.5, 63.2}, {27.68, 68.9}, {24.94, 60.17}};
  return points;
}

struct Baseline
{
  std::string wkt;
  std::string projstr;
  std::optional<int> epsg;
  std::size_t hash = 0;
  bool geographic = false;
  bool axis_swapped = false;
  // Forward transforms of test_points() from WGS84, then those back to WGS84.
  std::vector<std::pair<double, double>> forward;
  std::vector<std::pair<double, double>> round_trip;
};

// Computed once, single-threaded, in this process: the reference every
// concurrent result must match bit-exactly. Computing it in-process (rather
// than hardcoding coordinates) makes the comparison independent of the
// PROJ/GDAL/grid versions installed - what is asserted is that concurrency
// changes nothing, not what the correct coordinates are.
const std::map<std::string, Baseline>& baselines()
{
  static const std::map<std::string, Baseline> result = []
  {
    std::map<std::string, Baseline> out;
    for (const auto& name : crs_matrix())
    {
      Baseline b;
      Fmi::SpatialReference sr(name);
      b.wkt = sr.WKT();
      b.projstr = sr.projStr();
      b.epsg = sr.getEPSG();
      b.hash = sr.hashValue();
      b.geographic = sr.isGeographic();
      b.axis_swapped = sr.isAxisSwapped();

      Fmi::CoordinateTransformation forward("WGS84", name);
      Fmi::CoordinateTransformation inverse(name, "WGS84");
      for (const auto& p : test_points())
      {
        double x = p.first;
        double y = p.second;
        if (!forward.transform(x, y))
          throw std::runtime_error("baseline forward transform failed for " + name);
        b.forward.emplace_back(x, y);
        if (!inverse.transform(x, y))
          throw std::runtime_error("baseline inverse transform failed for " + name);
        b.round_trip.emplace_back(x, y);
      }
      out[name] = b;
    }
    return out;
  }();
  return result;
}

// Verify one CRS against its baseline from inside a worker thread. Returns the
// number of mismatches; the caller accumulates and asserts zero, since gtest
// EXPECTs are not thread-safe pre-1.10 everywhere we build.
int check_crs(const std::string& name, const Baseline& b)
{
  int bad = 0;

  Fmi::SpatialReference sr(name);
  if (sr.WKT() != b.wkt)
    ++bad;
  if (sr.projStr() != b.projstr)
    ++bad;
  if (sr.getEPSG() != b.epsg)
    ++bad;
  if (sr.hashValue() != b.hash)
    ++bad;
  if (sr.isGeographic() != b.geographic)
    ++bad;
  if (sr.isAxisSwapped() != b.axis_swapped)
    ++bad;

  Fmi::CoordinateTransformation forward("WGS84", name);
  Fmi::CoordinateTransformation inverse(name, "WGS84");
  for (std::size_t i = 0; i < test_points().size(); i++)
  {
    double x = test_points()[i].first;
    double y = test_points()[i].second;
    // Bit-exact: the identical transformation of the identical point must not
    // drift, no matter how many threads are doing it.
    if (!forward.transform(x, y) || x != b.forward[i].first || y != b.forward[i].second)
      ++bad;
    if (!inverse.transform(x, y) || x != b.round_trip[i].first || y != b.round_trip[i].second)
      ++bad;
  }

  return bad;
}

// Restore the global knobs no matter how a test exits.
struct CacheConfigGuard
{
  ~CacheConfigGuard()
  {
    Fmi::OGRSpatialReferenceFactory::SetCacheSize(1000);
    Fmi::OGRSpatialReferenceFactory::SetSampleStoreSize(64);
    Fmi::OGRCoordinateTransformationFactory::SetMaxSize(40UL * 40UL);
  }
};

}  // namespace

// ===========================================================================
// 1. Sustained correctness under contention
// ===========================================================================

TEST(SpatialReferenceConcurrency, DerivedValuesUnderContentionMatchBaseline)
{
  const auto& base = baselines();
  std::atomic<int> bad{0};

  run_threads(16,
              [&](int id)
              {
                const auto& matrix = crs_matrix();
                for (int round = 0; round < scaled(60); ++round)
                {
                  const auto& name = matrix[(id + round) % matrix.size()];
                  const auto& b = base.at(name);
                  Fmi::SpatialReference sr(name);
                  if (sr.WKT() != b.wkt || sr.projStr() != b.projstr || sr.getEPSG() != b.epsg ||
                      sr.hashValue() != b.hash || sr.isGeographic() != b.geographic ||
                      sr.isAxisSwapped() != b.axis_swapped)
                    ++bad;
                }
              });

  EXPECT_EQ(bad.load(), 0) << "derived values drifted from the single-threaded baseline";
}

TEST(SpatialReferenceConcurrency, TransformsUnderContentionAreBitExact)
{
  const auto& base = baselines();
  std::atomic<int> bad{0};

  run_threads(16,
              [&](int id)
              {
                const auto& matrix = crs_matrix();
                for (int round = 0; round < scaled(40); ++round)
                {
                  const auto& name = matrix[(id + round) % matrix.size()];
                  bad += check_crs(name, base.at(name));
                }
              });

  EXPECT_EQ(bad.load(), 0) << "a concurrent transform differed from the single-threaded result";
}

TEST(SpatialReferenceConcurrency, GeometryTransformsUnderContentionAreBitExact)
{
  const char* polygon_wkt =
      "POLYGON ((20 59,30 59,30 70,20 70,20 59),(24 61,26 61,26 63,24 63,24 61))";

  // Single-threaded reference per target CRS.
  std::map<std::string, std::string> expected;
  for (const auto& name : crs_matrix())
  {
    OGRGeometry* geom = nullptr;
    ASSERT_EQ(OGRGeometryFactory::createFromWkt(polygon_wkt, nullptr, &geom), OGRERR_NONE);
    Fmi::CoordinateTransformation t("WGS84", name);
    ASSERT_TRUE(t.transform(*geom)) << name;
    char* out = nullptr;
    ASSERT_EQ(geom->exportToWkt(&out), OGRERR_NONE);
    expected[name] = out;
    CPLFree(out);
    OGRGeometryFactory::destroyGeometry(geom);
  }

  std::atomic<int> bad{0};
  run_threads(12,
              [&](int id)
              {
                const auto& matrix = crs_matrix();
                for (int round = 0; round < scaled(25); ++round)
                {
                  const auto& name = matrix[(id + round) % matrix.size()];
                  OGRGeometry* geom = nullptr;
                  if (OGRGeometryFactory::createFromWkt(polygon_wkt, nullptr, &geom) != OGRERR_NONE)
                  {
                    ++bad;
                    continue;
                  }
                  Fmi::CoordinateTransformation t("WGS84", name);
                  if (!t.transform(*geom))
                    ++bad;
                  else
                  {
                    char* out = nullptr;
                    if (geom->exportToWkt(&out) != OGRERR_NONE)
                      ++bad;
                    else
                    {
                      if (expected[name] != out)
                        ++bad;
                      CPLFree(out);
                    }
                  }
                  OGRGeometryFactory::destroyGeometry(geom);
                }
              });

  EXPECT_EQ(bad.load(), 0) << "a concurrently transformed geometry differed from the baseline";
}

// ===========================================================================
// 2. Cold-start races: the first-ever use of a key is the widest window.
//    The warm path clones a thread-local sample with no locking at all; the
//    cold path runs the parse under the factory mutex, publishes the master
//    entry, runs the call_once derivation, and seeds per-thread samples. All
//    of that must be correct when every thread arrives at once.
// ===========================================================================

TEST(SpatialReferenceConcurrency, ColdCreateSameKeyFromManyThreads)
{
  for (int repeat = 0; repeat < scaled(5); ++repeat)
  {
    const auto key = fresh_key();
    constexpr int kThreads = 16;

    std::vector<std::shared_ptr<OGRSpatialReference>> results(kThreads);
    run_threads(kThreads,
                [&](int id) { results[id] = Fmi::OGRSpatialReferenceFactory::Create(key); });

    std::set<const void*> distinct;
    const auto reference_wkt = wkt_of(*results[0]);
    ASSERT_FALSE(reference_wkt.empty());
    for (const auto& srs : results)
    {
      ASSERT_TRUE(srs);
      distinct.insert(srs.get());
      EXPECT_EQ(wkt_of(*srs), reference_wkt) << "cold-start racers got different CRSes";
      EXPECT_TRUE(srs->IsProjected());
      EXPECT_EQ(srs->GetAxisMappingStrategy(), OAMS_TRADITIONAL_GIS_ORDER);
    }
    EXPECT_EQ(distinct.size(), results.size()) << "cold-start racers were handed a shared object";
  }
}

TEST(SpatialReferenceConcurrency, ColdDeriveSameKeyFromManyThreads)
{
  for (int repeat = 0; repeat < scaled(5); ++repeat)
  {
    const auto key = fresh_key();
    constexpr int kThreads = 16;

    // Every thread constructs a facade for the never-before-seen key at the
    // same instant: the call_once derivation must run exactly once, and every
    // instance must end up sharing that one Derived block.
    std::vector<const void*> derived_addr(kThreads, nullptr);
    std::vector<std::size_t> hashes(kThreads, 0);
    run_threads(kThreads,
                [&](int id)
                {
                  Fmi::SpatialReference sr(key);
                  derived_addr[id] = &sr.WKT();
                  hashes[id] = sr.hashValue();
                });

    for (int i = 1; i < kThreads; i++)
    {
      EXPECT_EQ(derived_addr[i], derived_addr[0])
          << "two racers derived (and cached) separate value blocks";
      EXPECT_EQ(hashes[i], hashes[0]);
    }
  }
}

TEST(SpatialReferenceConcurrency, ColdMixedFactoryAndFacadeOnOneKey)
{
  // Half the threads want a raw object, half want derived values, all on the
  // same cold key: the master parse and the derivation race each other through
  // the same entry.
  for (int repeat = 0; repeat < scaled(5); ++repeat)
  {
    const auto key = fresh_key();
    std::atomic<int> bad{0};

    run_threads(16,
                [&](int id)
                {
                  if (id % 2 == 0)
                  {
                    auto srs = Fmi::OGRSpatialReferenceFactory::Create(key);
                    if (!srs || wkt_of(*srs).empty() || !srs->IsProjected())
                      ++bad;
                  }
                  else
                  {
                    Fmi::SpatialReference sr(key);
                    if (sr.WKT().empty() || sr.isGeographic())
                      ++bad;
                  }
                });

    EXPECT_EQ(bad.load(), 0);
  }
}

TEST(SpatialReferenceConcurrency, ColdDistinctKeysInParallel)
{
  // Every thread parses a different cold key at once. The parses serialise on
  // the factory mutex by design; what is asserted is that they neither deadlock
  // nor cross-contaminate each other's results.
  constexpr int kThreads = 16;
  std::vector<std::string> keys(kThreads);
  std::vector<double> false_easting(kThreads);
  for (int i = 0; i < kThreads; i++)
  {
    keys[i] = fresh_key();
    const auto pos = keys[i].find("+x_0=");
    ASSERT_NE(pos, std::string::npos);
    false_easting[i] = std::atof(keys[i].c_str() + pos + 5);
  }

  std::atomic<int> bad{0};
  run_threads(kThreads,
              [&](int id)
              {
                for (int round = 0; round < scaled(3); ++round)
                {
                  auto srs = Fmi::OGRSpatialReferenceFactory::Create(keys[id]);
                  if (wkt_of(*srs).empty())
                    ++bad;
                  // Every key differs only in false easting; the derived values
                  // must carry this thread's value, not a neighbour's.
                  Fmi::SpatialReference sr(keys[id]);
                  const auto x0 = sr.projInfo().getDouble("x_0");
                  if (!x0 || *x0 != false_easting[id])
                    ++bad;
                }
              });

  EXPECT_EQ(bad.load(), 0) << "parallel cold parses contaminated each other";
}

TEST(SpatialReferenceConcurrency, ColdTransformationsInParallel)
{
  // All threads build the same never-before-seen transformation at once and
  // must agree with each other and with a single-threaded recomputation.
  for (int repeat = 0; repeat < scaled(3); ++repeat)
  {
    const auto key = fresh_key();
    constexpr int kThreads = 12;
    std::vector<std::pair<double, double>> results(kThreads);
    std::atomic<int> failures{0};

    run_threads(kThreads,
                [&](int id)
                {
                  double x = 25.0;
                  double y = 60.0;
                  Fmi::CoordinateTransformation t("WGS84", key);
                  if (!t.transform(x, y))
                    ++failures;
                  results[id] = {x, y};
                });

    ASSERT_EQ(failures.load(), 0);
    double x = 25.0;
    double y = 60.0;
    Fmi::CoordinateTransformation reference("WGS84", key);
    ASSERT_TRUE(reference.transform(x, y));
    for (int i = 0; i < kThreads; i++)
    {
      EXPECT_EQ(results[i].first, x) << "thread " << i;
      EXPECT_EQ(results[i].second, y) << "thread " << i;
    }
  }
}

// ===========================================================================
// 3. Facade semantics across threads
// ===========================================================================

TEST(SpatialReferenceConcurrency, SharedConstFacadeReadsAreSafeFromAllThreads)
{
  // One instance, shared by const reference: the derived-value accessors are
  // reads of immutable state and must be safe from any number of threads.
  const Fmi::SpatialReference shared("EPSG:3067");
  const auto& b = baselines().at("EPSG:3067");
  std::atomic<int> bad{0};

  run_threads(16,
              [&](int)
              {
                for (int round = 0; round < scaled(2000); ++round)
                {
                  if (shared.WKT() != b.wkt || shared.projStr() != b.projstr ||
                      shared.getEPSG() != b.epsg || shared.hashValue() != b.hash ||
                      shared.isGeographic() != b.geographic ||
                      shared.isAxisSwapped() != b.axis_swapped ||
                      shared.projInfo().projStr() != b.projstr)
                    ++bad;
                }
              });

  EXPECT_EQ(bad.load(), 0);
}

TEST(SpatialReferenceConcurrency, LazyCloneRaceYieldsExactlyOneObject)
{
  // The instance's private OGRSpatialReference is cloned on first get(), under
  // a call_once. All racers must observe the same object, and it must be built
  // exactly once - a lost clone would either leak or hand a half-built object
  // to somebody.
  for (int repeat = 0; repeat < scaled(20); ++repeat)
  {
    const Fmi::SpatialReference sr("EPSG:2393");
    constexpr int kThreads = 16;
    std::vector<const OGRSpatialReference*> seen(kThreads, nullptr);

    run_threads(kThreads, [&](int id) { seen[id] = sr.get(); });

    ASSERT_NE(seen[0], nullptr);
    for (int i = 1; i < kThreads; i++)
      EXPECT_EQ(seen[i], seen[0]) << "the lazy clone was built more than once";

    EXPECT_TRUE(sr.get()->IsProjected());
  }
}

TEST(SpatialReferenceConcurrency, ConcurrentCopiesGetPrivateObjects)
{
  // Copying is the documented way to share a SpatialReference between threads:
  // copies share the immutable derived values but each clones its own object.
  const Fmi::SpatialReference original("EPSG:3035");
  constexpr int kThreads = 12;
  // Each thread's last copy is kept alive so the address comparison below is
  // between live objects: comparing freed addresses would be meaningless, the
  // allocator may reuse them.
  std::vector<std::optional<Fmi::SpatialReference>> kept(kThreads);
  std::atomic<int> bad{0};

  run_threads(kThreads,
              [&](int id)
              {
                for (int round = 0; round < scaled(50); ++round)
                {
                  Fmi::SpatialReference copy(original);
                  if (&copy.WKT() != &original.WKT())  // derived values shared...
                    ++bad;
                  auto* obj = copy.get();  // ...objects private
                  if (obj == nullptr || obj->IsGeographic())
                    ++bad;
                  // Using the private object is legitimate, including mutation.
                  obj->SetFromUserInput("EPSG:4326");
                }
                kept[id].emplace(original);
                if (kept[id]->get() == nullptr)
                  ++bad;
              });

  EXPECT_EQ(bad.load(), 0);
  std::set<const void*> distinct;
  for (const auto& copy : kept)
    distinct.insert(copy->get());
  EXPECT_EQ(distinct.size(), kept.size()) << "two live copies shared one object";
  EXPECT_FALSE(original.get()->IsGeographic()) << "a copy's mutation reached the original";
}

TEST(SpatialReferenceConcurrency, FacadeFromRawObjectKeepsOneOwnedObject)
{
  // Pin the documented exception: built from a caller-supplied raw
  // OGRSpatialReference there is no definition string to re-clone from, so the
  // private clone made at construction is shared by copies. Such instances are
  // therefore NOT safe to copy across threads and use concurrently - the
  // definition-string constructor is. This test freezes that contract so a
  // change to it is a conscious one.
  OGRSpatialReference raw;
  ASSERT_EQ(raw.SetFromUserInput("EPSG:3067"), OGRERR_NONE);

  Fmi::SpatialReference original(raw);
  Fmi::SpatialReference copy(original);

  EXPECT_EQ(original.get(), copy.get())
      << "instances built from a raw object were expected to share their clone; if this "
         "changed on purpose, update the class documentation too";
  EXPECT_EQ(&original.WKT(), &copy.WKT());
}

TEST(SpatialReferenceConcurrency, ParallelConstructionFromPrivateRawObjects)
{
  // The expensive no-key path (re-derives everything, ~ms): each thread builds
  // facades from raw objects it owns privately, all at once.
  const auto& b = baselines().at("EPSG:3067");
  std::atomic<int> bad{0};

  run_threads(8,
              [&](int)
              {
                for (int round = 0; round < scaled(3); ++round)
                {
                  OGRSpatialReference raw;
                  if (raw.SetFromUserInput("EPSG:3067") != OGRERR_NONE)
                  {
                    ++bad;
                    continue;
                  }
                  raw.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
                  Fmi::SpatialReference sr(raw);
                  if (sr.isGeographic() || sr.getEPSG() != b.epsg)
                    ++bad;
                  if (sr.projStr() != b.projstr)
                    ++bad;
                }
              });

  EXPECT_EQ(bad.load(), 0);
}

TEST(SpatialReferenceConcurrency, Wgs84AliasSharesOneEntry)
{
  // "WGS84" is canonicalised to "EPSG:4326"; both names must resolve to the
  // same registry entry, from any thread.
  Fmi::SpatialReference a("WGS84");
  Fmi::SpatialReference b("EPSG:4326");
  EXPECT_EQ(&a.WKT(), &b.WKT()) << "the alias created a second entry";
  EXPECT_EQ(a.hashValue(), b.hashValue());

  std::atomic<int> bad{0};
  run_threads(8,
              [&](int id)
              {
                for (int round = 0; round < scaled(100); ++round)
                {
                  Fmi::SpatialReference sr((id + round) % 2 == 0 ? "WGS84" : "EPSG:4326");
                  if (&sr.WKT() != &a.WKT() || !sr.isGeographic())
                    ++bad;
                }
              });
  EXPECT_EQ(bad.load(), 0);
}

// ===========================================================================
// 4. Mutation storms
// ===========================================================================

TEST(SpatialReferenceConcurrency, MutationStormDoesNotDisturbVerifiers)
{
  // Half the threads mutate the objects they are handed, in several different
  // ways, as fast as they can; the other half continuously verify fresh objects
  // and live transformations against the baseline. This is the production
  // incident shape - one misbehaving (or merely legitimate) caller must not be
  // able to affect anybody's coordinates.
  const auto& base = baselines();
  std::atomic<int> bad{0};

  run_threads(16,
              [&](int id)
              {
                const bool mutator = (id % 2 == 0);
                const auto& matrix = crs_matrix();
                for (int round = 0; round < scaled(80); ++round)
                {
                  const auto& name = matrix[(id + round) % matrix.size()];
                  if (mutator)
                  {
                    auto srs = Fmi::OGRSpatialReferenceFactory::Create(name);
                    switch (round % 4)
                    {
                      case 0:
                        srs->SetFromUserInput("EPSG:4326");
                        break;
                      case 1:
                        srs->importFromEPSG(3857);
                        break;
                      case 2:
                        srs->SetAxisMappingStrategy(OAMS_AUTHORITY_COMPLIANT);
                        break;
                      default:
                        srs->Clear();
                        break;
                    }
                  }
                  else
                  {
                    bad += check_crs(name, base.at(name));
                  }
                }
              });

  EXPECT_EQ(bad.load(), 0) << "a mutating caller disturbed a verifying caller";
}

TEST(SpatialReferenceConcurrency, MutatingFacadeObjectsIsPrivate)
{
  // get() hands out the instance's private clone; mutating it must affect
  // neither other instances nor the shared derived values.
  const auto& b = baselines().at("EPSG:3067");
  std::atomic<int> bad{0};

  run_threads(8,
              [&](int)
              {
                for (int round = 0; round < scaled(50); ++round)
                {
                  Fmi::SpatialReference sr("EPSG:3067");
                  sr.get()->SetFromUserInput("EPSG:4326");
                  // The derived values were computed long before the mutation
                  // and are shared and immutable: they must not move.
                  if (sr.WKT() != b.wkt || sr.getEPSG() != b.epsg || sr.isGeographic())
                    ++bad;
                  // And a fresh instance must be pristine.
                  Fmi::SpatialReference fresh("EPSG:3067");
                  if (fresh.get()->IsGeographic())
                    ++bad;
                }
              });

  EXPECT_EQ(bad.load(), 0);
}

// ===========================================================================
// 5. The transformation pool
// ===========================================================================

TEST(SpatialReferenceConcurrency, PoolNeverHandsOneObjectToTwoHolders)
{
  std::mutex mutex;
  std::set<const void*> live;
  std::atomic<int> overlaps{0};
  std::atomic<int> bad{0};
  const auto& b = baselines().at("EPSG:3067");

  run_threads(
      16,
      [&](int)
      {
        for (int round = 0; round < scaled(200); ++round)
        {
          auto t = Fmi::OGRCoordinateTransformationFactory::Create("EPSG:4326", "EPSG:3067");
          {
            std::lock_guard<std::mutex> lock(mutex);
            if (!live.insert(t.get()).second)
              ++overlaps;
          }
          double x = test_points()[0].first;
          double y = test_points()[0].second;
          if (t->Transform(1, &x, &y) == 0 || x != b.forward[0].first || y != b.forward[0].second)
            ++bad;
          {
            std::lock_guard<std::mutex> lock(mutex);
            live.erase(t.get());
          }
        }  // t returns to the pool here, from this thread
      });

  EXPECT_EQ(overlaps.load(), 0) << "the pool handed one transformation to two threads at once";
  EXPECT_EQ(bad.load(), 0);
}

TEST(SpatialReferenceConcurrency, PoolRecyclingPreservesExactResults)
{
  // A transformation that has been used, returned to the pool and checked out
  // again must give the same bits as a brand new one - recycling must not
  // carry state that affects results.
  double x0 = 25.0;
  double y0 = 60.0;
  {
    auto t = Fmi::OGRCoordinateTransformationFactory::Create("EPSG:4326", "EPSG:2393");
    ASSERT_NE(t->Transform(1, &x0, &y0), 0);
  }  // returned to the pool

  for (int round = 0; round < scaled(50); ++round)
  {
    auto t = Fmi::OGRCoordinateTransformationFactory::Create("EPSG:4326", "EPSG:2393");
    double x = 25.0;
    double y = 60.0;
    ASSERT_NE(t->Transform(1, &x, &y), 0);
    EXPECT_EQ(x, x0) << "round " << round;
    EXPECT_EQ(y, y0) << "round " << round;
  }
}

TEST(SpatialReferenceConcurrency, TinyPoolChurnStaysCorrect)
{
  // A one-slot pool with six active pairs: nearly every Create() is a miss that
  // builds a fresh transformation while others are being destroyed into the
  // pool and immediately evicted. Maximum construction/destruction churn.
  CacheConfigGuard guard;
  Fmi::OGRCoordinateTransformationFactory::SetMaxSize(1);

  const auto& base = baselines();
  std::atomic<int> bad{0};

  run_threads(12,
              [&](int id)
              {
                const auto& matrix = crs_matrix();
                for (int round = 0; round < scaled(50); ++round)
                {
                  const auto& name = matrix[(id + round) % matrix.size()];
                  const auto& b = base.at(name);
                  Fmi::CoordinateTransformation t("WGS84", name);
                  double x = test_points()[1].first;
                  double y = test_points()[1].second;
                  if (!t.transform(x, y) || x != b.forward[1].first || y != b.forward[1].second)
                    ++bad;
                }
              });

  EXPECT_EQ(bad.load(), 0) << "pool eviction churn corrupted a transformation";
}

// ===========================================================================
// 6. Cache pressure
// ===========================================================================

TEST(SpatialReferenceConcurrency, MasterEvictionStormStaysCorrect)
{
  // A two-entry master store far smaller than the working set: every access
  // races parse, insert, evict and re-parse against fifteen other threads.
  // Nothing may go wrong except time.
  CacheConfigGuard guard;
  Fmi::OGRSpatialReferenceFactory::SetCacheSize(2);

  const auto& base = baselines();
  std::atomic<int> bad{0};

  run_threads(16,
              [&](int id)
              {
                const auto& matrix = crs_matrix();
                for (int round = 0; round < scaled(30); ++round)
                {
                  const auto& name = matrix[(id + round) % matrix.size()];
                  bad += check_crs(name, base.at(name));
                }
              });

  EXPECT_EQ(bad.load(), 0) << "an eviction storm produced a wrong value";
}

TEST(SpatialReferenceConcurrency, SampleStoreThrashStaysCorrect)
{
  // A one-entry per-thread sample store: every alternating Create() evicts the
  // sample and reseeds it from the master under the lock. The slow path must be
  // exactly as correct as the fast one.
  CacheConfigGuard guard;
  Fmi::OGRSpatialReferenceFactory::SetSampleStoreSize(1);

  const auto& base = baselines();
  std::atomic<int> bad{0};

  run_threads(12,
              [&](int id)
              {
                const auto& matrix = crs_matrix();
                for (int round = 0; round < scaled(40); ++round)
                {
                  const auto& name = matrix[(id + round) % matrix.size()];
                  auto srs = Fmi::OGRSpatialReferenceFactory::Create(name);
                  if (wkt_of(*srs) != base.at(name).wkt)
                    ++bad;
                }
              });

  EXPECT_EQ(bad.load(), 0) << "sample-store thrashing produced a wrong object";
}

TEST(SpatialReferenceConcurrency, ResizingCachesMidFlightIsSafe)
{
  // Operations teams resize caches on live servers. Two threads oscillate both
  // stores between minimal and normal while workers verify values and a reader
  // polls statistics.
  CacheConfigGuard guard;
  const auto& base = baselines();
  std::atomic<int> bad{0};
  std::atomic<bool> stop{false};

  run_threads(12,
              [&](int id)
              {
                if (id == 0)
                {
                  while (!stop)
                  {
                    Fmi::OGRSpatialReferenceFactory::SetCacheSize(1);
                    Fmi::OGRSpatialReferenceFactory::SetCacheSize(400);
                  }
                }
                else if (id == 1)
                {
                  while (!stop)
                  {
                    Fmi::OGRSpatialReferenceFactory::SetSampleStoreSize(1);
                    Fmi::OGRSpatialReferenceFactory::SetSampleStoreSize(64);
                  }
                }
                else if (id == 2)
                {
                  while (!stop)
                  {
                    (void)Fmi::OGRSpatialReferenceFactory::getCacheStats();
                    (void)Fmi::SpatialReference::getCacheStats();
                  }
                }
                else
                {
                  // Whatever way this verifier exits - normally or by exception -
                  // the oscillators must be released, or join() would hang.
                  struct StopGuard
                  {
                    std::atomic<bool>& flag;
                    ~StopGuard() { flag = true; }
                  } release_oscillators{stop};

                  const auto& matrix = crs_matrix();
                  for (int round = 0; round < scaled(40); ++round)
                  {
                    const auto& name = matrix[(id + round) % matrix.size()];
                    bad += check_crs(name, base.at(name));
                  }
                }
              });

  EXPECT_EQ(bad.load(), 0) << "resizing caches under load produced a wrong value";
}

TEST(SpatialReferenceConcurrency, HeldValuesSurviveEviction)
{
  // Entries are evicted from the store while callers still hold what came out
  // of them: the held derived values and the held clone must stay alive and
  // unchanged (shared_ptr semantics), and a post-eviction re-parse must
  // reproduce identical values.
  CacheConfigGuard guard;

  Fmi::SpatialReference held("EPSG:3067");
  auto held_clone = Fmi::OGRSpatialReferenceFactory::Create("EPSG:3067");
  const auto wkt_before = held.WKT();  // copy, for comparison
  const auto* wkt_addr = &held.WKT();

  // Force the entry out: shrink hard and pull enough distinct keys through.
  Fmi::OGRSpatialReferenceFactory::SetCacheSize(2);
  for (int i = 0; i < 8; i++)
    (void)Fmi::OGRSpatialReferenceFactory::Create(fresh_key());

  EXPECT_EQ(&held.WKT(), wkt_addr) << "eviction moved a held value block";
  EXPECT_EQ(held.WKT(), wkt_before);
  EXPECT_TRUE(held_clone->IsProjected());
  EXPECT_EQ(wkt_of(*held_clone), wkt_before);

  // The re-parsed entry is a new block with equal content.
  Fmi::SpatialReference fresh("EPSG:3067");
  EXPECT_EQ(fresh.WKT(), wkt_before);
  EXPECT_EQ(fresh.hashValue(), held.hashValue());
}

// ===========================================================================
// 7. Lifecycle: thread churn, cross-thread destruction, teardown helpers
// ===========================================================================

TEST(SpatialReferenceConcurrency, ThreadChurnManyShortLivedThreads)
{
  // A server that recreates worker threads exercises the per-thread sample
  // store's construction (which deliberately touches GDAL first, for TLS
  // destruction ordering) and destruction (unregister + release) once per
  // thread. Waves of short-lived threads, each doing a little real work.
  const auto& base = baselines();
  std::atomic<int> bad{0};

  for (int wave = 0; wave < scaled(3); ++wave)
  {
    run_threads(48,
                [&](int id)
                {
                  const auto& matrix = crs_matrix();
                  const auto& name = matrix[(wave + id) % matrix.size()];
                  bad += check_crs(name, base.at(name));
                });
  }

  EXPECT_EQ(bad.load(), 0) << "thread churn corrupted results";
}

TEST(SpatialReferenceConcurrency, ObjectsOutliveTheirCreatorThread)
{
  // Clones and pooled transformations are routinely handed across threads and
  // destroyed long after their creator exited (GDAL reassigns a PROJ context in
  // the destructor for exactly this case). Create everything in worker threads,
  // use it on this thread, destroy half of it in fresh threads.
  constexpr int kThreads = 8;
  std::vector<std::shared_ptr<OGRSpatialReference>> objects(kThreads);
  std::vector<std::optional<Fmi::CoordinateTransformation>> transforms(kThreads);

  run_threads(kThreads,
              [&](int id)
              {
                objects[id] = Fmi::OGRSpatialReferenceFactory::Create("EPSG:3067");
                transforms[id].emplace("WGS84", "EPSG:3067");
              });
  // All creator threads are gone. Use their objects here.
  const auto& b = baselines().at("EPSG:3067");
  for (int i = 0; i < kThreads; i++)
  {
    ASSERT_TRUE(objects[i]);
    EXPECT_EQ(wkt_of(*objects[i]), b.wkt);
    double x = test_points()[0].first;
    double y = test_points()[0].second;
    ASSERT_TRUE(transforms[i]->transform(x, y));
    EXPECT_EQ(x, b.forward[0].first);
    EXPECT_EQ(y, b.forward[0].second);
  }

  // Destroy on threads that did not create them, concurrently.
  run_threads(kThreads,
              [&](int id)
              {
                objects[id].reset();
                transforms[id].reset();  // returns the pooled transformation from here
              });

  // The pool and the factory must still be fully functional.
  double x = test_points()[0].first;
  double y = test_points()[0].second;
  Fmi::CoordinateTransformation after("WGS84", "EPSG:3067");
  ASSERT_TRUE(after.transform(x, y));
  EXPECT_EQ(x, b.forward[0].first);
  EXPECT_EQ(y, b.forward[0].second);
}

TEST(SpatialReferenceConcurrency, GeometryWithAssignedCrsCrossesThreads)
{
  // The wms/contour/download pattern: a geometry takes a GDAL-level reference
  // to the CRS and travels between threads, outliving the creator's handle and
  // the creator thread itself.
  constexpr int kThreads = 8;
  std::vector<OGRPoint*> points(kThreads, nullptr);

  run_threads(kThreads,
              [&](int id)
              {
                auto* point = new OGRPoint(25.0, 60.0);
                auto srs = Fmi::OGRSpatialReferenceFactory::Create("EPSG:2393");
                point->assignSpatialReference(srs.get());
                points[id] = point;
              });  // creators exit; only the geometries' references remain

  for (auto* point : points)
  {
    ASSERT_NE(point->getSpatialReference(), nullptr);
    EXPECT_TRUE(point->getSpatialReference()->IsProjected());
  }

  run_threads(kThreads, [&](int id) { delete points[id]; });
}

TEST(SpatialReferenceConcurrency, ReleaseThreadSamplesWhileHoldingClones)
{
  // ReleaseThreadSamples() drops the thread's sample objects; clones already
  // handed out are the caller's property and must be entirely unaffected, and
  // the store must reseed transparently - including when other threads are
  // doing the same thing at the same time.
  const auto& base = baselines();
  std::atomic<int> bad{0};

  run_threads(8,
              [&](int id)
              {
                const auto& matrix = crs_matrix();
                for (int round = 0; round < scaled(20); ++round)
                {
                  const auto& name = matrix[(id + round) % matrix.size()];
                  auto before = Fmi::OGRSpatialReferenceFactory::Create(name);

                  Fmi::OGRSpatialReferenceFactory::ReleaseThreadSamples();

                  if (wkt_of(*before) != base.at(name).wkt)  // the held clone is untouched
                    ++bad;
                  auto after = Fmi::OGRSpatialReferenceFactory::Create(name);  // reseeds
                  if (wkt_of(*after) != base.at(name).wkt)
                    ++bad;
                  if (before.get() == after.get())
                    ++bad;
                }
              });

  EXPECT_EQ(bad.load(), 0) << "ReleaseThreadSamples disturbed live objects or reseeding";
}

// ===========================================================================
// 8. Error paths under contention
// ===========================================================================

TEST(SpatialReferenceConcurrency, InvalidDefinitionsThrowCleanlyUnderContention)
{
  // Failed parses under concurrency must throw for every caller, every time,
  // and leave no residue: no poisoned entries, no lost locks, no effect on the
  // valid work interleaved with them.
  const std::vector<std::string> invalid = {
      "EPSG:99999999", "utter nonsense", "+proj=nosuchprojection +type=crs"};
  const auto& base = baselines();
  std::atomic<int> missing_throws{0};
  std::atomic<int> bad{0};

  run_threads(12,
              [&](int id)
              {
                // GDAL reports the failures via CPLError; keep test output clean.
                CPLPushErrorHandler(CPLQuietErrorHandler);
                for (int round = 0; round < scaled(15); ++round)
                {
                  const auto& key = invalid[(id + round) % invalid.size()];
                  try
                  {
                    auto srs = Fmi::OGRSpatialReferenceFactory::Create(key);
                    ++missing_throws;
                  }
                  catch (...)  // NOLINT - any exception type is acceptable, silence is not
                  {
                  }
                  try
                  {
                    Fmi::SpatialReference sr(key);
                    ++missing_throws;
                  }
                  catch (...)
                  {
                  }
                  try
                  {
                    auto srs = Fmi::OGRSpatialReferenceFactory::Create(std::string());
                    ++missing_throws;
                  }
                  catch (...)
                  {
                  }
                  // Valid work interleaved with the failures stays exact.
                  const auto& name = crs_matrix()[(id + round) % crs_matrix().size()];
                  bad += check_crs(name, base.at(name));
                }
                CPLPopErrorHandler();
              });

  EXPECT_EQ(missing_throws.load(), 0) << "an invalid definition did not throw";
  EXPECT_EQ(bad.load(), 0) << "failing parses disturbed valid concurrent work";
}

TEST(SpatialReferenceConcurrency, FailuresLeaveNoResidue)
{
  // After the storm above: failures must not have been cached as successes,
  // and successes must not have been forgotten.
  CPLPushErrorHandler(CPLQuietErrorHandler);
  EXPECT_THROW((void)Fmi::OGRSpatialReferenceFactory::Create("EPSG:99999999"), std::exception);
  EXPECT_THROW(Fmi::SpatialReference("utter nonsense"), std::exception);
  CPLPopErrorHandler();

  const auto& b = baselines().at("EPSG:3067");
  Fmi::SpatialReference sr("EPSG:3067");
  EXPECT_EQ(sr.WKT(), b.wkt);
  EXPECT_EQ(sr.getEPSG(), b.epsg);
}
