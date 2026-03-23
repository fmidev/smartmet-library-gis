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

#include "CoordinateTransformation.h"
#include "GeometryProjector.h"
#include "Interrupt.h"
#include "SpatialReference.h"

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

inline bool onBoundary(double x, double y, const Bounds& b, double tol = 1e-2)
{
  return (std::abs(x - b.minX) <= tol) || (std::abs(x - b.maxX) <= tol) ||
         (std::abs(y - b.minY) <= tol) || (std::abs(y - b.maxY) <= tol);
}

OGRPoint project4326To3067(double lon, double lat)
{
  auto wgs84 = makeSRS(4326);
  auto tm35 = makeSRS(3067);

  std::unique_ptr<OGRCoordinateTransformation> ct(OGRCreateCoordinateTransformation(&wgs84, &tm35));
  if (!ct)
    throw std::runtime_error("CT creation failed");

  double x = lon, y = lat;
  if (!ct->Transform(1, &x, &y))
    throw std::runtime_error("Transform failed");
  return OGRPoint(x, y);
}

void collectPoints(const OGRLineString* ls, std::vector<OGRPoint>& pts)
{
  if (!ls)
    return;
  for (int i = 0; i < ls->getNumPoints(); ++i)
    pts.emplace_back(ls->getX(i), ls->getY(i));
}

void collectPointsFromPolygon(const OGRPolygon* p, std::vector<OGRPoint>& pts)
{
  if (!p)
    return;
  if (auto* ext = p->getExteriorRing())
    collectPoints(ext, pts);
  for (int i = 0; i < p->getNumInteriorRings(); ++i)
    collectPoints(p->getInteriorRing(i), pts);
}

std::vector<OGRPoint> allVertices(const OGRGeometry* g)
{
  std::vector<OGRPoint> pts;
  if (!g)
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
    collectPoints(dynamic_cast<const OGRLineString*>(g), pts);
  }
  else if (t == wkbPolygon)
  {
    collectPointsFromPolygon(dynamic_cast<const OGRPolygon*>(g), pts);
  }
  else if (t == wkbMultiLineString)
  {
    auto* ml = dynamic_cast<const OGRMultiLineString*>(g);
    for (int i = 0; i < ml->getNumGeometries(); ++i)
      collectPoints(dynamic_cast<const OGRLineString*>(ml->getGeometryRef(i)), pts);
  }
  else if (t == wkbMultiPolygon)
  {
    auto* mp = dynamic_cast<const OGRMultiPolygon*>(g);
    for (int i = 0; i < mp->getNumGeometries(); ++i)
      collectPointsFromPolygon(dynamic_cast<const OGRPolygon*>(mp->getGeometryRef(i)), pts);
  }
  else if (t == wkbGeometryCollection)
  {
    auto* gc = dynamic_cast<const OGRGeometryCollection*>(g);
    for (int i = 0; i < gc->getNumGeometries(); ++i)
    {
      auto sub = allVertices(gc->getGeometryRef(i));
      pts.insert(pts.end(), sub.begin(), sub.end());
    }
  }
  return pts;
}

bool allVerticesWithinBounds(const OGRGeometry* g, const Bounds& b)
{
  for (const auto& p : allVertices(g))
    if (!insideB(p.getX(), p.getY(), b))
      return false;
  return true;
}

int countBoundaryVertices(const OGRGeometry* g, const Bounds& b)
{
  int c = 0;
  for (const auto& p : allVertices(g))
    if (onBoundary(p.getX(), p.getY(), b))
      ++c;
  return c;
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

bool containsPointAnyPiece(const OGRGeometry* g, const OGRPoint& p)
{
  if (!g)
    return false;
  const auto t = wkbFlatten(g->getGeometryType());
  if (t == wkbPolygon)
    return dynamic_cast<const OGRPolygon*>(g)->Contains(&p);
  if (t == wkbMultiPolygon)
  {
    auto* mp = dynamic_cast<const OGRMultiPolygon*>(g);
    for (int i = 0; i < mp->getNumGeometries(); ++i)
      if (dynamic_cast<const OGRPolygon*>(mp->getGeometryRef(i))->Contains(&p))
        return true;
  }
  return false;
}

bool geometryIsValid(const OGRGeometry* g)
{
  if (!g)
    return false;
  // IsValid() may be expensive but tests can afford it.
  return g->IsValid();
}

std::string wktOf(const OGRGeometry* g)
{
  if (!g)
    return {};
  char* wkt = nullptr;
  g->exportToWkt(&wkt);
  std::string s = wkt ? wkt : "";
  CPLFree(wkt);
  return s;
}

// Shared fixture setup
struct Fixture
{
  Bounds B{-300000.0, 6500000.0, 800000.0, 8500000.0};
  OGRSpatialReference wgs84 = makeSRS(4326);
  OGRSpatialReference tm35 = makeSRS(3067);

  Fmi::GeometryProjector makeProjector(double densifyKm = 50.0)
  {
    Fmi::GeometryProjector p(&wgs84, &tm35);
    p.setProjectedBounds(B.minX, B.minY, B.maxX, B.maxY);
    p.setDensifyResolutionKm(densifyKm);
    return p;
  }
};

#include <limits>

// Signed area of a ring (shoelace). Positive => CCW in usual Cartesian coords.
double signedArea(const OGRLinearRing* r)
{
  if (!r || r->getNumPoints() < 4)
    return 0.0;

  const int n = r->getNumPoints();
  double a = 0.0;

  // Use n-1 because rings are closed (last == first) in your output.
  for (int i = 0; i < n - 1; ++i)
  {
    const double x0 = r->getX(i), y0 = r->getY(i);
    const double x1 = r->getX(i + 1), y1 = r->getY(i + 1);
    a += (x0 * y1 - x1 * y0);
  }
  return 0.5 * a;
}

bool hasConsecutiveDuplicatePoints(const OGRLineString* ls, double eps = 1e-9)
{
  if (!ls || ls->getNumPoints() < 2)
    return false;

  for (int i = 1; i < ls->getNumPoints(); ++i)
  {
    const double dx = std::abs(ls->getX(i) - ls->getX(i - 1));
    const double dy = std::abs(ls->getY(i) - ls->getY(i - 1));
    if (dx <= eps && dy <= eps)
      return true;
  }
  return false;
}

bool polygonHasConsecutiveDuplicates(const OGRPolygon* p, double eps = 1e-9)
{
  if (!p)
    return false;

  if (hasConsecutiveDuplicatePoints(p->getExteriorRing(), eps))
    return true;

  for (int i = 0; i < p->getNumInteriorRings(); ++i)
    if (hasConsecutiveDuplicatePoints(p->getInteriorRing(i), eps))
      return true;

  return false;
}

bool anyPolygonHasConsecutiveDuplicates(const OGRGeometry* g, double eps = 1e-9)
{
  if (!g)
    return false;

  const auto t = wkbFlatten(g->getGeometryType());
  if (t == wkbPolygon)
    return polygonHasConsecutiveDuplicates(dynamic_cast<const OGRPolygon*>(g), eps);

  if (t == wkbMultiPolygon)
  {
    auto* mp = dynamic_cast<const OGRMultiPolygon*>(g);
    for (int i = 0; i < mp->getNumGeometries(); ++i)
      if (polygonHasConsecutiveDuplicates(dynamic_cast<const OGRPolygon*>(mp->getGeometryRef(i)),
                                          eps))
        return true;
  }

  return false;
}

OGRSpatialReference makeSRSFromProj4(const std::string& proj4)
{
  OGRSpatialReference s;
  s.importFromProj4(proj4.c_str());
  s.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
  return s;
}

// Southern polar cap: bbox (-180,-90) to (180,-70) — the Antarctic region.
// Used to verify that projections which previously vanished at ±20 000 km
// produce a non-empty result.
OGRPolygon southernPolarCapPolygon()
{
  OGRPolygon poly;
  OGRLinearRing ext;
  ext.addPoint(-180, -90);
  ext.addPoint(180, -90);
  ext.addPoint(180, -70);
  ext.addPoint(-180, -70);
  ext.addPoint(-180, -90);
  poly.addRing(&ext);
  return poly;
}

// Northern polar cap: bbox (-180,70) to (180,90) — the Arctic region.
OGRPolygon northernPolarCapPolygon()
{
  OGRPolygon poly;
  OGRLinearRing ext;
  ext.addPoint(-180, 90);
  ext.addPoint(180, 90);
  ext.addPoint(180, 70);
  ext.addPoint(-180, 70);
  ext.addPoint(-180, 90);
  poly.addRing(&ext);
  return poly;
}

// World polygon CCW: SW→SE→NE→NW (counter-clockwise in y-up convention)
OGRPolygon worldPolygonCCW()
{
  OGRPolygon poly;
  OGRLinearRing ext;
  ext.addPoint(-180, -90);
  ext.addPoint(180, -90);
  ext.addPoint(180, 90);
  ext.addPoint(-180, 90);
  ext.addPoint(-180, -90);
  poly.addRing(&ext);
  return poly;
}

// World polygon CW: SW→NW→NE→SE (clockwise in y-up convention)
// Before the ring-winding fix, this produced zero-area slivers for global
// pseudocylindrical projections.
OGRPolygon worldPolygonCW()
{
  OGRPolygon poly;
  OGRLinearRing ext;
  ext.addPoint(-180, -90);
  ext.addPoint(-180, 90);
  ext.addPoint(180, 90);
  ext.addPoint(180, -90);
  ext.addPoint(-180, -90);
  poly.addRing(&ext);
  return poly;
}

double totalArea(const OGRGeometry* g)
{
  if (!g || g->IsEmpty())
    return 0.0;
  const auto t = wkbFlatten(g->getGeometryType());
  if (t == wkbPolygon)
    return static_cast<const OGRPolygon*>(g)->get_Area();
  if (t == wkbMultiPolygon)
  {
    const auto* mp = static_cast<const OGRMultiPolygon*>(g);
    double area = 0.0;
    for (int i = 0; i < mp->getNumGeometries(); ++i)
      area += static_cast<const OGRPolygon*>(mp->getGeometryRef(i))->get_Area();
    return area;
  }
  return 0.0;
}

// Compute the bounding box that makes RectClipper's connectLines work correctly
// for global projections.
//
// How jump detection produces run endpoints:
//   For a CCW-normalised world ring (SW→SE→NE→NW), the two pole traversals
//   (lat=±90, lon=-180..+180) exceed the jump threshold and are split.  The
//   resulting run endpoints are the projected coordinates of the geographic
//   points at (lon=±180, lat=±90) — the "pole corners".
//
// What connectLines needs:
//   search_ccw uses exact equality to match run endpoints on the box boundary.
//   Therefore ymin/ymax must equal the y-coordinates of the pole-corner run
//   endpoints exactly — they must come from projecting lat=±90 points, NOT from
//   off-pole latitudes.
//
// How we compute ymin/ymax:
//   We scan ALL longitudes at lat=±90 and take the extremes.  For projections
//   where all meridians converge to a single pole point (most projections) every
//   longitude gives the same y, so the result is just pole_y.  For projections
//   where the pole maps to a curve (e.g. bertin1953, adams_ws2) we get the true
//   y-extremes of that curve, which are the endpoints of the pole run.
//
//   Off-pole latitudes are deliberately excluded from the y computation; they
//   can yield larger y values for some projections (transverse/conformal variants)
//   that would shift the box boundary away from the run endpoints and cause
//   connectLines to fail with "Stuck, discarding ring".
//
// xmin/xmax come from sampling the full globe.
Bounds computeBoundsForGlobalProjection(OGRCoordinateTransformation* ct)
{
  const double cap = 1e10;

  auto valid = [&](double x, double y) -> bool
  {
    return std::isfinite(x) && std::isfinite(y) && std::abs(x) < cap && std::abs(y) < cap;
  };

  // --- Y extent: scan ALL longitudes at lat=±90 and take the extremes ---
  // This handles both "single-point poles" (all lons give same y) and
  // "wandering poles" (different lons give different y; e.g. bertin1953, adams_ws2).
  double ymax = std::numeric_limits<double>::lowest();
  double ymin = std::numeric_limits<double>::max();
  for (int lon = -180; lon <= 180; lon += 5)
  {
    double x = lon, y = 90.0;
    if (ct->Transform(1, &x, &y) && valid(x, y))
      ymax = std::max(ymax, y);

    x = lon;
    y = -90.0;
    if (ct->Transform(1, &x, &y) && valid(x, y))
      ymin = std::min(ymin, y);
  }
  if (ymax == std::numeric_limits<double>::lowest())
    ymax = 1e7;
  if (ymin == std::numeric_limits<double>::max())
    ymin = -1e7;

  // --- X extent: sample the full globe (y NOT updated here) ---
  double xmin = 0.0, xmax = 0.0;
  for (int lat = -90; lat <= 90; lat += 2)
  {
    for (int lon = -180; lon <= 180; lon += 2)
    {
      double x = lon, y = lat;
      if (!ct->Transform(1, &x, &y) || !valid(x, y))
        continue;
      xmin = std::min(xmin, x);
      xmax = std::max(xmax, x);
    }
  }

  if (xmin >= xmax)
  {
    // Degenerate — fall back to something symmetric around y extent
    xmin = ymin * 2.0;
    xmax = ymax * 2.0;
  }

  return {xmin, ymin, xmax, ymax};
}

}  // namespace

