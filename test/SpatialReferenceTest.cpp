#include "SpatialReference.h"
#include "TestDefs.h"

#include <regression/tframe.h>

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

// Test driver
class tests : public tframe::tests
{
  // Overridden message separator
  virtual const char* error_message_prefix() const { return "\n\t"; }
  // Main test suite
  void test() { TEST(getepsg); }

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
