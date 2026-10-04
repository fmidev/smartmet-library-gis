// Stress / demonstration driver for spatial-reference thread safety.
//
// This is NOT named *Test, so "make test" builds but does not run it. It is a
// measurement and demonstration tool, driven by hand and under valgrind:
//
//   ./SpatialReferenceStress exclusivity 8 3
//   ./SpatialReferenceStress scale       8 3
//   ./SpatialReferenceStress race        4 3
//   valgrind --tool=helgrind ./SpatialReferenceStress race 4 1
//
// Background: PROJ's documented contract is that a PJ object is used by one
// thread at a time (docs/source/development/reference/datatypes.rst), and
// GDAL's OGRSpatialReference lazily rebuilds internal PROJ/WKT state even from
// logically-const methods. See ../docs/proj-gdal-thread-safety.md.
//
// The "exclusivity" mode measures the property that actually matters and can be
// asserted deterministically: whether two threads ever hold the same
// OGRSpatialReference at the same time. A shared cache guarantees they always
// do; giving each caller its own clone guarantees they never do.

#include "OGR.h"
#include "OGRSpatialReferenceFactory.h"
#include "SpatialReference.h"
#include "CoordinateTransformation.h"
#include "ProjInfo.h"

#include <ogr_spatialref.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace
{
// A handful of CRSes, as a server sees: a few hot ones and a tail.
const std::vector<std::string> g_crs = {
    "EPSG:4326", "EPSG:2393", "EPSG:3067", "EPSG:3857", "EPSG:3035", "EPSG:4258"};

std::atomic<bool> g_stop{false};
std::atomic<long> g_iter{0};
std::atomic<long> g_wrong{0};

// ---------------------------------------------------------------------------
// Exclusivity tracker: records which raw OGRSpatialReference pointers are in
// use right now, and by how many threads at once.
// ---------------------------------------------------------------------------
class ExclusivityTracker
{
 public:
  void acquire(const void *p)
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto n = ++m_inuse[p];
    if (n > 1)
    {
      ++m_overlaps;
      if (n > m_maxconcurrent)
        m_maxconcurrent = n;
    }
    ++m_acquisitions;
    m_distinct.insert(p);
  }

  void release(const void *p)
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (--m_inuse[p] == 0)
      m_inuse.erase(p);
  }

  void report() const
  {
    printf("acquisitions          : %ld\n", m_acquisitions);
    printf("overlapping acquires  : %ld   <-- two threads on one object\n", m_overlaps);
    printf("max concurrent on one : %ld\n", m_maxconcurrent);
    printf("distinct objects seen : %zu\n", m_distinct.size());
  }

  long overlaps() const { return m_overlaps; }

 private:
  mutable std::mutex m_mutex;
  std::map<const void *, long> m_inuse;
  std::set<const void *> m_distinct;
  long m_acquisitions = 0;
  long m_overlaps = 0;
  long m_maxconcurrent = 0;
};

ExclusivityTracker g_tracker;

// ---------------------------------------------------------------------------
// The realistic unit of work. Mirrors the production call sites:
//   newbase/NFmiGdalArea.cpp:204-219  Create(wkt) then use the shared object
//   gis/PostGIS.cpp:311               Create(...)->Clone()
//   plugins/download/DataStreamer.cpp:2384  copy out of the shared object
// ---------------------------------------------------------------------------
void use_own_object(const std::string &desc, bool track)
{
  auto srs = Fmi::OGRSpatialReferenceFactory::Create(desc);

  if (track)
    g_tracker.acquire(srs.get());

  // What NFmiGdalArea does: build a Fmi::SpatialReference from a dereferenced
  // OGRSpatialReference. That re-derives everything - Clone() + exportToWkt() +
  // exportToProj() + GetRoot() - and is the expensive path measured at ~1.7 ms
  // by SpatialReferenceCloneBench.
  Fmi::SpatialReference sr(*srs);
  if (sr.WKT().empty())
    ++g_wrong;

  if (track)
    g_tracker.release(srs.get());
}

