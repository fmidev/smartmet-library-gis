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
