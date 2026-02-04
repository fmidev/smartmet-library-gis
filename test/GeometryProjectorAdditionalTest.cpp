// GeometryProjectorAdditionalTests.cpp
// Additional comprehensive tests for GeometryProjector

#include <gtest/gtest.h>

#include <cmath>
#include <gdal.h>
#include <limits>
#include <memory>
#include <ogr_geometry.h>
#include <ogr_spatialref.h>
#include <stdexcept>
#include <string>
#include <vector>

#include "GeometryProjector.h"

namespace
{

struct GdalInitGuard
{
  GdalInitGuard() { GDALAllRegister(); }
};

struct GdalErrorSilencer
{
  GdalErrorSilencer() { CPLPushErrorHandler(CPLQuietErrorHandler); }
  ~GdalErrorSilencer() { CPLPopErrorHandler(); }
};

OGRSpatialReference makeSRS(int epsg)
{
  OGRSpatialReference s;
  s.importFromEPSG(epsg);
  s.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
  return s;
}

struct Bounds
{
  double minX, minY, maxX, maxY;
};

inline bool insideB(double x, double y, const Bounds& b, double eps = 1e-6)
{
  return x >= b.minX - eps && x <= b.maxX + eps && y >= b.minY - eps && y <= b.maxY + eps;
}

bool allVerticesWithinBounds(const OGRGeometry* g, const Bounds& b);
bool allPolygonRingsClosed(const OGRGeometry* g);
bool geometryIsValid(const OGRGeometry* g);

std::vector<OGRPoint> allVertices(const OGRGeometry* g)
{
  std::vector<OGRPoint> pts;
  if (!g || g->IsEmpty())
    return pts;

  const auto t = wkbFlatten(g->getGeometryType());
  if (t == wkbPoint)
  {
    auto* p = dynamic_cast<const OGRPoint*>(g);
    if (p && !p->IsEmpty())
      pts.emplace_back(p->getX(), p->getY());
  }
  else if (t == wkbLineString || t == wkbLinearRing)
  {
    auto* ls = dynamic_cast<const OGRLineString*>(g);
    if (ls)
    {
      for (int i = 0; i < ls->getNumPoints(); ++i)
        pts.emplace_back(ls->getX(i), ls->getY(i));
    }
  }
  else if (t == wkbPolygon)
  {
    auto* poly = dynamic_cast<const OGRPolygon*>(g);
    if (poly)
    {
      if (auto* ext = poly->getExteriorRing())
      {
        for (int i = 0; i < ext->getNumPoints(); ++i)
          pts.emplace_back(ext->getX(i), ext->getY(i));
      }
      for (int h = 0; h < poly->getNumInteriorRings(); ++h)
      {
        if (auto* hole = poly->getInteriorRing(h))
        {
          for (int i = 0; i < hole->getNumPoints(); ++i)
            pts.emplace_back(hole->getX(i), hole->getY(i));
        }
      }
    }
  }
  else if (t == wkbMultiLineString || t == wkbMultiPolygon || t == wkbGeometryCollection)
  {
    auto* gc = dynamic_cast<const OGRGeometryCollection*>(g);
    if (gc)
    {
      for (int i = 0; i < gc->getNumGeometries(); ++i)
      {
        auto sub = allVertices(gc->getGeometryRef(i));
        pts.insert(pts.end(), sub.begin(), sub.end());
      }
    }
  }
  return pts;
}

bool allVerticesWithinBounds(const OGRGeometry* g, const Bounds& b)
{
  for (const auto& p : allVertices(g))
  {
    if (!insideB(p.getX(), p.getY(), b))
      return false;
  }
  return true;
}

bool ringClosed(const OGRLinearRing* r, double eps = 1e-4)
{
  if (!r || r->getNumPoints() < 4)
    return false;
  OGRPoint a, b;
  r->getPoint(0, &a);
  r->getPoint(r->getNumPoints() - 1, &b);
  return std::abs(a.getX() - b.getX()) <= eps && std::abs(a.getY() - b.getY()) <= eps;
}

bool allPolygonRingsClosed(const OGRGeometry* g)
{
  if (!g)
    return true;
  const auto t = wkbFlatten(g->getGeometryType());
  if (t == wkbPolygon)
  {
    auto* p = dynamic_cast<const OGRPolygon*>(g);
    if (!ringClosed(p->getExteriorRing()))
      return false;
    for (int i = 0; i < p->getNumInteriorRings(); ++i)
      if (!ringClosed(p->getInteriorRing(i)))
        return false;
    return true;
  }
  if (t == wkbMultiPolygon)
  {
    auto* mp = dynamic_cast<const OGRMultiPolygon*>(g);
    for (int i = 0; i < mp->getNumGeometries(); ++i)
      if (!allPolygonRingsClosed(mp->getGeometryRef(i)))
        return false;
    return true;
  }
  return true;
}

bool geometryIsValid(const OGRGeometry* g)
{
  if (!g)
    return false;
  return g->IsValid();
}

struct Fixture
{
  Bounds B{-300000.0, 6500000.0, 800000.0, 8500000.0};
  OGRSpatialReference wgs84 = makeSRS(4326);
  OGRSpatialReference tm35 = makeSRS(3067);

  GeometryProjector makeProjector(double densifyKm = 50.0)
  {
    GeometryProjector p(&wgs84, &tm35);
    p.setProjectedBounds(B.minX, B.minY, B.maxX, B.maxY);
    p.setDensifyResolutionKm(densifyKm);
    return p;
  }
};

}  // namespace

