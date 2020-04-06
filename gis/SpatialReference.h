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

  const OGRSpatialReference &operator*() const { return *itsSR; }
  OGRSpatialReference *get() const { return itsSR; }

  operator OGRSpatialReference &() const { return *itsSR; }
  operator OGRSpatialReference *() const { return itsSR; }

  // Common accessors
  bool IsAxisSwapped() const;
  bool IsGeographic() const;

  // This is mostly for debugging
  const std::string &ProjStr() const { return itsProjStr; }

 private:
  std::string itsProjStr;  // set only if initialized from a string
  void init(const std::string &theSR);

  OGRSpatialReference *itsSR = nullptr;
};

}  // namespace Fmi
