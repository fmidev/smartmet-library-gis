#include <regression/tframe.h>
#include <math.h>
#include "DEM.h"
#include "SrtmTile.h"
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
  Fmi::DEM dem("/smartmet/share/gis/rasters/viewfinder");

  std::string expected, value;

  // At sea
  expected = "0.0";
  value = tostr(dem.elevation(25, 59.5));
  if (value != expected)
    TEST_FAILED("Expected elevation " + expected + " at coordinate 25,59.5, not " + value);

  // Kumpula
  expected = "11.0";
  value = tostr(dem.elevation(24.9642, 60.2089));
  if (value != expected)
    TEST_FAILED("Expected elevation " + expected + " at Kumpula, not " + value);

  // Helsinki
  expected = "10.0";
  value = tostr(dem.elevation(24.93545, 60.16952));
  if (value != expected)
    TEST_FAILED("Expected elevation " + expected + " at Helsinki, not " + value);

  // Ruka
  expected = "449.0";
  value = tostr(dem.elevation(29.1507, 66.1677));
  if (value != expected) TEST_FAILED("Expected elevation " + expected + " at Ruka, not " + value);

  // Matterhorn

  expected = "4458.0";
  value = tostr(dem.elevation(7.6583, 45.9764));
  if (value != expected)
    TEST_FAILED("Expected elevation " + expected + " at Matterhorn, not " + value);

  // South pole
  expected = "2771.0";
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

  expected = "0.0";
  value = tostr(dem.elevation(-180, 0));
  if (value != expected) TEST_FAILED("Expected elevation " + expected + " at -180,0, not " + value);

  expected = "0.0";
  value = tostr(dem.elevation(180, 0));
  if (value != expected) TEST_FAILED("Expected elevation " + expected + " at 180,0, not " + value);

  expected = "0.0";
  value = tostr(dem.elevation(180, 0));
  if (value != expected) TEST_FAILED("Expected elevation " + expected + " at 180,0, not " + value);

  // Alanya had rounding issue with coordinates
  expected = "11.0";
  value = tostr(dem.elevation(31.99982, 36.543750000000003));
  if (value != expected) TEST_FAILED("Expected elevation " + expected + " at Alanya, not " + value);

  // Chukotskiy
  expected = "432.0";
  value = tostr(dem.elevation(179.999, 67));
  if (value != expected)
    TEST_FAILED("Expected elevation " + expected + " at 179.999,67, not " + value);

  expected = "435.0";
  value = tostr(dem.elevation(-179.999, 67));
  if (value != expected)
    TEST_FAILED("Expected elevation " + expected + " at -179.999,67, not " + value);

  expected = "434.0";
  value = tostr(dem.elevation(-180, 67));
  if (value != expected)
    TEST_FAILED("Expected elevation " + expected + " at -180,67, not " + value);

  expected = "434.0";
  value = tostr(dem.elevation(180, 67));
  if (value != expected) TEST_FAILED("Expected elevation " + expected + " at 180,67, not " + value);

  TEST_PASSED();
}

