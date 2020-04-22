#pragma once

#include <memory>
#include <string>

class OGRSpatialReference;

/*
 * A proxy class for OGRSpatialReference
 */

namespace Fmi
{
class SpatialReference
{
 public:
  ~SpatialReference();
  SpatialReference() = delete;

  SpatialReference(const SpatialReference &other);
  SpatialReference(const OGRSpatialReference &other);  // since GDAL is not const correct
  SpatialReference(OGRSpatialReference &other);
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

  std::size_t hashValue() const;

  // This is mostly for debugging
  const std::string &projStr() const;

 private:
  class Impl;
  std::unique_ptr<Impl> impl;
};

}  // namespace Fmi