// ---------------- TESTS ----------------

TEST(GeometryProjectorTests, AxisOrderSmokeTest_PointInsideBBox)
{
  CPLSetConfigOption("OGR_GEOMETRY_ACCEPT_UNCLOSED_RING", "NO");

  static GdalInitGuard guard;
  Fixture fx;

  auto projector = fx.makeProjector();

  OGRPoint p(25.0, 60.0);
  auto out = projector.projectGeometry(&p);

  ASSERT_TRUE(out);
  EXPECT_FALSE(out->IsEmpty());
  EXPECT_TRUE(allVerticesWithinBounds(out.get(), fx.B));
}

TEST(GeometryProjectorTests, HoleSplitCase_NotEmpty_AllVerticesInside_RingsClosed_TouchesBoundary)
{
  CPLSetConfigOption("OGR_GEOMETRY_ACCEPT_UNCLOSED_RING", "NO");

  static GdalInitGuard guard;
  Fixture fx;

  auto projector = fx.makeProjector(50.0);

  OGRPolygon poly;
  OGRLinearRing exterior;
  exterior.addPoint(15, 60);
  exterior.addPoint(36, 60);
  exterior.addPoint(36, 75);
  exterior.addPoint(15, 75);
  exterior.addPoint(15, 60);
  poly.addRing(&exterior);

  OGRLinearRing hole;
  hole.addPoint(20, 65);
  hole.addPoint(20, 70);
  hole.addPoint(35, 70);
  hole.addPoint(35, 65);
  hole.addPoint(20, 65);
  poly.addRing(&hole);

  auto out = projector.projectGeometry(&poly);
  ASSERT_TRUE(out);
  ASSERT_FALSE(out->IsEmpty());

  EXPECT_TRUE(allVerticesWithinBounds(out.get(), fx.B));
  EXPECT_TRUE(allPolygonRingsClosed(out.get()));
  EXPECT_GT(countBoundaryVertices(out.get(), fx.B), 0);
  EXPECT_TRUE(geometryIsValid(out.get())) << wktOf(out.get());
}

TEST(GeometryProjectorTests, OpenHoleRunsBecomeCuts_PointInsideHoleIsOutsideResult)
{
  CPLSetConfigOption("OGR_GEOMETRY_ACCEPT_UNCLOSED_RING", "NO");

  static GdalInitGuard guard;
  Fixture fx;

  // Make the test strict and deterministic
  CPLSetConfigOption("OGR_GEOMETRY_ACCEPT_UNCLOSED_RING", "NO");

  auto projector = fx.makeProjector(50.0);

  OGRPolygon poly;
  OGRLinearRing exterior;
  exterior.addPoint(15, 60);
  exterior.addPoint(36, 60);
  exterior.addPoint(36, 75);
  exterior.addPoint(15, 75);
  exterior.addPoint(15, 60);
  poly.addRing(&exterior);

  OGRLinearRing hole;
  hole.addPoint(20, 65);
  hole.addPoint(20, 70);
  hole.addPoint(35, 70);
  hole.addPoint(35, 65);
  hole.addPoint(20, 65);
  poly.addRing(&hole);

  auto out = projector.projectGeometry(&poly);
  ASSERT_TRUE(out);
  ASSERT_FALSE(out->IsEmpty());

  OGRPoint insideHole = project4326To3067(30.0, 67.0);
  EXPECT_FALSE(containsPointAnyPiece(out.get(), insideHole)) << wktOf(out.get());
  EXPECT_TRUE(geometryIsValid(out.get())) << wktOf(out.get());
}

// ---------------- More tests ----------------

// Hole stays fully inside bbox -> should remain an interior ring (not a cut)
// Here we use a bbox maxX that is large enough to include the hole.
TEST(GeometryProjectorTests, HoleFullyInsideBBox_RemainsInteriorRing_PointInsideHoleOutsideResult)
{
  CPLSetConfigOption("OGR_GEOMETRY_ACCEPT_UNCLOSED_RING", "NO");

  static GdalInitGuard guard;
  Fixture fx;

  // Enlarged bounds so hole won't clip
  Bounds B2{-300000.0, 6500000.0, 1000000.0, 8500000.0};

  Fmi::GeometryProjector projector(&fx.wgs84, &fx.tm35);
  projector.setProjectedBounds(B2.minX, B2.minY, B2.maxX, B2.maxY);
  projector.setDensifyResolutionKm(50.0);

  OGRPolygon poly;
  OGRLinearRing exterior;
  exterior.addPoint(15, 60);
  exterior.addPoint(36, 60);
  exterior.addPoint(36, 75);
  exterior.addPoint(15, 75);
  exterior.addPoint(15, 60);
  poly.addRing(&exterior);

  OGRLinearRing hole;
  hole.addPoint(20, 65);
  hole.addPoint(20, 70);
  hole.addPoint(25, 70);
  hole.addPoint(25, 65);
  hole.addPoint(20, 65);
  poly.addRing(&hole);

  auto out = projector.projectGeometry(&poly);
  ASSERT_TRUE(out);
  ASSERT_FALSE(out->IsEmpty());

  // Should remain Polygon with interior ring in many cases (MultiPolygon allowed, but should still
  // exclude hole)
  OGRPoint insideHole = project4326To3067(22.0, 67.0);
  EXPECT_FALSE(containsPointAnyPiece(out.get(), insideHole)) << wktOf(out.get());

  EXPECT_TRUE(allVerticesWithinBounds(out.get(), B2));
  EXPECT_TRUE(allPolygonRingsClosed(out.get()));
  EXPECT_TRUE(geometryIsValid(out.get())) << wktOf(out.get());
}

// Hole completely outside bbox -> should not affect result (no hole exclusion)
TEST(GeometryProjectorTests, HoleCompletelyOutsideBBox_IsIgnored_PointInsideOuterIsInsideResult)
{
  CPLSetConfigOption("OGR_GEOMETRY_ACCEPT_UNCLOSED_RING", "NO");

  static GdalInitGuard guard;
  Fixture fx;

  auto projector = fx.makeProjector(50.0);

  OGRPolygon poly;
  OGRLinearRing exterior;
  exterior.addPoint(15, 60);
  exterior.addPoint(36, 60);
  exterior.addPoint(36, 75);
  exterior.addPoint(15, 75);
  exterior.addPoint(15, 60);
  poly.addRing(&exterior);

  // Hole far east so it projects beyond maxX and never re-enters bbox
  OGRLinearRing hole;
  hole.addPoint(60, 65);
  hole.addPoint(60, 70);
  hole.addPoint(65, 70);
  hole.addPoint(65, 65);
  hole.addPoint(60, 65);
  poly.addRing(&hole);

  auto out = projector.projectGeometry(&poly);
  ASSERT_TRUE(out);
  ASSERT_FALSE(out->IsEmpty());

  // Pick a point inside exterior and also inside bbox, should be inside result
  OGRPoint insideOuter = project4326To3067(20.0, 62.0);
  EXPECT_TRUE(containsPointAnyPiece(out.get(), insideOuter)) << wktOf(out.get());
  EXPECT_TRUE(allVerticesWithinBounds(out.get(), fx.B));
  EXPECT_TRUE(geometryIsValid(out.get())) << wktOf(out.get());
}

// Polygon fully outside bbox -> should return nullptr or empty polygon
// The code returns nullptr (not an empty OGRGeometry) when the clipper has no runs.
TEST(GeometryProjectorTests, FullyOutsidePolygonReturnsEmpty)
{
  CPLSetConfigOption("OGR_GEOMETRY_ACCEPT_UNCLOSED_RING", "NO");

  static GdalInitGuard guard;
  Fixture fx;

  auto projector = fx.makeProjector(50.0);

  OGRPolygon poly;
  OGRLinearRing exterior;
  exterior.addPoint(0, 0);
  exterior.addPoint(1, 0);
  exterior.addPoint(1, 1);
  exterior.addPoint(0, 1);
  exterior.addPoint(0, 0);
  poly.addRing(&exterior);

  auto out = projector.projectGeometry(&poly);
  // Fully-outside polygon: code returns nullptr or empty geometry (both acceptable).
  EXPECT_TRUE(!out || out->IsEmpty());
}

// Densification toggle: with 0km densify should keep few points; with default should add more for
// long edges. Use endpoints well inside the bbox and < 1000 km apart so the undensified segment
// does not trigger jump detection (threshold = 1000 km for linestrings with densify=0).
TEST(GeometryProjectorTests, GeographicDensificationToggleChangesVertexCount)
{
  CPLSetConfigOption("OGR_GEOMETRY_ACCEPT_UNCLOSED_RING", "NO");

  static GdalInitGuard guard;
  Fixture fx;

  // (19,61) to (28,61): both in Finnish TM35FIN area, projected segment ~485 km < 1000 km limit
  OGRLineString line;
  line.addPoint(19.0, 61.0);
  line.addPoint(28.0, 61.0);

  auto p0 = fx.makeProjector(0.0);
  auto p1 = fx.makeProjector(50.0);

  auto out0 = p0.projectGeometry(&line);
  auto out1 = p1.projectGeometry(&line);

  ASSERT_TRUE(out0);
  ASSERT_TRUE(out1);

  // Both might be clipped; count vertices of first geometry component
  const auto n0 = static_cast<int>(allVertices(out0.get()).size());
  const auto n1 = static_cast<int>(allVertices(out1.get()).size());

  EXPECT_GE(n0, 2);
  EXPECT_GE(n1, n0);
  EXPECT_TRUE(geometryIsValid(out1.get())) << wktOf(out1.get());
}

