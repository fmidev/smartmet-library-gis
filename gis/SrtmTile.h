// An API for handling STRM digital elevation model tiles (.hgt files)

#pragma once
#include <memory>
#include <string>

namespace Fmi
{
enum class TileType : std::size_t
{
  DEM1_3601,
  DEM3_1201,
  DEM9_401,
  DEM9_1201,
  LAND_COVER_361,
  DEM27_1201,
  DEM81_1001,
  UNDEFINED_TILETYPE
};

class SrtmTile
{
 public:
  ~SrtmTile();
  SrtmTile(const std::string& path);

  const std::string& path() const;
  std::size_t size() const;
  double longitude() const;
  double latitude() const;

  static bool valid_path(const std::string& path);
  static bool valid_size(const std::string& path);

  // Lack of interpolation support is intentional, it is provided
  // at the SrtmMatrix level where adjacent tiles are available
  static const int missing = -32768;
  int value(std::size_t i, std::size_t j) const;

  TileType type() const
  {
    return tiletype;
  }

 private:
  SrtmTile() = delete;
  SrtmTile(const SrtmTile& other) = delete;
  SrtmTile& operator=(const SrtmTile& other) = delete;

  class Impl;
  std::unique_ptr<Impl> impl;
  TileType tiletype;
};  // class SrtmTile
}  // namespace Fmi
