#include "SrtmMatrix.h"

#include "SrtmTile.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <vector>

namespace Fmi
{
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

 private:
  std::vector<TileType> itsTiles;
  std::size_t itsSize = 0;
};

// ----------------------------------------------------------------------
/*!
 * \brief Initialize the implementation details
 */
// ----------------------------------------------------------------------

SrtmMatrix::Impl::Impl()
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
  if (itsSize == 0)
    itsSize = tile->size();
  else if (itsSize != tile->size())
    throw std::runtime_error("Attempting to add a SRTM tile of size " +
                             std::to_string(tile->size()) + " to a 2D matrix with tile size " +
                             std::to_string(itsSize));

  // Shift to 0..360,0..180 coordinates
  int lon = tile->longitude() + 180;
  int lat = tile->latitude() + 90;

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
  double resolution = 1.0 / itsSize;  // either 1" or 3"

  // Handle the north pole gracefully if there was a tile
  // near it. If there isn't, we'll return -32768 as usual.
  lat = std::min(lat, 90 - resolution / 2);

  // Establish the tile containing the coordinate

  lon += 180;
  lat += 90;

  int tile_i = static_cast<int>(lon);  // rounded down
  int tile_j = static_cast<int>(lat);

  // Establish the grid cell containing the coordinate.

  int cell_i = static_cast<int>((lon - tile_i) / resolution);
  int cell_j = static_cast<int>((lat - tile_j) / resolution);

  // Just in case the above calculation overflows due to
  // numerical accuracies

  // cell_i = std::min(cell_i, static_cast<int>(itsSize-1));
  // cell_j = std::min(cell_j, static_cast<int>(itsSize-1));

  const auto& tile = itsTiles[tile_i + 360 * tile_j];
  if (!tile) return std::numeric_limits<double>::quiet_NaN();

  int h = tile->value(cell_i, cell_j);
  return h;
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

SrtmMatrix::SrtmMatrix() : impl(new SrtmMatrix::Impl()) {}
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

void SrtmMatrix::add(TileType tile) { impl->add(std::move(tile)); }
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

double SrtmMatrix::value(double lon, double lat) const { return impl->value(lon, lat); }
}  // namespace Fmi
