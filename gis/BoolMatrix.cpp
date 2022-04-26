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
  int count = 0;
  for (auto flag : m_data)
  {
    bits = (bits << 1) | flag;
    if (++count == 64)
    {
      Fmi::hash_combine(hash, Fmi::hash_value(bits));
      bits = 0;
      count = 0;
    }
  }

  if (count > 0)
    Fmi::hash_combine(hash, Fmi::hash_value(bits));

  return hash;
}

}  // namespace Fmi
