#pragma once

#include <memory>
#include <vector>

class OGRSpatialReference;
class OGRCoordinateTransformation;
class OGRGeometry;

namespace Fmi
{
class SpatialReference;

class CoordinateTransformation
{
 public:
  ~CoordinateTransformation() = default;
  CoordinateTransformation() = delete;

  // Handles implicit construction from strings, OGRSpatialReference etc
  CoordinateTransformation(const SpatialReference& theSource, const SpatialReference& theTarget);

  const OGRCoordinateTransformation& operator*() const;
  OGRCoordinateTransformation* get() const;

  bool transform(double& x, double& y) const;
  bool transform(std::vector<double>& x, std::vector<double>& y) const;
  bool transform(OGRGeometry& geom) const;

  const OGRSpatialReference& getSourceCS() const;
  const OGRSpatialReference& getTargetCS() const;

 private:
  std::shared_ptr<OGRCoordinateTransformation> m_transformation;

  bool m_swapInput = false;   // swap xy before calling GDAL?
  bool m_swapOutput = false;  // swap xy after calling GDAL?

};  // class CoordinateTransformation

}  // namespace Fmi
