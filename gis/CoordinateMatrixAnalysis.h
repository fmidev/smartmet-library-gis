#pragma once

#include "BoolMatrix.h"

namespace Fmi
{
class CoordinateMatrix;

// Analyze grid structure for contouring purposes

struct CoordinateAnalysis
{
  CoordinateAnalysis(const BoolMatrix& theValid, const BoolMatrix& theClockwise, bool theFlipping)
      : valid(theValid), clockwise(theClockwise), needs_flipping(theFlipping)
  {
  }

  BoolMatrix valid{0, 0};      // valid cells, no missing coordinates,convex etc
  BoolMatrix clockwise{0, 0};  // if valid true for clockwise cells, otherwise false
  bool needs_flipping{false};  // if the grid seems to be upside down
};

CoordinateAnalysis analysis(const CoordinateMatrix& matrix);

}  // namespace Fmi
