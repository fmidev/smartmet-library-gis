#include "BilinearCoordinateTransformation.h"
#include "CoordinateMatrix.h"
#include "CoordinateMatrixCache.h"
#include "CoordinateTransformation.h"
#include <boost/functional/hash.hpp>

namespace Fmi
{
BilinearCoordinateTransformation::BilinearCoordinateTransformation(
    const CoordinateTransformation& theTransformation,
    std::size_t nx,
    std::size_t ny,
    double x1,
    double y1,
    double x2,
    double y2)
    : m_nx(nx), m_ny(ny), m_x1(x1), m_y1(y1), m_x2(x2), m_y2(y2)
{
  // Search the cache first
  auto m_hash = CoordinateMatrix::hashValue(nx, ny, x1, y1, x2, y2);
  boost::hash_combine(m_hash, theTransformation.hashValue());
  m_matrix = CoordinateMatrixCache::Find(m_hash);

  if (!m_matrix)
  {
    // Create new transformation and cache it
    m_matrix.reset(new CoordinateMatrix(nx, ny, x1, y1, x2, y2));
    m_matrix->transform(theTransformation);  // projected grid
    CoordinateMatrixCache::Insert(m_hash, m_matrix);
  }
}

// Bilinear interpolation in a rectable

inline double bilinear(
    double dx, double dy, double topleft, double topright, double bottomleft, double bottomright)
{
  const auto mdx = 1 - dx;
  const auto mdy = 1 - dy;

  return (mdx * mdy * bottomleft + dx * mdy * bottomright + mdx * dy * topleft +
          dx * dy * topright);
}

bool BilinearCoordinateTransformation::transform(double& x, double& y) const
{
  // Return false if wanted point is outside the bbox
  if (x < m_x1 || x > m_x2 || y < m_y1 || y > m_y2) return false;

  // Calculate integer and fractional coordinates inside the grid

  const auto xpos = (x - m_x1) / (m_x2 - m_x1) * m_nx;
  const auto ypos = (y - m_y1) / (m_y2 - m_y1) * m_ny;
  auto i = static_cast<std::size_t>(xpos);
  auto j = static_cast<std::size_t>(ypos);

  // Avoid overflow at the edges
  if (i == m_nx) --i;
  if (j == m_ny) --j;

  const auto xfrac = xpos - i;
  const auto yfrac = ypos - j;

  // Interpolate projected coordinate

  x = bilinear(xfrac,
               yfrac,
               m_matrix->y(i, j + 1),
               m_matrix->y(i + 1, j + 1),
               m_matrix->y(i, j),
               m_matrix->y(i + 1, j));
  y = bilinear(xfrac,
               yfrac,
               m_matrix->y(i, j + 1),
               m_matrix->y(i + 1, j + 1),
               m_matrix->y(i, j),
               m_matrix->y(i + 1, j));
  return true;
}

}  // namespace Fmi
