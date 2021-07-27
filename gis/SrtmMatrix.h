// A 360x180 grid of SrtmTiles, some of which may be missing

#pragma once
#include <memory>

namespace Fmi
{
class SrtmTile;

class SrtmMatrix
{
 public:
  using TileType = std::unique_ptr<SrtmTile>;

  ~SrtmMatrix();
  SrtmMatrix();
  SrtmMatrix(const SrtmMatrix& other) = delete;
  SrtmMatrix& operator=(const SrtmMatrix& other) = delete;

  void add(TileType tile);
  static constexpr double missing = -32768;
  double value(double lon, double lat) const;

 private:
  class Impl;
  std::unique_ptr<Impl> impl;

};  // class SrtmMatrix
}  // namespace Fmi
