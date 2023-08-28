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

void wgs84()
{
  const auto info = Fmi::EPSGInfo::getInfo(4326);
  if (!info)
    TEST_FAILED("Failed to get EPSG info for 4326 (WGS84)");

  if (info->number != 4326)
    TEST_FAILED("Failed to get EPSG number for 4326 (WGS84)");

  const auto& box = info->bbox;
  if (box.west != -180)
    TEST_FAILED("Should get bbox.west=-180 for EPSG 4326 (WGS84)");
  if (box.east != 180)
    TEST_FAILED("Should get bbox.east=180 for EPSG 4326 (WGS84)");
  if (box.north != 90)
    TEST_FAILED("Should get bbox.north=90 for EPSG 4326 (WGS84)");
  if (box.south != -90)
    TEST_FAILED("Should get bbox.south=-90 for EPSG 4326 (WGS84)");

  const auto& bounds = info->bounds;
  if (bounds.west != -180)
    TEST_FAILED("Should get bounds.west=-180 for EPSG 4326 (WGS84)");
  if (bounds.east != 180)
    TEST_FAILED("Should get bounds.east=180 for EPSG 4326 (WGS84)");
  if (bounds.north != 90)
    TEST_FAILED("Should get bounds.north=90 for EPSG 4326 (WGS84)");
  if (bounds.south != -90)
    TEST_FAILED("Should get bounds.south=-90 for EPSG 4326 (WGS84)");

  TEST_PASSED();
}

void webmercator()
{
  const auto info = Fmi::EPSGInfo::getInfo(3857);
  if (!info)
    TEST_FAILED("Failed to get EPSG info for 3857 (WebMercator)");

  if (info->number != 3857)
    TEST_FAILED("Failed to get EPSG number info for 3857 (WebMercator)");

  const auto& box = info->bbox;
  if (box.west != -180)
    TEST_FAILED("Should get bbox.west=-180 for EPSG 3857 (WebMercator)");
  if (box.east != 180)
    TEST_FAILED("Should get bbox.east=180 for EPSG 3857 (WebMercator)");
  if (box.north != 85.06)
    TEST_FAILED("Should get bbox.north=85.06 for EPSG 3857 (WebMercator)");
  if (box.south != -85.06)
    TEST_FAILED("Should get bbox.south=-85.06 for EPSG 3857 (WebMercator)");

  using std::to_string;
  const auto& bounds = info->bounds;
  if (std::abs(bounds.west + 20037508.342789) > 1)
    TEST_FAILED("Should get bounds.west ~ -20037508 for EPSG 3857 (WebMercator), not " +
                to_string(bounds.west));
  if (std::abs(bounds.east - 20037508.342789) > 1)
    TEST_FAILED("Should get bounds.east ~ +20037508 for EPSG 3857 (WebMercator), not " +
                to_string(bounds.east));
  if (std::abs(bounds.north - 20048966.104015) > 1)
    TEST_FAILED("Should get bounds.north ~ +20048966 for EPSG 3857 (WebMercator), not " +
                to_string(bounds.north));
  if (std::abs(bounds.south + 20048966.104015) > 1)
    TEST_FAILED("Should get bounds.south ~ -20048966 for EPSG 3857 (WebMercator), not " +
                to_string(bounds.south));
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
    TEST(wgs84);
    TEST(webmercator);
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