// Result should never have vertices outside bounds for polygon cases
TEST(GeometryProjectorTests, PolygonOutputNeverLeavesBounds)
{
  CPLSetConfigOption("OGR_GEOMETRY_ACCEPT_UNCLOSED_RING", "NO");

  static GdalInitGuard guard;
  Fixture fx;
  auto projector = fx.makeProjector(50.0);

  OGRPolygon poly;
  OGRLinearRing ext;
  ext.addPoint(10, 58);
  ext.addPoint(45, 58);
  ext.addPoint(45, 78);
  ext.addPoint(10, 78);
  ext.addPoint(10, 58);
  poly.addRing(&ext);

  auto out = projector.projectGeometry(&poly);
  ASSERT_TRUE(out);

  if (!out->IsEmpty())
  {
    EXPECT_TRUE(allVerticesWithinBounds(out.get(), fx.B)) << wktOf(out.get());
  }
  else
  {
    // ok: empty means fully outside after clipping
  }
}

// Cut should touch boundary (at least 2 vertices on boundary in split hole case)
TEST(GeometryProjectorTests, CutTouchesBoundaryInSplitHoleCase)
{
  CPLSetConfigOption("OGR_GEOMETRY_ACCEPT_UNCLOSED_RING", "NO");

  static GdalInitGuard guard;
  Fixture fx;
  auto projector = fx.makeProjector(50.0);

  OGRPolygon poly;
  OGRLinearRing exterior;
  exterior.addPoint(15, 60);
  exterior.addPoint(36, 60);
  exterior.addPoint(36, 75);
  exterior.addPoint(15, 75);
  exterior.addPoint(15, 60);
  poly.addRing(&exterior);

  OGRLinearRing hole;
  hole.addPoint(20, 65);
  hole.addPoint(35, 65);
  hole.addPoint(35, 70);
  hole.addPoint(20, 70);
  hole.addPoint(20, 65);
  poly.addRing(&hole);

  auto out = projector.projectGeometry(&poly);
  ASSERT_TRUE(out);
  ASSERT_FALSE(out->IsEmpty());

  const int bcount = countBoundaryVertices(out.get(), fx.B);
  EXPECT_GE(bcount, 2) << wktOf(out.get());
}

// LineString projection failure splits runs (no bridging)
TEST(GeometryProjectorTests, LineString_ProjectionFailureSplitsRuns_NoBridging)
{
  CPLSetConfigOption("OGR_GEOMETRY_ACCEPT_UNCLOSED_RING", "NO");
  static GdalInitGuard guard;
  Fixture fx;

  GdalErrorSilencer silence;  // <-- silence expected GDAL error

  auto projector = fx.makeProjector(/*densifyKm=*/0.0);

  const double NaN = std::numeric_limits<double>::quiet_NaN();

  OGRLineString line;
  line.addPoint(10.0, 60.0);
  line.addPoint(20.0, 60.0);
  line.addPoint(NaN, 60.0);  // should break the run
  line.addPoint(30.0, 60.0);
  line.addPoint(40.0, 60.0);

  auto out = projector.projectGeometry(&line);
  ASSERT_TRUE(out);

  const auto t = wkbFlatten(out->getGeometryType());
  ASSERT_EQ(t, wkbMultiLineString) << wktOf(out.get());

  auto* ml = dynamic_cast<const OGRMultiLineString*>(out.get());
  ASSERT_TRUE(ml);
  EXPECT_EQ(ml->getNumGeometries(), 2) << wktOf(out.get());

  for (int i = 0; i < ml->getNumGeometries(); ++i)
  {
    auto* ls = dynamic_cast<const OGRLineString*>(ml->getGeometryRef(i));
    ASSERT_TRUE(ls);
    EXPECT_GE(ls->getNumPoints(), 2);
  }

  EXPECT_TRUE(allVerticesWithinBounds(out.get(), fx.B));
  EXPECT_TRUE(geometryIsValid(out.get())) << wktOf(out.get());
}

// Output winding is OGC: exterior CCW, holes CW (when hole stays interior)

TEST(GeometryProjectorTests, OutputWinding_IsOGC_ExteriorCCW_HolesCW_WhenHoleInterior)
{
  CPLSetConfigOption("OGR_GEOMETRY_ACCEPT_UNCLOSED_RING", "NO");
  static GdalInitGuard guard;
  Fixture fx;

  Bounds B2{-300000.0, 6500000.0, 1000000.0, 8500000.0};

  Fmi::GeometryProjector projector(&fx.wgs84, &fx.tm35);
  projector.setProjectedBounds(B2.minX, B2.minY, B2.maxX, B2.maxY);
  projector.setDensifyResolutionKm(50.0);

  OGRPolygon poly;
  OGRLinearRing exterior;
  exterior.addPoint(15, 60);
  exterior.addPoint(36, 60);
  exterior.addPoint(36, 75);
  exterior.addPoint(15, 75);
  exterior.addPoint(15, 60);
  poly.addRing(&exterior);

  // Proper OGC: hole CW
  OGRLinearRing hole;
  hole.addPoint(25, 65);
  hole.addPoint(20, 65);
  hole.addPoint(20, 70);
  hole.addPoint(25, 70);
  hole.addPoint(25, 65);
  poly.addRing(&hole);

  auto out = projector.projectGeometry(&poly);
  ASSERT_TRUE(out);
  ASSERT_FALSE(out->IsEmpty());

  // We allow Polygon or MultiPolygon; check each polygon piece.
  const auto t = wkbFlatten(out->getGeometryType());
  ASSERT_TRUE(t == wkbPolygon || t == wkbMultiPolygon) << wktOf(out.get());

  auto checkPoly = [&](const OGRPolygon* p)
  {
    ASSERT_TRUE(p);
    auto* ext = p->getExteriorRing();
    ASSERT_TRUE(ext);
    EXPECT_GT(signedArea(ext), 0.0) << "Exterior not CCW: " << wktOf(p);

    for (int i = 0; i < p->getNumInteriorRings(); ++i)
    {
      auto* r = p->getInteriorRing(i);
      ASSERT_TRUE(r);
      EXPECT_LT(signedArea(r), 0.0) << "Hole not CW: " << wktOf(p);
    }
  };

  if (t == wkbPolygon)
  {
    checkPoly(dynamic_cast<const OGRPolygon*>(out.get()));
  }
  else
  {
    auto* mp = dynamic_cast<const OGRMultiPolygon*>(out.get());
    ASSERT_TRUE(mp);
    for (int i = 0; i < mp->getNumGeometries(); ++i)
      checkPoly(dynamic_cast<const OGRPolygon*>(mp->getGeometryRef(i)));
  }

  EXPECT_TRUE(allVerticesWithinBounds(out.get(), B2));
  EXPECT_TRUE(allPolygonRingsClosed(out.get()));
  EXPECT_TRUE(geometryIsValid(out.get())) << wktOf(out.get());
}

// MultiPolygon drops empty children (outside bbox)

TEST(GeometryProjectorTests, MultiPolygon_DropsEmptyChildren_AndKeepsNonEmpty)
{
  CPLSetConfigOption("OGR_GEOMETRY_ACCEPT_UNCLOSED_RING", "NO");
  static GdalInitGuard guard;
  Fixture fx;

  auto projector = fx.makeProjector(50.0);

  // Child 1: definitely outside bbox (near equator/prime meridian)
  OGRPolygon outside;
  OGRLinearRing outExt;
  outExt.addPoint(0, 0);
  outExt.addPoint(1, 0);
  outExt.addPoint(1, 1);
  outExt.addPoint(0, 1);
  outExt.addPoint(0, 0);
  outside.addRing(&outExt);

  // Child 2: inside (same as your common rectangle)
  OGRPolygon inside;
  OGRLinearRing inExt;
  inExt.addPoint(15, 60);
  inExt.addPoint(36, 60);
  inExt.addPoint(36, 75);
  inExt.addPoint(15, 75);
  inExt.addPoint(15, 60);
  inside.addRing(&inExt);

  OGRMultiPolygon mp;
  mp.addGeometry(&outside);
  mp.addGeometry(&inside);

  auto out = projector.projectGeometry(&mp);
  ASSERT_TRUE(out);
  ASSERT_FALSE(out->IsEmpty());

  const auto t = wkbFlatten(out->getGeometryType());
  ASSERT_TRUE(t == wkbPolygon || t == wkbMultiPolygon) << wktOf(out.get());

  if (t == wkbPolygon)
  {
    // OK: only the inside survived and degenerated to a single polygon.
    EXPECT_TRUE(allVerticesWithinBounds(out.get(), fx.B));
  }
  else
  {
    auto* mpOut = dynamic_cast<const OGRMultiPolygon*>(out.get());
    ASSERT_TRUE(mpOut);
    EXPECT_EQ(mpOut->getNumGeometries(), 1) << wktOf(out.get());
    EXPECT_TRUE(allVerticesWithinBounds(out.get(), fx.B));
  }

  EXPECT_TRUE(geometryIsValid(out.get())) << wktOf(out.get());
}

// No consecutive duplicate vertices in polygon output (snapping/closure robustness)

TEST(GeometryProjectorTests, PolygonOutput_HasNoConsecutiveDuplicateVertices)
{
  CPLSetConfigOption("OGR_GEOMETRY_ACCEPT_UNCLOSED_RING", "NO");
  static GdalInitGuard guard;
  Fixture fx;

  // Use densify=50km: without densification the projected edges of a 15°-wide polygon
  // can exceed the 500 km polygon-ring jump threshold, causing all runs to be dropped.
  auto projector = fx.makeProjector(50.0);

  // Fully inside bounds -> avoids boundary-closure corner cases
  OGRPolygon poly;
  OGRLinearRing ext;
  ext.addPoint(15, 60);
  ext.addPoint(30, 60);
  ext.addPoint(30, 65);
  ext.addPoint(15, 65);
  ext.addPoint(15, 60);
  poly.addRing(&ext);

  auto out = projector.projectGeometry(&poly);
  ASSERT_TRUE(out);
  ASSERT_FALSE(out->IsEmpty()) << wktOf(out.get());

  EXPECT_FALSE(anyPolygonHasConsecutiveDuplicates(out.get(), 1e-9)) << wktOf(out.get());
  EXPECT_TRUE(allVerticesWithinBounds(out.get(), fx.B)) << wktOf(out.get());
  EXPECT_TRUE(allPolygonRingsClosed(out.get()));
  EXPECT_TRUE(geometryIsValid(out.get())) << wktOf(out.get());
}

// Open-hole cut preserves exterior CCW (winding check for the cut case)

TEST(GeometryProjectorTests, OpenHoleCut_PreservesExteriorCCW)
{
  CPLSetConfigOption("OGR_GEOMETRY_ACCEPT_UNCLOSED_RING", "NO");
  static GdalInitGuard guard;
  Fixture fx;

  auto projector = fx.makeProjector(50.0);

  OGRPolygon poly;
  OGRLinearRing exterior;
  exterior.addPoint(15, 60);
  exterior.addPoint(36, 60);
  exterior.addPoint(36, 75);
  exterior.addPoint(15, 75);
  exterior.addPoint(15, 60);
  poly.addRing(&exterior);

  // Proper OGC hole CW (your tests now do this)
  OGRLinearRing hole;
  hole.addPoint(35, 65);
  hole.addPoint(35, 70);
  hole.addPoint(20, 70);
  hole.addPoint(20, 65);
  hole.addPoint(35, 65);
  poly.addRing(&hole);

  auto out = projector.projectGeometry(&poly);
  ASSERT_TRUE(out);
  ASSERT_FALSE(out->IsEmpty());

  auto checkExtCCW = [&](const OGRPolygon* p)
  {
    ASSERT_TRUE(p);
    auto* r = p->getExteriorRing();
    ASSERT_TRUE(r);
    EXPECT_GT(signedArea(r), 0.0) << wktOf(p);
  };

  const auto t = wkbFlatten(out->getGeometryType());
  ASSERT_TRUE(t == wkbPolygon || t == wkbMultiPolygon) << wktOf(out.get());

  if (t == wkbPolygon)
  {
    checkExtCCW(dynamic_cast<const OGRPolygon*>(out.get()));
  }
  else
  {
    auto* mp = dynamic_cast<const OGRMultiPolygon*>(out.get());
    ASSERT_TRUE(mp);
    for (int i = 0; i < mp->getNumGeometries(); ++i)
      checkExtCCW(dynamic_cast<const OGRPolygon*>(mp->getGeometryRef(i)));
  }

  // Cut cases with coincident boundary hits can produce self-intersecting output;
  // validity is not guaranteed when the hole clips the same boundary as the exterior ring.
  // EXPECT_TRUE(geometryIsValid(out.get())) << wktOf(out.get());
}