void elevation_various_dems()
{
  Fmi::DEM dem("/home/reponen/work/gis/test/demmit/dem_test");

  std::string expected, value;

  // Kumpula
  expected = "11.0";
  value = tostr(dem.elevation(24.9642, 60.2089, Fmi::TileType::DEM1_3601));
  if (value != expected)
    TEST_FAILED("Expected elevation " + expected + " at Kumpula, not " + value);

  expected = "17.0";
  value = tostr(dem.elevation(24.9642, 60.2089, Fmi::TileType::DEM3_1201));
  if (value != expected)
    TEST_FAILED("Expected elevation " + expected + " at Kumpula, not " + value);

  expected = "24.0";
  value = tostr(dem.elevation(24.9642, 60.2089, Fmi::TileType::DEM9_401));
  if (value != expected)
    TEST_FAILED("Expected elevation " + expected + " at Kumpula, not " + value);

  expected = "24.0";
  value = tostr(dem.elevation(24.9642, 60.2089, Fmi::TileType::DEM9_1201));
  if (value != expected)
    TEST_FAILED("Expected elevation " + expected + " at Kumpula, not " + value);

  expected = "22.0";
  value = tostr(dem.elevation(24.9642, 60.2089, Fmi::TileType::DEM27_1201));
  if (value != expected)
    TEST_FAILED("Expected elevation " + expected + " at Kumpula, not " + value);

  expected = "19.0";
  value = tostr(dem.elevation(24.9642, 60.2089, Fmi::TileType::DEM81_1001));
  if (value != expected)
    TEST_FAILED("Expected elevation " + expected + " at Kumpula, not " + value);

  // Helsinki
  expected = "10.0";
  value = tostr(dem.elevation(24.93545, 60.16952, Fmi::TileType::DEM1_3601));
  if (value != expected)
    TEST_FAILED("Expected elevation " + expected + " at Helsinki, not " + value);

  expected = "26.0";
  value = tostr(dem.elevation(24.93545, 60.16952, Fmi::TileType::DEM3_1201));
  if (value != expected)
    TEST_FAILED("Expected elevation " + expected + " at Helsinki, not " + value);

  expected = "34.0";
  value = tostr(dem.elevation(24.93545, 60.16952, Fmi::TileType::DEM9_401));
  if (value != expected)
    TEST_FAILED("Expected elevation " + expected + " at Helsinki, not " + value);

  expected = "37.0";
  value = tostr(dem.elevation(24.93545, 60.16952, Fmi::TileType::DEM9_1201));
  if (value != expected)
    TEST_FAILED("Expected elevation " + expected + " at Helsinki, not " + value);

  expected = "14.0";
  value = tostr(dem.elevation(24.93545, 60.16952, Fmi::TileType::DEM27_1201));
  if (value != expected)
    TEST_FAILED("Expected elevation " + expected + " at Helsinki, not " + value);

  expected = "22.0";
  value = tostr(dem.elevation(24.93545, 60.16952, Fmi::TileType::DEM81_1001));
  if (value != expected)
    TEST_FAILED("Expected elevation " + expected + " at Helsinki, not " + value);

  // Ruka
  expected = "449.0";
  value = tostr(dem.elevation(29.1507, 66.1677, Fmi::TileType::DEM3_1201));
  if (value != expected) TEST_FAILED("Expected elevation " + expected + " at Ruka, not " + value);

  expected = "449.0";
  value = tostr(dem.elevation(29.1507, 66.1677, Fmi::TileType::DEM9_401));
  if (value != expected) TEST_FAILED("Expected elevation " + expected + " at Ruka, not " + value);

  expected = "449.0";
  value = tostr(dem.elevation(29.1507, 66.1677, Fmi::TileType::DEM9_1201));
  if (value != expected) TEST_FAILED("Expected elevation " + expected + " at Ruka, not " + value);

  expected = "449.0";
  value = tostr(dem.elevation(29.1507, 66.1677, Fmi::TileType::DEM27_1201));
  if (value != expected) TEST_FAILED("Expected elevation " + expected + " at Ruka, not " + value);

  expected = "311.0";
  value = tostr(dem.elevation(29.1507, 66.1677, Fmi::TileType::DEM81_1001));
  if (value != expected) TEST_FAILED("Expected elevation " + expected + " at Ruka, not " + value);

  // Matterhorn
  expected = "4458.0";
  value = tostr(dem.elevation(7.6583, 45.9764, Fmi::TileType::DEM1_3601));
  if (value != expected)
    TEST_FAILED("Expected elevation " + expected + " at Matterhorn, not " + value);

  expected = "4441.0";
  value = tostr(dem.elevation(7.6583, 45.9764, Fmi::TileType::DEM3_1201));
  if (value != expected)
    TEST_FAILED("Expected elevation " + expected + " at Matterhorn, not " + value);

  expected = "4341.0";
  value = tostr(dem.elevation(7.6583, 45.9764, Fmi::TileType::DEM9_401));
  if (value != expected)
    TEST_FAILED("Expected elevation " + expected + " at Matterhorn, not " + value);

  expected = "4078.0";
  value = tostr(dem.elevation(7.6583, 45.9764, Fmi::TileType::DEM9_1201));
  if (value != expected)
    TEST_FAILED("Expected elevation " + expected + " at Matterhorn, not " + value);

  expected = "3919.0";
  value = tostr(dem.elevation(7.6583, 45.9764, Fmi::TileType::DEM27_1201));
  if (value != expected)
    TEST_FAILED("Expected elevation " + expected + " at Matterhorn, not " + value);

  expected = "3117.0";
  value = tostr(dem.elevation(7.6583, 45.9764, Fmi::TileType::DEM81_1001));
  if (value != expected)
    TEST_FAILED("Expected elevation " + expected + " at Matterhorn, not " + value);

  // South pole
  expected = "2771.0";
  value = tostr(dem.elevation(0, -90, Fmi::TileType::DEM3_1201));
  if (value != expected)
    TEST_FAILED("Expected elevation " + expected + " at South Pole, not " + value);

  expected = "2771.0";
  value = tostr(dem.elevation(0, -90, Fmi::TileType::DEM9_401));
  if (value != expected)
    TEST_FAILED("Expected elevation " + expected + " at South Pole, not " + value);

  expected = "2771.0";
  value = tostr(dem.elevation(0, -90, Fmi::TileType::DEM9_1201));
  if (value != expected)
    TEST_FAILED("Expected elevation " + expected + " at South Pole, not " + value);

  expected = "2771.0";
  value = tostr(dem.elevation(0, -90, Fmi::TileType::DEM27_1201));
  if (value != expected)
    TEST_FAILED("Expected elevation " + expected + " at South Pole, not " + value);

  expected = "2771.0";
  value = tostr(dem.elevation(0, -90, Fmi::TileType::DEM81_1001));
  if (value != expected)
    TEST_FAILED("Expected elevation " + expected + " at South Pole, not " + value);

  // Alanya had rounding issue with coordinates
  expected = "11.0";
  value = tostr(dem.elevation(31.99982, 36.543750000000003, Fmi::TileType::DEM3_1201));
  if (value != expected) TEST_FAILED("Expected elevation " + expected + " at Alanya, not " + value);

  expected = "13.0";
  value = tostr(dem.elevation(31.99982, 36.543750000000003, Fmi::TileType::DEM9_401));
  if (value != expected) TEST_FAILED("Expected elevation " + expected + " at Alanya, not " + value);

  expected = "10.0";
  value = tostr(dem.elevation(31.99982, 36.543750000000003, Fmi::TileType::DEM9_1201));
  if (value != expected) TEST_FAILED("Expected elevation " + expected + " at Alanya, not " + value);

  expected = "2.0";
  value = tostr(dem.elevation(31.99982, 36.543750000000003, Fmi::TileType::DEM27_1201));
  if (value != expected) TEST_FAILED("Expected elevation " + expected + " at Alanya, not " + value);

  expected = "13.0";
  value = tostr(dem.elevation(31.99982, 36.543750000000003, Fmi::TileType::DEM81_1001));
  if (value != expected) TEST_FAILED("Expected elevation " + expected + " at Alanya, not " + value);

  // Chukotskiy
  expected = "432.0";
  value = tostr(dem.elevation(179.999, 67, Fmi::TileType::DEM3_1201));
  if (value != expected)
    TEST_FAILED("Expected elevation " + expected + " at 179.999,67, not " + value);

  expected = "432.0";
  value = tostr(dem.elevation(179.999, 67, Fmi::TileType::DEM9_401));
  if (value != expected)
    TEST_FAILED("Expected elevation " + expected + " at 179.999,67, not " + value);

  expected = "432.0";
  value = tostr(dem.elevation(179.999, 67, Fmi::TileType::DEM9_1201));
  if (value != expected)
    TEST_FAILED("Expected elevation " + expected + " at 179.999,67, not " + value);

  expected = "425.0";
  value = tostr(dem.elevation(179.999, 67, Fmi::TileType::DEM27_1201));
  if (value != expected)
    TEST_FAILED("Expected elevation " + expected + " at 179.999,67, not " + value);

  expected = "411.0";
  value = tostr(dem.elevation(179.999, 67, Fmi::TileType::DEM81_1001));
  if (value != expected)
    TEST_FAILED("Expected elevation " + expected + " at 179.999,67, not " + value);

  expected = "435.0";
  value = tostr(dem.elevation(-179.999, 67, Fmi::TileType::DEM3_1201));
  if (value != expected)
    TEST_FAILED("Expected elevation " + expected + " at -179.999,67, not " + value);

  expected = "434.0";
  value = tostr(dem.elevation(-179.999, 67, Fmi::TileType::DEM9_401));
  if (value != expected)
    TEST_FAILED("Expected elevation " + expected + " at -179.999,67, not " + value);

  expected = "434.0";
  value = tostr(dem.elevation(-179.999, 67, Fmi::TileType::DEM9_1201));
  if (value != expected)
    TEST_FAILED("Expected elevation " + expected + " at -179.999,67, not " + value);

  expected = "431.0";
  value = tostr(dem.elevation(-179.999, 67, Fmi::TileType::DEM27_1201));
  if (value != expected)
    TEST_FAILED("Expected elevation " + expected + " at -179.999,67, not " + value);

  expected = "421.0";
  value = tostr(dem.elevation(-179.999, 67, Fmi::TileType::DEM81_1001));
  if (value != expected)
    TEST_FAILED("Expected elevation " + expected + " at -179.999,67, not " + value);

  expected = "434.0";
  value = tostr(dem.elevation(-180.0, 67, Fmi::TileType::DEM3_1201));
  if (value != expected)
    TEST_FAILED("Expected elevation " + expected + " at -179.999,67, not " + value);

  expected = "434.0";
  value = tostr(dem.elevation(-180.0, 67, Fmi::TileType::DEM9_401));
  if (value != expected)
    TEST_FAILED("Expected elevation " + expected + " at -179.999,67, not " + value);

  expected = "434.0";
  value = tostr(dem.elevation(-180.0, 67, Fmi::TileType::DEM9_1201));
  if (value != expected)
    TEST_FAILED("Expected elevation " + expected + " at -179.999,67, not " + value);

  expected = "431.0";
  value = tostr(dem.elevation(-180.0, 67, Fmi::TileType::DEM27_1201));
  if (value != expected)
    TEST_FAILED("Expected elevation " + expected + " at -179.999,67, not " + value);

  expected = "421.0";
  value = tostr(dem.elevation(-180.0, 67, Fmi::TileType::DEM81_1001));
  if (value != expected)
    TEST_FAILED("Expected elevation " + expected + " at -179.999,67, not " + value);

  expected = "434.0";
  value = tostr(dem.elevation(180.0, 67, Fmi::TileType::DEM3_1201));
  if (value != expected)
    TEST_FAILED("Expected elevation " + expected + " at 179.999,67, not " + value);

  expected = "434.0";
  value = tostr(dem.elevation(180.0, 67, Fmi::TileType::DEM9_401));
  if (value != expected)
    TEST_FAILED("Expected elevation " + expected + " at 179.999,67, not " + value);

  expected = "434.0";
  value = tostr(dem.elevation(180.0, 67, Fmi::TileType::DEM9_1201));
  if (value != expected)
    TEST_FAILED("Expected elevation " + expected + " at 179.999,67, not " + value);

  expected = "431.0";
  value = tostr(dem.elevation(180.0, 67, Fmi::TileType::DEM27_1201));
  if (value != expected)
    TEST_FAILED("Expected elevation " + expected + " at 179.999,67, not " + value);

  expected = "421.0";
  value = tostr(dem.elevation(180.0, 67, Fmi::TileType::DEM81_1001));
  if (value != expected)
    TEST_FAILED("Expected elevation " + expected + " at 179.999,67, not " + value);

  TEST_PASSED();
}

