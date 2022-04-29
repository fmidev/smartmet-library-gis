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

}  // namespace Fmi
