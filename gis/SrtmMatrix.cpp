#include "SrtmMatrix.h"
#include "SrtmTile.h"
#include <boost/lexical_cast.hpp>
#include <limits>
#include <vector>
#include <iostream>

namespace Fmi
{
// returns tilesize in degrees
double tilesize_degrees(TileType tiletype)
{
  if (tiletype == TileType::DEM9_1201)
    return 3.0;
  else if (tiletype == TileType::DEM27_1201)
    return 9.0;
  else if (tiletype == TileType::DEM81_1001)
    return 22.5;

  // TileType::DEM1_3601, TileType::DEM3_1201, TileType::DEM9_401,TileType::LAND_COVER_361
  return 1.0;
}

// ----------------------------------------------------------------------
/*!
 * \brief Implementation details
 */
// ----------------------------------------------------------------------

class SrtmMatrix::Impl
{
 public:
  Impl();
  void add(TileType tile);
  double value(double lon, double lat) const;

  Fmi::TileType tiletype() const
  {
    if (itsTiles.size() > 0)
      for (unsigned int i = 0; i < itsTiles.size(); i++)
        if (itsTiles[i]) return itsTiles[i]->type();

    return Fmi::TileType::UNDEFINED_TILETYPE;
  }

 private:
  std::vector<TileType> itsTiles;
  std::size_t itsSize = 0;
};

// ----------------------------------------------------------------------
/*!
 * \brief Initialize the implementation details
 */
// ----------------------------------------------------------------------

SrtmMatrix::Impl::Impl() : itsTiles()
{
  itsTiles.resize(360 * 180);  // 1x1 degree tiles covering the world
}

// ----------------------------------------------------------------------
/*!
 * \brief Add a new tile to the matrix
 */
// ----------------------------------------------------------------------

void SrtmMatrix::Impl::add(TileType tile)
{
  std::string tilename = tile->path();
  if (itsSize == 0)
    itsSize = tile->size();
  else if (itsSize != tile->size())
    throw std::runtime_error(
        "Attempting to add a SRTM tile of size " + boost::lexical_cast<std::string>(tile->size()) +
        " to a 2D matrix with tile size " + boost::lexical_cast<std::string>(itsSize) +
        std::string(" ") + tilename);

  // Shift to 0..360,0..180 coordinates

  int lon = tile->longitude() + 180.0;
  int lat = tile->latitude() + 90.0;

  itsTiles[lon + 360 * lat] = std::move(tile);
}

// ----------------------------------------------------------------------
/*!
 * \brief Establish the grid value
 *
 * Algorithm notes: tile values represent the height at the center of the
 * grid cell.
 *
 * If any of the required tiles is missing, we return the missing value
 * indicator.
 */
// ----------------------------------------------------------------------

double SrtmMatrix::Impl::value(double lon, double lat) const
{
  // Width of one grid cell
  double tilesize_deg = tilesize_degrees(tiletype());
  double resolution = tilesize_deg / itsSize;  //  possible values: 1",3",9",27",81"

  //  std::cout << "lon, lat: " << lon << ", " << lat << std::endl;

  // Handle the north pole gracefully if there was a tile
  // near it. If there isn't, we'll return -32768 as usual.
  lat = std::min(lat, 90 - resolution / 2);

  // Establish the tile containing the coordinate

  lon += 180;
  lat += 90;

  double tile_i = lon;
  double tile_j = lat;

  // rounded down to the nearest tilesize_deg
  tile_i -= fmod(tile_i, tilesize_deg);
  tile_j -= fmod(tile_j, tilesize_deg);

  // Establish the grid cell containing the coordinate.
  int cell_i = static_cast<int>(((lon - tile_i) / resolution));
  int cell_j = static_cast<int>(((lat - tile_j) / resolution));

  // Just in case the above calculation overflows due to
  // numerical accuracies

  cell_i = std::min(cell_i, static_cast<int>(itsSize - 1));
  cell_j = std::min(cell_j, static_cast<int>(itsSize - 1));

  int tile_i_index = static_cast<int>(tile_i);  // rounded down
  int tile_j_index = static_cast<int>(tile_j);

  const auto& tile = itsTiles[tile_i_index + 360 * tile_j_index];

  //  std::cout << "value from tile: " << (tile_i_index + 360 * tile_j_index) << std::endl;

  if (!tile) return std::numeric_limits<double>::quiet_NaN();

  /*
  std::cout << "value from position " << cell_i << ", " << cell_j << " is "
            << tile->value(cell_i, cell_j) << std::endl;
  */
  return tile->value(cell_i, cell_j);
}

// ----------------------------------------------------------------------
/*!
 * \brief The destructor is needed for the impl-idiom
 */
// ----------------------------------------------------------------------

SrtmMatrix::~SrtmMatrix() = default;

// ----------------------------------------------------------------------
/*!
 * \brief SrtmMatrix constructor
 */
// ----------------------------------------------------------------------

SrtmMatrix::SrtmMatrix() : impl(new SrtmMatrix::Impl())
{
}
// ----------------------------------------------------------------------
/*!
 * \brief Add a new tile to the matrix
 *
 * This will throw if the size does not match the size of the
 * first inserted tile.
 *
 * Thread safety issue: always add all tiles before using
 * the elevation method.
 */
// ----------------------------------------------------------------------

void SrtmMatrix::add(TileType tile)
{
  impl->add(std::move(tile));
}
// ----------------------------------------------------------------------
/*!
 * \brief Calculate the DEM elevation for the given coordinate.
 *
 * Each tile cell represents the elevation at the center of the cell.
 * To use bilinear interpolation to get the elevation for an arbitrary
 * coordinate we need to establish the elevations at the 4 surrounding
 * cell centers, which may involve switching tiles, which may involve
 * checking whether longitudal wrapping should be done. Also, we need
 * to handle the polar regions separately when no wrapping can be done.
 *
 * May return NaN if data is not available.
 */
// ----------------------------------------------------------------------

double SrtmMatrix::value(double lon, double lat) const
{
  return impl->value(lon, lat);
}

TileType SrtmMatrix::tiletype() const
{
  return impl->tiletype();
}
}  // namespace Fmi