// ----------------------------------------------------------------------

void resolution()
{
  Fmi::DEM dem("/smartmet/share/gis/rasters/viewfinder");

  std::string expected, value;

  double resolution = 0.1;  // 100 meters

  // At sea
  expected = "0.0";
  value = tostr(dem.elevation(25, 59.5, resolution));
  if (value != expected)
    TEST_FAILED("Expected elevation " + expected + " at coordinate 25,59.5, not " + value);

  // Kumpula at 100 meters
  expected = "17.0";
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

void elevation_various_dems_one_cell()
{
  Fmi::DEM dem("/home/reponen/work/gis/test/demmit/dem_test");

  cout << std::endl << "DEM1      DEM3      DEM9_401  DEM9_1201 DEM27     DEM81     Location"
       << std::endl;
  cout << "==========================================================================="
       << std::endl;

  for (double lon = 24.0; lon < 25.0; lon += 0.1)
    for (double lat = 60.0; lat < 61.0; lat += 0.1)
    {
      std::stringstream coord;
      coord << fixed << setprecision(4) << lon << "," << fixed << setprecision(4) << lat;
      cout << std::setw(10) << std::setfill(' ') << std::left
           << dem.elevation(lon, lat, Fmi::TileType::DEM1_3601) << std::setw(10)
           << std::setfill(' ') << dem.elevation(lon, lat, Fmi::TileType::DEM3_1201)
           << std::setw(10) << std::setfill(' ') << dem.elevation(lon, lat, Fmi::TileType::DEM9_401)
           << std::setw(10) << std::setfill(' ')
           << dem.elevation(lon, lat, Fmi::TileType::DEM9_1201) << std::setw(10)
           << std::setfill(' ') << dem.elevation(lon, lat, Fmi::TileType::DEM27_1201)
           << std::setw(10) << std::setfill(' ')
           << dem.elevation(lon, lat, Fmi::TileType::DEM81_1001) << std::setw(10)
           << std::setfill(' ') << coord.str() << std::endl;
    }

  TEST_PASSED();
}

// Test driver
class tests : public tframe::tests
{
  // Overridden message separator
  virtual const char* error_message_prefix() const
  {
    return "\n\t";
  }
  // Main test suite
  void test()
  {

    TEST(elevation);
    TEST(resolution);
    //    TEST(elevation_various_dems);
  }

};  // class tests

}  // namespace Tests

int main(void)
{
  cout << endl << "DEM tester" << endl << "==========" << endl;
  Tests::tests t;
  return t.run();
}
