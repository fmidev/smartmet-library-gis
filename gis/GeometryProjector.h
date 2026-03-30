// GeometryProjector.h
#pragma once

#include <memory>

class OGRSpatialReference;
class OGRGeometry;

namespace Fmi
{

class GeometryProjector
{
 public:
  GeometryProjector(OGRSpatialReference* sourceSRS, OGRSpatialReference* targetSRS);
  ~GeometryProjector();

  GeometryProjector(const GeometryProjector&) = delete;
  GeometryProjector& operator=(const GeometryProjector&) = delete;

  GeometryProjector(GeometryProjector&&) noexcept;
  GeometryProjector& operator=(GeometryProjector&&) noexcept;

  void setProjectedBounds(double minX, double minY, double maxX, double maxY);
  bool hasProjectedBounds() const;
  void getProjectedBounds(double& minX, double& minY, double& maxX, double& maxY) const;

  // Default 50km. Set <=0 to disable geographic densification.
  void setDensifyResolutionKm(double km);
  double getDensifyResolutionKm() const;

  // Project + clip to bounds. Returns nullptr only if input geom is nullptr.
  std::unique_ptr<OGRGeometry> projectGeometry(const OGRGeometry* geom) const;

  void setJumpThreshold(double threshold);

 private:
  class Impl;
  std::unique_ptr<Impl> m_impl;
};

}  // namespace Fmi
