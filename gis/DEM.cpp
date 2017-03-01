#include "DEM.h"
#include "SrtmTile.h"
#include "SrtmMatrix.h"

#define BOOST_FILESYSTEM_NO_DEPRECATED
#include <boost/filesystem/operations.hpp>
#include <boost/algorithm/string/find.hpp>
#include <boost/regex.hpp>
#include <macgyver/String.h>

#include <cmath>
#include <limits>
#include <list>
#include <stdexcept>
#include <sstream>
#include <string>

namespace Fmi
{
namespace
{
std::string tiletype2string(TileType tiletype)
{
  std::string ret;

  switch (tiletype)
  {
    case TileType::DEM1_3601:
      ret = "DEM1_3601";
      break;
    case TileType::DEM3_1201:
      ret = "DEM3_1201";
      break;
    case TileType::DEM9_401:
      ret = "DEM9_401";
      break;
    case TileType::DEM9_1201:
      ret = "DEM9_1201";
      break;
    case TileType::LAND_COVER_361:
      ret = "LAND_COVER_361";
      break;
    case TileType::DEM27_1201:
      ret = "DEM27_1201";
      break;
    case TileType::DEM81_1001:
      ret = "DEM81_1001";
      break;
    default:
      ret = "UNDEFINED_TILETYPE";
  }

  return ret;
}

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
  using namespace boost::filesystem;
  if (!is_directory(path)) throw std::runtime_error("Not a directory: '" + path + "'");

  std::list<boost::filesystem::path> files;

  recursive_directory_iterator end_dir;
  for (recursive_directory_iterator it(path); it != end_dir; ++it)
  {
    if (is_regular_file(it->status()) && SrtmTile::valid_path(it->path().string()) &&
        SrtmTile::valid_size(it->path().string()))
    {
      files.push_back(it->path());
    }
  }

  return files;
}
}

// ----------------------------------------------------------------------
/*!
 * \brief Implementation details class
 */
// ----------------------------------------------------------------------

class DEM::Impl
{
 public:
  Impl(const std::string& path);
  double elevation(double lon, double lat) const;
  double elevation(double lon, double lat, double resolution) const;
  double elevation(double lon, double lat, TileType tiletype) const;

 private:
  // Note: We want the DEM level with largest tiles (most accurate) first.
  // However, we may skip levels which are of too good resolution for speed
  // and to avoid noise in rendered images.
  typedef std::map<TileType, SrtmMatrix, std::less<TileType>> SrtmMatrices;
  SrtmMatrices itsMatrices;
};

// ----------------------------------------------------------------------
/*!
 * \brief Construct the DEM data structures from a directory
 */
// ----------------------------------------------------------------------

