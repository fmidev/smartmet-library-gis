#pragma once
#include <vector>

namespace Fmi
{
class CoordinateTransformation;

class CoordinateMatrix
{
 public:
  // init to zero size
  CoordinateMatrix() = default;
  // init to missing values
  CoordinateMatrix(std::size_t nx, std::size_t ny);
  // init to a rectilinear grid
  CoordinateMatrix(std::size_t nx, std::size_t ny, double x1, double y1, double x2, double y2);

  // size accessors
  std::size_t width() const { return m_width; }
  std::size_t height() const { return m_height; }

  // data accessors
  double x(std::size_t i, std::size_t j) const { return m_x[i + j * m_width]; }
  double y(std::size_t i, std::size_t j) const { return m_y[i + j * m_width]; }

  std::pair<double, double> operator()(std::size_t i, std::size_t j) const
  {
    const auto pos = i + j * m_width;
    return {m_x[pos], m_y[pos]};
  }

  // data setters are normally not needed, constructing a 1D array of station coordinates
  // is likely the only exception
  void set(std::size_t i, std::size_t j, double xx, double yy)
  {
    const auto pos = i + j * m_width;
    m_x[pos] = xx;
    m_y[pos] = yy;
  }

  void set(std::size_t i, std::size_t j, const std::pair<double, double>& xy)
  {
    const auto pos = i + j * m_width;
    m_x[pos] = xy.first;
    m_y[pos] = xy.second;
  }

  template <typename T>
  void set(std::size_t i, std::size_t j, const T& xy)
  {
    const auto pos = i + j * m_width;
    m_x[pos] = xy.X();
    m_y[pos] = xy.Y();
  }

  // occasionally needed for speed
  void swap(CoordinateMatrix& other);

  // Always uses lon/lat x/y ordering.
  bool transform(const Fmi::CoordinateTransformation& transformation);

  // Hash value calculation

  std::size_t hashValue() const { return m_hash; }

  // Used externally when searching for projected matrices from a cache
  static std::size_t hashValue(
      std::size_t nx, std::size_t ny, double x1, double y1, double x2, double y2);

 private:
  std::size_t m_width = 0;
  std::size_t m_height = 0;
  std::vector<double> m_x;
  std::vector<double> m_y;
  std::size_t m_hash = 0;

};  // class CoordinateMatrix

}  // namespace Fmi
