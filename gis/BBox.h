#pragma once

namespace Fmi
{
struct BBox
{
  BBox(double w, double e, double s, double n) : west(w), east(e), south(s), north(n) {}
  BBox() = default;
  double west = 0;
  double east = 0;
  double south = 0;
  double north = 0;
};

}  // namespace Fmi
