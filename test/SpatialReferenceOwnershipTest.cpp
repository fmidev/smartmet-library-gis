// Regression tests for spatial-reference ownership.
//
// The contract: OGRSpatialReferenceFactory::Create() gives the caller its own
// OGRSpatialReference. Nobody else holds it, so the caller may read it, mutate
// it, hand it to a geometry, or keep it as long as it likes, and no other caller
// is affected. GDAL rebuilds internal PROJ state inside the object even from
// const methods, and PROJ's contract is one thread at a time per PJ, so a caller
// has to own what it operates on. See ../docs/proj-gdal-thread-safety.md.
//
// Before this change Create() returned one shared cached object per definition
// string, and most of the tests below failed by design.

#include "CoordinateTransformation.h"
#include "OGRSpatialReferenceFactory.h"
#include "ProjInfo.h"
#include "SpatialReference.h"

#include <ogr_geometry.h>
#include <ogr_spatialref.h>

#include <atomic>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

namespace
{
const char *kCRS = "EPSG:2393";  // KKJ / Finland Uniform, the CRS from the incident

std::string wkt_of(const OGRSpatialReference &srs)
{
  char *wkt = nullptr;
  if (srs.exportToWkt(&wkt) != OGRERR_NONE || wkt == nullptr)
    return {};
  const std::string result(wkt);
  CPLFree(wkt);
  return result;
}
}  // namespace

// ---------------------------------------------------------------------------
// Every caller gets its own object
// ---------------------------------------------------------------------------

TEST(SpatialReferenceOwnership, TwoCreatesGiveDifferentObjects)
{
  auto a = Fmi::OGRSpatialReferenceFactory::Create(kCRS);
  auto b = Fmi::OGRSpatialReferenceFactory::Create(kCRS);

  ASSERT_TRUE(a);
  ASSERT_TRUE(b);
  EXPECT_NE(a.get(), b.get()) << "callers must not be handed the same object";
}

// Note on what is deliberately NOT asserted here: that a *sequential* Create()
// returns a different address than one that has already been released. Nothing
// is recycled, but the allocator is free to hand the freed block straight back,
// so address identity proves nothing once the first object is gone. The property
// that matters - a later caller never inherits an earlier caller's modifications
// - is asserted by state in MutationIsNotVisibleToLaterCallers below. Address
// comparisons are only meaningful while both objects are alive.

TEST(SpatialReferenceOwnership, CopiesAreEquivalent)
{
  auto a = Fmi::OGRSpatialReferenceFactory::Create(kCRS);
  auto b = Fmi::OGRSpatialReferenceFactory::Create(kCRS);

  const auto wkt_a = wkt_of(*a);
  const auto wkt_b = wkt_of(*b);

  EXPECT_FALSE(wkt_a.empty());
  EXPECT_EQ(wkt_a, wkt_b) << "distinct copies must describe the same CRS";
  EXPECT_EQ(a->GetAxisMappingStrategy(), OAMS_TRADITIONAL_GIS_ORDER);
  EXPECT_EQ(b->GetAxisMappingStrategy(), OAMS_TRADITIONAL_GIS_ORDER);
}

TEST(SpatialReferenceOwnership, CreateByEpsgCodeMatchesCreateByName)
{
  auto by_name = Fmi::OGRSpatialReferenceFactory::Create("EPSG:3067");
  auto by_code = Fmi::OGRSpatialReferenceFactory::Create(3067);

  EXPECT_NE(by_name.get(), by_code.get());
  EXPECT_EQ(wkt_of(*by_name), wkt_of(*by_code));
}

TEST(SpatialReferenceOwnership, DistinctSpatialReferencesDoNotShareOneObject)
{
  // Fmi::SpatialReference caches its derived values by definition string. That
  // cache must not also pin one shared OGRSpatialReference, or every caller of
  // get() is back to sharing.
  Fmi::SpatialReference a(kCRS);
  Fmi::SpatialReference b(kCRS);

  EXPECT_EQ(a.hashValue(), b.hashValue()) << "derived values should still be cached";
  EXPECT_NE(a.get(), b.get()) << "but the objects handed out must be private";
}

TEST(SpatialReferenceOwnership, CopyingASpatialReferenceSharesDerivedValues)
{
  // The point of copying Fmi::SpatialReference rather than re-deriving from an
  // OGRSpatialReference: the expensive part (WKT, PROJ string, EPSG, flags) is
  // immutable and shared, so a copy is cheap and its accessors stay instant.
  Fmi::SpatialReference original(kCRS);
  Fmi::SpatialReference copy(original);

  EXPECT_EQ(original.hashValue(), copy.hashValue());
  EXPECT_EQ(&original.WKT(), &copy.WKT()) << "derived values should be shared, not recomputed";
  EXPECT_EQ(original.projInfo().projStr(), copy.projInfo().projStr());
  EXPECT_EQ(original.getEPSG(), copy.getEPSG());

  // ...but the objects are still separate.
  EXPECT_NE(original.get(), copy.get());
}

