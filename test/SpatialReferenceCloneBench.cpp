// Cost of the alternatives for handing a caller a private OGRSpatialReference.
//
//   ./SpatialReferenceCloneBench [seconds] [maxthreads]
//
// The question: is a check-out pool worth its machinery, or is cloning a sample
// object per call cheap enough that ownership can simply be given away?
//
// Cloning has one problem the pool does not: Clone() reads (and lazily rebuilds)
// the object it copies, so cloning from ONE shared sample needs a lock on every
// call. The thread_local variant below is the way out - each thread clones from
// its own sample and needs no lock at all.

#include "OGRSpatialReferenceFactory.h"
#include "ProjInfo.h"
#include "SpatialReference.h"

#include <ogr_spatialref.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace
{
const std::vector<std::string> g_crs = {
    "EPSG:4326", "EPSG:2393", "EPSG:3067", "EPSG:3857", "EPSG:3035", "EPSG:4258"};

std::atomic<bool> g_stop{false};
std::atomic<long> g_ops{0};

// One process-wide sample, as the "clone from a shared sample" design would have.
std::vector<OGRSpatialReference *> g_samples;
std::mutex g_sample_mutex;

void build_samples()
{
  for (const auto &crs : g_crs)
  {
    auto *srs = new OGRSpatialReference;
    srs->SetFromUserInput(crs.c_str());
    srs->SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
    // Materialise the lazy state once so we measure steady state.
    char *wkt = nullptr;
    srs->exportToWkt(&wkt);
    CPLFree(wkt);
    srs->IsProjected();
    g_samples.push_back(srs);
  }
}

// Per-thread samples: each thread clones from its own copy, so no lock.
OGRSpatialReference *thread_sample(size_t i)
{
  thread_local std::vector<std::shared_ptr<OGRSpatialReference>> mine;
  if (mine.empty())
  {
    for (auto *sample : g_samples)
    {
      std::lock_guard<std::mutex> lock(g_sample_mutex);  // once per thread only
      mine.emplace_back(sample->Clone(), [](OGRSpatialReference *p) { p->Release(); });
    }
  }
  return mine[i].get();
}

using Body = void (*)(size_t);

// (a) what the library does now: clone this thread's sample, caller owns it
void body_pool(size_t i)
{
  auto srs = Fmi::OGRSpatialReferenceFactory::Create(g_crs[i]);
  (void)srs->IsProjected();
}

// (b) clone from one shared sample, under the lock Clone() would require
void body_clone_shared(size_t i)
{
  OGRSpatialReference *copy = nullptr;
  {
    std::lock_guard<std::mutex> lock(g_sample_mutex);
    copy = g_samples[i]->Clone();
  }
  (void)copy->IsProjected();
  copy->Release();
}

// (c) clone from a per-thread sample, no lock
void body_clone_threadlocal(size_t i)
{
  auto *copy = thread_sample(i)->Clone();
  (void)copy->IsProjected();
  copy->Release();
}

// (d) full Fmi::SpatialReference from a definition string (derived values cached)
void body_spatialref_string(size_t i)
{
  Fmi::SpatialReference sr(g_crs[i]);
  (void)sr.isGeographic();
}

// (e) full Fmi::SpatialReference from an OGRSpatialReference: re-derives WKT,
//     PROJ string, EPSG and the flags. This is the cost the user's "clone the
//     Fmi::SpatialReference instead" idea is aimed at.
void body_spatialref_derive(size_t i)
{
  Fmi::SpatialReference sr(*g_samples[i]);
  (void)sr.isGeographic();
}

double run(Body body, int nthreads, int seconds)
{
  g_stop = false;
  g_ops = 0;
  auto t0 = std::chrono::steady_clock::now();

  std::vector<std::thread> threads;
  for (int t = 0; t < nthreads; t++)
    threads.emplace_back(
        [body, t]()
        {
          size_t i = t;
          long local = 0;
          while (!g_stop)
          {
            body(i++ % g_crs.size());
            ++local;
          }
          g_ops += local;
        });

  std::this_thread::sleep_for(std::chrono::seconds(seconds));
  g_stop = true;
  for (auto &th : threads)
    th.join();

  auto dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  return g_ops / dt;
}

struct Case
{
  const char *name;
  Body body;
};

}  // namespace

int main(int argc, char **argv)
{
  const int seconds = (argc > 1) ? atoi(argv[1]) : 2;
  const int maxthreads = (argc > 2) ? atoi(argv[2]) : 16;

  build_samples();
  for (const auto &crs : g_crs)  // warm every cache
    body_spatialref_string(&crs - g_crs.data());

  const std::vector<Case> cases = {
      {"factory Create() [new design]", body_pool},
      {"Clone, shared sample + lock", body_clone_shared},
      {"Clone, thread_local sample", body_clone_threadlocal},
      {"Fmi::SpatialReference(string)", body_spatialref_string},
      {"Fmi::SpatialReference(OGRSRS)", body_spatialref_derive},
  };

  printf("%-32s", "acquisitions/s");
  for (int n = 1; n <= maxthreads; n *= 2)
    printf("%12d", n);
  printf("\n");
  printf("%-32s", "");
  for (int n = 1; n <= maxthreads; n *= 2)
    printf("%12s", "threads");
  printf("\n");

  for (const auto &c : cases)
  {
    printf("%-32s", c.name);
    fflush(stdout);
    for (int n = 1; n <= maxthreads; n *= 2)
    {
      printf("%12.0f", run(c.body, n, seconds));
      fflush(stdout);
    }
    printf("\n");
  }

  return 0;
}
