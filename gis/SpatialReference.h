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

  const OGRSpatialReference &operator*() const { return *m_crs; }
  OGRSpatialReference *get() const { return m_crs; }

  operator OGRSpatialReference &() const { return *m_crs; }
  operator OGRSpatialReference *() const { return m_crs; }

  // Common accessors
  bool isAxisSwapped() const;
  bool isGeographic() const;

  // This is mostly for debugging
  const std::string &projStr() const { return m_projStr; }

 private:
  void init(const std::string &theCRS);

  std::string m_projStr;  // set only if initialized from a string
  OGRSpatialReference *m_crs = nullptr;
};

}  // namespace Fmi