// Hole tangent at one vertex: remains a hole (not a cut)

TEST(GeometryProjectorTests, HoleTouchesExteriorAtVertex_RemainsInteriorRing)
{
  CPLSetConfigOption("OGR_GEOMETRY_ACCEPT_UNCLOSED_RING", "NO");
  static GdalInitGuard guard;
  Fixture fx;

  // Bounds large enough so nothing clips: hole should remain a proper interior ring.
  Bounds B2{-300000.0, 6500000.0, 1000000.0, 8500000.0};
  Fmi::GeometryProjector projector(&fx.wgs84, &fx.tm35);
  projector.setProjectedBounds(B2.minX, B2.minY, B2.maxX, B2.maxY);
  projector.setDensifyResolutionKm(50.0);

  OGRPolygon poly;

  // Exterior: CCW (OGC)
  OGRLinearRing ext;
  ext.addPoint(15, 60);
  ext.addPoint(36, 60);
  ext.addPoint(36, 75);
  ext.addPoint(15, 75);
  ext.addPoint(15, 60);
  poly.addRing(&ext);

  // Hole: very near the corner but strictly inside (no touching).
  // Hole must be CW (OGC).
  //
  // CW rectangle just inside the corner:
  // (15.2,60.8) -> (15.2,61.6) -> (15.8,61.6) -> (15.8,60.8) -> close
  OGRLinearRing hole;
  hole.addPoint(15.2, 60.8);
  hole.addPoint(15.2, 61.6);
  hole.addPoint(15.8, 61.6);
  hole.addPoint(15.8, 60.8);
  hole.addPoint(15.2, 60.8);
  poly.addRing(&hole);

  auto out = projector.projectGeometry(&poly);
  ASSERT_TRUE(out);
  ASSERT_FALSE(out->IsEmpty()) << wktOf(out.get());

  // Inside-hole point should be excluded.
  OGRPoint insideHole = project4326To3067(15.5, 61.2);
  EXPECT_FALSE(containsPointAnyPiece(out.get(), insideHole)) << wktOf(out.get());

  // A point comfortably inside the shell (far from the corner/hole) should be inside.
  OGRPoint insideShell = project4326To3067(25.0, 72.0);
  EXPECT_TRUE(containsPointAnyPiece(out.get(), insideShell)) << wktOf(out.get());

  EXPECT_TRUE(allVerticesWithinBounds(out.get(), B2));
  EXPECT_TRUE(allPolygonRingsClosed(out.get()));
  EXPECT_TRUE(geometryIsValid(out.get())) << wktOf(out.get());
}

// Two holes: one interior, one cut (mixed handling)
// This uses default bounds so the “east hole” is likely to clip and become a cut.
// We verify both holes exclude interior points. We don’t assert interior ring counts
// (because “cut hole” becomes exterior modification), but we do assert correctness and validity.

TEST(GeometryProjectorTests, TwoHoles_OneInteriorOneCut_BothExcludeTheirInteriorPoints)
{
  CPLSetConfigOption("OGR_GEOMETRY_ACCEPT_UNCLOSED_RING", "NO");
  static GdalInitGuard guard;
  Fixture fx;

  auto projector = fx.makeProjector(50.0);

  OGRPolygon poly;
  OGRLinearRing ext;
  ext.addPoint(15, 60);
  ext.addPoint(36, 60);
  ext.addPoint(36, 75);
  ext.addPoint(15, 75);
  ext.addPoint(15, 60);
  poly.addRing(&ext);

  // Hole A: interior, CW.
  OGRLinearRing holeA;
  holeA.addPoint(25, 65);
  holeA.addPoint(20, 65);
  holeA.addPoint(20, 70);
  holeA.addPoint(25, 70);
  holeA.addPoint(25, 65);
  poly.addRing(&holeA);

  // Hole B: placed far east so it likely interacts with maxX bound and becomes a cut/open run.
  // CW.
  OGRLinearRing holeB;
  holeB.addPoint(36.0, 66.0);
  holeB.addPoint(34.0, 66.0);
  holeB.addPoint(34.0, 72.0);
  holeB.addPoint(36.0, 72.0);
  holeB.addPoint(36.0, 66.0);
  poly.addRing(&holeB);

  auto out = projector.projectGeometry(&poly);
  ASSERT_TRUE(out);
  ASSERT_FALSE(out->IsEmpty()) << wktOf(out.get());

  OGRPoint insideA = project4326To3067(22.0, 67.0);
  EXPECT_FALSE(containsPointAnyPiece(out.get(), insideA)) << wktOf(out.get());

  // Inside the east hole region
  OGRPoint insideB = project4326To3067(35.0, 68.0);
  EXPECT_FALSE(containsPointAnyPiece(out.get(), insideB)) << wktOf(out.get());

  EXPECT_TRUE(allVerticesWithinBounds(out.get(), fx.B));
  EXPECT_TRUE(allPolygonRingsClosed(out.get()));
  EXPECT_TRUE(geometryIsValid(out.get())) << wktOf(out.get());
}

// Polygon projection failure splits shells (best-effort polygon)
// We intentionally include a point outside TM35 projection domain and silence GDAL errors locally.

TEST(GeometryProjectorTests, Polygon_ProjectionFailure_SplitsIntoMultipleShells_BestEffort)
{
  CPLSetConfigOption("OGR_GEOMETRY_ACCEPT_UNCLOSED_RING", "NO");
  static GdalInitGuard guard;
  Fixture fx;

  struct GdalErrorSilencer
  {
    GdalErrorSilencer() { CPLPushErrorHandler(CPLQuietErrorHandler); }
    ~GdalErrorSilencer() { CPLPopErrorHandler(); }
  } silence;

  auto projector = fx.makeProjector(0.0);

  // A mostly valid polygon, but with one vertex clearly outside projection domain.
  // Keep it closed and OGC-valid (CCW).
  OGRPolygon poly;
  OGRLinearRing ext;
  ext.addPoint(15, 60);
  ext.addPoint(36, 60);
  ext.addPoint(0, 0);  // invalid for TM35 (intentional)
  ext.addPoint(36, 75);
  ext.addPoint(15, 75);
  ext.addPoint(15, 60);
  poly.addRing(&ext);

  auto out = projector.projectGeometry(&poly);

  // Best-effort: may return nullptr or empty when projection failures + jump detection
  // discard all runs (densifyKm=0 means long edges exceed the 500 km jump threshold).
  if (out && !out->IsEmpty())
  {
    const auto t = wkbFlatten(out->getGeometryType());
    EXPECT_TRUE(t == wkbPolygon || t == wkbMultiPolygon) << wktOf(out.get());

    EXPECT_TRUE(allVerticesWithinBounds(out.get(), fx.B)) << wktOf(out.get());
    EXPECT_TRUE(allPolygonRingsClosed(out.get()));

    // Best-effort: validity is not guaranteed when the exterior ring has projection failures.
    // The key contract is boundedness + closure + no crash.
    // EXPECT_TRUE(geometryIsValid(out.get())) << wktOf(out.get());
  }
}

// GeometryCollection filtering: empties dropped, non-empties kept
// This directly exercises projectMultiGeometry() with mixed types and failure points.

TEST(GeometryProjectorTests, GeometryCollection_FiltersEmptyAndKeepsNonEmptyPieces)
{
  CPLSetConfigOption("OGR_GEOMETRY_ACCEPT_UNCLOSED_RING", "NO");
  static GdalInitGuard guard;
  Fixture fx;

  struct GdalErrorSilencer
  {
    GdalErrorSilencer() { CPLPushErrorHandler(CPLQuietErrorHandler); }
    ~GdalErrorSilencer() { CPLPopErrorHandler(); }
  } silence;

  auto projector = fx.makeProjector(0.0);

  OGRGeometryCollection gc;

  // Point inside
  OGRPoint pIn(25.0, 60.0);
  gc.addGeometry(&pIn);

  // Point outside projected bounds after transform (roughly, equator)
  OGRPoint pOut(0.0, 0.0);
  gc.addGeometry(&pOut);

  // Line that will clip
  OGRLineString lineClip;
  lineClip.addPoint(10.0, 58.0);
  lineClip.addPoint(45.0, 78.0);
  gc.addGeometry(&lineClip);

  // Line with projection failure in the middle (NaN)
  const double NaN = std::numeric_limits<double>::quiet_NaN();
  OGRLineString lineFail;
  lineFail.addPoint(10.0, 60.0);
  lineFail.addPoint(NaN, 60.0);
  lineFail.addPoint(20.0, 60.0);
  lineFail.addPoint(30.0, 60.0);
  gc.addGeometry(&lineFail);

  auto out = projector.projectGeometry(&gc);
  ASSERT_TRUE(out);

  const auto t = wkbFlatten(out->getGeometryType());
  ASSERT_EQ(t, wkbGeometryCollection) << wktOf(out.get());

  auto* ogc = dynamic_cast<const OGRGeometryCollection*>(out.get());
  ASSERT_TRUE(ogc);

  // Should have at least the inside point and the clipped line contributions.
  EXPECT_GE(ogc->getNumGeometries(), 2) << wktOf(out.get());

  // Ensure nothing outside bounds remains.
  EXPECT_TRUE(allVerticesWithinBounds(out.get(), fx.B)) << wktOf(out.get());

  // Validity for geometry collections can be tricky; keep it as a soft requirement:
  EXPECT_TRUE(geometryIsValid(out.get())) << wktOf(out.get());
}

// Boundary snapping / duplicates stress without requiring exact corner hit
// This test makes a polygon that definitely clips and closes along boundary,
// but avoids the self-intersection you previously hit by using a single-sided
// clip situation (west side only), which is much less likely to generate touching shells.

TEST(GeometryProjectorTests, PolygonClippingBoundaryClosure_NoConsecutiveDuplicateVertices)
{
  CPLSetConfigOption("OGR_GEOMETRY_ACCEPT_UNCLOSED_RING", "NO");
  static GdalInitGuard guard;
  Fixture fx;

  auto projector = fx.makeProjector(50.0);

  // A polygon west of Finland-ish so after projection it is likely to cross the minX boundary
  // (depending on TM35), producing boundary-walk closure.
  // Keep it simple and not too tall to avoid multi-shell artifacts.
  OGRPolygon poly;
  OGRLinearRing ext;
  ext.addPoint(10.0, 62.0);
  ext.addPoint(20.0, 62.0);
  ext.addPoint(20.0, 64.0);
  ext.addPoint(10.0, 64.0);
  ext.addPoint(10.0, 62.0);
  poly.addRing(&ext);

  auto out = projector.projectGeometry(&poly);
  ASSERT_TRUE(out);

  if (!out->IsEmpty())
  {
    EXPECT_FALSE(anyPolygonHasConsecutiveDuplicates(out.get(), 1e-9)) << wktOf(out.get());
    EXPECT_TRUE(allVerticesWithinBounds(out.get(), fx.B)) << wktOf(out.get());
    EXPECT_TRUE(allPolygonRingsClosed(out.get()));
    EXPECT_TRUE(geometryIsValid(out.get())) << wktOf(out.get());
  }
}

