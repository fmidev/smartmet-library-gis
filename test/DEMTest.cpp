#include "DEM.h"
#include "TestDefs.h"
#include <regression/tframe.h>
using namespace std;

std::string tostr(double value)
{
  std::ostringstream out;
  out << std::fixed << std::setprecision(1) << value;
  return out.str();
}

namespace Tests
{
// ----------------------------------------------------------------------

void elevation()
{
  Fmi::DEM dem(GIS_VIEWFINDER);

  std::string expected, value;

  // At sea
  expected = "0.0";
  value = tostr(dem.elevation(25, 59.5));
  if (value != expected)
    TEST_FAILED("Expected elevation " + expected + " at coordinate 25,59.5, not " + value);

    // Kumpula
#if GIS_SMALLTESTDATA == 1
  expected = "24.0";
#else
  expected = "11.0";
#endif
  value = tostr(dem.elevation(24.9642, 60.2089));
  if (value != expected)
    TEST_FAILED("Expected elevation " + expected + " at Kumpula, not " + value);

    // Helsinki
#if GIS_SMALLTESTDATA == 1
  expected = "34.0";
#else
  expected = "10.0";
#endif
  value = tostr(dem.elevation(24.93545, 60.16952));
  if (value != expected)
    TEST_FAILED("Expected elevation " + expected + " at Helsinki, not " + value);

    // Ruka
#if GIS_SMALLTESTDATA == 1
  expected = "0.0";
#else
  expected = "449.0";
#endif
  value = tostr(dem.elevation(29.1507, 66.1677));
  if (value != expected) TEST_FAILED("Expected elevation " + expected + " at Ruka, not " + value);

    // Matterhorn
#if GIS_SMALLTESTDATA == 0
  expected = "4458.0";
#endif
  value = tostr(dem.elevation(7.6583, 45.9764));
  if (value != expected)
    TEST_FAILED("Expected elevation " + expected + " at Matterhorn, not " + value);

    // South pole
#if GIS_SMALLTESTDATA == 0
  expected = "2771.0";
#endif
  value = tostr(dem.elevation(0, -90));
  if (value != expected)
    TEST_FAILED("Expected elevation " + expected + " at South Pole, not " + value);

  // North pole
  expected = "0.0";
  value = tostr(dem.elevation(0, 90));
  if (value != expected)
    TEST_FAILED("Expected elevation " + expected + " at North Pole, not " + value);

  // More special points

  expected = "0.0";
  value = tostr(dem.elevation(0, 0));
  if (value != expected) TEST_FAILED("Expected elevation " + expected + " at 0,0, not " + value);
  value = tostr(dem.elevation(-180, 0));
  if (value != expected) TEST_FAILED("Expected elevation " + expected + " at -180,0, not " + value);
  value = tostr(dem.elevation(180, 0));
  if (value != expected) TEST_FAILED("Expected elevation " + expected + " at 180,0, not " + value);
  value = tostr(dem.elevation(180, 0));
  if (value != expected) TEST_FAILED("Expected elevation " + expected + " at 180,0, not " + value);

    // Alanya had rounding issue with coordinates
#if GIS_SMALLTESTDATA == 0
  expected = "11.0";
#endif
  value = tostr(dem.elevation(31.99982, 36.543750000000003));
  if (value != expected) TEST_FAILED("Expected elevation " + expected + " at Alanya, not " + value);

    // Chukotskiy
#if GIS_SMALLTESTDATA == 0
  expected = "432.0";
#else
  expected = "0.0";
#endif
  value = tostr(dem.elevation(179.999, 67));
  if (value != expected)
    TEST_FAILED("Expected elevation " + expected + " at 179.999,67, not " + value);

#if GIS_SMALLTESTDATA == 0
  expected = "435.0";
#endif
  value = tostr(dem.elevation(-179.999, 67));
  if (value != expected)
    TEST_FAILED("Expected elevation " + expected + " at -179.999,67, not " + value);

#if GIS_SMALLTESTDATA == 0
  expected = "434.0";
#endif
  value = tostr(dem.elevation(-180, 67));
  if (value != expected)
    TEST_FAILED("Expected elevation " + expected + " at -180,67, not " + value);

#if GIS_SMALLTESTDATA == 0
  expected = "434.0";
#endif
  value = tostr(dem.elevation(180, 67));
  if (value != expected) TEST_FAILED("Expected elevation " + expected + " at 180,67, not " + value);

  TEST_PASSED();
}

// ----------------------------------------------------------------------

void resolution()
{
  Fmi::DEM dem(GIS_VIEWFINDER);

  std::string expected, value;

  double resolution = 0.1;  // 100 meters

  // At sea
  expected = "0.0";
  value = tostr(dem.elevation(25, 59.5, resolution));
  if (value != expected)
    TEST_FAILED("Expected elevation " + expected + " at coordinate 25,59.5, not " + value);

    // Kumpula at 100 meters
#if GIS_SMALLTESTDATA == 0
  expected = "17.0";
#else
  expected = "24.0";
#endif
  value = tostr(dem.elevation(24.9642, 60.2089, resolution));
  if (value != expected)
    TEST_FAILED("Expected elevation " + expected + " at Kumpula with 100 m resolution, not " +
                value);

  // At sea
  resolution = 0.5;
  expected = "0.0";
  value = tostr(dem.elevation(25, 59.5, resolution));
  if (value != expected)
    TEST_FAILED("Expected elevation " + expected + " at coordinate 25,59.5, not " + value);

  // Kumpula at 500 meters
  expected = "24.0";
  value = tostr(dem.elevation(24.9642, 60.2089, resolution));
  if (value != expected)
    TEST_FAILED("Expected elevation " + expected + " at Kumpula with 500 m resolution, not " +
                value);

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
    TEST(elevation);
    TEST(resolution);
  }

};  // class tests

}  // namespace Tests

int main(void)
{
  cout << endl << "DEM tester" << endl << "==========" << endl;
  Tests::tests t;
  return t.run();
}
