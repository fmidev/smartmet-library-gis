#include "GeometryAmalgamator.h"
#include "Types.h"
#include <regression/tframe.h>
#include <cmath>
#include <ogr_geometry.h>
#include <sstream>
#include <vector>

using namespace std;
using namespace Fmi;

namespace Tests
{

OGRGeometryPtr make_geom(const char* wkt)
{
  OGRGeometry* geom = nullptr;
  OGRGeometryFactory::createFromWkt(wkt, nullptr, &geom);
  return OGRGeometryPtr(geom);
}

// Create a square polygon at (x,y) with given size
OGRGeometryPtr make_square(double x, double y, double size)
{
  auto* ring = new OGRLinearRing();
  ring->addPoint(x, y);
  ring->addPoint(x + size, y);
  ring->addPoint(x + size, y + size);
  ring->addPoint(x, y + size);
  ring->closeRings();
  auto* poly = new OGRPolygon();
  poly->addRingDirectly(ring);
  return OGRGeometryPtr(poly);
}

// Count polygons in result
int count_polygons(const std::vector<OGRGeometryPtr>& geoms)
{
  int count = 0;
  for (const auto& g : geoms)
    if (g && !g->IsEmpty())
      count++;
  return count;
}

// Total area of all polygons
double total_area(const std::vector<OGRGeometryPtr>& geoms)
{
  double area = 0;
  for (const auto& g : geoms)
    if (g)
      area += g->toPolygon()->get_Area();
  return area;
}

// ----------------------------------------------------------------------

void four_squares_merge()
{
  // 4 unit squares with 0.2-unit gaps in a 2x2 grid
  std::vector<OGRGeometryPtr> geoms;
  geoms.push_back(make_square(0, 0, 1));      // bottom-left
  geoms.push_back(make_square(1.2, 0, 1));    // bottom-right
  geoms.push_back(make_square(0, 1.2, 1));    // top-left
  geoms.push_back(make_square(1.2, 1.2, 1));  // top-right

  GeometryAmalgamator amalgamator;
  amalgamator.lengthLimit(0.3);  // > 0.2 gap
  amalgamator.areaLimit(0);
  amalgamator.apply(geoms);

  int n = count_polygons(geoms);
  if (n != 1)
    TEST_FAILED("Expected 1 merged polygon, got " + to_string(n));

  // Total area: 4 squares (4.0) + gap fill. Gap cross is ~0.2*0.2 + 4*0.2*1.0 = 0.84
  // The actual area depends on triangulation but should be > 4.0
  double area = total_area(geoms);
  if (area < 4.0)
    TEST_FAILED("Merged area too small: " + to_string(area));

  TEST_PASSED();
}

// ----------------------------------------------------------------------

void distant_squares_separate()
{
  // Two squares 5 units apart
  std::vector<OGRGeometryPtr> geoms;
  geoms.push_back(make_square(0, 0, 1));
  geoms.push_back(make_square(6, 0, 1));

  GeometryAmalgamator amalgamator;
  amalgamator.lengthLimit(0.5);  // much less than 5-unit gap
  amalgamator.areaLimit(0);
  amalgamator.apply(geoms);

  int n = count_polygons(geoms);
  if (n != 2)
    TEST_FAILED("Expected 2 separate polygons, got " + to_string(n));

  TEST_PASSED();
}

// ----------------------------------------------------------------------

void area_filter_only()
{
  std::vector<OGRGeometryPtr> geoms;
  geoms.push_back(make_square(0, 0, 0.5));  // area = 0.25

  GeometryAmalgamator amalgamator;
  amalgamator.lengthLimit(0);  // no amalgamation
  amalgamator.areaLimit(1.0);  // filter: need area >= 1.0
  amalgamator.apply(geoms);

  if (!geoms.empty())
    TEST_FAILED("Expected empty result, got " + to_string(geoms.size()) + " polygons");

  TEST_PASSED();
}

// ----------------------------------------------------------------------

void archipelago()
{
  // 10x10 grid of small islands (100 islands)
  // Each island: 0.8 x 0.8 (area = 0.64)
  // Gap between islands: 0.2 units
  // Grid spacing: 1.0 (0.8 island + 0.2 gap)

  std::vector<OGRGeometryPtr> geoms;
  for (int row = 0; row < 10; row++)
    for (int col = 0; col < 10; col++)
      geoms.push_back(make_square(col * 1.0, row * 1.0, 0.8));

  // Without amalgamation, area limit 2.0 would remove every island (each only 0.64)
  GeometryAmalgamator amalgamator;
  amalgamator.lengthLimit(0.3);  // > 0.2 gap → islands merge
  amalgamator.areaLimit(2.0);    // removes any remaining tiny fragments
  amalgamator.apply(geoms);

  int n = count_polygons(geoms);
  if (n != 1)
    TEST_FAILED("Expected 1 merged landmass, got " + to_string(n));

  // The merged area should be substantial: 100 * 0.64 = 64 base area + gap fill
  double area = total_area(geoms);
  if (area < 50.0)
    TEST_FAILED("Merged archipelago area too small: " + to_string(area) + " (expected > 50.0)");

  TEST_PASSED();
}

// ----------------------------------------------------------------------

void archipelago_with_mainland()
{
  // Same 10x10 archipelago grid
  std::vector<OGRGeometryPtr> geoms;
  for (int row = 0; row < 10; row++)
    for (int col = 0; col < 10; col++)
      geoms.push_back(make_square(col * 1.0, row * 1.0, 0.8));

  // Add a mainland polygon 0.5 units away from the archipelago grid
  // Archipelago extends from (0,0) to (9.8,9.8)
  // Mainland at x = 10.3 (gap = 10.3 - 9.8 = 0.5 > lengthLimit 0.3)
  geoms.push_back(make_square(10.3, 0, 5));

  GeometryAmalgamator amalgamator;
  amalgamator.lengthLimit(0.3);
  amalgamator.areaLimit(2.0);
  amalgamator.apply(geoms);

  int n = count_polygons(geoms);
  if (n != 2)
    TEST_FAILED("Expected 2 polygons (archipelago + mainland), got " + to_string(n));

  TEST_PASSED();
}

// ----------------------------------------------------------------------

void none_disabled()
{
  std::vector<OGRGeometryPtr> geoms;
  geoms.push_back(make_square(0, 0, 1));
  geoms.push_back(make_square(2, 0, 1));

  int before = count_polygons(geoms);

  GeometryAmalgamator amalgamator;
  amalgamator.lengthLimit(0);
  amalgamator.areaLimit(0);
  amalgamator.apply(geoms);

  int after = count_polygons(geoms);
  if (before != after)
    TEST_FAILED("Expected no change, had " + to_string(before) + " polygons, got " +
                to_string(after));

  TEST_PASSED();
}

// Test driver
class tests : public tframe::tests
{
  virtual const char* error_message_prefix() const { return "\n\t"; }
  void test()
  {
    TEST(four_squares_merge);
    TEST(distant_squares_separate);
    TEST(area_filter_only);
    TEST(archipelago);
    TEST(archipelago_with_mainland);
    TEST(none_disabled);
  }
};

}  // namespace Tests

int main()
{
  cout << "\nGeometryAmalgamator tester" << endl << "==========================\n";
  Tests::tests t;
  return t.run();
}
