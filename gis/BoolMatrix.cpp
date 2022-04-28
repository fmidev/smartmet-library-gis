#include "BoolMatrix.h"
#include <macgyver/Exception.h>
#include <macgyver/Hash.h>

namespace Fmi
{
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
  std::size_t hash = 0;
  std::uint64_t bits = 0;
  const auto n = m_w * m_h;
  for (auto i = 0UL; i < n; i++)
  {
    bits = (bits << 1) | m_data[i];
    if ((i & 63) == 0 || (i == n - 1))
    {
      Fmi::hash_combine(hash, Fmi::hash_value(bits));
      bits = 0;
    }
  }

  return hash;
}

}  // namespace Fmi
