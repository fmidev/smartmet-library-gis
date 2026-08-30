// API-stability pins for the spatial-reference entry points.
//
// The rule this enforces: the old API is never changed. If a signature has to
// evolve, a NEW method is added and the old one is kept (and marked
// deprecated) - existing callers in every downstream repo keep compiling and
// keep their behaviour. The thread-safety rework was done under that rule: the
// pre-existing declarations below are exactly what shipped before it, and the
// rework only ADDED SetSampleStoreSize/getSampleStoreSize/ReleaseThreadSamples.
//
// Every check here is a compile-time one: a function pointer (or a
// static_assert) with the exact historical type. If somebody changes a
// signature, removes a method, or makes an implicit constructor explicit, this
// file stops compiling - which turns an accidental API break into a visible,
// deliberate decision. When a method is deliberately deprecated-and-replaced,
// keep its pin here (the old symbol must survive) and add a pin for the
// replacement.
//
// The runtime bodies are minimal smoke calls proving the pinned entry points
// are not just declared but still work.

#include "CoordinateTransformation.h"
#include "OGRCoordinateTransformationFactory.h"
#include "OGRSpatialReferenceFactory.h"
#include "ProjInfo.h"
#include "SpatialReference.h"

#include <ogr_geometry.h>
#include <ogr_spatialref.h>

#include <gtest/gtest.h>

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

namespace
{
// ---------------------------------------------------------------------------
// Fmi::OGRSpatialReferenceFactory - free functions
// ---------------------------------------------------------------------------
namespace factory_pins
{
using namespace Fmi::OGRSpatialReferenceFactory;

[[maybe_unused]] std::shared_ptr<OGRSpatialReference> (*create_str)(const std::string&) = &Create;
[[maybe_unused]] std::shared_ptr<OGRSpatialReference> (*create_epsg)(int) = &Create;
[[maybe_unused]] void (*set_cache_size)(std::size_t) = &SetCacheSize;
[[maybe_unused]] Fmi::Cache::CacheStats (*get_cache_stats)() = &getCacheStats;
[[maybe_unused]] std::mutex& (*get_mutex)() = &mutex;
}  // namespace factory_pins

// ---------------------------------------------------------------------------
// Fmi::SpatialReference - constructors, accessors, conversions
// ---------------------------------------------------------------------------
namespace spatial_reference_pins
{
using Fmi::SpatialReference;

// Constructors, including the implicit ones callers rely on for
// CoordinateTransformation("WGS84", "EPSG:3067") style calls.
static_assert(std::is_constructible_v<SpatialReference, const SpatialReference&>);
static_assert(std::is_constructible_v<SpatialReference, SpatialReference&&>);
static_assert(std::is_constructible_v<SpatialReference, const OGRSpatialReference&>);
static_assert(std::is_constructible_v<SpatialReference, OGRSpatialReference&>);
static_assert(
    std::is_constructible_v<SpatialReference, const std::shared_ptr<OGRSpatialReference>&>);
static_assert(std::is_convertible_v<const char*, SpatialReference>);
static_assert(std::is_convertible_v<const std::string&, SpatialReference>);
static_assert(std::is_convertible_v<int, SpatialReference>);
static_assert(!std::is_default_constructible_v<SpatialReference>);

// Accessors.
[[maybe_unused]] const OGRSpatialReference& (SpatialReference::*deref)() const =
    &SpatialReference::operator*;
[[maybe_unused]] OGRSpatialReference* (SpatialReference::*get)() const = &SpatialReference::get;
[[maybe_unused]] bool (SpatialReference::*is_axis_swapped)() const =
    &SpatialReference::isAxisSwapped;
[[maybe_unused]] bool (SpatialReference::*is_geographic)() const = &SpatialReference::isGeographic;
[[maybe_unused]] bool (SpatialReference::*treats_as_lat_long)() const =
    &SpatialReference::EPSGTreatsAsLatLong;
[[maybe_unused]] std::optional<int> (SpatialReference::*get_epsg)() const =
    &SpatialReference::getEPSG;
[[maybe_unused]] const Fmi::ProjInfo& (SpatialReference::*proj_info)() const =
    &SpatialReference::projInfo;
[[maybe_unused]] std::size_t (SpatialReference::*hash_value)() const = &SpatialReference::hashValue;
[[maybe_unused]] const std::string& (SpatialReference::*wkt)() const = &SpatialReference::WKT;
[[maybe_unused]] const std::string& (SpatialReference::*proj_str)() const =
    &SpatialReference::projStr;
[[maybe_unused]] void (*set_cache_size)(std::size_t) = &SpatialReference::setCacheSize;
[[maybe_unused]] Fmi::Cache::CacheStats (*get_cache_stats)() = &SpatialReference::getCacheStats;

// Implicit conversions to GDAL types.
static_assert(std::is_convertible_v<const SpatialReference&, OGRSpatialReference&>);
static_assert(std::is_convertible_v<const SpatialReference&, OGRSpatialReference*>);
}  // namespace spatial_reference_pins

// ---------------------------------------------------------------------------
// Fmi::CoordinateTransformation
// ---------------------------------------------------------------------------
namespace coordinate_transformation_pins
{
using Fmi::CoordinateTransformation;
using Fmi::SpatialReference;

static_assert(std::is_constructible_v<CoordinateTransformation,
                                      const SpatialReference&,
                                      const SpatialReference&>);
static_assert(std::is_copy_constructible_v<CoordinateTransformation>);
static_assert(std::is_nothrow_move_constructible_v<CoordinateTransformation>);
static_assert(!std::is_default_constructible_v<CoordinateTransformation>);

[[maybe_unused]] const OGRCoordinateTransformation& (CoordinateTransformation::*deref)() const =
    &CoordinateTransformation::operator*;
[[maybe_unused]] OGRCoordinateTransformation* (CoordinateTransformation::*get)() const =
    &CoordinateTransformation::get;
[[maybe_unused]] bool (CoordinateTransformation::*transform_xy)(double&, double&) const =
    &CoordinateTransformation::transform;
[[maybe_unused]] bool (CoordinateTransformation::*transform_vectors)(
    std::vector<double>&, std::vector<double>&) const = &CoordinateTransformation::transform;
[[maybe_unused]] bool (CoordinateTransformation::*transform_geom)(OGRGeometry&) const =
    &CoordinateTransformation::transform;
[[maybe_unused]] OGRGeometry* (CoordinateTransformation::*transform_geometry)(
    const OGRGeometry&, double) const = &CoordinateTransformation::transformGeometry;
[[maybe_unused]] const SpatialReference& (CoordinateTransformation::*source_cs)() const =
    &CoordinateTransformation::getSourceCS;
[[maybe_unused]] const SpatialReference& (CoordinateTransformation::*target_cs)() const =
    &CoordinateTransformation::getTargetCS;
[[maybe_unused]] std::size_t (CoordinateTransformation::*hash_value)() const =
    &CoordinateTransformation::hashValue;
}  // namespace coordinate_transformation_pins

// ---------------------------------------------------------------------------
// Fmi::OGRCoordinateTransformationFactory
// ---------------------------------------------------------------------------
namespace transformation_factory_pins
{
using namespace Fmi::OGRCoordinateTransformationFactory;

static_assert(std::is_same_v<Ptr, std::unique_ptr<OGRCoordinateTransformation, Deleter>>,
              "the pooled-transformation handle type is part of the API");

[[maybe_unused]] Ptr (*create_ss)(const std::string&, const std::string&) = &Create;
[[maybe_unused]] Ptr (*create_ii)(int, int) = &Create;
[[maybe_unused]] Ptr (*create_is)(int, const std::string&) = &Create;
[[maybe_unused]] Ptr (*create_si)(const std::string&, int) = &Create;
[[maybe_unused]] void (*set_max_size)(std::size_t) = &SetMaxSize;
[[maybe_unused]] void (*pool_delete)(std::size_t,
                                     std::unique_ptr<OGRCoordinateTransformation>) = &Delete;
}  // namespace transformation_factory_pins

}  // namespace

