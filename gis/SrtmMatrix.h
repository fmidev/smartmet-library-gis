// A 360x180 grid of SrtmTiles, some of which may be missing

#pragma once
#include <memory>

namespace Fmi
{
class SrtmTile;
enum class TileType : std::size_t;

class SrtmMatrix
{
 public:
  typedef std::unique_ptr<SrtmTile> TileType;

  ~SrtmMatrix();
  SrtmMatrix();

  void add(TileType tile);
  static constexpr double missing = -32768;
  double value(double lon, double lat) const;
  Fmi::TileType tiletype() const;

 private:
  SrtmMatrix(const SrtmMatrix& other) = delete;
  SrtmMatrix& operator=(const SrtmMatrix& other) = delete;

  class Impl;
  std::unique_ptr<Impl> impl;

};  // class SrtmMatrix
}  // namespace Fmi