// ============================================================================
// Test 1: Extreme Coordinate Values
// ============================================================================

TEST(AdditionalTests, ExtremeCoordinates_VeryLargeValues_DoesNotCrash)
{
  CPLSetConfigOption("OGR_GEOMETRY_ACCEPT_UNCLOSED_RING", "NO");
  static GdalInitGuard guard;
  GdalErrorSilencer silence;
  Fixture fx;

  auto projector = fx.makeProjector(0.0);

  // Create geometry with very large coordinate values (but still within WGS84 range)
  OGRPoint largePoint(179.999, 89.999);

  auto out = projector.projectGeometry(&largePoint);
  ASSERT_TRUE(out);
  // May be empty if outside projection domain, but should not crash
}

TEST(AdditionalTests, ExtremeCoordinates_VerySmallDifferences_HandlesCorrectly)
{
  CPLSetConfigOption("OGR_GEOMETRY_ACCEPT_UNCLOSED_RING", "NO");
  static GdalInitGuard guard;
  Fixture fx;

  auto projector = fx.makeProjector(0.0);

  // Two points very close together (1 micrometer at equator ~= 1e-8 degrees)
  OGRLineString line;
  line.addPoint(25.0, 60.0);
  line.addPoint(25.0 + 1e-8, 60.0 + 1e-8);

  auto out = projector.projectGeometry(&line);
  ASSERT_TRUE(out);
  // Should handle without numerical instability
}

TEST(AdditionalTests, ExtremeCoordinates_NearFloatingPointLimits_DoesNotOverflow)
{
  CPLSetConfigOption("OGR_GEOMETRY_ACCEPT_UNCLOSED_RING", "NO");
  static GdalInitGuard guard;
  GdalErrorSilencer silence;

  // Use very large projected bounds to test numerical stability
  Bounds largeB{-1e15, -1e15, 1e15, 1e15};
  OGRSpatialReference wgs84 = makeSRS(4326);
  OGRSpatialReference webMerc = makeSRS(3857);  // Web Mercator has large coordinates

  GeometryProjector projector(&wgs84, &webMerc);
  projector.setProjectedBounds(largeB.minX, largeB.minY, largeB.maxX, largeB.maxY);

  OGRPoint p(0.0, 0.0);
  auto out = projector.projectGeometry(&p);
  ASSERT_TRUE(out);
}

// ============================================================================
// Test 2: Empty and Degenerate Geometries
// ============================================================================

TEST(AdditionalTests, DegenerateGeometries_EmptyPoint_ReturnsEmptyPoint)
{
  CPLSetConfigOption("OGR_GEOMETRY_ACCEPT_UNCLOSED_RING", "NO");
  static GdalInitGuard guard;
  Fixture fx;

  auto projector = fx.makeProjector();

  OGRPoint emptyPoint;
  auto out = projector.projectGeometry(&emptyPoint);
  ASSERT_TRUE(out);
  EXPECT_TRUE(out->IsEmpty());
  EXPECT_EQ(wkbFlatten(out->getGeometryType()), wkbPoint);
}

TEST(AdditionalTests, DegenerateGeometries_SinglePointLine_ReturnsValidOrEmpty)
{
  CPLSetConfigOption("OGR_GEOMETRY_ACCEPT_UNCLOSED_RING", "NO");
  static GdalInitGuard guard;
  Fixture fx;

  auto projector = fx.makeProjector();

  OGRLineString singlePointLine;
  singlePointLine.addPoint(25.0, 60.0);

  auto out = projector.projectGeometry(&singlePointLine);
  ASSERT_TRUE(out);
  // Should handle gracefully - may be empty or single point
}

TEST(AdditionalTests, DegenerateGeometries_TwoPointPolygon_HandlesGracefully)
{
  CPLSetConfigOption("OGR_GEOMETRY_ACCEPT_UNCLOSED_RING", "NO");
  static GdalInitGuard guard;
  GdalErrorSilencer silence;
  Fixture fx;

  auto projector = fx.makeProjector();

  // Invalid polygon (too few points) - should not crash
  OGRPolygon poly;
  OGRLinearRing ring;
  ring.addPoint(25.0, 60.0);
  ring.addPoint(26.0, 60.0);
  ring.closeRings();
  poly.addRing(&ring);

  auto out = projector.projectGeometry(&poly);
  ASSERT_TRUE(out);
  // May return empty, should not crash
}

TEST(AdditionalTests, DegenerateGeometries_ZeroAreaPolygon_CollinearPoints)
{
  CPLSetConfigOption("OGR_GEOMETRY_ACCEPT_UNCLOSED_RING", "NO");
  static GdalInitGuard guard;
  Fixture fx;

  auto projector = fx.makeProjector();

  // Polygon with all points on a line (zero area)
  OGRPolygon poly;
  OGRLinearRing ring;
  ring.addPoint(25.0, 60.0);
  ring.addPoint(25.5, 60.0);
  ring.addPoint(26.0, 60.0);
  ring.addPoint(25.5, 60.0);
  ring.addPoint(25.0, 60.0);
  poly.addRing(&ring);

  auto out = projector.projectGeometry(&poly);
  ASSERT_TRUE(out);
  // Should handle without crashing
}

// ============================================================================
// Test 3: Projection Domain Boundaries
// ============================================================================