// ---------------------------------------------------------------------------
// Runtime smoke: the pinned entry points still do their historical job.
// ---------------------------------------------------------------------------

TEST(ApiStability, FactoryEntryPointsWork)
{
  auto a = Fmi::OGRSpatialReferenceFactory::Create("EPSG:3067");
  auto b = Fmi::OGRSpatialReferenceFactory::Create(3067);
  ASSERT_TRUE(a);
  ASSERT_TRUE(b);
  EXPECT_TRUE(a->IsProjected());

  const auto stats = Fmi::OGRSpatialReferenceFactory::getCacheStats();
  EXPECT_GE(stats.size, 1U);

  std::lock_guard<std::mutex> lock(Fmi::OGRSpatialReferenceFactory::mutex());
}

TEST(ApiStability, SpatialReferenceEntryPointsWork)
{
  const Fmi::SpatialReference sr("EPSG:4326");
  EXPECT_TRUE(sr.isGeographic());
  EXPECT_FALSE(sr.WKT().empty());
  EXPECT_FALSE(sr.projStr().empty());
  EXPECT_EQ(sr.getEPSG(), std::optional<int>(4326));
  EXPECT_NE(sr.hashValue(), 0U);
  EXPECT_NE(sr.get(), nullptr);

  // The implicit conversions downstream repos lean on.
  const OGRSpatialReference& ref = sr;
  OGRSpatialReference* ptr = sr;
  EXPECT_EQ(&ref, ptr);
}

TEST(ApiStability, CoordinateTransformationEntryPointsWork)
{
  Fmi::CoordinateTransformation t("WGS84", "EPSG:3067");
  double x = 25.0;
  double y = 60.0;
  ASSERT_TRUE(t.transform(x, y));
  EXPECT_GT(x, 100000.0);  // roughly mid-Finland easting, sanity only

  std::vector<double> xs{25.0};
  std::vector<double> ys{60.0};
  EXPECT_TRUE(t.transform(xs, ys));

  EXPECT_TRUE(t.getSourceCS().isGeographic());
  EXPECT_FALSE(t.getTargetCS().isGeographic());
}

TEST(ApiStability, TransformationFactoryEntryPointsWork)
{
  auto t = Fmi::OGRCoordinateTransformationFactory::Create("EPSG:4326", "EPSG:3067");
  ASSERT_TRUE(t);
  double x = 25.0;
  double y = 60.0;
  EXPECT_NE(t->Transform(1, &x, &y), 0);
}
