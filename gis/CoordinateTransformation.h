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

  // Implicit construction from strings, etc
  CoordinateTransformation(const SpatialReference& theSource, const SpatialReference& theTarget);
  CoordinateTransformation(const OGRSpatialReference& theSource,
                           const OGRSpatialReference& theTarget);

  const OGRCoordinateTransformation& operator*() const;
  OGRCoordinateTransformation* get() const;

  bool Transform(double& x, double& y) const;
  bool Transform(std::vector<double>& x, std::vector<double>& y) const;
  bool Transform(OGRGeometry& geom) const;

  const OGRSpatialReference& GetSourceCS() const;
  const OGRSpatialReference& GetTargetCS() const;

 private:
  std::shared_ptr<OGRCoordinateTransformation> itsTransformation;

  bool itsInputSwapFlag = false;   // swap xy before calling GDAL?
  bool itsOutputSwapFlag = false;  // swap xy after calling GDAL?

};  // class CoordinateTransformation

}  // namespace Fmi
