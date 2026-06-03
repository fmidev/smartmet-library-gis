#include "OGR.h"

#include <macgyver/StaticCleanup.h>
#include <ogr_geometry.h>
#include <ogr_spatialref.h>
#include <regression/tframe.h>

#include <cmath>
#include <memory>
#include <string>

using namespace std;

namespace
{
constexpr double kPi = 3.14159265358979323846;

std::unique_ptr<OGRPolygon> make_polygon(const std::string& wkt, OGRSpatialReference* srs = nullptr)
{
  OGRGeometry* g = nullptr;
  OGRGeometryFactory::createFromWkt(wkt.c_str(), srs, &g);
  return std::unique_ptr<OGRPolygon>(static_cast<OGRPolygon*>(g));
}

std::unique_ptr<OGRGeometry> make_geom(const std::string& wkt, OGRSpatialReference* srs = nullptr)
{
  OGRGeometry* g = nullptr;
  OGRGeometryFactory::createFromWkt(wkt.c_str(), srs, &g);
  return std::unique_ptr<OGRGeometry>(g);
}

bool nearly_equal(double a, double b, double tol)
{
  return std::abs(a - b) <= tol;
}
}  // namespace

namespace Tests
{
// ----------------------------------------------------------------------

void compactness_scalar()
{
  // Square: area = 1, perimeter = 4 → 4π/16 = π/4
  if (!nearly_equal(Fmi::OGR::compactness(1.0, 4.0), kPi / 4.0, 1e-12))
    TEST_FAILED("Scalar compactness(1, 4) should be π/4");

  // Circle of radius 1: area = π, perimeter = 2π → 4π·π/(2π)² = 1.0
  if (!nearly_equal(Fmi::OGR::compactness(kPi, 2.0 * kPi), 1.0, 1e-12))
    TEST_FAILED("Scalar compactness(π, 2π) should be 1.0");

  // Non-positive inputs return 0 — degenerate / missing data.
  if (Fmi::OGR::compactness(0.0, 4.0) != 0.0)
    TEST_FAILED("Zero area should produce 0 compactness");
  if (Fmi::OGR::compactness(1.0, 0.0) != 0.0)
    TEST_FAILED("Zero perimeter should produce 0 compactness");
  if (Fmi::OGR::compactness(-1.0, 4.0) != 0.0)
    TEST_FAILED("Negative area should produce 0 compactness");

  TEST_PASSED();
}

// ----------------------------------------------------------------------

void compactness_square_planar()
{
  // 1×1 square in planar coords. Theoretical 4πA/L² = π/4 ≈ 0.7854.
  auto poly = make_polygon("POLYGON ((0 0, 1 0, 1 1, 0 1, 0 0))");
  const double c = Fmi::OGR::compactness(*poly);
  if (!nearly_equal(c, kPi / 4.0, 1e-9))
    TEST_FAILED("Expected π/4 ≈ 0.7854 for unit square, got " + std::to_string(c));
  TEST_PASSED();
}

// ----------------------------------------------------------------------

void compactness_circle_approximation()
{
  // 64-gon approximating a unit circle: compactness should approach 1.0.
  std::string wkt = "POLYGON ((";
  const int n = 64;
  for (int i = 0; i <= n; ++i)
  {
    const double t = 2.0 * kPi * (i % n) / n;
    if (i > 0)
      wkt += ", ";
    wkt += std::to_string(std::cos(t)) + " " + std::to_string(std::sin(t));
  }
  wkt += "))";
  auto poly = make_polygon(wkt);
  const double c = Fmi::OGR::compactness(*poly);
  if (c < 0.997 || c > 1.0001)
    TEST_FAILED("Expected ~1.0 for 64-gon circle, got " + std::to_string(c));
  TEST_PASSED();
}

// ----------------------------------------------------------------------

void compactness_thin_rectangle()
{
  // 100×0.01 rectangle: highly elongated, compactness ≈ 4π·1/200.02² ≈ 3.14e-4
  auto poly = make_polygon("POLYGON ((0 0, 100 0, 100 0.01, 0 0.01, 0 0))");
  const double c = Fmi::OGR::compactness(*poly);
  if (c > 0.01)
    TEST_FAILED("Expected very small compactness for thin rectangle, got " + std::to_string(c));
  TEST_PASSED();
}

// ----------------------------------------------------------------------

void compactness_geographic_latitude_invariance()
{
  // A 1°×1° square at the equator and a similarly-shaped square at 60°N
  // (with longitude span widened by 1/cos(60°) = 2 so the metric shape is
  // roughly the same square on the sphere) should both score near π/4.
  // Without spherical handling, the high-latitude square would score far
  // worse because raw lat/lon space squashes longitudes.
  OGRSpatialReference wgs84;
  wgs84.importFromEPSG(4326);
  wgs84.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);