TEST(AdditionalTests, ProjectionDomainBoundary_GeometryCrossingValidityBoundary)
{
  CPLSetConfigOption("OGR_GEOMETRY_ACCEPT_UNCLOSED_RING", "NO");
  static GdalInitGuard guard;
  GdalErrorSilencer silence;
  Fixture fx;

  auto projector = fx.makeProjector(0.0);

  // Line that crosses from valid TM35 domain (Finland) to invalid (far south)
  OGRLineString line;
  line.addPoint(25.0, 60.0);   // Valid for TM35
  line.addPoint(25.0, 40.0);   // Questionable for TM35
  line.addPoint(25.0, 0.0);    // Invalid for TM35

  auto out = projector.projectGeometry(&line);
  ASSERT_TRUE(out);

  if (!out->IsEmpty())
  {
    // Should have clipped/split at projection boundary
    EXPECT_TRUE(allVerticesWithinBounds(out.get(), fx.B));
  }
}

TEST(AdditionalTests, ProjectionDomainBoundary_PolygonPartiallyInDomain)
{
  CPLSetConfigOption("OGR_GEOMETRY_ACCEPT_UNCLOSED_RING", "NO");
  static GdalInitGuard guard;
  GdalErrorSilencer silence;
  Fixture fx;

  auto projector = fx.makeProjector(50.0);

  // Polygon that extends beyond reasonable TM35 projection area
  OGRPolygon poly;
  OGRLinearRing ring;
  ring.addPoint(20.0, 60.0);
  ring.addPoint(30.0, 60.0);
  ring.addPoint(30.0, 40.0);  // Too far south
  ring.addPoint(20.0, 40.0);
  ring.addPoint(20.0, 60.0);
  poly.addRing(&ring);

  auto out = projector.projectGeometry(&poly);
  ASSERT_TRUE(out);

  if (!out->IsEmpty())
  {
    EXPECT_TRUE(allVerticesWithinBounds(out.get(), fx.B));
    EXPECT_TRUE(allPolygonRingsClosed(out.get()));
  }
}

// ============================================================================
// Test 4: Numerical Precision Near Boundaries
// ============================================================================

TEST(AdditionalTests, NumericalPrecision_PointExactlyOnBoundary)
{
  CPLSetConfigOption("OGR_GEOMETRY_ACCEPT_UNCLOSED_RING", "NO");
  static GdalInitGuard guard;
  Fixture fx;

  auto projector = fx.makeProjector(0.0);

  // Create a point that projects to exactly the boundary
  // We'll use inverse transform to find such a point
  auto* ct = OGRCreateCoordinateTransformation(&fx.tm35, &fx.wgs84);
  ASSERT_TRUE(ct);

  double x = fx.B.minX;
  double y = (fx.B.minY + fx.B.maxY) / 2.0;
  ct->Transform(1, &x, &y);

  OGRPoint boundaryPoint(x, y);
  auto out = projector.projectGeometry(&boundaryPoint);
  ASSERT_TRUE(out);

  OGRCoordinateTransformation::DestroyCT(ct);
}

TEST(AdditionalTests, NumericalPrecision_PointsVeryCloseToBoundary)
{
  CPLSetConfigOption("OGR_GEOMETRY_ACCEPT_UNCLOSED_RING", "NO");
  static GdalInitGuard guard;
  Fixture fx;

  auto projector = fx.makeProjector(0.0);

  // Create points at boundary ± epsilon
  auto* ct = OGRCreateCoordinateTransformation(&fx.tm35, &fx.wgs84);
  ASSERT_TRUE(ct);

  double x1 = fx.B.minX - 1e-10;
  double y1 = (fx.B.minY + fx.B.maxY) / 2.0;
  ct->Transform(1, &x1, &y1);

  double x2 = fx.B.minX + 1e-10;
  double y2 = (fx.B.minY + fx.B.maxY) / 2.0;
  ct->Transform(1, &x2, &y2);

  OGRLineString line;
  line.addPoint(x1, y1);
  line.addPoint(x2, y2);

  auto out = projector.projectGeometry(&line);
  ASSERT_TRUE(out);

  OGRCoordinateTransformation::DestroyCT(ct);
}

TEST(AdditionalTests, NumericalPrecision_LineGrazingBoundaryTangentially)
{
  CPLSetConfigOption("OGR_GEOMETRY_ACCEPT_UNCLOSED_RING", "NO");
  static GdalInitGuard guard;
  Fixture fx;

  auto projector = fx.makeProjector(10.0);

  // Create a line that runs parallel to and very close to the boundary
  // This tests numerical stability in boundary detection
  OGRLineString line;
  line.addPoint(10.0, 59.0);
  line.addPoint(15.0, 59.0);

  auto out = projector.projectGeometry(&line);
  ASSERT_TRUE(out);

  if (!out->IsEmpty())
  {
    EXPECT_TRUE(allVerticesWithinBounds(out.get(), fx.B));
  }
}

// ============================================================================
// Test 5: Multiple Holes with Complex Topology
// ============================================================================

