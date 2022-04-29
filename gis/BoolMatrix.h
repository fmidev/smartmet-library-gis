#pragma once
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <vector>

namespace Fmi
{
class BoolMatrix
{
 public:
  BoolMatrix(std::size_t width, std::size_t height, bool flag = false);

  std::size_t width() const { return m_w; }
  std::size_t height() const { return m_h; }
  std::size_t size() const { return m_w * m_h; }
  bool empty() const { return m_w == 0 || m_h == 0; }

  bool operator()(std::size_t i, std::size_t j) const
  {
    std::uint64_t pos = j * m_w + i;
    std::uint64_t idx = pos / 64UL;
    std::uint64_t bit = pos & 63UL;
    return m_data[idx] & (1UL << bit);
  }

  void set(std::size_t i, std::size_t j, bool flag)
  {
    std::uint64_t pos = j * m_w + i;
    std::uint64_t idx = pos / 64UL;
    std::uint64_t bit = pos & 63UL;
    if (flag)
      m_data[idx] |= (1UL << bit);
    else
      m_data[idx] &= ~(1UL << bit);
  }

  void swap(BoolMatrix& other);

  std::size_t hashValue() const;

 private:
  std::size_t m_w = 0;
  std::size_t m_h = 0;
  std::vector<std::uint64_t> m_data;

};  // class Matrix
}  // namespace Fmi
