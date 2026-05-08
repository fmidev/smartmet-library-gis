#include "Box.h"
#include "GeometrySimplifier.h"
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

// Helper to create a geometry from WKT
OGRGeometryPtr make_geom(const char* wkt)
{
  OGRGeometry* geom = nullptr;
  OGRGeometryFactory::createFromWkt(wkt, nullptr, &geom);
  return OGRGeometryPtr(geom);
}

// ----------------------------------------------------------------------

void visvalingam_whyatt()
{
  // Same zigzag: triangle areas are small
  // Triangle at (1,0.1): area = 0.5 * |1*(0-0) - 1*0.1 + ... | = 0.1
  auto geom = make_geom("LINESTRING (0 0, 1 0.1, 2 0, 3 0.1, 4 0)");
  std::vector<OGRGeometryPtr> geoms = {geom};

  GeometrySimplifier simplifier;
  simplifier.type(GeometrySimplifier::Type::VisvalingamWhyatt);
  simplifier.tolerance(0.3);  // area threshold: triangles with area < 0.3 are removed
  simplifier.apply(geoms, false);

  const auto box = Box::identity();
  auto result = OGR::exportToSvg(*geoms[0], box, 1);
  string expected = "M0 0 4 0";

  if (result != expected)
    TEST_FAILED("Visvalingam-Whyatt basic test failed, expected " + expected + ", got " + result);

  // With small tolerance, keep all points
  auto geom2 = make_geom("LINESTRING (0 0, 1 0.1, 2 0, 3 0.1, 4 0)");
  std::vector<OGRGeometryPtr> geoms2 = {geom2};

  GeometrySimplifier simplifier2;
  simplifier2.type(GeometrySimplifier::Type::VisvalingamWhyatt);
  simplifier2.tolerance(0.01);
  simplifier2.apply(geoms2, false);

  auto result2 = OGR::exportToSvg(*geoms2[0], box, 1);
  string expected2 = "M0 0 1 0.1 2 0 3 0.1 4 0";

  if (result2 != expected2)
    TEST_FAILED("Visvalingam-Whyatt preserve test failed, expected " + expected2 + ", got " +
                result2);

  TEST_PASSED();
}

// ----------------------------------------------------------------------

void topology_preservation()
{
  // 4 adjacent polygons (2x2 grid) — same geometry as GeometrySmootherTest.
  // With topology preservation:
  // - Outer boundary midpoints (count=1) are anchors — must be kept
  // - Shared edge midpoints (count=2) can be simplified — D-P removes them (collinear)
  // - Corner points shared by 4 polygons (count=4) are anchors — must be kept
  // - Corner points shared by 2 polygons (count=2) are kept by D-P (distance > tolerance)

  const char* g1 = "POLYGON ((0 0,0 1,0 2,1 2,2 2,2 1,2 0,1 0,0 0))";
  const char* g2 = "POLYGON ((0 2,0 3,0 4,1 4,2 4,2 3,2 2,1 2,0 2))";
  const char* g3 = "POLYGON ((2 0,2 1,2 2,3 2,4 2,4 1,4 0,3 0,2 0))";
  const char* g4 = "POLYGON ((2 2,2 3,2 4,3 4,4 4,4 3,4 2,3 2,2 2))";

  std::vector<OGRGeometryPtr> geoms;
  geoms.push_back(make_geom(g1));
  geoms.push_back(make_geom(g2));
  geoms.push_back(make_geom(g3));
  geoms.push_back(make_geom(g4));

  GeometrySimplifier simplifier;
  simplifier.type(GeometrySimplifier::Type::VisvalingamWhyatt);
  simplifier.tolerance(0.5);

  simplifier.apply(geoms, true);

  const auto box = Box::identity();
  auto r1 = OGR::exportToSvg(*geoms[0], box, 1);
  auto r2 = OGR::exportToSvg(*geoms[1], box, 1);
  auto r3 = OGR::exportToSvg(*geoms[2], box, 1);
  auto r4 = OGR::exportToSvg(*geoms[3], box, 1);

  // D-P removes collinear shared-edge midpoints (count=2, distance=0) but keeps outer
  // boundary midpoints (count=1, anchors) and non-collinear corners where D-P distance
  // exceeds tolerance (e.g. (0,2) on line (0,1)->(2,2) has distance ~0.89).

  string ok1 = "M0 0 0 1 0 2 2 2 2 0 1 0Z";
  string ok2 = "M0 2 0 3 0 4 1 4 2 4 2 2Z";
  string ok3 = "M2 0 2 2 4 2 4 1 4 0 3 0Z";
  string ok4 = "M2 2 2 4 3 4 4 4 4 3 4 2Z";

  if (r1 != ok1)
    TEST_FAILED("Topology test g1 failed, expected " + ok1 + ", got " + r1);
  if (r2 != ok2)
    TEST_FAILED("Topology test g2 failed, expected " + ok2 + ", got " + r2);
  if (r3 != ok3)
    TEST_FAILED("Topology test g3 failed, expected " + ok3 + ", got " + r3);
  if (r4 != ok4)
    TEST_FAILED("Topology test g4 failed, expected " + ok4 + ", got " + r4);

  TEST_PASSED();
}

