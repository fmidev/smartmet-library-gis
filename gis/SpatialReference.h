#pragma once

#include <memory>
#include <string>

class OGRSpatialReference;

/*
 * A proxy class for OGRSpatialReference
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
  SpatialReference(const std::shared_ptr<OGRSpatialReference> &other);  // for legacy code
  SpatialReference(const char *theDesc);
  SpatialReference(const std::string &theDesc);
  SpatialReference(int epsg);

  SpatialReference &operator=(const SpatialReference &other) = delete;

  // Excplicit and implicit accessors

  const OGRSpatialReference &operator*() const;
  OGRSpatialReference *get() const;

  operator OGRSpatialReference &() const;
  operator OGRSpatialReference *() const;

  // Common accessors
  bool isAxisSwapped() const;
  bool isGeographic() const;
  bool EPSGTreatsAsLatLong() const;

  // Proj.4 info
  const ProjInfo &projInfo() const;

  std::size_t hashValue() const;

  // This is mostly for debugging
  const std::string &projStr() const;

  // Internal cache size
  static void setCacheSize(std::size_t newMaxSize);

 private:
  class Impl;
  std::unique_ptr<Impl> impl;
};

}  // namespace Fmi
