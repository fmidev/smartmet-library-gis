#pragma once
#include <boost/dynamic_bitset.hpp>

namespace Fmi
{
class BoolMatrix
{
 public:
  BoolMatrix(std::size_t width, std::size_t height, bool flag = false)
      : m_w(width), m_h(height), m_data(width * height, flag)
  {
  }

  std::size_t width() const { return m_w; }
  std::size_t height() const { return m_h; }
  bool empty() const { return m_data.empty(); }

  bool operator()(std::size_t i, std::size_t j) const { return m_data[j * m_w + i]; }

  void set(std::size_t i, std::size_t j, bool flag) { m_data[j * m_w + i] = flag; }

  void swap(BoolMatrix& other);

  std::size_t hashValue() const;

 private:
  std::size_t m_w = 0;
  std::size_t m_h = 0;
  boost::dynamic_bitset<> m_data;

};  // class Matrix
}  // namespace Fmi
