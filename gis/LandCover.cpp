#define BOOST_FILESYSTEM_NO_DEPRECATED

#include "LandCover.h"
#include "SrtmMatrix.h"
#include "SrtmTile.h"
#include <boost/filesystem/operations.hpp>
#include <boost/regex.hpp>
#include <fmt/format.h>
#include <cmath>
#include <limits>
#include <list>
#include <stdexcept>
#include <string>

namespace Fmi
{
namespace
{
// ----------------------------------------------------------------------
/*!
 * \brief Find all .hgt files recursively from the input directory
 *
 * Note: We validate the filenames by ourselves, since SrtmTile
 * constructor would throw for invalid names and sizes.
 */
// ----------------------------------------------------------------------

std::list<boost::filesystem::path> find_hgt_files(const std::string& path)
{
  if (!boost::filesystem::is_directory(path))
    throw std::runtime_error("Not a directory: '" + path + "'");

  std::list<boost::filesystem::path> files;

  boost::filesystem::recursive_directory_iterator end_dir;
  for (boost::filesystem::recursive_directory_iterator it(path); it != end_dir; ++it)
  {
    if (boost::filesystem::is_regular_file(it->status()) &&
        SrtmTile::valid_path(it->path().string()) && SrtmTile::valid_size(it->path().string()))
    {
      files.push_back(it->path());
    }
  }
  return files;
}
}  // namespace

// ----------------------------------------------------------------------
/*!
 * \brief Implementation details class
 */
// ----------------------------------------------------------------------

class LandCover::Impl
{
 public:
  Impl(const std::string& path);
  LandCover::Type coverType(double lon, double lat) const;

 private:
  // Note: We want the LandCover level with largest tiles (most accurate) first
  using SrtmMatrices = std::map<std::size_t, SrtmMatrix, std::greater<std::size_t>>;
  SrtmMatrices itsMatrices;
};

// ----------------------------------------------------------------------
/*!
 * \brief Construct the LandCover data structures from a directory
 */
// ----------------------------------------------------------------------

LandCover::Impl::Impl(const std::string& path)
{
  auto files = find_hgt_files(path);

  // Read all the files
  for (const auto& p : files)
  {
    std::unique_ptr<SrtmTile> tile(new SrtmTile(p.string()));
    SrtmMatrix& matrix = itsMatrices[tile->size()];  // creates new matrix if necessary
    matrix.add(std::move(tile));
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Return the elevation for the given valid coordinate
 */
// ----------------------------------------------------------------------

LandCover::Type LandCover::Impl::coverType(double lon, double lat) const
{
  // Normalize the coordinates to ranges (-180,180( and (-90,90(

  if (lon >= 180) lon -= 360;

  // Try the matrices starting from largest tile size and hence most accurate

  Type covertype = NoData;
  for (const auto& size_matrix : itsMatrices)
  {
    double value = size_matrix.second.value(lon, lat);
    if (!std::isnan(value) && (value != SrtmMatrix::missing))
    {
      covertype = Type(value);
      if (covertype != LandCover::NoData) return covertype;
    }
  }

  if (covertype == LandCover::NoData) return LandCover::Sea;

  return covertype;
}

// ----------------------------------------------------------------------
/*!
 * \brief The destructor needs to be defined in the cpp file for the impl-idiom
 */
// ----------------------------------------------------------------------

LandCover::~LandCover() = default;

// ----------------------------------------------------------------------
/*!
 * \brief The constructor reads recursively all .hgt files
 */
//----------------------------------------------------------------------

LandCover::LandCover(const std::string& path) : impl(new LandCover::Impl(path)) {}
// ----------------------------------------------------------------------
/*!
 * \brief Return the land cover type for the given coordinate
 */
// ----------------------------------------------------------------------

LandCover::Type LandCover::coverType(double lon, double lat) const
{
  if (lon < -180 || lon > 180 || lat < -90 || lat > 90)
  {
    throw std::runtime_error(fmt::format(
        "LandCover: Input coordinate {},{} is out of bounds [-180,180],[-90,90]", lon, lat));
  }

  return impl->coverType(lon, lat);
}

// ----------------------------------------------------------------------
/*!
 * \brief Return true if the land cover type is water for the given coordinate
 */
// ----------------------------------------------------------------------

bool LandCover::isOpenWater(LandCover::Type theType)
{
  switch (theType)
  {
    case IrrigatedCropLand:
    case RainFedCropLand:
    case MosaicCropLand:
    case MosaicVegetation:
    case ClosedToOpenBroadLeavedDeciduousForest:
    case ClosedBroadLeavedDeciduousForest:
    case OpenBroadLeavedDeciduousForest:
    case ClosedNeedleLeavedEvergreenForest:
    case OpenNeedleLeavedDeciduousOrEvergreenForest:
    case MixedForest:
    case MosaicForest:
    case MosaicGrassLand:
    case ShrubLand:
    case Herbaceous:
    case SparseVegetation:
    case RegularlyFloodedForest:
    case PermanentlyFloodedForest:
    case RegularlyFloodedGrassland:
    case Urban:
    case Bare:
    case PermanentSnow:
    case NoData:
    case RiverEstuary:
      return false;
    case Lakes:
    case Sea:
    case CaspianSea:
      return true;
  }
  // Some compilers do not see all values have been handled above
  return false;
}

}  // namespace Fmi
