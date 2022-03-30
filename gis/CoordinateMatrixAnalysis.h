#pragma once

#include "BoolMatrix.h"

namespace Fmi
{
class CoordinateMatrix;

// Analyze grid structure for contouring purposes

struct CoordinateAnalysis
{
  CoordinateAnalysis(const BoolMatrix& theValid,
                     const BoolMatrix& theClockwise,
                     bool theFlipping,
                     std::size_t shift)
      : valid(theValid), clockwise(theClockwise), needs_flipping(theFlipping), shift(shift)
  {
  }

  BoolMatrix valid{0, 0};      // valid cells, no missing coordinates,convex etc
  BoolMatrix clockwise{0, 0};  // if valid true for clockwise cells, otherwise false
  bool needs_flipping{false};  // if the grid seems to be upside down
  std::size_t shift{0};        // horizontal shift for wraparound handling
};

CoordinateAnalysis analysis(const CoordinateMatrix& coords);

}  // namespace Fmi