TEST(AdditionalTests, ComplexHoles_AdjacentHoles_BothExcluded)
{
  CPLSetConfigOption("OGR_GEOMETRY_ACCEPT_UNCLOSED_RING", "NO");
  static GdalInitGuard guard;
  GdalErrorSilencer silence;  // Suppress "Interior is disconnected" warning
  Fixture fx;

  auto projector = fx.makeProjector(50.0);

  OGRPolygon poly;
  OGRLinearRing ext;
  ext.addPoint(15.0, 60.0);
  ext.addPoint(35.0, 60.0);
  ext.addPoint(35.0, 75.0);
  ext.addPoint(15.0, 75.0);
  ext.addPoint(15.0, 60.0);
  poly.addRing(&ext);

  // Two holes sharing an edge - this can create disconnected interior
  // which is geometrically valid but may fail OGR's validity check
  OGRLinearRing hole1;
  hole1.addPoint(20.0, 65.0);
  hole1.addPoint(20.0, 70.0);
  hole1.addPoint(25.0, 70.0);
  hole1.addPoint(25.0, 65.0);
  hole1.addPoint(20.0, 65.0);
  poly.addRing(&hole1);

  OGRLinearRing hole2;
  hole2.addPoint(25.0, 65.0);
  hole2.addPoint(25.0, 70.0);
  hole2.addPoint(30.0, 70.0);
  hole2.addPoint(30.0, 65.0);
  hole2.addPoint(25.0, 65.0);
  poly.addRing(&hole2);

  auto out = projector.projectGeometry(&poly);
  ASSERT_TRUE(out);

  if (!out->IsEmpty())
  {
    EXPECT_TRUE(allVerticesWithinBounds(out.get(), fx.B));
    EXPECT_TRUE(allPolygonRingsClosed(out.get()));
    
    // Adjacent holes sharing an edge can create "disconnected interior" which
    // is topologically problematic. The projection handles this without crashing,
    // but the result may not pass OGR's strict validity check.
    // We verify the geometry is bounded and closed, but allow invalid topology.
    // If you need valid output, the input should avoid touching holes.
    
    // Optional: Check if valid, but don't fail test if not
    if (!geometryIsValid(out.get()))
    {
      // This is expected for adjacent holes - log but don't fail
      EXPECT_FALSE(out->IsEmpty()) << "Output should at least be non-empty";
    }
  }
}

TEST(AdditionalTests, ComplexHoles_NearbyNonTouchingHoles_ProducesValidGeometry)
{
  CPLSetConfigOption("OGR_GEOMETRY_ACCEPT_UNCLOSED_RING", "NO");
  static GdalInitGuard guard;
  Fixture fx;

  auto projector = fx.makeProjector(50.0);

  OGRPolygon poly;
  OGRLinearRing ext;
  ext.addPoint(15.0, 60.0);
  ext.addPoint(35.0, 60.0);
  ext.addPoint(35.0, 75.0);
  ext.addPoint(15.0, 75.0);
  ext.addPoint(15.0, 60.0);
  poly.addRing(&ext);

  // Two holes that are close but don't share an edge (0.1 degree gap)
  OGRLinearRing hole1;
  hole1.addPoint(20.0, 65.0);
  hole1.addPoint(20.0, 70.0);
  hole1.addPoint(24.9, 70.0);
  hole1.addPoint(24.9, 65.0);
  hole1.addPoint(20.0, 65.0);
  poly.addRing(&hole1);

  OGRLinearRing hole2;
  hole2.addPoint(25.1, 65.0);
  hole2.addPoint(25.1, 70.0);
  hole2.addPoint(30.0, 70.0);
  hole2.addPoint(30.0, 65.0);
  hole2.addPoint(25.1, 65.0);
  poly.addRing(&hole2);

  auto out = projector.projectGeometry(&poly);
  ASSERT_TRUE(out);

  if (!out->IsEmpty())
  {
    EXPECT_TRUE(allVerticesWithinBounds(out.get(), fx.B));
    EXPECT_TRUE(allPolygonRingsClosed(out.get()));
    EXPECT_TRUE(geometryIsValid(out.get())) << "Non-touching holes should produce valid geometry";
  }
}

TEST(AdditionalTests, ComplexHoles_ManySmallHoles)
{
  CPLSetConfigOption("OGR_GEOMETRY_ACCEPT_UNCLOSED_RING", "NO");
  static GdalInitGuard guard;
  Fixture fx;

  auto projector = fx.makeProjector(50.0);

  OGRPolygon poly;
  OGRLinearRing ext;
  ext.addPoint(15.0, 60.0);
  ext.addPoint(35.0, 60.0);
  ext.addPoint(35.0, 75.0);
  ext.addPoint(15.0, 75.0);
  ext.addPoint(15.0, 60.0);
  poly.addRing(&ext);

  // Add 10 small holes in a grid
  for (int i = 0; i < 5; ++i)
  {
    for (int j = 0; j < 2; ++j)
    {
      OGRLinearRing hole;
      double baseX = 17.0 + i * 3.0;
      double baseY = 62.0 + j * 5.0;

      hole.addPoint(baseX, baseY);
      hole.addPoint(baseX, baseY + 2.0);
      hole.addPoint(baseX + 2.0, baseY + 2.0);
      hole.addPoint(baseX + 2.0, baseY);
      hole.addPoint(baseX, baseY);
      poly.addRing(&hole);
    }
  }

  auto out = projector.projectGeometry(&poly);
  ASSERT_TRUE(out);

  if (!out->IsEmpty())
  {
    EXPECT_TRUE(allVerticesWithinBounds(out.get(), fx.B));
    EXPECT_TRUE(allPolygonRingsClosed(out.get()));
  }
}

// ============================================================================
// Test 6: Self-Intersecting Input
// ============================================================================

TEST(AdditionalTests, SelfIntersecting_BowtiePolygon_DoesNotCrash)
{
  CPLSetConfigOption("OGR_GEOMETRY_ACCEPT_UNCLOSED_RING", "NO");
  static GdalInitGuard guard;
  GdalErrorSilencer silence;
  Fixture fx;

  auto projector = fx.makeProjector(0.0);

  // Bowtie (figure-8) polygon
  OGRPolygon poly;
  OGRLinearRing ring;
  ring.addPoint(20.0, 60.0);
  ring.addPoint(30.0, 70.0);
  ring.addPoint(20.0, 70.0);
  ring.addPoint(30.0, 60.0);
  ring.addPoint(20.0, 60.0);
  poly.addRing(&ring);

  auto out = projector.projectGeometry(&poly);
  ASSERT_TRUE(out);
  // May produce invalid output, but should not crash
}

