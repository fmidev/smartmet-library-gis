#include "OGRCoordinateTransformationFactory.h"
#include "TestDefs.h"

#include <macgyver/StaticCleanup.h>
#include <regression/tframe.h>

#include <ogr_spatialref.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <pthread.h>
#include <string>
#include <thread>
#include <vector>

using namespace std;

namespace Tests
{
void create()
{
  using Fmi::OGRCoordinateTransformationFactory::Create;

  auto trans1 = Create("WGS84", "EPSG:2393");

  auto* ptr1 = trans1.get();  // save the address
  trans1.reset();             // delete the transformation returning it to the factory

  auto trans2 = Create("WGS84", "EPSG:2393");  // get the same transformation again

  if (ptr1 != trans2.get())
    TEST_FAILED("Should get same transformation back after releasing it");

  TEST_PASSED();
}

// Regression test for the shared-OGRSpatialReference thread-safety crash.
//
// Create() hands out cached, shared OGRSpatialReference objects and then calls
// OGRCreateCoordinateTransformation() on them. Those objects are not thread-safe:
// GDAL lazily (re)builds their internal PROJ/WKT state on first use, so two
// threads triggering that first build concurrently corrupted the heap
// (production: SIGSEGV in refreshProjObj/exportToWkt/proj_create_from_wkt;
// locally: "pure virtual method called" in proj_as_wkt). This crashed the whole
// SmartMet server on invalid/rare projections under concurrent load.
//
// The test forces a COLD cache miss every round (fresh CRS) and makes many
// threads hit that CRS's first build simultaneously (barrier). Without the
// OGRSpatialReferenceFactory::mutex() serialisation this aborts the process in a
// fraction of a second; with it, it runs indefinitely. Duration is short by
// default; set GIS_THREADSAFETY_SECONDS to soak-test longer.
void concurrent_create_is_threadsafe()
{
  using Fmi::OGRCoordinateTransformationFactory::Create;

  const unsigned nthreads = std::max(8u, 2u * std::thread::hardware_concurrency());
  double seconds = 3.0;
  if (const char* s = std::getenv("GIS_THREADSAFETY_SECONDS"))
    seconds = std::atof(s);

  pthread_barrier_t startb;
  pthread_barrier_t endb;
  pthread_barrier_init(&startb, nullptr, nthreads + 1);
  pthread_barrier_init(&endb, nullptr, nthreads + 1);

  std::string crs;                 // current round's fresh CRS (published via startb)
  std::atomic<bool> stop{false};

  auto worker = [&]()
  {
    for (;;)
    {
      pthread_barrier_wait(&startb);
      if (stop.load(std::memory_order_acquire))
        return;
      try
      {
        auto t = Create(std::string("WGS84"), crs);
        (void)t;
      }
      catch (...)
      {
        // A bad/unsupported projection throws cleanly -- not the crash we guard.
      }
      pthread_barrier_wait(&endb);
    }
  };

  std::vector<std::thread> pool;
  pool.reserve(nthreads);
  for (unsigned i = 0; i < nthreads; ++i)
    pool.emplace_back(worker);

  const auto t0 = std::chrono::steady_clock::now();
  unsigned long round = 0;
  for (;;)
  {
    char buf[256];
    std::snprintf(buf, sizeof buf,
                  "+proj=stere +lat_0=90 +lon_0=%.5f +lat_ts=60 +ellps=WGS84 "
                  "+datum=WGS84 +units=m +no_defs",
                  15.0 + (double)(round % 100000) * 0.0007);
    crs = buf;                          // fresh CRS -> cold miss -> fresh race window
    pthread_barrier_wait(&startb);      // all workers hit the same cold CRS together
    pthread_barrier_wait(&endb);        // round complete
    ++round;
    if (std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() >= seconds)
      break;
  }

  stop.store(true, std::memory_order_release);
  pthread_barrier_wait(&startb);        // release workers with the stop flag set
  for (auto& th : pool)
    th.join();
  pthread_barrier_destroy(&startb);
  pthread_barrier_destroy(&endb);

  if (round == 0)
    TEST_FAILED("no rounds executed");

  TEST_PASSED();
}

// Test driver
class tests : public tframe::tests
{
  // Overridden message separator
  virtual const char* error_message_prefix() const { return "\n\t"; }
  // Main test suite
  void test()
  {
    TEST(create);
    TEST(concurrent_create_is_threadsafe);
  }

};  // class tests

}  // namespace Tests

int main(void)
{
  // Clear the SpatialReference/PROJ-backed caches before unordered static
  // destruction at exit (otherwise double-frees with some GDAL/PROJ versions).
  Fmi::StaticCleanup::AtExit cleanup;
  cout << endl << "OGRCoordinateTransformationFactory tester" << endl << "==========" << endl;
  Tests::tests t;
  return t.run();
}