// ---------------------------------------------------------------------------
// Transform verification: the production symptom was a silently degenerate
// transformation, so check values, not just liveness.
// ---------------------------------------------------------------------------
struct Expected
{
  double x;
  double y;
};
std::map<std::string, Expected> g_expected;

void compute_expected()
{
  for (const auto &crs : g_crs)
  {
    double x = 25.0;
    double y = 60.0;
    Fmi::CoordinateTransformation t("WGS84", crs);
    if (!t.transform(x, y))
    {
      printf("FATAL: baseline transform WGS84 -> %s failed\n", crs.c_str());
      exit(2);
    }
    g_expected[crs] = {x, y};
  }
}

void verify_transform(const std::string &crs)
{
  double x = 25.0;
  double y = 60.0;
  Fmi::CoordinateTransformation t("WGS84", crs);
  if (!t.transform(x, y))
  {
    ++g_wrong;
    return;
  }
  const auto &e = g_expected[crs];
  // Bit-exact: the same transformation of the same point must not drift.
  if (x != e.x || y != e.y)
  {
    ++g_wrong;
    static std::once_flag once;
    std::call_once(once, [&]() {
      printf("  WRONG RESULT %s: got (%.6f, %.6f) expected (%.6f, %.6f)\n",
             crs.c_str(), x, y, e.x, e.y);
    });
  }
}

void worker(int id, bool track, bool transforms)
{
  size_t i = id;
  while (!g_stop)
  {
    const auto &crs = g_crs[i++ % g_crs.size()];
    use_own_object(crs, track);
    if (transforms)
      verify_transform(crs);
    ++g_iter;
  }
}

double run(int nthreads, int seconds, bool track, bool transforms)
{
  g_stop = false;
  g_iter = 0;
  auto t0 = std::chrono::steady_clock::now();

  std::vector<std::thread> t;
  for (int i = 0; i < nthreads; i++)
    t.emplace_back(worker, i, track, transforms);

  std::this_thread::sleep_for(std::chrono::seconds(seconds));
  g_stop = true;
  for (auto &th : t)
    th.join();

  auto dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  return g_iter / dt;
}

}  // namespace