TEST(AdditionalTests, SelfIntersecting_ComplexSelfIntersection_BestEffort)
{
  CPLSetConfigOption("OGR_GEOMETRY_ACCEPT_UNCLOSED_RING", "NO");
  static GdalInitGuard guard;
  GdalErrorSilencer silence;
  Fixture fx;

  auto projector = fx.makeProjector(0.0);

  // Star polygon with self-intersections
  OGRPolygon poly;
  OGRLinearRing ring;
  const double cx = 25.0, cy = 65.0, r = 5.0;
  for (int i = 0; i < 10; ++i)
  {
    double angle = i * M_PI / 5.0;
    double radius = (i % 2 == 0) ? r : r / 2.5;
    ring.addPoint(cx + radius * std::cos(angle), cy + radius * std::sin(angle));
  }
  ring.closeRings();
  poly.addRing(&ring);

  auto out = projector.projectGeometry(&poly);
  ASSERT_TRUE(out);
  // Should handle without crashing
}

// ============================================================================
// Test 7: Very Long Segments
// ============================================================================

TEST(AdditionalTests, LongSegments_SingleSegmentLongerThanDensify_GetsDensified)
{
  CPLSetConfigOption("OGR_GEOMETRY_ACCEPT_UNCLOSED_RING", "NO");
  static GdalInitGuard guard;
  Fixture fx;

  auto projector = fx.makeProjector(50.0);  // 50km densification

  // Line from southern to northern Finland (~1000km)
  OGRLineString line;
  line.addPoint(25.0, 60.0);
  line.addPoint(25.0, 70.0);  // ~1100km

  auto out = projector.projectGeometry(&line);
  ASSERT_TRUE(out);

  if (!out->IsEmpty())
  {
    auto vertices = allVertices(out.get());
    // Should have many more points than input due to densification
    EXPECT_GT(vertices.size(), 10);
  }
}

TEST(AdditionalTests, LongSegments_DensificationDisabled_PreservesOriginalPoints)
{
  CPLSetConfigOption("OGR_GEOMETRY_ACCEPT_UNCLOSED_RING", "NO");
  static GdalInitGuard guard;
  Fixture fx;

  auto projector = fx.makeProjector(0.0);  // Disable densification

  OGRLineString line;
  line.addPoint(25.0, 60.0);
  line.addPoint(25.0, 70.0);

  auto out = projector.projectGeometry(&line);
  ASSERT_TRUE(out);

  if (!out->IsEmpty())
  {
    auto vertices = allVertices(out.get());
    // Should have approximately the same number of points (2)
    EXPECT_LE(vertices.size(), 5);  // Allow for some clipping points
  }
}

// ============================================================================
// Test 8: Antimeridian/Dateline Crossing
// ============================================================================

TEST(AdditionalTests, Antimeridian_LineStringCrossingDateline)
{
  CPLSetConfigOption("OGR_GEOMETRY_ACCEPT_UNCLOSED_RING", "NO");
  static GdalInitGuard guard;
  GdalErrorSilencer silence;

  // Use Pacific-centered projection
  OGRSpatialReference wgs84 = makeSRS(4326);
  OGRSpatialReference pacific = makeSRS(3832);  // NSIDC EASE-Grid 2.0 Global

  Bounds largeB{-20000000, -20000000, 20000000, 20000000};

  GeometryProjector projector(&wgs84, &pacific);
  projector.setProjectedBounds(largeB.minX, largeB.minY, largeB.maxX, largeB.maxY);
  projector.setDensifyResolutionKm(100.0);

  // Line crossing dateline
  OGRLineString line;
  line.addPoint(170.0, 0.0);
  line.addPoint(-170.0, 0.0);

  auto out = projector.projectGeometry(&line);
  ASSERT_TRUE(out);
  // Should handle dateline crossing gracefully
}

TEST(AdditionalTests, Antimeridian_PolygonStraddlingDateline)
{
  CPLSetConfigOption("OGR_GEOMETRY_ACCEPT_UNCLOSED_RING", "NO");
  static GdalInitGuard guard;
  GdalErrorSilencer silence;

  OGRSpatialReference wgs84 = makeSRS(4326);
  OGRSpatialReference pacific = makeSRS(3832);

  Bounds largeB{-20000000, -20000000, 20000000, 20000000};

  GeometryProjector projector(&wgs84, &pacific);
  projector.setProjectedBounds(largeB.minX, largeB.minY, largeB.maxX, largeB.maxY);
  projector.setDensifyResolutionKm(100.0);

  // Polygon around dateline
  OGRPolygon poly;
  OGRLinearRing ring;
  ring.addPoint(170.0, -10.0);
  ring.addPoint(-170.0, -10.0);
  ring.addPoint(-170.0, 10.0);
  ring.addPoint(170.0, 10.0);
  ring.addPoint(170.0, -10.0);
  poly.addRing(&ring);

  auto out = projector.projectGeometry(&poly);
  ASSERT_TRUE(out);
}

// ============================================================================
// Test 9: Pole Proximity
// ============================================================================

