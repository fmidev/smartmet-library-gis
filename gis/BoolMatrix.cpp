#include "BoolMatrix.h"

namespace Fmi
{
void BoolMatrix::swap(BoolMatrix& other)
{
  std::swap(m_w, other.m_w);
  std::swap(m_h, other.m_h);
  std::swap(m_data, other.m_data);
}

}  // namespace Fmi