int main(int argc, char **argv)
{
  const std::string mode = (argc > 1) ? argv[1] : "exclusivity";
  const int nthreads = (argc > 2) ? atoi(argv[2]) : 8;
  const int seconds = (argc > 3) ? atoi(argv[3]) : 3;

  // Warm every cache single-threaded first, so we measure steady state and not
  // the cold-miss parse path (which is already serialised by design).
  compute_expected();
  for (const auto &crs : g_crs)
    use_own_object(crs, false);

  printf("mode=%s threads=%d seconds=%d\n\n", mode.c_str(), nthreads, seconds);

  if (mode == "exclusivity")
  {
    run(nthreads, seconds, true, false);
    g_tracker.report();
    printf("\nwrong results         : %ld\n", g_wrong.load());
    // Non-zero overlap is the contract violation we are demonstrating.
    printf("\n%s\n", g_tracker.overlaps() > 0
                         ? "RESULT: EXCLUSIVITY VIOLATED (shared object handed to concurrent threads)"
                         : "RESULT: exclusivity held (no object used by two threads at once)");
    return 0;
  }

  if (mode == "poolsize")
  {
    // How much does keeping several private copies of each CRS actually cost?
    const int crses = (argc > 2) ? atoi(argv[2]) : 200;
    const int copies = (argc > 3) ? atoi(argv[3]) : 24;

    Fmi::OGRSpatialReferenceFactory::SetSampleStoreSize(static_cast<size_t>(crses));

    // A realistic spread of EPSG codes, each checked out "copies" times at once.
    std::vector<std::shared_ptr<OGRSpatialReference>> held;
    int built = 0;
    for (int code = 2000; code < 2000 + crses * 4 && built < crses; ++code)
    {
      try
      {
        std::vector<std::shared_ptr<OGRSpatialReference>> batch;
        for (int c = 0; c < copies; ++c)
          batch.push_back(Fmi::OGRSpatialReferenceFactory::Create(code));
        for (auto &p : batch)
          held.push_back(p);
        ++built;
      }
      catch (...)
      {
        continue;  // not every code in the range is a valid CRS
      }
    }
    printf("distinct CRSes        : %d\n", built);
    printf("copies per CRS        : %d\n", copies);
    printf("objects checked out   : %zu\n", held.size());
    held.clear();  // all destroyed: nothing is retained
    printf("per-thread samples    : %zu max\n",
           Fmi::OGRSpatialReferenceFactory::getSampleStoreSize());
    return 0;
  }

  if (mode == "scale")
  {
    printf("%8s %14s\n", "threads", "ops/s");
    for (int n : {1, 2, 4, 8, 16})
    {
      if (n > nthreads)
        break;
      auto rate = run(n, seconds, false, false);
      printf("%8d %14.0f\n", n, rate);
    }
    printf("\nwrong results         : %ld\n", g_wrong.load());
    return 0;
  }

  if (mode == "poison")
  {
    // Deterministic demonstration of the shared-object failure mode, and the
    // one that matches the production incident: because Create() hands out a
    // mutable pointer to a process-wide cached object, a single mutation by any
    // one caller silently changes the result of every later transformation in
    // the process that uses that CRS. No concurrency needed.
    //
    // Note: CoordinateTransformation builds via
    //   OGRCoordinateTransformationFactory::Create(source.projInfo().projStr(), ...)
    // so the object the transform path shares is the one cached under the PROJ
    // string, not under "EPSG:4326". Poison that one.
    const std::string probe = "EPSG:32635";  // cold: not warmed above

    double x1 = 25.0;
    double y1 = 60.0;
    Fmi::CoordinateTransformation before("WGS84", probe);
    before.transform(x1, y1);
    printf("before poisoning : WGS84 (25,60) -> %s = (%.1f, %.1f)\n", probe.c_str(), x1, y1);

    // Nothing in the API prevents this: Create() returns a mutable pointer to a
    // process-wide cached object. One caller reusing "its" object rewrites the
    // source CRS of every transformation the server builds afterwards.
    const std::string wgs84_projstr = Fmi::SpatialReference("WGS84").projInfo().projStr();
    Fmi::OGRSpatialReferenceFactory::Create(wgs84_projstr)->SetFromUserInput("EPSG:3067");
    printf("\n  ... one caller called SetFromUserInput() on the shared WGS84 object\n");
    printf("      (cache key \"%s\")\n\n", wgs84_projstr.c_str());

    double x2 = 25.0;
    double y2 = 60.0;
    Fmi::CoordinateTransformation after("WGS84", "EPSG:3395");  // also cold
    after.transform(x2, y2);
    printf("after poisoning  : WGS84 (25,60) -> EPSG:3395 = (%.1f, %.1f)\n", x2, y2);

    // Sanity reference computed from a private, unpoisoned object.
    OGRSpatialReference priv;
    priv.SetFromUserInput("EPSG:4326");
    priv.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
    OGRSpatialReference tgt;
    tgt.SetFromUserInput("EPSG:3395");
    tgt.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
    double x3 = 25.0;
    double y3 = 60.0;
    auto *ct = OGRCreateCoordinateTransformation(&priv, &tgt);
    ct->Transform(1, &x3, &y3);
    delete ct;
    printf("private object   : WGS84 (25,60) -> EPSG:3395 = (%.1f, %.1f)  <-- correct\n", x3, y3);

    const bool poisoned = (x2 != x3 || y2 != y3);
    printf("\n%s\n",
           poisoned ? "RESULT: POISONED - one caller's mutation corrupted the whole process"
                    : "RESULT: not poisoned - callers cannot affect each other");
    return poisoned ? 1 : 0;
  }

  if (mode == "race")
  {
    auto rate = run(nthreads, seconds, false, true);
    printf("iterations            : %ld\n", g_iter.load());
    printf("ops/s                 : %.0f\n", rate);
    printf("wrong results         : %ld\n", g_wrong.load());
    return g_wrong ? 1 : 0;
  }

  fprintf(stderr, "unknown mode '%s'\n", mode.c_str());
  return 2;
}
