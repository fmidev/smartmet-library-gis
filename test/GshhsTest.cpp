#include "Gshhs.h"

#include <ogr_geometry.h>
#include <ogr_spatialref.h>
#include <regression/tframe.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using namespace std;

namespace
{
// Mirror the on-disk layout used by the legacy GSHHS v1.x binary format.
struct DiskHeader
{
  std::int32_t id;
  std::int32_t n;
  std::int32_t level;
  std::int32_t west;
  std::int32_t east;
  std::int32_t south;
  std::int32_t north;
  std::int32_t area;
  std::int16_t greenwich;
  std::int16_t source;
};
static_assert(sizeof(DiskHeader) == 36, "Unexpected GSHHS header size");

struct DiskPoint
{
  std::int32_t x;
  std::int32_t y;
};

struct PolygonSpec
{
  int id;
  int level;
  std::int32_t area_tenth_km2;  // header.area is in 1/10 km^2
  std::vector<std::pair<double, double>> lonlats;
};

// Encode µdeg
std::int32_t udeg(double v) { return static_cast<std::int32_t>(v * 1.0e6); }

std::string make_tmp_path(const char* tag)
{
  std::string path = std::string("/tmp/gshhs_test_") + tag + "_" + std::to_string(::getpid()) + ".b";
  return path;
}

void write_file(const std::string& path, const std::vector<PolygonSpec>& polys)
{
  std::ofstream out(path, std::ios::binary);
  for (const auto& p : polys)
  {
    DiskHeader h{};
    h.id = p.id;
    h.n = static_cast<std::int32_t>(p.lonlats.size());
    h.level = p.level;
    h.area = p.area_tenth_km2;
    h.greenwich = 0;
    h.source = 1;

    std::int32_t w = INT32_MAX;
    std::int32_t e = INT32_MIN;
    std::int32_t s = INT32_MAX;
    std::int32_t n = INT32_MIN;
    for (auto& [lon, lat] : p.lonlats)
    {
      w = std::min(w, udeg(lon));
      e = std::max(e, udeg(lon));
      s = std::min(s, udeg(lat));
      n = std::max(n, udeg(lat));
    }
    h.west = w;
    h.east = e;
    h.south = s;
    h.north = n;

    out.write(reinterpret_cast<const char*>(&h), sizeof(h));
    for (auto& [lon, lat] : p.lonlats)
    {
      DiskPoint pt{udeg(lon), udeg(lat)};
      out.write(reinterpret_cast<const char*>(&pt), sizeof(pt));
    }
  }
}

class TmpFile
{
 public:
  explicit TmpFile(std::string p) : path_(std::move(p)) {}
  ~TmpFile() { std::remove(path_.c_str()); }
  const std::string& path() const { return path_; }

 private:
  std::string path_;
};

}  // namespace

