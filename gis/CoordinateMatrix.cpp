#include "CoordinateMatrix.h"
#include "CoordinateTransformation.h"
#include <cmath>

namespace Fmi
{
// PROJ uses HUGE_VAL as a missing value, hence we do too to avoid unnecessary modifications to data

CoordinateMatrix::CoordinateMatrix(std::size_t nx, std::size_t ny)
    : m_width{nx},
      m_height{ny},
      m_x{std::vector<double>(nx * ny, HUGE_VAL)},
      m_y{std::vector<double>(nx * ny, HUGE_VAL)}
{
}

// Initialize X coordinates to x1...x2 and Y coordinates to y1..y2 with constant step sizes
CoordinateMatrix::CoordinateMatrix(
    std::size_t nx, std::size_t ny, double x1, double y1, double x2, double y2)
    : m_width{nx},
      m_height{ny},
      m_x{std::vector<double>(nx * ny, HUGE_VAL)},
      m_y{std::vector<double>(nx * ny, HUGE_VAL)}
{
  const auto dx = nx > 1 ? (x2 - x1) / (nx - 1) : 0;
  const auto dy = ny > 1 ? (y2 - y1) / (ny - 1) : 0;

  std::size_t pos = 0;
  for (std::size_t j = 0; j < ny; j++)
  {
    const auto y = y1 + j * dy;
    for (std::size_t i = 0; i < nx; i++)
    {
      m_x[pos] = x1 + i * dx;
      m_y[pos] = y;
      ++pos;
    }
  }
}

// Swap contents
void CoordinateMatrix::swap(CoordinateMatrix& other)
{
  std::swap(m_width, other.m_width);
  std::swap(m_width, other.m_height);
  std::swap(m_x, other.m_x);
  std::swap(m_y, other.m_y);
}

// Project the coordinates. User takes responsibility on making sure the input coordinates
// are in the correct spatial reference. Input/output order is always lon/lat or x/y,
// EPSG rules are followed only temporarily to make the projection work.

bool CoordinateMatrix::transform(const CoordinateTransformation& transformation)
{
  return transformation.transform(m_x, m_y);
}

}  // namespace Fmi
