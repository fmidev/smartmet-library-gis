#define BOOST_FILESYSTEM_NO_DEPRECATED
#include "DEM.h"

#include "SrtmMatrix.h"
#include "SrtmTile.h"
#include <macgyver/Exception.h>

#include <filesystem>
#include <fmt/format.h>

#include <cmath>
#include <limits>
#include <list>
#include <map>
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

std::list<std::filesystem::path> find_hgt_files(const std::string& path)
{
  try
  {
    if (!std::filesystem::is_directory(path))
      throw Fmi::Exception::Trace(BCP, "Not a directory: '" + path + "'");

    std::list<std::filesystem::path> files;

    std::filesystem::recursive_directory_iterator end_dir;
    for (std::filesystem::recursive_directory_iterator it(path); it != end_dir; ++it)
    {
      if (is_regular_file(it->status()) && SrtmTile::valid_path(it->path().string()) &&
          SrtmTile::valid_size(it->path().string()))
      {
        files.push_back(it->path());
      }
    }
    return files;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}
}  // namespace

// ----------------------------------------------------------------------
/*!
 * \brief Implementation details class
 */
// ----------------------------------------------------------------------

class DEM::Impl
{
 public:
  explicit Impl(const std::string& path);
  double elevation(double lon, double lat) const;
  double elevation(double lon, double lat, double resolution) const;

 private:
  // Note: We want the DEM level with largest tiles (most accurate) first.
  // However, we may skip levels which are of too good resolution for speed
  // and to avoid noise in rendered images.
  using SrtmMatrices = std::map<std::size_t, SrtmMatrix, std::greater<std::size_t>>;
  SrtmMatrices itsMatrices;
};

// ----------------------------------------------------------------------
/*!
 * \brief Construct the DEM data structures from a directory
 */
// ----------------------------------------------------------------------

DEM::Impl::Impl(const std::string& path)
{
  try
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
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Return the elevation for the given valid coordinate
 */
// ----------------------------------------------------------------------

double DEM::Impl::elevation(double lon, double lat) const
{
  try
  {
    // Normalize the coordinates to ranges (-180,180( and (-90,90(

    if (lon >= 180)
      lon -= 360;

    // Try the matrices starting from largest tile size and hence most accurate

    double value = SrtmMatrix::missing;
    for (const auto& size_matrix : itsMatrices)
    {
      value = size_matrix.second.value(lon, lat);
      if (!std::isnan(value) && (value != SrtmMatrix::missing))
        return value;
    }

    // Now value is either NaN to indicate a value at sea or
    // the missing value -32768, which we convert to NaN

    if (std::isnan(value))
      return 0;
    return std::numeric_limits<double>::quiet_NaN();
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Return the elevation with desired max resolution
 */
// ----------------------------------------------------------------------

double DEM::Impl::elevation(double lon, double lat, double resolution) const
{
  try
  {
    // Normalize the coordinates to ranges (-180,180( and (-90,90(

    if (lon >= 180)
      lon -= 360;

    // Size limit corresponding to the resolution
    // 3601 = 1 second = 30 meters
    // 1201 = 3 seconds = 90 meters
    //  401 = 9 seconds = 270 meters

    auto sizelimit = static_cast<std::size_t>(3600 * 30 / (1000 * resolution));

    // Find the first tileset of sufficient accuracy

    auto it = itsMatrices.begin();

    while (it != itsMatrices.end())
    {
      if (it->first < sizelimit)
      {
        if (it != itsMatrices.begin())
          --it;
        break;
      }
      ++it;
    }
    if (it == itsMatrices.end())
      --it;

    double value = SrtmMatrix::missing;
    while (it != itsMatrices.end())
    {
      value = it->second.value(lon, lat);
      if (!std::isnan(value) && (value != SrtmMatrix::missing))
        return value;
      ++it;
    }

    // Now value is either NaN to indicate a value at sea or
    // the missing value -32768, which we convert to NaN

    if (std::isnan(value))
      return 0;
    return std::numeric_limits<double>::quiet_NaN();
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
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

DEM::DEM(const std::string& path) : impl(new DEM::Impl(path)) {}
// ----------------------------------------------------------------------
/*!
 * \brief Return the elevation for the given coordinate
 */
// ----------------------------------------------------------------------

double DEM::elevation(double lon, double lat) const
{
  try
  {
    if (lon < -180 || lon > 180 || lat < -90 || lat > 90)
    {
      throw Fmi::Exception::Trace(
          BCP,
          fmt::format(
              "DEM: Input coordinate {},{} is out of bounds [-180,180],[-90,90]", lon, lat));
    }

    return impl->elevation(lon, lat);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Return the elevation for the given coordinate with data if given resolution
 */
// ----------------------------------------------------------------------

double DEM::elevation(double lon, double lat, double resolution) const
{
  try
  {
    if (lon < -180 || lon > 180 || lat < -90 || lat > 90)
    {
      throw Fmi::Exception::Trace(
          BCP,
          fmt::format(
              "DEM: Input coordinate {},{} is out of bounds [-180,180],[-90,90]", lon, lat));
    }

    if (resolution < 0)
      throw Fmi::Exception::Trace(BCP, "Desired DEM resolution cannot be negative");

    if (resolution == 0)
      return impl->elevation(lon, lat);
    return impl->elevation(lon, lat, resolution);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

}  // namespace Fmi
