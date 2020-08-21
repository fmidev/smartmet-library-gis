#pragma once

#include <memory>
#include <vector>

namespace Fmi
{
class CoordinateTransformation;
class CoordinateMatrix;

// Create bilinear interpolation grid from rectilinear unprojected grid coordinates to
// projected grid coordinates

class BilinearCoordinateTransformation
{
 public:
  BilinearCoordinateTransformation(const CoordinateTransformation& theTransformation,
                                   std::size_t nx,
                                   std::size_t ny,
                                   double x1,
                                   double y1,
                                   double x2,
                                   double y2);

  bool transform(double& x, double& y) const;

  std::size_t hashValue() const { return m_hash; }

 private:
  const std::size_t m_nx;
  const std::size_t m_ny;
  const double m_x1;
  const double m_y1;
  const double m_x2;
  const double m_y2;
  std::size_t m_hash;
  std::shared_ptr<CoordinateMatrix> m_matrix;

};  // class BilinearCoordinateTransformation

}  // namespace Fmi
