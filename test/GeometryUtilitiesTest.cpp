// Tests for OGR::inside, OGR::normalizeWindingOrder, CoordinateMatrix and ProjInfo

#include "CoordinateMatrix.h"
#include "CoordinateTransformation.h"
#include "OGR.h"
#include "ProjInfo.h"
#include "SpatialReference.h"
#include <macgyver/StaticCleanup.h>
#include <regression/tframe.h>
#include <cmath>
#include <memory>
#include <string>
#include <ogr_geometry.h>

using namespace std;

namespace Tests
{
std::unique_ptr<OGRGeometry> geometry(const char* wkt)
{
  OGRGeometry* geom = nullptr;
  if (OGRGeometryFactory::createFromWkt(wkt, nullptr, &geom) != OGRERR_NONE)
    throw std::runtime_error(std::string("Failed to parse ") + wkt);
  return std::unique_ptr<OGRGeometry>(geom);
}

// ----------------------------------------------------------------------

void inside()
{
  auto poly = geometry("POLYGON ((0 0,10 0,10 10,0 10,0 0),(4 4,6 4,6 6,4 6,4 4))");

  if (!Fmi::OGR::inside(*poly, 2, 2))
    TEST_FAILED("2,2 should be inside the polygon");
  if (Fmi::OGR::inside(*poly, 5, 5))
    TEST_FAILED("5,5 is in the hole and should not be inside");
  if (Fmi::OGR::inside(*poly, 11, 5))
    TEST_FAILED("11,5 should not be inside");
  if (Fmi::OGR::inside(*poly, -1, -1))
    TEST_FAILED("-1,-1 should not be inside");

  auto multi = geometry("MULTIPOLYGON (((0 0,1 0,1 1,0 1,0 0)),((5 5,6 5,6 6,5 6,5 5)))");
  if (!Fmi::OGR::inside(*multi, 0.5, 0.5) || !Fmi::OGR::inside(*multi, 5.5, 5.5))
    TEST_FAILED("Points in both parts of the multipolygon should be inside");
  if (Fmi::OGR::inside(*multi, 3, 3))
    TEST_FAILED("3,3 is between the parts and should not be inside");

  auto line = geometry("LINESTRING (0 0,10 10)");
  if (Fmi::OGR::inside(*line, 5, 5))
    TEST_FAILED("A line has no inside");

  TEST_PASSED();
}

// ----------------------------------------------------------------------

void winding_order()
{
  // Clockwise exterior and counter-clockwise hole
  auto geom = geometry("POLYGON ((0 0,0 10,10 10,10 0,0 0),(4 4,6 4,6 6,4 6,4 4))");
  Fmi::OGR::normalizeWindingOrder(geom.get());
  auto* poly = dynamic_cast<OGRPolygon*>(geom.get());
  if (poly == nullptr)
    TEST_FAILED("Expected a polygon");
  if (poly->getExteriorRing()->isClockwise())
    TEST_FAILED("The exterior ring should be counter-clockwise");
  if (!poly->getInteriorRing(0)->isClockwise())
    TEST_FAILED("The interior ring should be clockwise");

  // Normalizing again must not change anything
  std::string wkt1 = Fmi::OGR::exportToWkt(*geom);
  Fmi::OGR::normalizeWindingOrder(geom.get());
  if (Fmi::OGR::exportToWkt(*geom) != wkt1)
    TEST_FAILED("Normalizing twice should be idempotent");

  TEST_PASSED();
}

// ----------------------------------------------------------------------

void coordinate_matrix()
{
  Fmi::CoordinateMatrix m(5, 3, 10, 60, 30, 70);
  if (m.width() != 5 || m.height() != 3)
    TEST_FAILED("Matrix should be 5x3");
  if (m.x(0, 0) != 10 || m.y(0, 0) != 60 || m.x(4, 2) != 30 || m.y(4, 2) != 70)
    TEST_FAILED("Corner coordinates are wrong");
  if (std::abs(m.x(1, 0) - 15) > 1e-12 || std::abs(m.y(0, 1) - 65) > 1e-12)
    TEST_FAILED("Grid spacing should be 5 x 5");

  // Equal grids have equal hashes, different extents differ
  Fmi::CoordinateMatrix same(5, 3, 10, 60, 30, 70);
  Fmi::CoordinateMatrix other(5, 3, 10, 60, 31, 70);
  if (m.hashValue() != same.hashValue())
    TEST_FAILED("Equal matrices should have equal hash values");
  if (m.hashValue() == other.hashValue())
    TEST_FAILED("Different extents should give different hash values");

  // Transforming to another projection changes the coordinates and the hash
  Fmi::CoordinateTransformation trans("WGS84", "EPSG:3067");
  auto projected = m;
  if (!projected.transform(trans))
    TEST_FAILED("Transformation to EPSG:3067 failed");
  if (projected.hashValue() == m.hashValue())
    TEST_FAILED("Transformation should change the hash value");
  // 27E,60N is the false easting meridian of TM35FIN
  Fmi::CoordinateMatrix center(1, 1, 27, 60, 27, 60);
  center.transform(trans);
  if (std::abs(center.x(0, 0) - 500000) > 0.01)
    TEST_FAILED("27E should map to x=500000 in EPSG:3067, got " + std::to_string(center.x(0, 0)));

  TEST_PASSED();
}

// ----------------------------------------------------------------------

void proj_info()
{
  Fmi::ProjInfo info("+proj=stere +lat_0=90 +lat_ts=60 +lon_0=20 +R=6371220 +units=m +no_defs");
  if (info.getString("proj") != std::string("stere"))
    TEST_FAILED("proj should be stere");
  if (!info.getDouble("lon_0") || *info.getDouble("lon_0") != 20)
    TEST_FAILED("lon_0 should be 20");
  if (!info.getDouble("R") || *info.getDouble("R") != 6371220)
    TEST_FAILED("R should be 6371220");
  if (!info.getBool("no_defs"))
    TEST_FAILED("no_defs should be set");
  if (info.getDouble("lat_1"))
    TEST_FAILED("lat_1 should not be set");

  if (!info.erase("no_defs") || info.getBool("no_defs"))
    TEST_FAILED("Erasing no_defs should succeed");
  if (info.erase("no_defs"))
    TEST_FAILED("Erasing a missing setting should fail");

  TEST_PASSED();
}

// ----------------------------------------------------------------------

class tests : public tframe::tests
{
  const char* error_message_prefix() const override { return "\n\t"; }
  void test() override
  {
    TEST(inside);
    TEST(winding_order);
    TEST(coordinate_matrix);
    TEST(proj_info);
  }
};

}  // namespace Tests

int main()
{
  Fmi::StaticCleanup::AtExit cleanup;
  cout << endl << "Geometry utilities tester" << endl << "=========================" << endl;
  Tests::tests t;
  return t.run();
}
