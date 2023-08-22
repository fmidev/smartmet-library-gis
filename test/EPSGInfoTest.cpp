#include "EPSGInfo.h"
#include "TestDefs.h"
#include <regression/tframe.h>

using namespace std;

namespace Tests
{
void isvalid()
{
  if (!Fmi::EPSGInfo::isValid(4326))
    TEST_FAILED("EPSG 4326 should be valid");
  if (Fmi::EPSGInfo::isValid(123456))
    TEST_FAILED("EPSG 123456 should be invalid");

  TEST_PASSED();
}

void getepsg()
{
  auto info = Fmi::EPSGInfo::getEPSG(4326);
  if (info.number != 4326)
    TEST_FAILED("Failed to get EPSG info for 4326");

  info = Fmi::EPSGInfo::getEPSG(3857);
  if (info.number != 3857)
    TEST_FAILED("Failed to get EPSG info for 3857");

  TEST_PASSED();
}

void getbbox()
{
  // WGS84
  auto box = Fmi::EPSGInfo::getBBox(4326);
  if (box.west != -180)
    TEST_FAILED("Should get bbox.west=-180 for EPSG 4326");
  if (box.east != 180)
    TEST_FAILED("Should get bbox.east=180 for EPSG 4326");
  if (box.north != 90)
    TEST_FAILED("Should get bbox.north=90 for EPSG 4326");
  if (box.south != -90)
    TEST_FAILED("Should get bbox.south=-90 for EPSG 4326");

  // WebMercator
  box = Fmi::EPSGInfo::getBBox(3857);
  if (box.west != -180)
    TEST_FAILED("Should get bbox.west=-180 for EPSG 4326");
  if (box.east != 180)
    TEST_FAILED("Should get bbox.east=180 for EPSG 4326");
  if (box.north != 85.06)
    TEST_FAILED("Should get bbox.north=85.06 for EPSG 4326");
  if (box.south != -85.06)
    TEST_FAILED("Should get bbox.south=-85.06 for EPSG 4326");

  TEST_PASSED();
}

void getprojectedbounds()
{
  // WGS84
  auto box = Fmi::EPSGInfo::getProjectedBounds(4326);
  if (box.west != -180)
    TEST_FAILED("Should get bbox.west=-180 for EPSG 4326");
  if (box.east != 180)
    TEST_FAILED("Should get bbox.east=180 for EPSG 4326");
  if (box.north != 90)
    TEST_FAILED("Should get bbox.north=90 for EPSG 4326");
  if (box.south != -90)
    TEST_FAILED("Should get bbox.south=-90 for EPSG 4326");

  // WebMercator
  using std::to_string;
  box = Fmi::EPSGInfo::getProjectedBounds(3857);
  if (std::abs(box.west + 20037508.342789) > 1)
    TEST_FAILED("Should get bbox.west ~ -20037508 for EPSG 3857, not " + to_string(box.west));
  if (std::abs(box.east - 20037508.342789) > 1)
    TEST_FAILED("Should get bbox.east ~ +20037508 for EPSG 3857, not " + to_string(box.east));
  if (std::abs(box.north - 20048966.104015) > 1)
    TEST_FAILED("Should get bbox.north ~ +20048966 for EPSG 3857, not " + to_string(box.north));
  if (std::abs(box.south + 20048966.104015) > 1)
    TEST_FAILED("Should get bbox.south ~ -20048966 for EPSG 3857, not " + to_string(box.south));

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
    TEST(isvalid);
    TEST(getepsg);
    TEST(getbbox);
    TEST(getprojectedbounds);
  }

};  // class tests

}  // namespace Tests

int main(void)
{
  cout << endl
       << "EPSGInfo tester\n"
          "==============="
       << endl;
  Tests::tests t;
  return t.run();
}
