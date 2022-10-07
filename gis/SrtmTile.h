// An API for handling STRM digital elevation model tiles (.hgt files)

#pragma once
#include <memory>
#include <string>

namespace Fmi
{
class SrtmTile
{
 public:
  ~SrtmTile();
  SrtmTile(const std::string& path);
  SrtmTile() = delete;
  SrtmTile(const SrtmTile& other) = delete;
  SrtmTile& operator=(const SrtmTile& other) = delete;

  const std::string& path() const;
  std::size_t size() const;
  int longitude() const;
  int latitude() const;

  static bool valid_path(const std::string& path);
  static bool valid_size(const std::string& path);

  // Lack of interpolation support is intentional, it is provided
  // at the SrtmMatrix level where adjacent tiles are available
  static const int missing = -32768;
  int value(std::size_t i, std::size_t j) const;

 private:
  class Impl;
  std::unique_ptr<Impl> impl;

};  // class SrtmTile
}  // namespace Fmi