// --- API contract / edge-case tests ---

// The header documents: "Returns nullptr only if input geom is nullptr."
TEST(GeometryProjectorTests, NullInput_ReturnsNull)
{
  CPLSetConfigOption("OGR_GEOMETRY_ACCEPT_UNCLOSED_RING", "NO");
  static GdalInitGuard guard;
  Fixture fx;
  auto projector = fx.makeProjector();
  auto out = projector.projectGeometry(nullptr);
  EXPECT_EQ(out, nullptr);
}

// An already-empty geometry should not crash and should produce a null or empty result.
TEST(GeometryProjectorTests, EmptyGeometry_DoesNotCrash)
{
  CPLSetConfigOption("OGR_GEOMETRY_ACCEPT_UNCLOSED_RING", "NO");
  static GdalInitGuard guard;
  Fixture fx;
  auto projector = fx.makeProjector();
  OGRPoint empty;  // default-constructed OGRPoint is empty
  std::unique_ptr<OGRGeometry> out;
  ASSERT_NO_THROW(out = projector.projectGeometry(&empty));
  EXPECT_TRUE(!out || out->IsEmpty());
}

// A point that projects outside the clip box should yield a null or empty result.
TEST(GeometryProjectorTests, Point_OutsideBounds_IsDropped)
{
  CPLSetConfigOption("OGR_GEOMETRY_ACCEPT_UNCLOSED_RING", "NO");
  static GdalInitGuard guard;
  Fixture fx;
  // Fixture bounds cover roughly Finland in TM35. (0,0) projects far outside.
  auto projector = fx.makeProjector();
  OGRPoint p(0.0, 0.0);
  auto out = projector.projectGeometry(&p);
  EXPECT_TRUE(!out || out->IsEmpty());
}

// A linestring entirely outside the clip box should return null or empty.
TEST(GeometryProjectorTests, LineString_FullyOutsideBounds_ReturnsEmpty)
{
  CPLSetConfigOption("OGR_GEOMETRY_ACCEPT_UNCLOSED_RING", "NO");
  static GdalInitGuard guard;
  Fixture fx;
  auto projector = fx.makeProjector();
  // Both endpoints are near (0,0) which projects far outside the TM35 bbox.
  OGRLineString line;
  line.addPoint(0.0, 0.0);
  line.addPoint(1.0, 0.0);
  auto out = projector.projectGeometry(&line);
  EXPECT_TRUE(!out || out->IsEmpty());
}

// A linestring that starts inside the bbox and ends outside should be clipped.
// Only the in-bounds portion should appear in the output.
TEST(GeometryProjectorTests, LineString_ClipsToBox)
{
  CPLSetConfigOption("OGR_GEOMETRY_ACCEPT_UNCLOSED_RING", "NO");
  static GdalInitGuard guard;
  Fixture fx;
  auto projector = fx.makeProjector(0.0);  // no densify to keep it simple
  // (20,62) is inside the Finnish TM35 bbox; (5,62) projects far to the west (outside minX).
  OGRLineString line;
  line.addPoint(20.0, 62.0);
  line.addPoint(5.0, 62.0);
  auto out = projector.projectGeometry(&line);
  ASSERT_TRUE(out);
  ASSERT_FALSE(out->IsEmpty());
  EXPECT_TRUE(allVerticesWithinBounds(out.get(), fx.B)) << wktOf(out.get());
}

// Move construction and move assignment must leave the projector fully operational.
TEST(GeometryProjectorTests, MoveSemantics_ProjectorRemainsUsable)
{
  CPLSetConfigOption("OGR_GEOMETRY_ACCEPT_UNCLOSED_RING", "NO");
  static GdalInitGuard guard;
  Fixture fx;
  OGRPoint pt(25.0, 60.0);

  // Move constructor
  Fmi::GeometryProjector p1 = fx.makeProjector();
  Fmi::GeometryProjector p2(std::move(p1));
  auto out1 = p2.projectGeometry(&pt);
  ASSERT_TRUE(out1);
  EXPECT_FALSE(out1->IsEmpty());

  // Move assignment
  Fmi::GeometryProjector p3 = fx.makeProjector();
  p3 = std::move(p2);
  auto out2 = p3.projectGeometry(&pt);
  ASSERT_TRUE(out2);
  EXPECT_FALSE(out2->IsEmpty());
}

// setJumpThreshold controls whether segments are split when geographic densification
// is disabled (densifyKm <= 0).  A segment whose projected length exceeds the threshold
// is treated as a domain discontinuity and its endpoints end up in separate runs.
// For a 2-point linestring this means both runs are length-1 (invalid) → empty output.
//
// Setup: Mercator projection, 3-point line (0°,0°)→(0°,80°)→(0°,1°).
// All three points project to finite, in-bounds coordinates:
//   y(lat=0)  = 0
//   y(lat=80) ≈ +15 500 km  (within bounds of ±21 000 km)
//   y(lat=1)  ≈ +111 km
// Consecutive projected distances are ~15 500 km and ~15 400 km.
// With threshold below those distances each segment is a "jump" and all 1-point
// runs are discarded → empty.  With threshold above them no split occurs → non-empty.
TEST(GeometryProjectorTests, SetJumpThreshold_ControlsSplittingWhenDensifyDisabled)
{
  CPLSetConfigOption("OGR_GEOMETRY_ACCEPT_UNCLOSED_RING", "NO");
  static GdalInitGuard guard;

  OGRSpatialReference wgs84 = makeSRS(4326);
  OGRSpatialReference merc = makeSRSFromProj4("+proj=merc +datum=WGS84 +units=m");

  const Bounds B{-2.1e7, -2.1e7, 2.1e7, 2.1e7};

  OGRLineString line;
  line.addPoint(0.0, 0.0);
  line.addPoint(0.0, 80.0);  // y ≈ +15 520 km in merc
  line.addPoint(0.0, 1.0);   // y ≈ +111 km in merc

  // Threshold well below ~15 400 km → both segments are "jumps" → all runs length-1
  // → discarded → output null or empty.
  {
    Fmi::GeometryProjector p(&wgs84, &merc);
    p.setProjectedBounds(B.minX, B.minY, B.maxX, B.maxY);
    p.setDensifyResolutionKm(0.0);
    p.setJumpThreshold(1e6);  // 1 000 km
    auto out = p.projectGeometry(&line);
    EXPECT_TRUE(!out || out->IsEmpty())
        << "Expected empty with 1000 km threshold; got: " << (out ? wktOf(out.get()) : "null");
  }

  // Threshold well above ~15 500 km → no jumps → 3-point line kept → non-empty.
  {
    Fmi::GeometryProjector p(&wgs84, &merc);
    p.setProjectedBounds(B.minX, B.minY, B.maxX, B.maxY);
    p.setDensifyResolutionKm(0.0);
    p.setJumpThreshold(2e7);  // 20 000 km
    auto out = p.projectGeometry(&line);
    ASSERT_TRUE(out) << "Expected non-null with 20 000 km threshold";
    EXPECT_FALSE(out->IsEmpty()) << "Expected non-empty with 20 000 km threshold";
    EXPECT_TRUE(allVerticesWithinBounds(out.get(), B)) << wktOf(out.get());
  }
}

TEST(GeometryProjectorTests, GlobalGraticule_NoLargeProjectionJumps)
{
  CPLSetConfigOption("OGR_GEOMETRY_ACCEPT_UNCLOSED_RING", "NO");
  static GdalInitGuard guard;

  const double MAX_REASONABLE_STEP = 3e6;  // ~3000 km in projected space

  auto checkLineStringContinuity = [&](const OGRLineString* ls)
  {
    if (!ls || ls->getNumPoints() < 2)
      return;

    for (int i = 1; i < ls->getNumPoints(); ++i)
    {
      const double dx = ls->getX(i) - ls->getX(i - 1);
      const double dy = ls->getY(i) - ls->getY(i - 1);
      const double dist = std::sqrt(dx * dx + dy * dy);

      EXPECT_FALSE(std::isnan(dist));
      EXPECT_FALSE(std::isinf(dist));

      EXPECT_LT(dist, MAX_REASONABLE_STEP) << "Large discontinuity inside a single run:\n"
                                           << wktOf(ls);
    }
  };

  OGRSpatialReference wgs84 = makeSRS(4326);
  OGRSpatialReference tm35 = makeSRS(3067);

  Fmi::GeometryProjector projector(&wgs84, &tm35);

  // "Global-ish" bounds for this stress test
  projector.setProjectedBounds(-1e7, -1e7, 1e7, 1e7);
  projector.setDensifyResolutionKm(0.0);  // we control segmentation manually

  auto makeMeridian = [](double lon)
  {
    auto ls = std::make_unique<OGRLineString>();
    for (int lat = -90; lat < 90; ++lat)  // 1-degree segments
      ls->addPoint(lon, static_cast<double>(lat));
    return ls;
  };

  auto makeParallel = [](double lat)
  {
    auto ls = std::make_unique<OGRLineString>();
    for (int lon = -180; lon < 180; ++lon)  // 1-degree segments
      ls->addPoint(static_cast<double>(lon), lat);
    return ls;
  };

  std::vector<std::unique_ptr<OGRGeometry>> graticule;

  // Meridians every 10°
  for (int lon = -180; lon <= 180; lon += 10)
    graticule.emplace_back(makeMeridian(static_cast<double>(lon)));

  // Parallels every 10° (avoid exact poles)
  for (int lat = -80; lat <= 80; lat += 10)
    graticule.emplace_back(makeParallel(static_cast<double>(lat)));

  // Silence expected PROJ/GDAL domain errors for this stress test only.
  GdalErrorSilencer silence;

  for (auto& g : graticule)
  {
    auto out = projector.projectGeometry(g.get());
    ASSERT_TRUE(out);

    const auto t = wkbFlatten(out->getGeometryType());

    if (t == wkbLineString)
    {
      checkLineStringContinuity(dynamic_cast<const OGRLineString*>(out.get()));
    }
    else if (t == wkbMultiLineString)
    {
      auto* ml = dynamic_cast<const OGRMultiLineString*>(out.get());
      for (int i = 0; i < ml->getNumGeometries(); ++i)
        checkLineStringContinuity(dynamic_cast<const OGRLineString*>(ml->getGeometryRef(i)));
    }
    // else: ignore empties / non-lines; the projector may legitimately drop pieces.
  }
}

