#pragma once

#include "BoolMatrix.h"

namespace Fmi
{
class CoordinateMatrix;

// Analyze grid structure for contouring purposes

struct CoordinateAnalysis
{
  CoordinateAnalysis(const BoolMatrix& invalid, const BoolMatrix& clockwise, bool needs_flipping)
      : m_invalid(invalid), m_clockwise(clockwise), m_needs_flipping(needs_flipping)
  {
  }

  BoolMatrix m_invalid{0, 0};    // invalid cells due to missing coordinates, concavity etc
  BoolMatrix m_clockwise{0, 0};  // if valid true for clockwise cells, otherwise false
  bool m_needs_flipping{false};  // if the grid seems to be upside down
};

CoordinateAnalysis analysis(const CoordinateMatrix& matrix);

}  // namespace Fmi
