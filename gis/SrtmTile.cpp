#define BOOST_FILESYSTEM_NO_DEPRECATED

#include "SrtmTile.h"

#include <boost/filesystem/operations.hpp>
#include <fmt/format.h>
#include <macgyver/Exception.h>
#include <macgyver/MappedFile.h>
#include <filesystem>
#include <mutex>

namespace Fmi
{
// ----------------------------------------------------------------------
/*!
 * \brief Validate a .hgt filename
 */
// ----------------------------------------------------------------------

bool SrtmTile::valid_path(const std::string &path)
{
  try
  {
    std::filesystem::path p(path);

    // Avoid locale locs by not using regex here
    auto name = p.filename().string();
    return (name.size() == 1 + 2 + 1 + 3 + 4 && (name[0] == 'N' || name[0] == 'S') &&
            std::isdigit(name[1]) && std::isdigit(name[2]) && (name[3] == 'E' || name[3] == 'W') &&
            std::isdigit(name[4]) && std::isdigit(name[5]) && std::isdigit(name[6]) &&
            name[7] == '.' && name[8] == 'h' && name[9] == 'g' && name[10] == 't');
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Validate .hgt file size
 *
 * The file size should be of the form N*N*2 for a square tile
 * covering a 1x1 degree tile.
 */
// ----------------------------------------------------------------------

bool SrtmTile::valid_size(const std::string &path)
{
  try
  {
    auto sz = std::filesystem::file_size(path);
    auto n = static_cast<std::size_t>(sqrt(sz / 2.0));
    return (sz == 2 * n * n);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Implementation details
 */
// ----------------------------------------------------------------------

class SrtmTile::Impl
{
 public:
  explicit Impl(const std::string &path);
  ~Impl();
  const std::string &path() const { return itsPath; }
  std::size_t size() const { return itsSize; }
  int longitude() const { return itsLon; }
  int latitude() const { return itsLat; }
  int value(std::size_t i, std::size_t j);

 private:
  std::string itsPath;
  std::size_t itsSize;
  int itsLon;
  int itsLat;

  std::mutex itsMutex;
  std::unique_ptr<Fmi::MappedFile> itsFileMapping;
};

// ----------------------------------------------------------------------
/*!
 * \brief Map the tile to memory
 */
// ----------------------------------------------------------------------

SrtmTile::Impl::Impl(const std::string &path) : itsPath(path)
{
  try
  {
    if (!valid_path(path))
      throw Fmi::Exception::Trace(BCP, "Not a valid filename for a .hgt file: '" + path + "'");
    if (!valid_size(path))
      throw Fmi::Exception::Trace(BCP,
                                  "Not a valid size of form 2*N*N for a .hgt file: '" + path + "'");

    auto sz = std::filesystem::file_size(path);
    itsSize = static_cast<std::size_t>(sqrt(sz / 2.0));

    // Sample filename : S89E172.hgt

    std::filesystem::path p(path);
    auto name = p.filename().string();

    int sign = (name[0] == 'S' ? -1 : 1);
    itsLat = sign * std::stoi(name.substr(1, 2));

    sign = (name[3] == 'W' ? -1 : 1);
    itsLon = sign * std::stoi(name.substr(4, 3));

    itsFileMapping.reset(
        new Fmi::MappedFile(
            itsPath,
            boost::iostreams::mapped_file::readonly,
            2 * itsSize * itsSize,
            0));
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

SrtmTile::Impl::~Impl() = default;

// ----------------------------------------------------------------------
/*!
 * \brief Return the value at the given tile coordinate
 */
// ----------------------------------------------------------------------

int SrtmTile::Impl::value(std::size_t i, std::size_t j)
{
  try
  {
    // Sanity checks
    if (i >= itsSize || j >= itsSize)
      throw Fmi::Exception::Trace(
          BCP,
          fmt::format("SrtmFile indexes {},{} is out of range, size of tile is {}", i, j, itsSize));

    const auto *ptr = reinterpret_cast<const std::int16_t *>(itsFileMapping->const_data());
    std::int16_t big_endian = ptr[i + (itsSize - j - 1) * itsSize];
    std::int16_t little_endian = ((big_endian >> 8) & 0xff) + ((big_endian & 0xff) << 8);
    return little_endian;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief The destructor is needed for the impl-idiom
 */
// ----------------------------------------------------------------------

SrtmTile::~SrtmTile() = default;

// ----------------------------------------------------------------------
/*!
 * \brief Read a SRTM tile
 */
// ----------------------------------------------------------------------

SrtmTile::SrtmTile(const std::string &path) : impl(new SrtmTile::Impl(path)) {}
// ----------------------------------------------------------------------
/*!
 * \brief Return the original path of the tile
 */
// ----------------------------------------------------------------------

const std::string &SrtmTile::path() const
{
  try
  {
    return impl->path();
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}
// ----------------------------------------------------------------------
/*!
 * \brief Return the size of the tile
 */
// ----------------------------------------------------------------------

std::size_t SrtmTile::size() const
{
  try
  {
    return impl->size();
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}
// ----------------------------------------------------------------------
/*!
 * \brief Return the lower left corner longitude of the tile
 */
// ----------------------------------------------------------------------

int SrtmTile::longitude() const
{
  try
  {
    return impl->longitude();
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}
// ----------------------------------------------------------------------
/*!
 * \brief Return the lower left corner latitude of the tile
 */
// ----------------------------------------------------------------------

int SrtmTile::latitude() const
{
  try
  {
    return impl->latitude();
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}
// ----------------------------------------------------------------------
/*!
 * \brief Return the value at the given tile coordinate
 *
 * Lack of interpolation support is intentional, it is provided
 * at the SrtmMatrix level where adjacent tiles are available
 */
// ----------------------------------------------------------------------

int SrtmTile::value(std::size_t i, std::size_t j) const
{
  try
  {
    return impl->value(i, j);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}
}  // namespace Fmi