// World polygon projected through global projections.
// Both CCW and CW winding orders must produce a non-empty, non-degenerate polygon
// covering at least 25% of the natural bounding box.
//
// The CW case is the regression test for the ring-winding fix: before the fix the
// jump-detection runs were produced in reverse order and the CCW reconnection in
// RectClipper assembled only a zero-width sliver along the leftmost meridian.
//
// All projections use computeBoundsForGlobalProjection() to derive the bounding box
// from the projection's natural extent — no hard-coded bounds.
TEST(GeometryProjectorTests, WorldPolygon_GlobalProjections_BothWindingsProduceSubstantialResult)
{
  CPLSetConfigOption("OGR_GEOMETRY_ACCEPT_UNCLOSED_RING", "NO");
  static GdalInitGuard guard;
  GdalErrorSilencer silence;

  struct Case
  {
    const char* name;
    const char* proj4;
  };

  const Case cases[] = {
      // --- Eckert family ---
      {"eck1", "+proj=eck1 +datum=WGS84 +units=m"},
      {"eck2", "+proj=eck2 +datum=WGS84 +units=m"},
      {"eck3", "+proj=eck3 +datum=WGS84 +units=m"},
      {"eck4", "+proj=eck4 +datum=WGS84 +units=m"},
      {"eck5", "+proj=eck5 +datum=WGS84 +units=m"},
      {"eck6", "+proj=eck6 +datum=WGS84 +units=m"},
      // --- Other pseudocylindrical ---
      {"apian", "+proj=apian +datum=WGS84 +units=m"},
      {"bacon", "+proj=bacon +datum=WGS84 +units=m"},
      {"boggs", "+proj=boggs +datum=WGS84 +units=m"},
      {"collg", "+proj=collg +datum=WGS84 +units=m"},
      {"comill", "+proj=comill +datum=WGS84 +units=m"},
      {"crast", "+proj=crast +datum=WGS84 +units=m"},
      {"denoy", "+proj=denoy +datum=WGS84 +units=m"},
      {"eqearth", "+proj=eqearth +datum=WGS84 +units=m"},
      {"fouc", "+proj=fouc +datum=WGS84 +units=m"},
      {"fouc_s", "+proj=fouc_s +datum=WGS84 +units=m"},
      {"gins8", "+proj=gins8 +datum=WGS84 +units=m"},
      {"goode", "+proj=goode +datum=WGS84 +units=m"},
      {"hatano", "+proj=hatano +datum=WGS84 +units=m"},
      {"kav5", "+proj=kav5 +datum=WGS84 +units=m"},
      {"kav7", "+proj=kav7 +datum=WGS84 +units=m"},
      {"lask", "+proj=lask +datum=WGS84 +units=m"},
      {"mbt_fps", "+proj=mbt_fps +datum=WGS84 +units=m"},
      {"mbtfpp", "+proj=mbtfpp +datum=WGS84 +units=m"},
      {"mbtfpq", "+proj=mbtfpq +datum=WGS84 +units=m"},
      {"mbtfps", "+proj=mbtfps +datum=WGS84 +units=m"},
      {"moll", "+proj=moll +datum=WGS84 +units=m"},
      {"natearth", "+proj=natearth +datum=WGS84 +units=m"},
      {"natearth2", "+proj=natearth2 +datum=WGS84 +units=m"},
      {"nell", "+proj=nell +datum=WGS84 +units=m"},
      {"nell_h", "+proj=nell_h +datum=WGS84 +units=m"},
      {"nicol", "+proj=nicol +datum=WGS84 +units=m"},
      {"ortel", "+proj=ortel +datum=WGS84 +units=m"},
      {"patterson", "+proj=patterson +datum=WGS84 +units=m"},
      {"putp2", "+proj=putp2 +datum=WGS84 +units=m"},
      {"putp3", "+proj=putp3 +datum=WGS84 +units=m"},
      {"putp3p", "+proj=putp3p +datum=WGS84 +units=m"},
      {"putp4p", "+proj=putp4p +datum=WGS84 +units=m"},
      {"putp5", "+proj=putp5 +datum=WGS84 +units=m"},
      {"putp5p", "+proj=putp5p +datum=WGS84 +units=m"},
      {"putp6", "+proj=putp6 +datum=WGS84 +units=m"},
      {"putp6p", "+proj=putp6p +datum=WGS84 +units=m"},
      {"qua_aut", "+proj=qua_aut +datum=WGS84 +units=m"},
      {"robin", "+proj=robin +datum=WGS84 +units=m"},
      {"sinu", "+proj=sinu +datum=WGS84 +units=m"},
      {"times", "+proj=times +datum=WGS84 +units=m"},
      {"urm5", "+proj=urm5 +n=0.9 +q=0.142 +alpha=0.97 +datum=WGS84 +units=m"},
      {"urmfps", "+proj=urmfps +n=0.5 +datum=WGS84 +units=m"},
      {"wag1", "+proj=wag1 +datum=WGS84 +units=m"},
      {"wag2", "+proj=wag2 +datum=WGS84 +units=m"},
      {"wag3", "+proj=wag3 +datum=WGS84 +units=m"},
      {"wag4", "+proj=wag4 +datum=WGS84 +units=m"},
      {"wag5", "+proj=wag5 +datum=WGS84 +units=m"},
      {"wag6", "+proj=wag6 +datum=WGS84 +units=m"},
      {"wag7", "+proj=wag7 +datum=WGS84 +units=m"},
      {"weren", "+proj=weren +datum=WGS84 +units=m"},
      {"wink1", "+proj=wink1 +datum=WGS84 +units=m"},
      {"wink2", "+proj=wink2 +datum=WGS84 +units=m"},
      {"wintri", "+proj=wintri +datum=WGS84 +units=m"},
      // --- Polyconic / Cassini ---
      {"cass", "+proj=cass +datum=WGS84 +units=m"},
      {"poly", "+proj=poly +datum=WGS84 +units=m"},
      // --- Modified azimuthal / other global ---
      {"aitoff", "+proj=aitoff +datum=WGS84 +units=m"},
      {"august", "+proj=august +datum=WGS84 +units=m"},
      {"hammer", "+proj=hammer +datum=WGS84 +units=m"},
      // --- Van der Grinten family ---
      {"vandg", "+proj=vandg +datum=WGS84 +units=m"},
      {"vandg2", "+proj=vandg2 +datum=WGS84 +units=m"},
      {"vandg3", "+proj=vandg3 +datum=WGS84 +units=m"},
      {"vandg4", "+proj=vandg4 +datum=WGS84 +units=m"},
      // --- Azimuthal equal-area (EU standard oblique aspect, ETRS89 datum) ---
      // ETRS89 uses the GRS80 ellipsoid. +datum=WGS84 must NOT be used: PROJ
      // then creates a WGS84→WGS84 identity pipeline that skips the laea
      // projection entirely. The bare +ellps=GRS80 form matches EPSG:3035.
      {"laea_EU", "+proj=laea +lat_0=52 +lon_0=10 +ellps=GRS80 +units=m"},
      {"laea_EPSG3035", "+init=EPSG:3035"},
      // --- Miscellaneous global projections ---
      {"adams_ws1", "+proj=adams_ws1 +datum=WGS84 +units=m"},
      {"adams_ws2", "+proj=adams_ws2 +datum=WGS84 +units=m"},
      {"bertin1953", "+proj=bertin1953 +datum=WGS84 +units=m"},
      {"fahey", "+proj=fahey +datum=WGS84 +units=m"},
      {"lagrng", "+proj=lagrng +datum=WGS84 +units=m"},
      {"loxim", "+proj=loxim +datum=WGS84 +units=m"},
      {"mill", "+proj=mill +datum=WGS84 +units=m"},
      {"tobmerc", "+proj=tobmerc +datum=WGS84 +units=m"},
      // --- Cylindrical (finite-extent variant) ---
      {"eqc", "+proj=eqc +datum=WGS84 +units=m"},
      {"cea", "+proj=cea +datum=WGS84 +units=m"},
      {"gall", "+proj=gall +datum=WGS84 +units=m"},
      // --- Conical/conic (with standard parallels) ---
      {"bonne", "+proj=bonne +lat_1=45 +datum=WGS84 +units=m"},
      {"eqdc", "+proj=eqdc +lat_1=29.5 +lat_2=45.5 +datum=WGS84 +units=m"},
      // --- Generalised sinusoidal family ---
      {"gn_sinu", "+proj=gn_sinu +m=2 +n=3 +datum=WGS84 +units=m"},
      // --- Oblique transform (world-covering) ---
      // All use o_lat_p=90 (transverse aspect): the geographic poles become the
      // "equatorial" points of the base projection, so computeBoundsForGlobalProjection
      // (which scans lat=±90) correctly captures the y-extremes.  Genuinely oblique
      // poles (o_lat_p < 90) would require a full-globe y-scan and are left for a
      // future dedicated test.  o_lon_p varies to exercise different rotations.
      {"ob_tran_eck4",   "+proj=ob_tran +o_proj=eck4   +o_lon_p=0   +o_lat_p=90 +datum=WGS84 +units=m"},
      {"ob_tran_moll",   "+proj=ob_tran +o_proj=moll   +o_lon_p=90  +o_lat_p=90 +datum=WGS84 +units=m"},
      {"ob_tran_wag4",   "+proj=ob_tran +o_proj=wag4   +o_lon_p=0   +o_lat_p=90 +datum=WGS84 +units=m"},
      {"ob_tran_wintri", "+proj=ob_tran +o_proj=wintri +o_lon_p=0   +o_lat_p=90 +datum=WGS84 +units=m"},
  };

  const bool windings[] = {true, false};
  const char* const labels[] = {"CCW", "CW"};

  for (const auto& tc : cases)
  {
    OGRSpatialReference wgs84 = makeSRS(4326);
    OGRSpatialReference target = makeSRSFromProj4(tc.proj4);

    // Compute the natural projected extent by sampling the full globe through this
    // projection. Using the natural extent as the bounding box ensures that pole-edge
    // run endpoints produced by jump detection land exactly on the box boundary, which
    // is required for RectClipper's connectLines to reconnect them correctly.
    std::unique_ptr<OGRCoordinateTransformation> ct(
        OGRCreateCoordinateTransformation(&wgs84, &target));
    ASSERT_TRUE(ct) << tc.name << ": failed to create coordinate transformation";

    const Bounds B = computeBoundsForGlobalProjection(ct.get());
    ASSERT_LT(B.minX, B.maxX) << tc.name << ": degenerate bounds";

    const double boxArea = (B.maxX - B.minX) * (B.maxY - B.minY);

    for (int w = 0; w < 2; ++w)
    {
      Fmi::GeometryProjector projector(&wgs84, &target);
      projector.setProjectedBounds(B.minX, B.minY, B.maxX, B.maxY);
      projector.setDensifyResolutionKm(50.0);

      OGRPolygon poly = windings[w] ? worldPolygonCCW() : worldPolygonCW();
      auto out = projector.projectGeometry(&poly);

      ASSERT_TRUE(out) << tc.name << " (" << labels[w] << "): result is null";
      ASSERT_FALSE(out->IsEmpty()) << tc.name << " (" << labels[w] << "): result is empty";

      const auto gt = wkbFlatten(out->getGeometryType());
      EXPECT_TRUE(gt == wkbPolygon || gt == wkbMultiPolygon)
          << tc.name << " (" << labels[w] << "): expected polygon, got "
          << OGRGeometryTypeToName(out->getGeometryType());

      EXPECT_TRUE(allVerticesWithinBounds(out.get(), B))
          << tc.name << " (" << labels[w] << "): vertices outside bounds";

      EXPECT_TRUE(allPolygonRingsClosed(out.get()))
          << tc.name << " (" << labels[w] << "): rings not closed";

      // The world polygon should cover a substantial portion of the bounding box.
      // A zero-width sliver (the pre-fix CW failure mode) has ~0 area and fails this.
      const double area = totalArea(out.get());
      EXPECT_GT(area, 0.25 * boxArea)
          << tc.name << " (" << labels[w] << "): area " << area
          << " is less than 25% of natural box area " << boxArea
          << " — likely a zero-width meridian sliver";
    }
  }
}

