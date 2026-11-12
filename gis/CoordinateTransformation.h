#pragma once

#include <cstddef>
#include <memory>
#include <vector>

class OGRCoordinateTransformation;
class OGRGeometry;

namespace Fmi
{
class GeometryProjector;
class SpatialReference;

class CoordinateTransformation
{
 public:
  ~CoordinateTransformation() = default;
  CoordinateTransformation() = delete;
  CoordinateTransformation(const CoordinateTransformation& other);
  CoordinateTransformation(CoordinateTransformation&& other) noexcept;

  // Handles implicit construction from strings, OGRSpatialReference etc
  CoordinateTransformation(const SpatialReference& theSource, const SpatialReference& theTarget);

  CoordinateTransformation& operator=(const CoordinateTransformation&) = delete;
  CoordinateTransformation& operator=(CoordinateTransformation&&) = delete;

  const OGRCoordinateTransformation& operator*() const;
  OGRCoordinateTransformation* get() const;

  bool transform(double& x, double& y) const;
  bool transform(std::vector<double>& x, std::vector<double>& y) const;
  bool transform(OGRGeometry& geom) const;

  // Intelligent transform handling antemeridians etc
  OGRGeometry* transformGeometry(const OGRGeometry& geom, double theMaximumSegmentLength = 0) const;
  OGRGeometry* transformGeometry(const OGRGeometry& geom, const GeometryProjector& projector) const;

  const SpatialReference& getSourceCS() const;
  const SpatialReference& getTargetCS() const;

  std::size_t hashValue() const;

 private:
  class Impl;

  std::shared_ptr<Impl> impl;

};  // class CoordinateTransformation

}  // namespace Fmi
