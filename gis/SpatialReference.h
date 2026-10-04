#pragma once

#include <macgyver/Cache.h>
#include <memory>
#include <optional>
#include <string>

class OGRSpatialReference;

/*
 * A spatial reference.
 *
 * Construct one from a definition string (EPSG code, WKT, PROJ string) and the
 * values derived from it - WKT, PROJ string, EPSG code, axis flags, hash - are
 * computed once, cached under that string, and shared with every other instance
 * built from it. Copying is therefore free and the accessors never call into
 * GDAL.
 *
 * Constructing from an OGRSpatialReference instead has no key to cache under and
 * so re-derives all of it, measured at ~1.7 ms against 0.2 us for the cached
 * path. Prefer the definition string wherever one exists.
 *
 * get(), operator* and the implicit conversions hand out an OGRSpatialReference
 * private to this instance, cloned on first use. It is yours: reading and even
 * modifying it cannot affect any other holder.
 */

namespace Fmi
{
class ProjInfo;

class SpatialReference
{
 public:
  ~SpatialReference();
  SpatialReference() = delete;

  SpatialReference(const SpatialReference &other);
  SpatialReference(const OGRSpatialReference &other);  // since GDAL is not const correct
  SpatialReference(OGRSpatialReference &other);
  SpatialReference(SpatialReference &&other) noexcept;

  SpatialReference(const std::shared_ptr<OGRSpatialReference> &other);  // for legacy code
  SpatialReference(const char *theDesc);
  SpatialReference(const std::string &theDesc);
  SpatialReference(int epsg);

  SpatialReference &operator=(const SpatialReference &other) = delete;
  SpatialReference &operator=(SpatialReference &&other) = delete;

  // Excplicit and implicit accessors

  const OGRSpatialReference &operator*() const;
  OGRSpatialReference *get() const;

  operator OGRSpatialReference &() const;
  operator OGRSpatialReference *() const;

  // Common accessors
  bool isAxisSwapped() const;
  bool isGeographic() const;
  bool EPSGTreatsAsLatLong() const;
  std::optional<int> getEPSG() const;

  // Proj.4 info
  const ProjInfo &projInfo() const;

  std::size_t hashValue() const;

  const std::string &WKT() const;

  // This is mostly for debugging
  const std::string &projStr() const;

  // Size of the store shared with OGRSpatialReferenceFactory: one entry per CRS
  // definition string, holding the master object and these derived values.
  static void setCacheSize(std::size_t newMaxSize);

  // Statistics for that same single store, so this and
  // OGRSpatialReferenceFactory::getCacheStats() report identical numbers.
  static Cache::CacheStats getCacheStats();

 private:
  class Impl;
  std::unique_ptr<Impl> impl;
};

}  // namespace Fmi