  auto eq = make_polygon("POLYGON ((0 0, 1 0, 1 1, 0 1, 0 0))", &wgs84);
  auto hi = make_polygon("POLYGON ((0 60, 2 60, 2 61, 0 61, 0 60))", &wgs84);

  const double c_eq = Fmi::OGR::compactness(*eq);
  const double c_hi = Fmi::OGR::compactness(*hi);

  // The equator square is exactly metric-square; expect ~π/4.
  if (c_eq < 0.78 || c_eq > 0.79)
    TEST_FAILED("Equator square compactness " + std::to_string(c_eq) +
                " not near π/4 with spherical maths");

  // The 60°N "stretched" square approximates a metric square; should be in
  // the same ballpark (within 5%).
  if (std::abs(c_eq - c_hi) > 0.05)
    TEST_FAILED("Latitude invariance failed: eq=" + std::to_string(c_eq) +
                " hi=" + std::to_string(c_hi));

  TEST_PASSED();
}

// ----------------------------------------------------------------------

void compactness_modes_with_hole()
{
  // 10×10 square outer with a 5×5 square hole in the middle.
  // Exterior: 4π·100/40² = π/4 ≈ 0.785 (unchanged by hole).
  // Net:      4π·(100-25)/(40+20)² = 4π·75/3600 ≈ 0.262.
  auto poly = make_polygon(
      "POLYGON ((0 0, 10 0, 10 10, 0 10, 0 0),"
      " (3 3, 8 3, 8 8, 3 8, 3 3))");

  const double c_ext = Fmi::OGR::compactness(*poly, Fmi::OGR::CompactnessMode::Exterior);
  const double c_net = Fmi::OGR::compactness(*poly, Fmi::OGR::CompactnessMode::Net);

  if (!nearly_equal(c_ext, kPi / 4.0, 1e-9))
    TEST_FAILED("Exterior mode should ignore holes; got " + std::to_string(c_ext));

  const double expected_net = 4.0 * kPi * 75.0 / (60.0 * 60.0);
  if (!nearly_equal(c_net, expected_net, 1e-9))
    TEST_FAILED("Net mode wrong: expected " + std::to_string(expected_net) +
                ", got " + std::to_string(c_net));

  if (c_net >= c_ext)
    TEST_FAILED("Net should be lower than Exterior when there are holes");

  TEST_PASSED();
}

// ----------------------------------------------------------------------

void filter_keeps_round_drops_thin()
{
  // MultiPolygon: a square (≈0.785) and a thin rectangle (≈3e-4).
  auto geom = make_geom(
      "MULTIPOLYGON ("
      "((0 0, 1 0, 1 1, 0 1, 0 0)),"
      "((10 0, 110 0, 110 0.01, 10 0.01, 10 0)))");

  std::unique_ptr<OGRGeometry> result(Fmi::OGR::filterByCompactness(*geom, 0.5));
  if (!result)
    TEST_FAILED("Threshold 0.5 should keep the square");
  if (result->getGeometryType() != wkbMultiPolygon)
    TEST_FAILED("Result should remain a MultiPolygon");
  auto* mp = static_cast<OGRMultiPolygon*>(result.get());
  if (mp->getNumGeometries() != 1)
    TEST_FAILED("Threshold 0.5 should leave exactly one polygon");
  TEST_PASSED();
}

// ----------------------------------------------------------------------