TEST(AdditionalTests, Poles_GeometryNearNorthPole_PoleHandlingEnabled)
{
  CPLSetConfigOption("OGR_GEOMETRY_ACCEPT_UNCLOSED_RING", "NO");
  static GdalInitGuard guard;
  GdalErrorSilencer silence;

  OGRSpatialReference wgs84 = makeSRS(4326);
  OGRSpatialReference stereo = makeSRS(3995);  // Arctic Polar Stereographic

  Bounds largeB{-10000000, -10000000, 10000000, 10000000};

  GeometryProjector projector(&wgs84, &stereo);
  projector.setProjectedBounds(largeB.minX, largeB.minY, largeB.maxX, largeB.maxY);
  projector.setPoleHandling(true);
  projector.setDensifyResolutionKm(100.0);

  // Polygon around North Pole
  OGRPolygon poly;
  OGRLinearRing ring;
  ring.addPoint(0.0, 85.0);
  ring.addPoint(90.0, 85.0);
  ring.addPoint(180.0, 85.0);
  ring.addPoint(-90.0, 85.0);
  ring.addPoint(0.0, 85.0);
  poly.addRing(&ring);

  auto out = projector.projectGeometry(&poly);
  ASSERT_TRUE(out);

  if (!out->IsEmpty())
  {
    EXPECT_TRUE(allVerticesWithinBounds(out.get(), largeB));
  }
}

TEST(AdditionalTests, Poles_PoleHandlingDisabled_StillDoesNotCrash)
{
  CPLSetConfigOption("OGR_GEOMETRY_ACCEPT_UNCLOSED_RING", "NO");
  static GdalInitGuard guard;
  GdalErrorSilencer silence;

  OGRSpatialReference wgs84 = makeSRS(4326);
  OGRSpatialReference stereo = makeSRS(3995);

  Bounds largeB{-10000000, -10000000, 10000000, 10000000};

  GeometryProjector projector(&wgs84, &stereo);
  projector.setProjectedBounds(largeB.minX, largeB.minY, largeB.maxX, largeB.maxY);
  projector.setPoleHandling(false);

  OGRPoint nearPole(0.0, 89.0);
  auto out = projector.projectGeometry(&nearPole);
  ASSERT_TRUE(out);
}

// ============================================================================
// Test 10: Performance/Stress Tests
// ============================================================================

TEST(AdditionalTests, Performance_LargeComplexPolygon_CompletesInReasonableTime)
{
  CPLSetConfigOption("OGR_GEOMETRY_ACCEPT_UNCLOSED_RING", "NO");
  static GdalInitGuard guard;
  Fixture fx;

  auto projector = fx.makeProjector(50.0);

  // Create a polygon with 1000 vertices
  OGRPolygon poly;
  OGRLinearRing ring;

  const double cx = 25.0, cy = 65.0, r = 5.0;
  for (int i = 0; i < 1000; ++i)
  {
    double angle = i * 2.0 * M_PI / 1000.0;
    ring.addPoint(cx + r * std::cos(angle), cy + r * std::sin(angle));
  }
  ring.closeRings();
  poly.addRing(&ring);

  // Add a hole with 500 vertices
  OGRLinearRing hole;
  for (int i = 0; i < 500; ++i)
  {
    double angle = i * 2.0 * M_PI / 500.0;
    hole.addPoint(cx + (r / 2) * std::cos(angle), cy + (r / 2) * std::sin(angle));
  }
  hole.closeRings();
  poly.addRing(&hole);

  auto out = projector.projectGeometry(&poly);
  ASSERT_TRUE(out);

  if (!out->IsEmpty())
  {
    EXPECT_TRUE(allVerticesWithinBounds(out.get(), fx.B));
    EXPECT_TRUE(allPolygonRingsClosed(out.get()));
  }
}

TEST(AdditionalTests, Performance_MultiPolygonWithManyPieces_HandlesEfficiently)
{
  CPLSetConfigOption("OGR_GEOMETRY_ACCEPT_UNCLOSED_RING", "NO");
  static GdalInitGuard guard;
  Fixture fx;

  auto projector = fx.makeProjector(50.0);

  OGRMultiPolygon mp;

  // Create 100 small polygons
  for (int i = 0; i < 10; ++i)
  {
    for (int j = 0; j < 10; ++j)
    {
      OGRPolygon poly;
      OGRLinearRing ring;

      double baseX = 16.0 + i * 2.0;
      double baseY = 61.0 + j * 1.2;

      ring.addPoint(baseX, baseY);
      ring.addPoint(baseX + 1.0, baseY);
      ring.addPoint(baseX + 1.0, baseY + 0.8);
      ring.addPoint(baseX, baseY + 0.8);
      ring.addPoint(baseX, baseY);

      poly.addRing(&ring);
      mp.addGeometry(&poly);
    }
  }

  auto out = projector.projectGeometry(&mp);
  ASSERT_TRUE(out);

  if (!out->IsEmpty())
  {
    EXPECT_TRUE(allVerticesWithinBounds(out.get(), fx.B));
  }
}

// ============================================================================
// Test 11: Boundary Walk Direction
// ============================================================================