// ---------------------------------------------------------------------------
// Callers own their object and may modify it
// ---------------------------------------------------------------------------

TEST(SpatialReferenceOwnership, MutatingMyObjectDoesNotAffectAnyoneElse)
{
  auto victim = Fmi::OGRSpatialReferenceFactory::Create(kCRS);
  auto mine = Fmi::OGRSpatialReferenceFactory::Create(kCRS);

  ASSERT_TRUE(mine->IsProjected());
  mine->SetFromUserInput("EPSG:4326");  // legitimate: the object is mine

  EXPECT_TRUE(victim->IsProjected()) << "another holder must be unaffected";
  EXPECT_FALSE(victim->IsGeographic());
}

TEST(SpatialReferenceOwnership, MutationIsNotVisibleToLaterCallers)
{
  {
    auto mutated = Fmi::OGRSpatialReferenceFactory::Create(kCRS);
    mutated->SetFromUserInput("EPSG:4326");
  }

  auto fresh = Fmi::OGRSpatialReferenceFactory::Create(kCRS);
  EXPECT_TRUE(fresh->IsProjected()) << "the next caller must get an intact CRS";
  EXPECT_FALSE(fresh->IsGeographic());
}

TEST(SpatialReferenceOwnership, MutatingAnObjectDoesNotCorruptTheThreadsSample)
{
  // The clone source is a per-thread sample. Mutating a clone must not reach it,
  // or one caller would poison every later request served by the same thread.
  for (int round = 0; round < 5; ++round)
  {
    auto srs = Fmi::OGRSpatialReferenceFactory::Create(kCRS);
    ASSERT_TRUE(srs->IsProjected()) << "sample corrupted after " << round << " mutations";
    srs->SetFromUserInput("EPSG:4326");
  }
}

TEST(SpatialReferenceOwnership, TransformsStayCorrectAfterAMutatingCaller)
{
  // The production symptom was a silently degenerate transformation that
  // persisted for the life of the process. Hold a mutated object and verify that
  // transformations built meanwhile are still right.
  double x0 = 25.0;
  double y0 = 60.0;
  Fmi::CoordinateTransformation reference("WGS84", "EPSG:3067");
  ASSERT_TRUE(reference.transform(x0, y0));

  const std::string key = Fmi::SpatialReference("WGS84").projInfo().projStr();
  auto poisoner = Fmi::OGRSpatialReferenceFactory::Create(key);
  poisoner->SetFromUserInput("EPSG:3067");

  double x1 = 25.0;
  double y1 = 60.0;
  Fmi::CoordinateTransformation after("WGS84", "EPSG:3035");  // a cold pair
  ASSERT_TRUE(after.transform(x1, y1));

  // Independently computed with a wholly private pair of objects.
  OGRSpatialReference src;
  OGRSpatialReference dst;
  ASSERT_EQ(src.SetFromUserInput("EPSG:4326"), OGRERR_NONE);
  ASSERT_EQ(dst.SetFromUserInput("EPSG:3035"), OGRERR_NONE);
  src.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
  dst.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
  double x2 = 25.0;
  double y2 = 60.0;
  auto *ct = OGRCreateCoordinateTransformation(&src, &dst);
  ASSERT_NE(ct, nullptr);
  ASSERT_TRUE(ct->Transform(1, &x2, &y2));
  delete ct;

  EXPECT_NEAR(x1, x2, 1e-6) << "a mutating caller must not corrupt other transformations";
  EXPECT_NEAR(y1, y2, 1e-6);
}

// ---------------------------------------------------------------------------
// Handing the object to a geometry
// ---------------------------------------------------------------------------

TEST(SpatialReferenceOwnership, GeometryMayOutliveTheCallersReference)
{
  // A dozen call sites across wms/contour/download do this: the geometry takes
  // its own GDAL reference and keeps it for its whole life, well past the
  // caller's shared_ptr. That has to stay safe, and the object must never be
  // handed to anyone else while the geometry is using it.
  OGRPoint point(25.0, 60.0);
  const void *given = nullptr;
  {
    auto srs = Fmi::OGRSpatialReferenceFactory::Create(kCRS);
    given = srs.get();
    point.assignSpatialReference(srs.get());
  }  // our reference is gone; the geometry's is not

  ASSERT_NE(point.getSpatialReference(), nullptr);
  EXPECT_TRUE(point.getSpatialReference()->IsProjected()) << "geometry's CRS was damaged";

  auto next = Fmi::OGRSpatialReferenceFactory::Create(kCRS);
  EXPECT_NE(next.get(), given) << "the object the geometry uses was given to someone else";
}

// ---------------------------------------------------------------------------
// Concurrency
// ---------------------------------------------------------------------------