void filter_returns_null_when_all_dropped()
{
  auto geom = make_geom(
      "MULTIPOLYGON (((0 0, 100 0, 100 0.01, 0 0.01, 0 0)))");
  std::unique_ptr<OGRGeometry> result(Fmi::OGR::filterByCompactness(*geom, 0.5));
  if (result)
    TEST_FAILED("All-dropped result should be nullptr, got non-null");
  TEST_PASSED();
}

// ----------------------------------------------------------------------

void filter_preserves_spatial_reference()
{
  OGRSpatialReference wgs84;
  wgs84.importFromEPSG(4326);
  wgs84.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);

  auto geom = make_geom("POLYGON ((0 0, 1 0, 1 1, 0 1, 0 0))", &wgs84);
  std::unique_ptr<OGRGeometry> result(Fmi::OGR::filterByCompactness(*geom, 0.5));
  if (!result)
    TEST_FAILED("Should keep the polygon");
  const auto* sr = result->getSpatialReference();
  if (sr == nullptr)
    TEST_FAILED("Filter dropped the spatial reference");
  TEST_PASSED();
}

// ----------------------------------------------------------------------

void filter_passes_through_non_polygons()
{
  auto geom = make_geom(
      "GEOMETRYCOLLECTION ("
      "POLYGON ((0 0, 1 0, 1 1, 0 1, 0 0)),"
      "LINESTRING (0 0, 5 5))");
  std::unique_ptr<OGRGeometry> result(Fmi::OGR::filterByCompactness(*geom, 0.5));
  if (!result)
    TEST_FAILED("Collection with a kept polygon should not be empty");
  if (result->getGeometryType() != wkbGeometryCollection)
    TEST_FAILED("Result should remain a GeometryCollection");
  auto* col = static_cast<OGRGeometryCollection*>(result.get());
  if (col->getNumGeometries() != 2)
    TEST_FAILED("Linestring should pass through alongside the polygon");
  TEST_PASSED();
}

// ----------------------------------------------------------------------

void filter_min_area_planar()
{
  // theMinArea is in km². For planar coords, GDAL's get_Area is in CRS
  // units squared, which the filter then compares against minArea·1e6 —
  // matching the despeckle convention. So a 1×1 square in metric CRS is
  // 1 m² = 1e-6 km², and a minArea of 0.5 km² (= 500000 m²) drops it.
  auto poly = make_polygon("POLYGON ((0 0, 1 0, 1 1, 0 1, 0 0))");
  std::unique_ptr<OGRGeometry> small(
      Fmi::OGR::filterByCompactness(*poly, 0.0, 0.5));  // 0.5 km²
  if (small)
    TEST_FAILED("1 m² polygon should be dropped at minArea=0.5 km²");

  auto poly2 = make_polygon("POLYGON ((0 0, 2000 0, 2000 2000, 0 2000, 0 0))");  // 4 km²
  std::unique_ptr<OGRGeometry> big(Fmi::OGR::filterByCompactness(*poly2, 0.0, 0.5));
  if (!big)
    TEST_FAILED("4 km² polygon should pass minArea=0.5 km²");
  TEST_PASSED();
}

// ----------------------------------------------------------------------

class tests : public tframe::tests
{
  virtual const char* error_message_prefix() const { return "\n\t"; }
  void test()
  {
    TEST(compactness_scalar);
    TEST(compactness_square_planar);
    TEST(compactness_circle_approximation);
    TEST(compactness_thin_rectangle);
    TEST(compactness_geographic_latitude_invariance);
    TEST(compactness_modes_with_hole);
    TEST(filter_keeps_round_drops_thin);
    TEST(filter_returns_null_when_all_dropped);
    TEST(filter_preserves_spatial_reference);
    TEST(filter_passes_through_non_polygons);
    TEST(filter_min_area_planar);
  }
};

}  // namespace Tests

int main()
{
  // Clear the SpatialReference/PROJ-backed caches before unordered static
  // destruction at exit (otherwise double-frees with some GDAL/PROJ versions).
  Fmi::StaticCleanup::AtExit cleanup;
  cout << endl
       << "OGR compactness tester\n"
          "======================\n";
  Tests::tests t;
  return t.run();
}