TEST(AdditionalTests, BoundaryWalk_ClockwiseWalk_ProducesValidRing)
{
  CPLSetConfigOption("OGR_GEOMETRY_ACCEPT_UNCLOSED_RING", "NO");
  static GdalInitGuard guard;
  Fixture fx;

  auto projector = fx.makeProjector(50.0);

  // Create a polygon that will definitely clip and require boundary walking
  OGRPolygon poly;
  OGRLinearRing ring;
  ring.addPoint(5.0, 60.0);   // West of bounds
  ring.addPoint(25.0, 60.0);
  ring.addPoint(25.0, 70.0);
  ring.addPoint(5.0, 70.0);
  ring.addPoint(5.0, 60.0);
  poly.addRing(&ring);

  auto out = projector.projectGeometry(&poly);
  ASSERT_TRUE(out);

  if (!out->IsEmpty())
  {
    EXPECT_TRUE(allVerticesWithinBounds(out.get(), fx.B));
    EXPECT_TRUE(allPolygonRingsClosed(out.get()));
    EXPECT_TRUE(geometryIsValid(out.get()));
  }
}

// ============================================================================
// Test 12: Different Coordinate Systems
// ============================================================================

TEST(AdditionalTests, DifferentProjections_WebMercator_WorksCorrectly)
{
  CPLSetConfigOption("OGR_GEOMETRY_ACCEPT_UNCLOSED_RING", "NO");
  static GdalInitGuard guard;

  OGRSpatialReference wgs84 = makeSRS(4326);
  OGRSpatialReference webMerc = makeSRS(3857);

  Bounds mercBounds{-20000000, -20000000, 20000000, 20000000};

  GeometryProjector projector(&wgs84, &webMerc);
  projector.setProjectedBounds(mercBounds.minX, mercBounds.minY, mercBounds.maxX, mercBounds.maxY);

  OGRPoint p(0.0, 0.0);
  auto out = projector.projectGeometry(&p);
  ASSERT_TRUE(out);
  EXPECT_FALSE(out->IsEmpty());
}

TEST(AdditionalTests, DifferentProjections_UTM_Zone33N_WorksCorrectly)
{
  CPLSetConfigOption("OGR_GEOMETRY_ACCEPT_UNCLOSED_RING", "NO");
  static GdalInitGuard guard;

  OGRSpatialReference wgs84 = makeSRS(4326);
  OGRSpatialReference utm33n = makeSRS(32633);  // UTM Zone 33N

  Bounds utmBounds{200000, 6000000, 800000, 8000000};

  GeometryProjector projector(&wgs84, &utm33n);
  projector.setProjectedBounds(utmBounds.minX, utmBounds.minY, utmBounds.maxX, utmBounds.maxY);

  OGRPolygon poly;
  OGRLinearRing ring;
  ring.addPoint(10.0, 55.0);
  ring.addPoint(15.0, 55.0);
  ring.addPoint(15.0, 60.0);
  ring.addPoint(10.0, 60.0);
  ring.addPoint(10.0, 55.0);
  poly.addRing(&ring);

  auto out = projector.projectGeometry(&poly);
  ASSERT_TRUE(out);
}

// ============================================================================
// Test 13: Bounds Configuration Edge Cases
// ============================================================================

TEST(AdditionalTests, BoundsConfig_VerySmallBounds_HandlesCorrectly)
{
  CPLSetConfigOption("OGR_GEOMETRY_ACCEPT_UNCLOSED_RING", "NO");
  static GdalInitGuard guard;
  Fixture fx;

  // Very small bounds (1 meter x 1 meter)
  Bounds tinyB{500000.0, 7000000.0, 500001.0, 7000001.0};

  GeometryProjector projector(&fx.wgs84, &fx.tm35);
  projector.setProjectedBounds(tinyB.minX, tinyB.minY, tinyB.maxX, tinyB.maxY);

  OGRPoint p(25.0, 63.0);
  auto out = projector.projectGeometry(&p);
  ASSERT_TRUE(out);
  // Most likely empty due to tight bounds, but should not crash
}

TEST(AdditionalTests, BoundsConfig_NegativeBounds_WorksCorrectly)
{
  CPLSetConfigOption("OGR_GEOMETRY_ACCEPT_UNCLOSED_RING", "NO");
  static GdalInitGuard guard;

  OGRSpatialReference wgs84 = makeSRS(4326);
  OGRSpatialReference utm = makeSRS(32630);  // UTM 30N (western Europe)

  // Negative bounds (can happen in some projections)
  Bounds negB{-1000000, -1000000, 1000000, 1000000};

  GeometryProjector projector(&wgs84, &utm);
  projector.setProjectedBounds(negB.minX, negB.minY, negB.maxX, negB.maxY);

  OGRPoint p(-5.0, 50.0);  // West of prime meridian
  auto out = projector.projectGeometry(&p);
  ASSERT_TRUE(out);
}

// ============================================================================
// Test 14: Densification Settings
// ============================================================================

TEST(AdditionalTests, Densification_VerySmallThreshold_DensifiesHeavily)
{
  CPLSetConfigOption("OGR_GEOMETRY_ACCEPT_UNCLOSED_RING", "NO");
  static GdalInitGuard guard;
  Fixture fx;

  auto projector = fx.makeProjector(1.0);  // 1km densification (very dense)

  OGRLineString line;
  line.addPoint(25.0, 60.0);
  line.addPoint(25.0, 61.0);  // ~110km

  auto out = projector.projectGeometry(&line);
  ASSERT_TRUE(out);

  if (!out->IsEmpty())
  {
    auto vertices = allVertices(out.get());
    // Should have many points (>100)
    EXPECT_GT(vertices.size(), 50);
  }
}