// ----------------------------------------------------------------------

void closed_ring()
{
  // A polygon ring with extra vertices on edges
  auto geom = make_geom("POLYGON ((0 0, 0 1, 0 2, 1 2, 2 2, 2 1, 2 0, 1 0, 0 0))");
  std::vector<OGRGeometryPtr> geoms = {geom};

  GeometrySimplifier simplifier;
  simplifier.type(GeometrySimplifier::Type::VisvalingamWhyatt);
  simplifier.tolerance(0.5);
  simplifier.apply(geoms, false);

  const auto box = Box::identity();
  auto result = OGR::exportToSvg(*geoms[0], box, 1);

  // The midpoints are collinear with the corners, so VW should remove them.
  // VW treats the closed ring as a linear sequence with the first and last
  // input vertex pinned (sentinel triangle areas), so the expected result is
  // the four corners plus one pinned mid-edge starting vertex.
  string expected = "M0 0 0 2 2 2 2 0 1 0Z";

  if (result != expected)
    TEST_FAILED("Closed ring test failed, expected " + expected + ", got " + result);

  TEST_PASSED();
}

// ----------------------------------------------------------------------

void degenerate_protection()
{
  // A triangle — simplification cannot reduce it further
  auto geom = make_geom("POLYGON ((0 0, 2 3, 4 0, 0 0))");
  std::vector<OGRGeometryPtr> geoms = {geom};

  GeometrySimplifier simplifier;
  simplifier.type(GeometrySimplifier::Type::VisvalingamWhyatt);
  simplifier.tolerance(100);  // very aggressive
  simplifier.apply(geoms, false);

  const auto box = Box::identity();
  auto result = OGR::exportToSvg(*geoms[0], box, 1);
  string expected = "M0 0 2 3 4 0Z";

  if (result != expected)
    TEST_FAILED("Degenerate protection test failed, expected " + expected + ", got " + result);

  TEST_PASSED();
}

// ----------------------------------------------------------------------

void none_type()
{
  auto geom = make_geom("LINESTRING (0 0, 1 0.1, 2 0, 3 0.1, 4 0)");
  std::vector<OGRGeometryPtr> geoms = {geom};

  const auto box = Box::identity();
  auto before = OGR::exportToSvg(*geoms[0], box, 1);

  GeometrySimplifier simplifier;
  simplifier.type(GeometrySimplifier::Type::None);
  simplifier.tolerance(1.0);
  simplifier.apply(geoms, false);

  auto after = OGR::exportToSvg(*geoms[0], box, 1);

  if (before != after)
    TEST_FAILED("None type should not modify geometry, got " + after);

  TEST_PASSED();
}

// Test driver
class tests : public tframe::tests
{
  virtual const char* error_message_prefix() const { return "\n\t"; }
  void test()
  {
    TEST(visvalingam_whyatt);
    TEST(topology_preservation);
    TEST(closed_ring);
    TEST(degenerate_protection);
    TEST(none_type);
  }
};

}  // namespace Tests

int main()
{
  cout << "\nGeometrySimplifier tester" << endl << "=========================\n";
  Tests::tests t;
  return t.run();
}
