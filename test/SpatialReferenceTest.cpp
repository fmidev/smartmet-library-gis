#include "SpatialReference.h"
#include "TestDefs.h"

#include <macgyver/StaticCleanup.h>
#include <regression/tframe.h>
#include <atomic>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

using namespace std;

namespace Tests
{
void getepsg()
{
  {
    Fmi::SpatialReference crs("EPSG:3857");
    if (crs.getEPSG() != 3857)
      TEST_FAILED("Failed to get 3857 for 'EPSG:3857'");
  }
  {
    Fmi::SpatialReference crs("EPSG:4326");
    if (crs.getEPSG() != 4326)
      TEST_FAILED("Failed to get 4326 for 'EPSG:4326'");
  }
  {
    Fmi::SpatialReference crs("WGS84");
    if (crs.getEPSG() != 4326)
      TEST_FAILED("Failed to get 4326 for 'WGS84'");
  }
  {
    Fmi::SpatialReference crs(
        "+proj=stere +lat_0=60 +lon_0=20 +lat_ts=60.000000 +lon_ts=20.000000 +k=1.000000 "
        "+x_0=3500000 +y_0=0 +ellps=WGS84 +datum=WGS84 +units=m no_defs");
    auto ret = crs.getEPSG();
    if (crs.getEPSG())
      TEST_FAILED("Should not get EPSG for FMI polar stereographic CRS, got " +
                  std::to_string(*ret));
  }

  TEST_PASSED();
}

void getepsg_parallel()
{
  const std::size_t num_threads = 10;
  const std::size_t num_tests = 16384;

  std::atomic<int> num_errors(0);
  std::atomic<int> num_runs(0);

  const auto on_error = [&num_errors]() { num_errors++; };

  const auto thread_proc = [&on_error, &num_runs, num_tests]() -> void
  {
    for (std::size_t i = 0; i < num_tests; i++)
    {
      {
        Fmi::SpatialReference crs("EPSG:3857");
        if (crs.getEPSG() != 3857)
          on_error();
      }

      {
        Fmi::SpatialReference crs("EPSG:4326");
        if (crs.getEPSG() != 4326)
          on_error();
      }
      {
        Fmi::SpatialReference crs("WGS84");
        if (crs.getEPSG() != 4326)
          on_error();
      }
      {
        Fmi::SpatialReference crs(
            "+proj=stere +lat_0=60 +lon_0=20 +lat_ts=60.000000 +lon_ts=20.000000 +k=1.000000 "
            "+x_0=3500000 +y_0=0 +ellps=WGS84 +datum=WGS84 +units=m no_defs");
        if (crs.getEPSG())
          on_error();
      }
      num_runs++;
    }
  };

  std::vector<std::shared_ptr<std::thread> > test_threads;
  for (std::size_t i = 0; i < num_threads; i++)
  {
    test_threads.emplace_back(new std::thread(thread_proc),
                              [](std::thread* t)
                              {
                                t->join();
                                delete t;
                              });
  }

  test_threads.clear();

  if (num_errors > 0)
    TEST_FAILED("Unexpected errors when running parallel tests");

  if (num_runs != num_threads * num_tests)
    TEST_FAILED("Expected " + std::to_string(num_threads * num_tests) + " runs, but got " +
                std::to_string(num_runs));

  TEST_PASSED();
}

// The test above reuses four CRS definitions, so after the first iteration every
// lookup is a warm cache hit. The interesting path is the cold miss, where the
// properties are derived from the OGRSpatialReference shared by all threads with
// GDAL calls that are not safe to run on it concurrently: GetRoot() builds the
// node tree on demand, and EPSGTreatsAsLatLong() demotes and restores the
// underlying PJ* on every call for a BoundCRS. Rendezvous all threads on each
// definition in turn so their cold misses actually collide.
void getepsg_parallel_cold()
{
  struct Case
  {
    std::string desc;
    std::optional<int> epsg;
  };

  std::vector<Case> cases;

  // WGS 84 / UTM zone NN N, all with an EPSG authority node
  for (int zone = 1; zone <= 60; zone++)
    cases.push_back({"EPSG:" + std::to_string(32600 + zone), 32600 + zone});

  // Named datums from OGRSpatialReferenceFactory's known_datums table. These
  // expand to +towgs84 definitions, which PROJ represents as a BoundCRS, and
  // they carry no EPSG authority. (The +nadgrids entries are left out: they
  // need grid files that need not be installed.)
  for (const auto* datum : {"FMI", "GGRS87", "NAD83", "carthage", "ire65", "nzgd49", "OSGB36"})
    cases.push_back({datum, std::nullopt});

  const std::size_t num_threads = 10;

  std::atomic<int> num_errors(0);
  std::atomic<std::size_t> arrived(0);

  const auto thread_proc = [&]() -> void
  {
    for (std::size_t i = 0; i < cases.size(); i++)
    {
      // Wait until every thread is about to construct the same definition
      const std::size_t target = num_threads * (i + 1);
      arrived++;
      while (arrived < target)
        std::this_thread::yield();

      try
      {
        Fmi::SpatialReference crs(cases[i].desc);
        if (crs.getEPSG() != cases[i].epsg)
          num_errors++;
        if (crs.WKT().empty())
          num_errors++;
      }
      catch (...)
      {
        num_errors++;
      }
    }
  };

  std::vector<std::shared_ptr<std::thread> > test_threads;
  for (std::size_t i = 0; i < num_threads; i++)
  {
    test_threads.emplace_back(new std::thread(thread_proc),
                              [](std::thread* t)
                              {
                                t->join();
                                delete t;
                              });
  }

  test_threads.clear();

  if (num_errors > 0)
    TEST_FAILED("Got " + std::to_string(num_errors.load()) +
                " errors when deriving CRS properties concurrently");

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
    TEST(getepsg);
    TEST(getepsg_parallel);
    TEST(getepsg_parallel_cold);
  }

};  // class tests

}  // namespace Tests

int main(void)
{
  // Clear the SpatialReference cache (holding OGRSpatialReference objects backed
  // by GDAL/PROJ global state) before unordered static destruction at exit,
  // which otherwise double-frees with some GDAL/PROJ versions.
  Fmi::StaticCleanup::AtExit cleanup;

  cout << endl
       << "SpatialReference tester\n"
          "=======================\n";
  Tests::tests t;
  return t.run();
}
