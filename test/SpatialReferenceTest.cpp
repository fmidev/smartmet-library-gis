#include "SpatialReference.h"
#include "TestDefs.h"

#include <regression/tframe.h>
#include <atomic>
#include <memory>
#include <thread>

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

    const auto on_error =
        [&num_errors] ()
        {
            num_errors++;
        };

    const auto thread_proc =
        [&on_error, &num_runs, num_tests] () -> void
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
        test_threads.emplace_back(
            new std::thread(thread_proc),
            [](std::thread* t) { t->join(); delete t; });
    }

    test_threads.clear();

    if (num_errors > 0)
        TEST_FAILED("Unexpected errors when running parallel tests");

    if (num_runs != num_threads * num_tests)
        TEST_FAILED("Expected " + std::to_string(num_threads * num_tests) + " runs, but got " + std::to_string(num_runs));

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
  }

};  // class tests

}  // namespace Tests

int main(void)
{
  cout << endl
       << "SpatialReference tester\n"
          "=======================\n";
  Tests::tests t;
  return t.run();
}
