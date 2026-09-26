// ======================================================================
/*!
 * \brief Exact coordinate to timezone resolution from vector polygons
 *
 * Designed for the timezone-boundary-builder data set, which is built from
 * OpenStreetMap boundaries:
 *
 *   https://github.com/evansiroky/timezone-boundary-builder
 *
 * The "with-oceans" variant covers the entire globe and is installed by
 * the smartmet-timezones RPM as default_source. Any GDAL/OGR vector source
 * with a timezone name attribute can be used, including PostGIS tables.
 *
 * Constructed objects are immutable and hence thread safe. Building the
 * search structure for the full data set takes about a second on a modern
 * multicore machine and needs about 90 MB of memory. A search takes
 * about 50 nanoseconds.
 *
 * See docs/gis-timezones.md for details.
 */
// ======================================================================

#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class OGRLayer;

namespace Fmi
{
class TimeZoneFinder
{
 public:
  // The shapefile installed by the smartmet-timezones RPM
  static const char* const default_source;

  struct Options
  {
    std::string field = "tzid";          // attribute holding the IANA timezone name
    std::size_t max_vertices = 256;      // max vertices in a polygon piece, minimum 16
    std::vector<std::string> preferred;  // preferred zones in overlapping (disputed) areas
    bool make_valid = true;              // repair invalid geometries with GEOSMakeValid
    unsigned int threads = 0;            // build threads, 0 = hardware concurrency
    bool verbose = false;                // report invalid geometries and statistics

    // Optional load area in degrees. Only polygons intersecting the area are
    // read, which makes construction much faster for local use, and searches
    // outside the area throw instead of returning a wrong zone. Areas crossing
    // the antimeridian are not supported.
    struct Area
    {
      double west = -180;
      double south = -90;
      double east = 180;
      double north = 90;
    };
    std::optional<Area> area;
  };

  struct Statistics
  {
    std::size_t features = 0;
    std::size_t invalid_features = 0;
    std::size_t zones = 0;
    std::size_t pieces = 0;
    std::size_t full_pieces = 0;    // rectangles completely inside a zone
    std::size_t uniform_cells = 0;  // search grid cells with a single zone
    std::size_t vertices = 0;
    std::size_t bytes = 0;  // approximate memory use
  };

  ~TimeZoneFinder();
  TimeZoneFinder(const TimeZoneFinder& other) = delete;
  TimeZoneFinder& operator=(const TimeZoneFinder& other) = delete;
  TimeZoneFinder(TimeZoneFinder&& other) noexcept;
  TimeZoneFinder& operator=(TimeZoneFinder&& other) noexcept;

  // Read the default_source with default options
  TimeZoneFinder();

  // Read any GDAL/OGR vector data source, by default its first layer
  explicit TimeZoneFinder(const std::string& theSource);
  TimeZoneFinder(const std::string& theSource, const Options& theOptions);
  TimeZoneFinder(const std::string& theSource,
                 const std::string& theLayer,
                 const Options& theOptions);

  // Read an already opened layer, for example a PostGIS table
  TimeZoneFinder(OGRLayer& theLayer, const Options& theOptions);

  // Zone at the coordinate. Points not covered by any polygon get the
  // nautical Etc/GMT zone of the longitude, which never happens with the
  // with-oceans data set. Throws for non-finite coordinates and for
  // coordinates outside the load area.
  const std::string& zoneName(double theLongitude, double theLatitude) const;

  // All zones covering the coordinate, preferred zone first. Normally one,
  // several in overlapping areas, empty if there are no polygons there.
  std::vector<std::string> zoneNames(double theLongitude, double theLatitude) const;

  // True if searches are possible at the coordinate: always without a load area,
  // otherwise if the coordinate is inside it. Throws for non-finite coordinates.
  bool contains(double theLongitude, double theLatitude) const;

  const std::vector<std::string>& zones() const;
  const Statistics& statistics() const;

 private:
  class Impl;
  std::unique_ptr<Impl> itsImpl;
};

}  // namespace Fmi

// ======================================================================
