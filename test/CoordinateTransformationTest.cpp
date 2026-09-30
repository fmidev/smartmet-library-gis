#include "CoordinateTransformation.h"
#include "SpatialReference.h"
#include "TestDefs.h"

#include <macgyver/StaticCleanup.h>
#include <ogr_spatialref.h>
#include <regression/tframe.h>
#include <cmath>
#include <memory>

using namespace std;

namespace Tests
{
// The transformation must match GDAL's own, including datum shifts which
// cannot be expressed with PROJ strings (the OSGB36 datum of EPSG:27700).

void datum_shift()
{
  const double lon = -0.1276;
  const double lat = 51.5072;

  OGRSpatialReference src;
  OGRSpatialReference dst;
  src.importFromEPSG(4326);
  dst.importFromEPSG(27700);
  src.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
  dst.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
  std::unique_ptr<OGRCoordinateTransformation> gdal(OGRCreateCoordinateTransformation(&src, &dst));
  if (!gdal)
    TEST_FAILED("Failed to create the GDAL transformation");

  double x1 = lon;
  double y1 = lat;
  gdal->Transform(1, &x1, &y1);

  Fmi::CoordinateTransformation trans("EPSG:4326", "EPSG:27700");
  double x2 = lon;
  double y2 = lat;
  if (!trans.transform(x2, y2))
    TEST_FAILED("Failed to transform London to EPSG:27700");

  const double dist = std::hypot(x2 - x1, y2 - y1);
  if (dist > 0.01)
    TEST_FAILED("EPSG:4326 -> EPSG:27700 differs from GDAL by " + std::to_string(dist) + " m");

  TEST_PASSED();
}

// Test driver
class tests : public tframe::tests
{
  // Overridden message separator
  virtual const char* error_message_prefix() const { return "\n\t"; }
  // Main test suite
  void test() { TEST(datum_shift); }

};  // class tests

}  // namespace Tests

int main(void)
{
  Fmi::StaticCleanup::AtExit cleanup;

  cout << endl
       << "CoordinateTransformation tester\n"
          "===============================\n";
  Tests::tests t;
  return t.run();
}
