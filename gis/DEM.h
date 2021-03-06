#pragma once
#include <memory>
#include <string>

namespace Fmi
{
class DEM
{
 public:
  ~DEM();
  DEM(const std::string& path);

  // May return NaN if elevation is unknown
  double elevation(double lon, double lat) const;

  double elevation(double lon, double lat, double resolution) const;

 private:
  DEM() = delete;
  DEM(const DEM& other) = delete;
  DEM& operator=(const DEM& other) = delete;

  class Impl;
  std::unique_ptr<Impl> impl;

};  // class DEM
}  // namespace Fmi
