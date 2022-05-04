#include "BoolMatrix.h"
#include <macgyver/Exception.h>
#include <macgyver/Hash.h>

namespace Fmi
{
BoolMatrix::BoolMatrix(std::size_t width, std::size_t height, bool flag)
    : m_w(width),
      m_h(height),
      m_data((width * height + 63) / 64, (flag ? 0xffffffffffffffffUL : 0UL))
{
}

BoolMatrix BoolMatrix::operator&(const BoolMatrix& other) const
{
  auto tmp = *this;
  tmp &= other;
  return tmp;
}

BoolMatrix BoolMatrix::operator|(const BoolMatrix& other) const
{
  auto tmp = *this;
  tmp |= other;
  return tmp;
}

BoolMatrix& BoolMatrix::operator&=(const BoolMatrix& other)
{
  if (m_w != other.m_w || m_h != other.m_h)
    throw Fmi::Exception(BCP, "BoolMatrix size mismatch");

  for (auto i = 0UL; i < m_data.size(); i++)
    m_data[i] &= other.m_data[i];

  return *this;
}

BoolMatrix& BoolMatrix::operator|=(const BoolMatrix& other)
{
  if (m_w != other.m_w || m_h != other.m_h)
    throw Fmi::Exception(BCP, "BoolMatrix size mismatch");

  for (auto i = 0UL; i < m_data.size(); i++)
    m_data[i] |= other.m_data[i];

  return *this;
}

BoolMatrix BoolMatrix::operator~() const
{
  auto tmp = *this;

  for (auto i = 0UL; i < tmp.m_data.size(); i++)
    tmp.m_data[i] = ~tmp.m_data[i];
  return tmp;
}

BoolMatrix BoolMatrix::operator^(bool flag) const
{
  auto tmp = *this;
  auto val = (flag ? 0xffffffffffffffffUL : 0UL);

  for (auto i = 0UL; i < tmp.m_data.size(); i++)
    tmp.m_data[i] = tmp.m_data[i] ^ val;

  return tmp;
}

void BoolMatrix::swap(BoolMatrix& other)
{
  try
  {
    std::swap(m_w, other.m_w);
    std::swap(m_h, other.m_h);
    std::swap(m_data, other.m_data);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

std::size_t BoolMatrix::hashValue() const
{
  // We ignore on purpose the last bits, since in contouring the extra bits of uint64_t are
  // always initialized the same way. Hence we're not using a generic Fmi::Bitset but a BoolMatrix
  // with an explicit purpose.

  std::size_t hash = 0;
  for (auto bits : m_data)
    Fmi::hash_combine(hash, Fmi::hash_value(bits));

  return hash;
}

std::array<std::size_t, 4> BoolMatrix::bbox() const
{
  auto imin = m_w;
  auto imax = 0UL;
  auto jmin = m_h;
  auto jmax = 0UL;

  for (auto j = 0UL; j < m_h; j++)
  {
    for (auto i = 0UL; i < m_w;)
    {
      // Skip blocks of zeros and ones quickly. Not optimized for skipping the first bits of the
      // next row. The loop could be written to iterate over pos instead, and we'd get real
      // 64-bit increments by calculating i&j from pos.

      auto pos = j * m_w + i;
      if ((pos & 63) == 0)
      {
        auto bits = m_data[pos / 64];
        if (bits == 0UL)
        {
          i += 64;  // skip all bits
          continue;
        }
        if (bits == 0xffffffffffffffffUL)
        {
          imin = std::min(imin, i);
          imax = std::max(imax, std::min(m_w - 1, i + 63));
          jmin = std::min(jmin, j);
          jmax = std::max(jmax, j);
          i += 64;
          continue;
        }
      }

      if ((*this)(i, j))
      {
        imin = std::min(imin, i);
        jmin = std::min(jmin, j);
        imax = std::max(imax, i);
        jmax = std::max(jmax, j);
      }
      i++;
    }
  }

  return {imin, jmin, imax, jmax};
}

}  // namespace Fmi