namespace Tests
{
// ----------------------------------------------------------------------

void empty_file()
{
  TmpFile tmp(make_tmp_path("empty"));
  std::ofstream(tmp.path(), std::ios::binary).close();

  auto geom = Fmi::Gshhs::read(tmp.path());
  if (!geom)
    TEST_FAILED("Empty file should yield non-null geometry");
  if (geom->getGeometryType() != wkbMultiPolygon)
    TEST_FAILED("Expected MultiPolygon");
  auto* mp = geom->toMultiPolygon();
  if (mp->getNumGeometries() != 0)
    TEST_FAILED("Empty file should yield empty MultiPolygon");

  auto* srs = geom->getSpatialReference();
  if (!srs || srs->GetEPSGGeogCS() != 4326)
    TEST_FAILED("Result should be in WGS84");

  TEST_PASSED();
}

// ----------------------------------------------------------------------

void single_square()
{
  TmpFile tmp(make_tmp_path("square"));
  // Open ring (no duplicate end point) — the reader must close it.
  PolygonSpec sq{1, 1, 1000, {{0, 0}, {10, 0}, {10, 10}, {0, 10}}};
  write_file(tmp.path(), {sq});

  auto geom = Fmi::Gshhs::read(tmp.path());
  auto* mp = geom->toMultiPolygon();
  if (mp->getNumGeometries() != 1)
    TEST_FAILED("Expected exactly one polygon");

  auto* poly = static_cast<OGRPolygon*>(mp->getGeometryRef(0));
  auto* ring = poly->getExteriorRing();
  if (ring->getNumPoints() != 5)
    TEST_FAILED("Ring should be closed (5 points for a square)");

  OGRPoint p0;
  OGRPoint p4;
  ring->getPoint(0, &p0);
  ring->getPoint(4, &p4);
  if (p0.getX() != p4.getX() || p0.getY() != p4.getY())
    TEST_FAILED("First and last point should match after closing");

  TEST_PASSED();
}

// ----------------------------------------------------------------------

void bbox_filter_skips_outside()
{
  TmpFile tmp(make_tmp_path("bbox"));
  PolygonSpec asia{1, 1, 1000, {{100, 30}, {110, 30}, {110, 40}, {100, 40}}};
  PolygonSpec europe{2, 1, 1000, {{0, 50}, {10, 50}, {10, 60}, {0, 60}}};
  write_file(tmp.path(), {asia, europe});

  Fmi::Gshhs::Options opts;
  opts.minLongitude = -10;
  opts.maxLongitude = 30;
  opts.minLatitude = 40;
  opts.maxLatitude = 70;

  auto geom = Fmi::Gshhs::read(tmp.path(), opts);
  auto* mp = geom->toMultiPolygon();
  if (mp->getNumGeometries() != 1)
    TEST_FAILED("Bounding box should keep europe and skip asia");
  TEST_PASSED();
}

// ----------------------------------------------------------------------

void min_island_area_filter()
{
  TmpFile tmp(make_tmp_path("island_area"));
  // header.area is in 1/10 km^2: 5000 → 500 km^2, 50 → 5 km^2
  PolygonSpec big{1, 1, 5000, {{0, 0}, {1, 0}, {1, 1}, {0, 1}}};
  PolygonSpec small{2, 1, 50, {{2, 2}, {3, 2}, {3, 3}, {2, 3}}};
  write_file(tmp.path(), {big, small});

  Fmi::Gshhs::Options opts;
  opts.minIslandAreaKm2 = 100;

  auto geom = Fmi::Gshhs::read(tmp.path(), opts);
  auto* mp = geom->toMultiPolygon();
  if (mp->getNumGeometries() != 1)
    TEST_FAILED("Min-island-area filter should keep only the big polygon");
  TEST_PASSED();
}

// ----------------------------------------------------------------------

void min_lake_area_filter()
{
  TmpFile tmp(make_tmp_path("lake_area"));
  // Both are level-2 (lakes); only minLakeAreaKm2 should affect them.
  PolygonSpec big_lake{1, 2, 5000, {{0, 0}, {1, 0}, {1, 1}, {0, 1}}};
  PolygonSpec small_lake{2, 2, 50, {{2, 2}, {3, 2}, {3, 3}, {2, 3}}};
  // A level-1 island the same size as the small lake — must NOT be filtered
  // by minLakeAreaKm2.
  PolygonSpec small_island{3, 1, 50, {{4, 4}, {5, 4}, {5, 5}, {4, 5}}};
  write_file(tmp.path(), {big_lake, small_lake, small_island});

  Fmi::Gshhs::Options opts;
  opts.minLakeAreaKm2 = 100;

  auto geom = Fmi::Gshhs::read(tmp.path(), opts);
  auto* mp = geom->toMultiPolygon();
  if (mp->getNumGeometries() != 2)
    TEST_FAILED("Min-lake-area filter should drop only the small lake");
  TEST_PASSED();
}

// ----------------------------------------------------------------------

void min_lake_roundness_filter()
{
  TmpFile tmp(make_tmp_path("roundness"));

  // Round-ish lake: 1°×1° square at the equator.
  // Real area ≈ 111.32² ≈ 12392 km²; perimeter ≈ 4·111.32 = 445.28 km.
  // Compactness = 4π·12392/445.28² ≈ 0.785 (the theoretical π/4 of a square).
  PolygonSpec round_lake{1, 2, 123920,  // 12392 km² × 10
                         {{0, 0}, {1, 0}, {1, 1}, {0, 1}}};

  // Ragged lake: thin 1°×0.01° rectangle at the equator.
  // Compactness ≈ 0.04 — should be dropped by any reasonable threshold.
  PolygonSpec ragged_lake{2, 2, 1239,  // 123.9 km² × 10
                          {{2, 0}, {3, 0}, {3, 0.01}, {2, 0.01}}};

  write_file(tmp.path(), {round_lake, ragged_lake});

  // Threshold 0.5: keeps the square, drops the thin rectangle.
  {
    Fmi::Gshhs::Options opts;
    opts.minLakeRoundness = 0.5;
    auto geom = Fmi::Gshhs::read(tmp.path(), opts);
    auto* mp = geom->toMultiPolygon();
    if (mp->getNumGeometries() != 1)
      TEST_FAILED("Roundness threshold 0.5 should keep only the round lake");
  }

  // Threshold 0.9: drops both (square's compactness is π/4 ≈ 0.785 < 0.9).
  {
    Fmi::Gshhs::Options opts;
    opts.minLakeRoundness = 0.9;
    auto geom = Fmi::Gshhs::read(tmp.path(), opts);
    auto* mp = geom->toMultiPolygon();
    if (mp->getNumGeometries() != 0)
      TEST_FAILED("Roundness threshold 0.9 should drop both lakes");
  }

  // Threshold 0 (disabled): keeps both.
  {
    Fmi::Gshhs::Options opts;
    auto geom = Fmi::Gshhs::read(tmp.path(), opts);
    auto* mp = geom->toMultiPolygon();
    if (mp->getNumGeometries() != 2)
      TEST_FAILED("Disabled roundness filter should keep both lakes");
  }

  TEST_PASSED();
}

// ----------------------------------------------------------------------

void roundness_does_not_filter_islands()
{
  TmpFile tmp(make_tmp_path("roundness_island"));
  // A level-1 ragged "island" that would fail any roundness check —
  // the filter must NOT apply to top-level polygons.
  PolygonSpec ragged_island{1, 1, 1239, {{2, 0}, {3, 0}, {3, 0.01}, {2, 0.01}}};
  write_file(tmp.path(), {ragged_island});

  Fmi::Gshhs::Options opts;
  opts.minLakeRoundness = 0.9;  // would drop a lake with this shape

  auto geom = Fmi::Gshhs::read(tmp.path(), opts);
  auto* mp = geom->toMultiPolygon();
  if (mp->getNumGeometries() != 1)
    TEST_FAILED("Roundness filter must not apply to islands (level==1)");
  TEST_PASSED();
}

// ----------------------------------------------------------------------

void max_level_filter()
{
  TmpFile tmp(make_tmp_path("level"));
  PolygonSpec land{1, 1, 1000, {{0, 0}, {10, 0}, {10, 10}, {0, 10}}};
  PolygonSpec lake{2, 2, 100, {{2, 2}, {4, 2}, {4, 4}, {2, 4}}};
  PolygonSpec island{3, 3, 10, {{2.5, 2.5}, {3.5, 2.5}, {3.5, 3.5}, {2.5, 3.5}}};
  write_file(tmp.path(), {land, lake, island});

  Fmi::Gshhs::Options opts;
  opts.maxLevel = 1;
  auto geom = Fmi::Gshhs::read(tmp.path(), opts);
  auto* mp = geom->toMultiPolygon();
  if (mp->getNumGeometries() != 1)
    TEST_FAILED("maxLevel=1 should keep only land polygon");
  TEST_PASSED();
}

// ----------------------------------------------------------------------

void corrupt_file_throws()
{
  TmpFile tmp(make_tmp_path("corrupt"));
  std::ofstream out(tmp.path(), std::ios::binary);
  // Write 12 random bytes — too short for a header.
  const char garbage[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
  out.write(garbage, sizeof(garbage));
  out.close();

  // 12 bytes < 36 byte header → fread returns 0 → empty multipolygon (legal).
  // To force an error we need a header with valid level-detection but corrupt
  // n. Easier: a header indicating 1000 points but no body.
  TmpFile tmp2(make_tmp_path("truncated"));
  PolygonSpec p{1, 1, 100, {}};
  // We'll write the header with n=1000 but no point bytes.
  DiskHeader h{};
  h.id = 1;
  h.n = 1000;
  h.level = 1;
  h.area = 100;
  h.west = 0;
  h.east = 10000000;
  h.south = 0;
  h.north = 10000000;
  std::ofstream out2(tmp2.path(), std::ios::binary);
  out2.write(reinterpret_cast<const char*>(&h), sizeof(h));
  out2.close();

  bool threw = false;
  try
  {
    Fmi::Gshhs::read(tmp2.path());
  }
  catch (...)
  {
    threw = true;
  }
  if (!threw)
    TEST_FAILED("Truncated file should throw");
  TEST_PASSED();
}

// ----------------------------------------------------------------------

void missing_file_throws()
{
  bool threw = false;
  try
  {
    Fmi::Gshhs::read("/nonexistent/path/file.b");
  }
  catch (...)
  {
    threw = true;
  }
  if (!threw)
    TEST_FAILED("Missing file should throw");
  TEST_PASSED();
}

// Test driver
class tests : public tframe::tests
{
  virtual const char* error_message_prefix() const { return "\n\t"; }
  void test()
  {
    TEST(empty_file);
    TEST(single_square);
    TEST(bbox_filter_skips_outside);
    TEST(min_island_area_filter);
    TEST(min_lake_area_filter);
    TEST(min_lake_roundness_filter);
    TEST(roundness_does_not_filter_islands);
    TEST(max_level_filter);
    TEST(corrupt_file_throws);
    TEST(missing_file_throws);
  }
};

}  // namespace Tests

int main()
{
  cout << endl
       << "Gshhs tester\n"
          "============\n";
  Tests::tests t;
  return t.run();
}