TEST(SpatialReferenceOwnership, ConcurrentHoldersNeverAliasOneObject)
{
  constexpr int kThreads = 8;
  constexpr int kRounds = 400;
  const std::vector<std::string> crs = {"EPSG:4326", "EPSG:2393", "EPSG:3067", "EPSG:3857"};

  std::mutex mutex;
  std::set<const void *> live;  // objects currently held
  std::atomic<int> overlaps{0};

  auto body = [&](int id)
  {
    for (int round = 0; round < kRounds; ++round)
    {
      const auto &name = crs[(id + round) % crs.size()];
      auto srs = Fmi::OGRSpatialReferenceFactory::Create(name);
      {
        std::lock_guard<std::mutex> lock(mutex);
        if (!live.insert(srs.get()).second)
          ++overlaps;
      }
      // Do the kind of work that rebuilds PROJ state inside the object.
      (void)wkt_of(*srs);
      (void)srs->IsProjected();
      {
        std::lock_guard<std::mutex> lock(mutex);
        live.erase(srs.get());
      }
    }
  };

  std::vector<std::thread> threads;
  for (int i = 0; i < kThreads; ++i)
    threads.emplace_back(body, i);
  for (auto &t : threads)
    t.join();

  EXPECT_EQ(overlaps.load(), 0) << "an object was held by two threads at once";
}

TEST(SpatialReferenceOwnership, ConcurrentMutatorsDoNotDisturbEachOther)
{
  // Every thread mutates its own object as hard as it can while checking that
  // what it was given was intact to begin with.
  std::atomic<int> bad{0};

  auto body = [&](int id)
  {
    for (int round = 0; round < 200; ++round)
    {
      auto srs = Fmi::OGRSpatialReferenceFactory::Create(kCRS);
      if (srs->IsProjected() == 0 || srs->IsGeographic() != 0)
        ++bad;
      srs->SetFromUserInput((id % 2) == 0 ? "EPSG:4326" : "EPSG:3857");
    }
  };

  std::vector<std::thread> threads;
  for (int i = 0; i < 8; ++i)
    threads.emplace_back(body, i);
  for (auto &t : threads)
    t.join();

  EXPECT_EQ(bad.load(), 0) << "a caller was handed an object somebody else had modified";
}

TEST(SpatialReferenceOwnership, ConcurrentTransformsAgreeWithTheSingleThreadedResult)
{
  const std::vector<std::string> crs = {"EPSG:2393", "EPSG:3067", "EPSG:3857", "EPSG:3035"};

  // Baseline, single-threaded.
  std::vector<std::pair<double, double>> expected;
  for (const auto &name : crs)
  {
    double x = 25.0;
    double y = 60.0;
    Fmi::CoordinateTransformation t("WGS84", name);
    ASSERT_TRUE(t.transform(x, y));
    expected.emplace_back(x, y);
  }

  std::atomic<int> wrong{0};
  auto body = [&](int id)
  {
    for (int round = 0; round < 300; ++round)
    {
      const size_t i = (id + round) % crs.size();
      double x = 25.0;
      double y = 60.0;
      Fmi::CoordinateTransformation t("WGS84", crs[i]);
      if (!t.transform(x, y) || x != expected[i].first || y != expected[i].second)
        ++wrong;
    }
  };

  std::vector<std::thread> threads;
  for (int i = 0; i < 8; ++i)
    threads.emplace_back(body, i);
  for (auto &t : threads)
    t.join();

  EXPECT_EQ(wrong.load(), 0) << "concurrent transformations drifted from the baseline";
}

// ---------------------------------------------------------------------------
// Per-thread sample store
// ---------------------------------------------------------------------------

TEST(SpatialReferenceOwnership, SampleStoreSizeIsConfigurableAndNeverBlocks)
{
  const auto original = Fmi::OGRSpatialReferenceFactory::getSampleStoreSize();

  // Deliberately smaller than the number of CRSes used below: exceeding it must
  // cost an extra clone, never an error or a wrong result.
  Fmi::OGRSpatialReferenceFactory::SetSampleStoreSize(2);
  EXPECT_EQ(Fmi::OGRSpatialReferenceFactory::getSampleStoreSize(), 2U);

  const std::vector<std::string> crs = {
      "EPSG:4326", "EPSG:2393", "EPSG:3067", "EPSG:3857", "EPSG:3035"};
  for (int round = 0; round < 3; ++round)
  {
    for (const auto &name : crs)
    {
      auto srs = Fmi::OGRSpatialReferenceFactory::Create(name);
      ASSERT_TRUE(srs);
      EXPECT_FALSE(wkt_of(*srs).empty()) << "thrashing the sample store broke " << name;
    }
  }

  Fmi::OGRSpatialReferenceFactory::SetSampleStoreSize(original);
}

TEST(SpatialReferenceOwnership, ManySimultaneousHoldersAreAllDistinct)
{
  // The sample store bounds how many *samples* a thread keeps, never how many
  // objects can be held at once.
  std::vector<std::shared_ptr<OGRSpatialReference>> held;
  for (int i = 0; i < 64; ++i)
    held.push_back(Fmi::OGRSpatialReferenceFactory::Create(kCRS));

  std::set<const void *> distinct;
  for (const auto &p : held)
    distinct.insert(p.get());

  EXPECT_EQ(distinct.size(), held.size()) << "simultaneous holders shared an object";
}
