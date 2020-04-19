#define BOOST_FILESYSTEM_NO_DEPRECATED

#include "SrtmTile.h"
#include <boost/filesystem/operations.hpp>
#include <boost/interprocess/file_mapping.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include <boost/move/unique_ptr.hpp>
#include <boost/thread.hpp>
#include <fmt/format.h>
#include <stdexcept>

using FileMapping = boost::interprocess::file_mapping;
using MappedRegion = boost::interprocess::mapped_region;

using MutexType = boost::shared_mutex;
using ReadLock = boost::shared_lock<MutexType>;
using WriteLock = boost::unique_lock<MutexType>;
using UpgradeReadLock = boost::upgrade_lock<MutexType>;
using UpgradeWriteLock = boost::upgrade_to_unique_lock<MutexType>;

namespace Fmi
{
// ----------------------------------------------------------------------
/*!
 * \brief Validate a .hgt filename
 */
// ----------------------------------------------------------------------

bool SrtmTile::valid_path(const std::string &path)
{
  boost::filesystem::path p(path);

  // Avoid locale locs by not using regex here
  auto name = p.filename().string();
  return (name.size() == 1 + 2 + 1 + 3 + 4 && (name[0] == 'N' || name[0] == 'S') &&
          std::isdigit(name[1]) && std::isdigit(name[2]) && (name[3] == 'E' || name[3] == 'W') &&
          std::isdigit(name[4]) && std::isdigit(name[5]) && std::isdigit(name[6]) &&
          name[7] == '.' && name[8] == 'h' && name[9] == 'g' && name[10] == 't');
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
  auto sz = boost::filesystem::file_size(path);
  auto n = static_cast<std::size_t>(sqrt(sz / 2.0));
  return (sz == 2 * n * n);
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

  MutexType itsMutex;
  boost::movelib::unique_ptr<FileMapping> itsFileMapping;
  boost::movelib::unique_ptr<MappedRegion> itsMappedRegion;
};

// ----------------------------------------------------------------------
/*!
 * \brief Map the tile to memory
 */
// ----------------------------------------------------------------------

SrtmTile::Impl::Impl(const std::string &path) : itsPath(path)
{
  if (!valid_path(path))
    throw std::runtime_error("Not a valid filename for a .hgt file: '" + path + "'");
  if (!valid_size(path))
    throw std::runtime_error("Not a valid size of form 2*N*N for a .hgt file: '" + path + "'");

  auto sz = boost::filesystem::file_size(path);
  itsSize = static_cast<std::size_t>(sqrt(sz / 2.0));

  // Sample filename : S89E172.hgt

  boost::filesystem::path p(path);
  auto name = p.filename().string();

  int sign = (name[0] == 'S' ? -1 : 1);
  itsLat = sign * std::stoi(name.substr(1, 2));

  sign = (name[3] == 'W' ? -1 : 1);
  itsLon = sign * std::stoi(name.substr(4, 3));
}

// ----------------------------------------------------------------------
/*!
 * \brief Return the value at the given tile coordinate
 */
// ----------------------------------------------------------------------

int SrtmTile::Impl::value(std::size_t i, std::size_t j)
{
  // Sanity checks
  if (i >= itsSize || j >= itsSize)
    throw std::runtime_error(
        fmt::format("SrtmFile indexes {},{} is out of range, size of tile is {}", i, j, itsSize));

  // We use lazy initialization to reduce core sizes in case
  // private mapped files are not disabled in core dumps.
  // We use private mapping intentionally, this class is expected
  // to be used in servers where no other process maps the same
  // files, and we wish to reserve shared mapping for core dumps.

  // We do not intend to write to the DEM files, but we use read_write
  // mode to get private mappings instead of shared ones.

  UpgradeReadLock readlock(itsMutex);

  if (!itsFileMapping)
  {
    UpgradeWriteLock writelock(readlock);
    if (!itsFileMapping)
    {
      // std::cout << "Mapping " << itsPath << std::endl;
      itsFileMapping =
          boost::movelib::make_unique<FileMapping>(itsPath.c_str(), boost::interprocess::read_only);

      itsMappedRegion = boost::movelib::make_unique<MappedRegion>(
          *itsFileMapping, boost::interprocess::read_only, 0, 2 * itsSize * itsSize);

      // We do not expect any normal access patterns, so disable prefetching
      itsMappedRegion->advise(boost::interprocess::mapped_region::advice_random);
    }
  }

  // Now the data is definitely available. Note: data runs from
  // north down, but we index if from bottom up

  auto *ptr = reinterpret_cast<std::int16_t *>(itsMappedRegion->get_address());
  std::int16_t big_endian = ptr[i + (itsSize - j - 1) * itsSize];
  std::int16_t little_endian = ((big_endian >> 8) & 0xff) + ((big_endian & 0xff) << 8);
  return little_endian;
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

const std::string &SrtmTile::path() const { return impl->path(); }
// ----------------------------------------------------------------------
/*!
 * \brief Return the size of the tile
 */
// ----------------------------------------------------------------------

std::size_t SrtmTile::size() const { return impl->size(); }
// ----------------------------------------------------------------------
/*!
 * \brief Return the lower left corner longitude of the tile
 */
// ----------------------------------------------------------------------

int SrtmTile::longitude() const { return impl->longitude(); }
// ----------------------------------------------------------------------
/*!
 * \brief Return the lower left corner latitude of the tile
 */
// ----------------------------------------------------------------------

int SrtmTile::latitude() const { return impl->latitude(); }
// ----------------------------------------------------------------------
/*!
 * \brief Return the value at the given tile coordinate
 *
 * Lack of interpolation support is intentional, it is provided
 * at the SrtmMatrix level where adjacent tiles are available
 */
// ----------------------------------------------------------------------

int SrtmTile::value(std::size_t i, std::size_t j) const { return impl->value(i, j); }
}  // namespace Fmi
