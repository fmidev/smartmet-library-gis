#include "Box.h"
#include "GeometrySmoother.h"
#include "OGR.h"
#include "Types.h"
#include <geos/geom/Geometry.h>
#include <regression/tframe.h>
#include <ogr_geometry.h>
#include <vector>

using namespace std;
using namespace Fmi;

namespace Tests
{

// ----------------------------------------------------------------------

void apply()
{
  using OGR::exportToSvg;

  // Ref: http://en.wikipedia.org/wiki/Well-known_text

  const char* g1 = "POLYGON ((0 0,0 1,0 2,1 2,2 2,2 1,2 0,1 0,0 0))";
  const char* g2 = "POLYGON ((0 2,0 3,0 4,1 4,2 4,2 3,2 2,1 2,0 2))";
  const char* g3 = "POLYGON ((2 0,2 1,2 2,3 2,4 2,4 1,4 0,3 0,2 0))";
  const char* g4 = "POLYGON ((2 2,2 3,2 4,3 4,4 4,4 3,4 2,3 2,2 2))";

  const auto box = Fmi::Box::identity();

  std::vector<OGRGeometryPtr> geoms;

  OGRGeometry* geom = nullptr;
  OGRGeometryFactory::createFromWkt(g1, NULL, &geom);
  geoms.push_back(OGRGeometryPtr(geom));
  OGRGeometryFactory::createFromWkt(g2, NULL, &geom);
  geoms.push_back(OGRGeometryPtr(geom));
  OGRGeometryFactory::createFromWkt(g3, NULL, &geom);
  geoms.push_back(OGRGeometryPtr(geom));
  OGRGeometryFactory::createFromWkt(g4, NULL, &geom);
  geoms.push_back(OGRGeometryPtr(geom));

  GeometrySmoother smoother;
  smoother.type(GeometrySmoother::Type::Average);
  smoother.radius(2);
  smoother.iterations(1);

  bool preserve_topology = true;
  smoother.apply(geoms, preserve_topology);

  auto r1 = exportToSvg(*geoms[0], box, 2);
  auto r2 = exportToSvg(*geoms[1], box, 2);
  auto r3 = exportToSvg(*geoms[2], box, 2);
  auto r4 = exportToSvg(*geoms[3], box, 2);

  std::string ok1 = "M0 0 0 1 0 2 1 2 2 2 2 1 2 0 1 0Z";
  std::string ok2 = "M0 2 0 3 0 4 1 4 2 4 2 3 2 2 1 2Z";
  std::string ok3 = "M2 0 2 1 2 2 3 2 4 2 4 1 4 0 3 0Z";
  std::string ok4 = "M2 2 2 3 2 4 3 4 4 4 4 3 4 2 3 2Z";

  if (r1 != ok1)
    TEST_FAILED("Test 1 failed, expected " + ok1 + ", got " + r1);
  if (r2 != ok2)
    TEST_FAILED("Test 2 failed, expected " + ok2 + ", got " + r2);
  if (r3 != ok3)
    TEST_FAILED("Test 3 failed, expected " + ok3 + ", got " + r3);
  if (r4 != ok4)
    TEST_FAILED("Test 4 failed, expected " + ok4 + ", got " + r4);

  TEST_PASSED();
}

// Test driver
class tests : public tframe::tests
{
  // Overridden message separator
  virtual const char* error_message_prefix() const { return "\n\t"; }
  // Main test suite
  void test() { TEST(apply); }

};  // class tests

}  // namespace Tests

int main()
{
  cout << "\nGeometrySmoother tester" << endl << "=======================\n";
  Tests::tests t;
  return t.run();
}