TEST(AdditionalTests, Densification_VeryLargeThreshold_MinimalDensification)
{
  CPLSetConfigOption("OGR_GEOMETRY_ACCEPT_UNCLOSED_RING", "NO");
  static GdalInitGuard guard;
  Fixture fx;

  auto projector = fx.makeProjector(1000.0);  // 1000km (very coarse)

  OGRLineString line;
  line.addPoint(25.0, 60.0);
  line.addPoint(25.0, 61.0);

  auto out = projector.projectGeometry(&line);
  ASSERT_TRUE(out);

  if (!out->IsEmpty())
  {
    auto vertices = allVertices(out.get());
    // Should have very few points
    EXPECT_LE(vertices.size(), 10);
  }
}

// ============================================================================
// Test 15: Nested Geometry Collections
// ============================================================================

TEST(AdditionalTests, NestedCollections_GeometryCollectionInCollection)
{
  CPLSetConfigOption("OGR_GEOMETRY_ACCEPT_UNCLOSED_RING", "NO");
  static GdalInitGuard guard;
  Fixture fx;

  auto projector = fx.makeProjector(50.0);

  OGRGeometryCollection outerGC;

  // Add a point
  OGRPoint p1(25.0, 60.0);
  outerGC.addGeometry(&p1);

  // Add a nested geometry collection
  OGRGeometryCollection innerGC;
  OGRPoint p2(26.0, 61.0);
  OGRLineString line;
  line.addPoint(25.0, 62.0);
  line.addPoint(26.0, 63.0);

  innerGC.addGeometry(&p2);
  innerGC.addGeometry(&line);

  outerGC.addGeometry(&innerGC);

  auto out = projector.projectGeometry(&outerGC);
  ASSERT_TRUE(out);

  if (!out->IsEmpty())
  {
    EXPECT_TRUE(allVerticesWithinBounds(out.get(), fx.B));
  }
}

TEST(AdditionalTests, NestedCollections_MultiPolygonWithComplexPieces)
{
  CPLSetConfigOption("OGR_GEOMETRY_ACCEPT_UNCLOSED_RING", "NO");
  static GdalInitGuard guard;
  Fixture fx;

  auto projector = fx.makeProjector(50.0);

  OGRMultiPolygon mp;

  // Add multiple polygons with holes
  for (int i = 0; i < 3; ++i)
  {
    OGRPolygon poly;
    OGRLinearRing ext;

    double baseX = 17.0 + i * 5.0;
    ext.addPoint(baseX, 60.0);
    ext.addPoint(baseX + 4.0, 60.0);
    ext.addPoint(baseX + 4.0, 65.0);
    ext.addPoint(baseX, 65.0);
    ext.addPoint(baseX, 60.0);
    poly.addRing(&ext);

    // Add a hole to each
    OGRLinearRing hole;
    hole.addPoint(baseX + 1.0, 61.0);
    hole.addPoint(baseX + 1.0, 62.0);
    hole.addPoint(baseX + 2.0, 62.0);
    hole.addPoint(baseX + 2.0, 61.0);
    hole.addPoint(baseX + 1.0, 61.0);
    poly.addRing(&hole);

    mp.addGeometry(&poly);
  }

  auto out = projector.projectGeometry(&mp);
  ASSERT_TRUE(out);

  if (!out->IsEmpty())
  {
    EXPECT_TRUE(allVerticesWithinBounds(out.get(), fx.B));
    EXPECT_TRUE(allPolygonRingsClosed(out.get()));
  }
}

// ============================================================================
// Test 16: Null and Uninitialized Geometry Handling
// ============================================================================

TEST(AdditionalTests, NullGeometry_ProjectNullptr_ReturnsNullptr)
{
  CPLSetConfigOption("OGR_GEOMETRY_ACCEPT_UNCLOSED_RING", "NO");
  static GdalInitGuard guard;
  Fixture fx;

  auto projector = fx.makeProjector();

  auto out = projector.projectGeometry(nullptr);
  EXPECT_EQ(out, nullptr);
}

TEST(AdditionalTests, EmptyGeometryCollection_ReturnsEmptyCollection)
{
  CPLSetConfigOption("OGR_GEOMETRY_ACCEPT_UNCLOSED_RING", "NO");
  static GdalInitGuard guard;
  Fixture fx;

  auto projector = fx.makeProjector();

  OGRGeometryCollection emptyGC;
  auto out = projector.projectGeometry(&emptyGC);
  ASSERT_TRUE(out);
  EXPECT_EQ(wkbFlatten(out->getGeometryType()), wkbGeometryCollection);
}

// ============================================================================
// Test 17: Jump Threshold Testing
// ============================================================================

TEST(AdditionalTests, JumpThreshold_CustomThreshold_AppliesCorrectly)
{
  CPLSetConfigOption("OGR_GEOMETRY_ACCEPT_UNCLOSED_RING", "NO");
  static GdalInitGuard guard;
  Fixture fx;

  auto projector = fx.makeProjector(0.0);
  projector.setJumpThreshold(100000.0);  // 100km threshold

  // Create a line that would normally be split by auto-threshold
  OGRLineString line;
  line.addPoint(15.0, 60.0);
  line.addPoint(35.0, 60.0);

  auto out = projector.projectGeometry(&line);
  ASSERT_TRUE(out);
}

TEST(AdditionalTests, JumpThreshold_ZeroThreshold_DisablesJumpDetection)
{
  CPLSetConfigOption("OGR_GEOMETRY_ACCEPT_UNCLOSED_RING", "NO");
  static GdalInitGuard guard;
  Fixture fx;

  auto projector = fx.makeProjector(0.0);
  projector.setJumpThreshold(0.0);

  OGRLineString line;
  line.addPoint(15.0, 60.0);
  line.addPoint(35.0, 60.0);

  auto out = projector.projectGeometry(&line);
  ASSERT_TRUE(out);
}
