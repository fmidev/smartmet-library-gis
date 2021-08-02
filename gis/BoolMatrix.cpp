#include "BoolMatrix.h"
#include <macgyver/Exception.h>

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

}  // namespace Fmi