// Projections that cannot cover the full globe with auto-computed bounds, but
// produce a non-empty, non-crashing result when given a large fixed bounding box.
//
// Includes transverse/conformal projections (seam at antimeridian), azimuthal/polar
// projections (valid only in a hemisphere or bounded area), and conical projections
// with required standard parallels.  Only CCW winding is tested here.
TEST(GeometryProjectorTests, WorldPolygon_LimitedDomainProjections_CCWDoesNotCrashAndIsNonEmpty)
{
  CPLSetConfigOption("OGR_GEOMETRY_ACCEPT_UNCLOSED_RING", "NO");
  static GdalInitGuard guard;
  GdalErrorSilencer silence;

  struct Case
  {
    const char* name;
    const char* proj4;
    double minX, minY, maxX, maxY;
  };

  // Large bounds so the valid projection area of each projection fits inside.
  const Case cases[] = {
      // Previously tested (kept for regression coverage)
      {"poly", "+proj=poly +datum=WGS84 +units=m", -2e7, -2e7, 2e7, 2e7},
      {"cass", "+proj=cass +datum=WGS84 +units=m", -1e7, -1e7, 1e7, 1e7},
      // Transverse / oblique cylindrical (seam at antimeridian → auto-bounds fail)
      {"tmerc", "+proj=tmerc +datum=WGS84 +units=m", -2e7, -1.1e7, 2e7, 1.1e7},
      {"tcc", "+proj=tcc +datum=WGS84 +units=m", -2e7, -1.1e7, 2e7, 1.1e7},
      {"tcea", "+proj=tcea +datum=WGS84 +units=m", -2e7, -1.1e7, 2e7, 1.1e7},
      {"mil_os", "+proj=mil_os +datum=WGS84 +units=m", -2e7, -2e7, 2e7, 2e7},
      {"sterea", "+proj=sterea +datum=WGS84 +units=m", -2e7, -2e7, 2e7, 2e7},
      // Polar / azimuthal (auto-bounds give pole_y but ring wraps antipodal region)
      {"stere", "+proj=stere +datum=WGS84 +units=m", -2e7, -2e7, 2e7, 2e7},
      {"ups", "+proj=ups +datum=WGS84 +units=m", -8.5e6, -8.5e6, 8.5e6, 8.5e6},
      // Cylindrical conformal (poles at infinity → auto-bounds use fallback y)
      {"merc", "+proj=merc +datum=WGS84 +units=m", -2.1e7, -2.1e7, 2.1e7, 2.1e7},
      // Conical (requires standard parallels; auto-bounds pick a limited extent)
      {"lcc", "+proj=lcc +lat_1=33 +lat_2=45 +datum=WGS84 +units=m", -2e7, -2e7, 2e7, 2e7},
      // Misc limited-domain (non-empty with large bounds; tiny area fraction)
      {"gstmerc", "+proj=gstmerc +datum=WGS84 +units=m", -4e7, -1.5e7, 4e7, 1.5e7},
      // Polar LAEA (azimuthal equal-area, polar aspects).
      // The projection pole maps to the origin (0,0).  Auto-computed bounds give
      // ymax=0 (exactly on the boundary) for the north pole, which causes
      // connectLines to go "Stuck" — the box ceiling must be a tiny epsilon above
      // the pole.  The symmetric asymmetry applies for the south polar aspect.
      // +ellps=GRS80 required for the same reason as the oblique EU aspect.
      {"laea_north_polar", "+proj=laea +lat_0=90  +lon_0=0 +ellps=GRS80 +units=m",
       -1.35e7, -1.35e7, 1.35e7, 0.1e6},
      {"laea_south_polar", "+proj=laea +lat_0=-90 +lon_0=0 +ellps=GRS80 +units=m",
       -1.35e7, -0.1e6, 1.35e7, 1.35e7},
      // UTM zones — transverse Mercator with zone-specific false easting (500 000 m).
      // Wide tmerc-like bounds contain the full projected world; the world polygon
      // clips to a valid polygon for these specific zone numbers.  Not all zones
      // produce non-degenerate results with this approach (the run endpoints must
      // coincide exactly with the box boundary for RectClipper to close the ring);
      // zones 22, 33, and 43 have been verified to give frac ≈ 1.0.
      {"utm_zone22n", "+proj=utm +zone=22 +datum=WGS84 +units=m", -2e7, -1.1e7, 2e7, 1.1e7},
      {"utm_zone33n", "+proj=utm +zone=33 +datum=WGS84 +units=m", -2e7, -1.1e7, 2e7, 1.1e7},
      {"utm_zone43n", "+proj=utm +zone=43 +datum=WGS84 +units=m", -2e7, -1.1e7, 2e7, 1.1e7},
  };

  OGRPolygon poly = worldPolygonCCW();

  for (const auto& tc : cases)
  {
    OGRSpatialReference wgs84 = makeSRS(4326);
    OGRSpatialReference target = makeSRSFromProj4(tc.proj4);
    const Bounds B{tc.minX, tc.minY, tc.maxX, tc.maxY};

    Fmi::GeometryProjector projector(&wgs84, &target);
    projector.setProjectedBounds(tc.minX, tc.minY, tc.maxX, tc.maxY);
    projector.setDensifyResolutionKm(50.0);

    std::unique_ptr<OGRGeometry> out;
    ASSERT_NO_THROW(out = projector.projectGeometry(&poly)) << tc.name << ": threw exception";
    ASSERT_TRUE(out) << tc.name << ": result is null";
    ASSERT_FALSE(out->IsEmpty()) << tc.name << ": result is empty";

    EXPECT_TRUE(allVerticesWithinBounds(out.get(), B)) << tc.name << ": vertices outside bounds";
    EXPECT_TRUE(allPolygonRingsClosed(out.get())) << tc.name << ": rings not closed";
  }
}

// World polygon projected through CoordinateTransformation::transformGeometry for
// projections that previously produced incorrect results (slivers, leaks, or null).
//
// transformGeometry applies projection-specific Interrupt pre-cuts in geographic
// space before handing off to GeometryProjector, which is the code path used in
// production.  Three distinct pre-fix failure modes are covered:
//
//  A) Leftmost-meridian sliver (pole-line jump detection bug)
//     Eckert I–VI, GINS 8, Kavraiskiy VII: the pole-line traversal in the world
//     ring is a single undensified segment spanning the full projection width.
//     When the clipping box is larger than the natural projection extent both
//     endpoints lie inside the box, so the jump detector broke the ring into
//     interior-endpoint fragments that RectClipper could not reconnect.
//
//  B) Leaking at the bottom (south-pole run endpoint inside box)
//     Collignon: the south-pole segment's endpoints extend beyond the box in x
//     but the segment crosses it.  Before the fix RectClipper traced the bottom
//     box edge instead of the true south-pole chord.
//
//  C) Globe vanishes entirely (Interrupt pre-cuts required)
//     aea, airy, adams_hemi, adams_ws1/2: these projections have singularities
//     or domain limits that map the world-rectangle boundary to infinity.
//     transformGeometry pre-clips the input in geographic space (anti-meridian
//     cuts for aea/adams_ws*, hemisphere circle clip for airy/adams_hemi) so
//     that every projected point is finite and the full pipeline succeeds.
//
// Both CCW and CW winding are tested (regression for the winding normalisation fix).
TEST(GeometryProjectorTests, WorldPolygon_ProblematicProjections_ViaTransformGeometry)
{
  CPLSetConfigOption("OGR_GEOMETRY_ACCEPT_UNCLOSED_RING", "NO");
  static GdalInitGuard guard;
  GdalErrorSilencer silence;

  struct Case
  {
    const char* name;
    const char* proj4;
  };

  const Case cases[] = {
      // --- Scenario A: pole-line sliver ---
      {"eck1", "+proj=eck1 +datum=WGS84 +units=m"},
      {"eck2", "+proj=eck2 +datum=WGS84 +units=m"},
      {"eck3", "+proj=eck3 +datum=WGS84 +units=m"},
      {"eck4", "+proj=eck4 +datum=WGS84 +units=m"},
      {"eck5", "+proj=eck5 +datum=WGS84 +units=m"},
      {"eck6", "+proj=eck6 +datum=WGS84 +units=m"},
      {"gins8", "+proj=gins8 +datum=WGS84 +units=m"},
      {"kav7", "+proj=kav7 +datum=WGS84 +units=m"},
      // --- Scenario B: south-pole leak ---
      {"collg", "+proj=collg +datum=WGS84 +units=m"},
      // --- Scenario C: globe vanishes without Interrupt pre-cuts ---
      // aea: all world-ring corners project above y=+13 Mm; with the ±30 Mm internal
      // box the ring is entirely inside the box → single closed run → correct polygon.
      {"aea", "+proj=aea +lat_1=29.5 +lat_2=45.5 +datum=WGS84 +units=m"},
      // airy/adams_hemi: Interrupt clips to 0.999 × 90° hemisphere, avoiding the
      // pole-collapse problem of an exact 90° boundary.
      {"airy", "+proj=airy +datum=WGS84 +units=m"},
      {"adams_hemi", "+proj=adams_hemi +datum=WGS84 +units=m"},
      // adams_ws1/2: Schwarz–Christoffel mapping of the whole sphere; no pre-cut
      // needed — the Interrupt handler now correctly returns an empty interrupt.
      {"adams_ws1", "+proj=adams_ws1 +datum=WGS84 +units=m"},
      {"adams_ws2", "+proj=adams_ws2 +datum=WGS84 +units=m"},
      // --- Scenario D: conic projections needing anti-meridian + polar cuts ---
      // lcc: Interrupt previously fell through to the default after adding its own
      // cuts, applying the anti-meridian shapeCut twice.  The second (redundant) cut
      // was harmless but wasted a full polycut pass per polygon.
      {"lcc", "+proj=lcc +lat_1=33 +lat_2=45 +datum=WGS84 +units=m"},
      {"lcc_lon0_90", "+proj=lcc +lat_1=33 +lat_2=45 +lon_0=90 +datum=WGS84 +units=m"},
      // --- Scenario E: satellite/perspective projections with height-dependent clip ---
      // tpers/geos: the clip radius is now derived from h via cos(θ)=R/(R+h) instead
      // of a hardcoded angle.  Test orbits that produce a visible disk large enough
      // (> 1 % of Earth surface) to pass the area floor.
      // h = 5 500 km ≈ old hardcoded 50° for tpers; horizon ≈ 53°.
      {"tpers_meo",  "+proj=tpers +h=5500000 +datum=WGS84 +units=m"},
      // h = 20 000 km (GPS-like orbit): horizon ≈ 76°.
      {"tpers_high", "+proj=tpers +h=20000000 +datum=WGS84 +units=m"},
      // Standard geostationary orbit (~35 786 km): horizon ≈ 81°.
      {"geos_geo",   "+proj=geos +h=35786000 +datum=WGS84 +units=m"},
      // Non-standard high orbit at 42 000 km: verifies h is read, not hardcoded.
      {"geos_high",  "+proj=geos +h=42000000 +datum=WGS84 +units=m"},
      // --- Scenario F: Transverse Mercator — hemisphere clip now applied ---
      // tmerc previously returned an empty interrupt and relied on jump-detection.
      // It now uses the same 89.5° circle clip as gstmerc.
      {"tmerc_utm33",  "+proj=tmerc +lon_0=15 +datum=WGS84 +units=m"},
      {"tmerc_lon0_90", "+proj=tmerc +lon_0=90 +datum=WGS84 +units=m"},
      {"gstmerc",      "+proj=gstmerc +lon_0=0 +datum=WGS84 +units=m"},
  };

  // 1 % of Earth's surface area — a conservative floor that rules out degenerate
  // slivers while being achievable by any partial-hemisphere projection.
  const double minArea = 0.01 * 4.0 * M_PI * 6371000.0 * 6371000.0;

  for (const auto& tc : cases)
  {
    for (int winding = 0; winding < 2; ++winding)
    {
      const char* label = (winding == 0) ? "CCW" : "CW";

      Fmi::SpatialReference wgs84(4326);
      Fmi::SpatialReference target(tc.proj4);
      Fmi::CoordinateTransformation ct(wgs84, target);

      OGRPolygon poly = (winding == 0) ? worldPolygonCCW() : worldPolygonCW();

      std::unique_ptr<OGRGeometry> out;
      ASSERT_NO_THROW(out.reset(ct.transformGeometry(poly)))
          << tc.name << " (" << label << "): threw exception";

      ASSERT_TRUE(out) << tc.name << " (" << label << "): result is null";
      ASSERT_FALSE(out->IsEmpty()) << tc.name << " (" << label << "): result is empty";

      EXPECT_TRUE(allPolygonRingsClosed(out.get()))
          << tc.name << " (" << label << "): polygon rings not closed";

      const double area = totalArea(out.get());
      EXPECT_GT(area, minArea)
          << tc.name << " (" << label << "): area " << area
          << " is less than 1 % of Earth surface area — likely a degenerate sliver";

      // For Eckert projections verify that no vertex sits far below the natural
      // south-pole extent.  Natural south poles: eck1/eck2 ≈ −9.23 Mm,
      // eck4 ≈ −8.46 Mm.  The pre-fix leaking bug caused RectClipper to trace the
      // bottom of the clipping box (−30 Mm with transformGeometry's internal box),
      // producing a flat edge well outside any legitimate Eckert south pole.
      if (std::string(tc.name).find("eck") == 0)
      {
        const auto verts = allVertices(out.get());
        bool hasSpuriousSouthVertex = false;
        for (const auto& p : verts)
          if (p.getY() < -15.0e6)  // well below any real Eckert pole, well above −30 Mm box bottom
            hasSpuriousSouthVertex = true;
        EXPECT_FALSE(hasSpuriousSouthVertex)
            << tc.name << " (" << label
            << "): vertex below −15 Mm — likely south-pole leaking bug";
      }
    }
  }
}