DEM::Impl::Impl(const std::string& path)
{
  auto files = find_hgt_files(path);

  // Read all the files
  for (const auto& p : files)
  {
    std::unique_ptr<SrtmTile> tile(new SrtmTile(p.string()));

    SrtmMatrix& matrix = itsMatrices[tile->type()];  // creates new matrix if necessary
    matrix.add(std::move(tile));
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Return the elevation for the given valid coordinate
 */
// ----------------------------------------------------------------------

double DEM::Impl::elevation(double lon, double lat) const
{
  // Normalize the coordinates to ranges (-180,180( and (-90,90(
  if (lon >= 180) lon -= 360;

  // Try the matrices starting from largest tile size and hence most accurate

  double value = SrtmMatrix::missing;
  for (const auto& size_matrix : itsMatrices)
  {
    value = size_matrix.second.value(lon, lat);
    if (!isnan(value) && (value != SrtmMatrix::missing)) return value;
  }

  // Now value is either NaN to indicate a value at sea or
  // the missing value -32768, which we convert to NaN

  if (isnan(value))
    return 0;
  else
    return std::numeric_limits<double>::quiet_NaN();
}

// ----------------------------------------------------------------------
/*!
 * \brief Return the elevation with desired max resolution
 */
// ----------------------------------------------------------------------

double DEM::Impl::elevation(double lon, double lat, double resolution) const
{
  if (itsMatrices.size() == 0) return std::numeric_limits<double>::quiet_NaN();

  // Normalize the coordinates to ranges (-180,180( and (-90,90(

  if (lon >= 180) lon -= 360;

  // Size limit corresponding to the resolution
  // 3601 = 1 second = 30 meters
  // 1201 = 3 seconds = 93 meters
  // 401 = 9 seconds = 278 meters
  // 134 = 27 seconds = 831 meters
  // 45 = 81 seconds = 2474 meters

  TileType tiletype = TileType::UNDEFINED_TILETYPE;

  // deduce tiletype from resolution
  if (resolution < 0.1)
    tiletype = TileType::DEM1_3601;
  else if (resolution < 0.5)
    tiletype = TileType::DEM3_1201;
  else if (resolution < 1.0)
    tiletype = TileType::DEM9_401;
  else if (resolution < 2.5)
    tiletype = TileType::DEM27_1201;
  else
    tiletype = TileType::DEM81_1001;

  SrtmMatrices::const_iterator it = itsMatrices.begin();

  // Find tileset of tiletype that corresponds to requsted resolution
  while (it != itsMatrices.end() && it->first != tiletype)
    ++it;

  // If tiletype not found take the first tileset (best accuracy)
  if (it == itsMatrices.end()) it = itsMatrices.begin();

  double value = it->second.value(lon, lat);
  if (!isnan(value) && (value != SrtmMatrix::missing)) return value;

  // Now value is either NaN to indicate a value at sea or
  // the missing value -32768, which we convert to NaN

  if (isnan(value))
    return 0;
  else
    return std::numeric_limits<double>::quiet_NaN();
}

// ----------------------------------------------------------------------
/*!
 * \brief Return the elevation using requested tiletype
 */
// ----------------------------------------------------------------------

double DEM::Impl::elevation(double lon, double lat, TileType tiletype) const
{
  if (itsMatrices.size() == 0) return std::numeric_limits<double>::quiet_NaN();

  // Normalize the coordinates to ranges (-180,180( and (-90,90(
  if (lon >= 180) lon -= 360;

  // Find tileset of the requested tiletype
  SrtmMatrices::const_iterator it = itsMatrices.begin();

  while (it != itsMatrices.end() && it->first != tiletype)
    it++;

  // If tiletype not found take the first tileset (best accuracy)
  if (it == itsMatrices.end()) it = itsMatrices.begin();

  double value = it->second.value(lon, lat);
  if (!isnan(value) && (value != SrtmMatrix::missing)) return value;

  // Now value is either NaN to indicate a value at sea or
  // the missing value -32768, which we convert to NaN

  if (isnan(value))
    return 0;
  else
    return std::numeric_limits<double>::quiet_NaN();
}

// ----------------------------------------------------------------------
/*!
 * \brief The destructor needs to be defined in the cpp file for the impl-idiom
 */
// ----------------------------------------------------------------------

DEM::~DEM() = default;

// ----------------------------------------------------------------------
/*!
 * \brief The constructor reads recursively all .hgt files
 */
//----------------------------------------------------------------------

DEM::DEM(const std::string& path) : impl(new DEM::Impl(path))
{
}
// ----------------------------------------------------------------------
/*!
 * \brief Return the elevation for the given coordinate
 */
// ----------------------------------------------------------------------

double DEM::elevation(double lon, double lat) const
{
  if (lon < -180 || lon > 180 || lat < -90 || lat > 90)
  {
    std::ostringstream out;
    out << "DEM: Input coordinate " << lon << "," << lat << " is out of bounds [-180,180],[-90,90]";
    throw std::runtime_error(out.str());
  }

  return impl->elevation(lon, lat);
}

// ----------------------------------------------------------------------
/*!
 * \brief Return the elevation for the given coordinate with data if given resolution
 */
// ----------------------------------------------------------------------

double DEM::elevation(double lon, double lat, double resolution) const
{
  if (lon < -180 || lon > 180 || lat < -90 || lat > 90)
  {
    std::ostringstream out;
    out << "DEM: Input coordinate " << lon << "," << lat << " is out of bounds [-180,180],[-90,90]";
    throw std::runtime_error(out.str());
  }

  if (resolution < 0) throw std::runtime_error("Desired DEM resolution cannot be negative");

  if (resolution == 0)
    return impl->elevation(lon, lat);
  else
    return impl->elevation(lon, lat, resolution);
}

// ----------------------------------------------------------------------
/*!
 * \brief Return the elevation for the given coordinate with data if given resolution
 */
// ----------------------------------------------------------------------

double DEM::elevation(double lon, double lat, TileType tiletype) const
{
  if (fabs(lon) > 180 || fabs(lat) > 90)
  {
    std::ostringstream out;
    out << "DEM: Input coordinate " << lon << "," << lat << " is out of bounds [-180,180],[-90,90]";
    throw std::runtime_error(out.str());
  }

  return impl->elevation(lon, lat, tiletype);
}

}  // namespace Fmi