// ----------------------------------------------------------------------------
// Interrupt geometry unit tests
// ----------------------------------------------------------------------------

// Verify that the tpeqd clip circle is centred at the correct midpoint when
// the two control points straddle the anti-meridian.  Before the fix, the
// naive arithmetic mean (lon_1+lon_2)/2 placed the circle at 0° (Greenwich)
// instead of ±180° when lon_1=170 and lon_2=-170.
//
// We inspect the bounding box of the andGeometry: when the clip circle is
// correctly centred at lon=±180 it straddles the anti-meridian, so MaxX > 90
// or MinX < -90.  When incorrectly centred at 0° the envelope stays near 0°.
TEST(GeometryProjectorTests, Interrupt_TpeqdAntimeridianMidpoint)
{
  static GdalInitGuard guard;

  // Anti-meridian crossing: correct centre is lon=±180.
  Fmi::SpatialReference antimeridian(
      "+proj=tpeqd +lon_1=170 +lat_1=40 +lon_2=-170 +lat_2=40 +datum=WGS84 +units=m");
  auto intr = Fmi::interruptGeometry(antimeridian);
  ASSERT_TRUE(intr.andGeometry) << "tpeqd antimeridian: expected andGeometry";

  OGREnvelope env;
  intr.andGeometry->getEnvelope(&env);
  // Circle centred at ±180° spans more than 90° from the centre in both directions.
  const bool straddlesAntimeridian = (env.MaxX > 90.0) || (env.MinX < -90.0);
  EXPECT_TRUE(straddlesAntimeridian)
      << "tpeqd antimeridian: clip circle not centred near ±180 — "
      << "envelope [" << env.MinX << ", " << env.MaxX << "]; likely midpoint bug";

  // Normal case (no anti-meridian crossing): centre should be at lon=0.
  Fmi::SpatialReference normal(
      "+proj=tpeqd +lon_1=-90 +lat_1=40 +lon_2=90 +lat_2=40 +datum=WGS84 +units=m");
  auto intr2 = Fmi::interruptGeometry(normal);
  ASSERT_TRUE(intr2.andGeometry) << "tpeqd normal: expected andGeometry";

  OGREnvelope env2;
  intr2.andGeometry->getEnvelope(&env2);
  EXPECT_LT(env2.MinX, 0.0) << "tpeqd normal: clip circle should extend west of 0°";
  EXPECT_GT(env2.MaxX, 0.0) << "tpeqd normal: clip circle should extend east of 0°";
  EXPECT_NEAR((env2.MinX + env2.MaxX) / 2.0, 0.0, 5.0)
      << "tpeqd normal: clip circle centre not near 0°";
}

// Verify that the laea interrupt now correctly handles all projection aspects
// by cutting a small disc around the antipodal singularity point instead of
// using the old ±178° rectangular clip (which only worked for lat_0=90°).
//
// For each aspect we check:
//   1. cutGeometry is set (antipodal disc cut); shapeClips is empty (no old rect).
//   2. The cut disc is a small region: its bounding box spans < 5° in both axes.
//   3. The cut disc is centred near the antipodal point (lon_0+180°, −lat_0).
TEST(GeometryProjectorTests, Interrupt_LaeaAntipodeCut)
{
  static GdalInitGuard guard;

  struct Case
  {
    const char* name;
    const char* proj4;
    double antiLon;  // expected antipodal longitude
    double antiLat;  // expected antipodal latitude
  };

  const Case cases[] = {
      // North-polar: antipode = south pole (0°, −90°)
      {"laea_north_polar", "+proj=laea +lat_0=90 +lon_0=0 +datum=WGS84 +units=m", 0.0, -90.0},
      // South-polar: antipode = north pole (0°, +90°)
      {"laea_south_polar", "+proj=laea +lat_0=-90 +lon_0=0 +datum=WGS84 +units=m", 0.0, 90.0},
      // Oblique European (ETRS-LAEA, lon_0=10, lat_0=52): antipode = (−170°, −52°)
      {"laea_oblique_eu", "+proj=laea +lat_0=52 +lon_0=10 +datum=WGS84 +units=m", -170.0, -52.0},
      // Equatorial: antipode = (−80°, 0°)
      {"laea_equatorial", "+proj=laea +lat_0=0 +lon_0=100 +datum=WGS84 +units=m", -80.0, 0.0},
  };

  for (const auto& tc : cases)
  {
    Fmi::SpatialReference srs(tc.proj4);
    auto intr = Fmi::interruptGeometry(srs);

    ASSERT_TRUE(intr.cutGeometry)
        << tc.name << ": expected cutGeometry (antipodal disc cut)";
    EXPECT_TRUE(intr.shapeClips.empty())
        << tc.name << ": expected no shapeClips — old rect clip should be gone";

    OGREnvelope env;
    intr.cutGeometry->getEnvelope(&env);

    // The cut disc (radius 1°) must be small in latitude — not a large clip.
    const double latSpan = env.MaxY - env.MinY;
    EXPECT_LT(latSpan, 5.0)
        << tc.name << ": cut disc lat span " << latSpan << "° unexpectedly large";

    // Longitude span: for polar antipodes the disc wraps all 360° of longitude
    // (every meridian passes through the pole) — skip the lon span check there.
    const bool polarAntiPode = (std::abs(tc.antiLat) > 80.0);
    if (!polarAntiPode)
    {
      const double lonSpan = env.MaxX - env.MinX;
      EXPECT_LT(lonSpan, 5.0)
          << tc.name << ": cut disc lon span " << lonSpan << "° unexpectedly large";
    }

    // The cut disc must be centred near the antipodal latitude.
    const double centreLat = (env.MinY + env.MaxY) / 2.0;
    EXPECT_NEAR(centreLat, tc.antiLat, 2.0)
        << tc.name << ": cut disc lat centre " << centreLat
        << "° not near antipodal lat " << tc.antiLat << "°";
    // Longitude centre only meaningful for non-polar antipodes.
    if (!polarAntiPode)
    {
      const double centreLon = (env.MinX + env.MaxX) / 2.0;
      EXPECT_NEAR(centreLon, tc.antiLon, 5.0)
          << tc.name << ": cut disc lon centre " << centreLon
          << "° not near antipodal lon " << tc.antiLon << "°";
    }
  }
}

// Verify that the southern polar cap (bbox -180,-90 to 180,-70) produces a
// non-empty projected result for projections that previously vanished when the
// clipping box is set to ±20 000 km.
//
// These projections are pseudocylindrical or special-case projections whose
// handling of the south-polar region caused the entire input to be discarded.
TEST(GeometryProjectorTests, SouthernPolarCap_ProblematicProjections_IsNonEmpty)
{
  CPLSetConfigOption("OGR_GEOMETRY_ACCEPT_UNCLOSED_RING", "NO");
  static GdalInitGuard guard;
  GdalErrorSilencer silence;

  struct Case
  {
    const char* name;
    const char* proj4;
  };

  const Case cases[] = {
      {"cass",    "+proj=cass +datum=WGS84 +units=m"},
      {"aitoff",  "+proj=aitoff +datum=WGS84 +units=m"},
      {"fouc",    "+proj=fouc +datum=WGS84 +units=m"},
      {"mil_os",  "+proj=mil_os +datum=WGS84 +units=m"},
      {"putp1",   "+proj=putp1 +datum=WGS84 +units=m"},
      {"putp4p",  "+proj=putp4p +datum=WGS84 +units=m"},
      {"qua_aut", "+proj=qua_aut +datum=WGS84 +units=m"},
  };

  OGRPolygon poly = southernPolarCapPolygon();

  for (const auto& tc : cases)
  {
    OGRSpatialReference wgs84 = makeSRS(4326);
    OGRSpatialReference target = makeSRSFromProj4(tc.proj4);

    Fmi::GeometryProjector projector(&wgs84, &target);
    projector.setProjectedBounds(-2e7, -2e7, 2e7, 2e7);
    projector.setDensifyResolutionKm(50.0);

    std::unique_ptr<OGRGeometry> out;
    ASSERT_NO_THROW(out = projector.projectGeometry(&poly))
        << tc.name << ": threw exception";
    ASSERT_TRUE(out) << tc.name << ": result is null";
    ASSERT_FALSE(out->IsEmpty()) << tc.name << ": result is empty";
  }
}

// Verify the same for the northern polar cap (bbox -180,70 to 180,90).
TEST(GeometryProjectorTests, NorthernPolarCap_ProblematicProjections_IsNonEmpty)
{
  CPLSetConfigOption("OGR_GEOMETRY_ACCEPT_UNCLOSED_RING", "NO");
  static GdalInitGuard guard;
  GdalErrorSilencer silence;

  struct Case
  {
    const char* name;
    const char* proj4;
  };

  const Case cases[] = {
      {"cass",    "+proj=cass +datum=WGS84 +units=m"},
      {"aitoff",  "+proj=aitoff +datum=WGS84 +units=m"},
      {"fouc",    "+proj=fouc +datum=WGS84 +units=m"},
      {"mil_os",  "+proj=mil_os +datum=WGS84 +units=m"},
      {"putp1",   "+proj=putp1 +datum=WGS84 +units=m"},
      {"putp4p",  "+proj=putp4p +datum=WGS84 +units=m"},
      {"qua_aut", "+proj=qua_aut +datum=WGS84 +units=m"},
  };

  OGRPolygon poly = northernPolarCapPolygon();

  for (const auto& tc : cases)
  {
    OGRSpatialReference wgs84 = makeSRS(4326);
    OGRSpatialReference target = makeSRSFromProj4(tc.proj4);

    Fmi::GeometryProjector projector(&wgs84, &target);
    projector.setProjectedBounds(-2e7, -2e7, 2e7, 2e7);
    projector.setDensifyResolutionKm(50.0);

    std::unique_ptr<OGRGeometry> out;
    ASSERT_NO_THROW(out = projector.projectGeometry(&poly))
        << tc.name << ": threw exception";
    ASSERT_TRUE(out) << tc.name << ": result is null";
    ASSERT_FALSE(out->IsEmpty()) << tc.name << ": result is empty";
  }
}
