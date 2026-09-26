// ======================================================================
/*!
 * \brief Implementation of class TimeZoneFinder
 */
// ======================================================================

#include "TimeZoneFinder.h"
#include <fmt/format.h>
#include <macgyver/Exception.h>
#include <atomic>
#include <cmath>
#include <geos_c.h>
#include <iostream>
#include <limits>
#include <map>
#include <mutex>
#include <ogrsf_frmts.h>
#include <thread>
#include <utility>

namespace Fmi
{
namespace
{
// Source coordinates are OpenStreetMap coordinates with 7 decimals
const double scale = 1e7;

std::int32_t quantize(double theValue)
{
  return static_cast<std::int32_t>(std::lround(theValue * scale));
}

// Integer coordinate limits. Points are moved inside the half-open data
// extent, see TimeZoneFinder::matches.
const std::int64_t x_offset = 1800000000;
const std::int64_t y_offset = 900000000;
const std::int32_t max_x = 1799999999;
const std::int32_t max_y = 899999999;

// Cell of an integer coordinate and the first integer coordinate of a cell,
// defined so that each integer point belongs to exactly one cell.
int grid_index(std::int32_t theValue, std::int64_t theOffset, int theCount)
{
  auto i = static_cast<int>((theValue + theOffset) * theCount / (2 * theOffset));
  return std::max(0, std::min(theCount - 1, i));
}

std::int64_t grid_start(std::int64_t theIndex, std::int64_t theOffset, std::int64_t theCount)
{
  return (theIndex * 2 * theOffset + theCount - 1) / theCount - theOffset;
}

// ----------------------------------------------------------------------
/*!
 * \brief GEOS context with message handlers, one per thread
 */
// ----------------------------------------------------------------------

void geos_message(const char* /* fmt */, ...) {}

class GeosContext
{
 public:
  GeosContext() : itsHandle(GEOS_init_r())
  {
    GEOSContext_setNoticeHandler_r(itsHandle, geos_message);
    GEOSContext_setErrorHandler_r(itsHandle, geos_message);
  }
  ~GeosContext() { GEOS_finish_r(itsHandle); }
  GeosContext(const GeosContext&) = delete;
  GeosContext& operator=(const GeosContext&) = delete;
  GEOSContextHandle_t get() const { return itsHandle; }

 private:
  GEOSContextHandle_t itsHandle;
};

// Owning GEOS geometry pointer bound to a context
struct GeosDeleter
{
  GEOSContextHandle_t handle;
  void operator()(GEOSGeometry* theGeom) const { GEOSGeom_destroy_r(handle, theGeom); }
};
using GeosPtr = std::unique_ptr<GEOSGeometry, GeosDeleter>;

// A feature read from the data source, the zone name is resolved to an index
struct SourceFeature
{
  std::uint32_t zone;
  std::vector<unsigned char> wkb;
};

// Nautical zones at 15 degree intervals, Etc/GMT sign convention is inverted
std::vector<std::string> nautical_zones()
{
  std::vector<std::string> ret;
  for (int offset = -12; offset <= 12; offset++)
  {
    if (offset == 0)
      ret.emplace_back("Etc/GMT");
    else
      ret.push_back(fmt::format("Etc/GMT{:+d}", -offset));
  }
  return ret;
}

// Piece bounding box, inclusive integer bounds in 1e-7 degrees
struct Box
{
  std::int32_t xmin;
  std::int32_t ymin;
  std::int32_t xmax;
  std::int32_t ymax;
};

using Value = std::pair<Box, std::uint32_t>;  // piece bbox and index

// Search grid: world cells hold the zone index when a single zone covers
// every point of the cell, otherwise mixed_flag plus an index to the list
// of pieces intersecting the cell. All pieces are cut to at most this size.
constexpr int grid_columns = 2048;  // 360/2048 = 0.17578125 degrees
constexpr int grid_rows = 1024;
constexpr std::uint32_t mixed_flag = 0x80000000;

struct Piece
{
  std::uint32_t zone = 0;        // index to the zone names
  std::uint32_t first_ring = 0;  // index to the ring offsets
  std::uint32_t rings = 0;       // 0 for a rectangle completely inside the zone
};

// Subdivision output for a single feature
struct FeaturePieces
{
  std::vector<Piece> pieces;
  std::vector<Box> boxes;
  std::vector<std::int32_t> coords;            // x,y pairs
  std::vector<std::uint32_t> ring_offsets{0};  // ring i is [off[i],off[i+1]) in points
  double area = 0;                             // in square degrees, for ranking zones
  bool invalid = false;
  std::string reason;
};

// ----------------------------------------------------------------------
/*!
 * \brief Recursive cutting of a polygon into pieces with few vertices
 */
// ----------------------------------------------------------------------

class Subdivider
{
 public:
  static FeaturePieces process(GEOSContextHandle_t h,
                               const SourceFeature& theFeature,
                               const TimeZoneFinder::Options& theOptions);

  Subdivider(GEOSContextHandle_t theHandle,
             std::uint32_t theZone,
             std::size_t theMaxVertices,
             std::int64_t theMinColumns,
             std::int64_t theMinRows,
             FeaturePieces& theOutput)
      : h(theHandle),
        itsZone(theZone),
        itsMaxVertices(theMaxVertices),
        itsMinColumns(theMinColumns),
        itsMinRows(theMinRows),
        itsOutput(theOutput)
  {
  }

  // Cells are identified by integer indices at a dyadic level so that the
  // boundaries of a child cell are computed with the same formula as the
  // boundaries of the search grid cells. The children of cell i out of
  // n are cells 2i and 2i+1 out of 2n, and grid_start(2i,2n) equals
  // grid_start(i,n), hence the cells of all levels and all zones nest
  // exactly, and the cells at the grid level are the grid cells.
  struct Cell
  {
    std::int64_t i;   // column index
    std::int64_t nx;  // number of columns at this level
    std::int64_t j;   // row index
    std::int64_t ny;  // number of rows at this level
    int depth;

    std::int64_t x1() const { return grid_start(i, x_offset, nx); }
    std::int64_t x2() const { return grid_start(i + 1, x_offset, nx); }
    std::int64_t y1() const { return grid_start(j, y_offset, ny); }
    std::int64_t y2() const { return grid_start(j + 1, y_offset, ny); }
  };

  void subdivide(const GEOSGeometry* theGeom, const Cell& theCell)
  {
    if (GEOSisEmpty_r(h, theGeom) != 0)
      return;

    if (isFullRectangle(theGeom, theCell))
    {
      emitRectangle(theCell);
      return;
    }

    // Pieces are made at least as small as the search grid cells so
    // that every grid cell inside a zone becomes a full rectangle. Depth 50
    // means cells of about 1e-5 degrees, way past the point where cutting
    // is useful.
    auto n = static_cast<std::size_t>(GEOSGetNumCoordinates_r(h, theGeom));
    bool small = (theCell.nx >= itsMinColumns && theCell.ny >= itsMinRows);
    if ((small && n <= itsMaxVertices) || theCell.depth >= 50)
    {
      emitRings(theGeom, theCell);
      return;
    }

    // Split along the longer side of the cell. The split depends only on
    // the cell, hence all zones are cut on the same world aligned grid.
    if (360 * theCell.ny >= 180 * theCell.nx)
    {
      clipAndRecurse(theGeom,
                     {2 * theCell.i, 2 * theCell.nx, theCell.j, theCell.ny, theCell.depth + 1});
      clipAndRecurse(theGeom,
                     {2 * theCell.i + 1, 2 * theCell.nx, theCell.j, theCell.ny, theCell.depth + 1});
    }
    else
    {
      clipAndRecurse(theGeom,
                     {theCell.i, theCell.nx, 2 * theCell.j, 2 * theCell.ny, theCell.depth + 1});
      clipAndRecurse(theGeom,
                     {theCell.i, theCell.nx, 2 * theCell.j + 1, 2 * theCell.ny, theCell.depth + 1});
    }
  }

 private:
  void clipAndRecurse(const GEOSGeometry* theGeom, const Cell& theCell)
  {
    const double xmin = theCell.x1() / scale;
    const double ymin = theCell.y1() / scale;
    const double xmax = theCell.x2() / scale;
    const double ymax = theCell.y2() / scale;

    // Cutting is not needed when the geometry is completely inside or outside
    // the cell, which is the common case near the root of the recursion.
    double gxmin = 0;
    double gymin = 0;
    double gxmax = 0;
    double gymax = 0;
    GEOSGeom_getXMin_r(h, theGeom, &gxmin);
    GEOSGeom_getYMin_r(h, theGeom, &gymin);
    GEOSGeom_getXMax_r(h, theGeom, &gxmax);
    GEOSGeom_getYMax_r(h, theGeom, &gymax);
    if (gxmin >= xmax || gxmax <= xmin || gymin >= ymax || gymax <= ymin)
      return;
    if (gxmin >= xmin && gxmax <= xmax && gymin >= ymin && gymax <= ymax)
    {
      subdivide(theGeom, theCell);
      return;
    }

    GeosPtr clipped(GEOSClipByRect_r(h, theGeom, xmin, ymin, xmax, ymax), GeosDeleter{h});
    if (!clipped)
      throw Fmi::Exception(BCP, "GEOSClipByRect failed");
    subdivide(clipped.get(), theCell);
  }

  // A hole-free single polygon whose vertices are all on the cell boundary
  // and whose area equals the cell area is the cell itself.
  bool isFullRectangle(const GEOSGeometry* theGeom, const Cell& theCell) const
  {
    const double xmin = theCell.x1() / scale;
    const double ymin = theCell.y1() / scale;
    const double xmax = theCell.x2() / scale;
    const double ymax = theCell.y2() / scale;

    const GEOSGeometry* poly = theGeom;
    if (GEOSGeomTypeId_r(h, poly) == GEOS_MULTIPOLYGON ||
        GEOSGeomTypeId_r(h, poly) == GEOS_GEOMETRYCOLLECTION)
    {
      if (GEOSGetNumGeometries_r(h, poly) != 1)
        return false;
      poly = GEOSGetGeometryN_r(h, poly, 0);
    }
    if (GEOSGeomTypeId_r(h, poly) != GEOS_POLYGON || GEOSGetNumInteriorRings_r(h, poly) != 0)
      return false;

    const GEOSCoordSequence* seq = GEOSGeom_getCoordSeq_r(h, GEOSGetExteriorRing_r(h, poly));
    unsigned int size = 0;
    GEOSCoordSeq_getSize_r(h, seq, &size);
    for (unsigned int i = 0; i < size; i++)
    {
      double x = 0;
      double y = 0;
      GEOSCoordSeq_getXY_r(h, seq, i, &x, &y);
      if (x != xmin && x != xmax && y != ymin && y != ymax)
        return false;
    }

    double area = 0;
    GEOSArea_r(h, poly, &area);
    return area >= (xmax - xmin) * (ymax - ymin) * (1 - 1e-9);
  }

  void emitRectangle(const Cell& theCell)
  {
    Piece piece;
    piece.zone = itsZone;
    piece.first_ring = 0;
    piece.rings = 0;
    itsOutput.pieces.push_back(piece);
    // The crossing number test treats the left and bottom edges as inside
    // and the right and top edges as outside, the rectangles do the same.
    itsOutput.boxes.push_back({static_cast<std::int32_t>(theCell.x1()),
                               static_cast<std::int32_t>(theCell.y1()),
                               static_cast<std::int32_t>(theCell.x2() - 1),
                               static_cast<std::int32_t>(theCell.y2() - 1)});
  }

  void emitRings(const GEOSGeometry* theGeom, const Cell& theCell)
  {
    Piece piece;
    piece.zone = itsZone;
    piece.first_ring = static_cast<std::uint32_t>(itsOutput.ring_offsets.size() - 1);

    itsXmin = std::numeric_limits<std::int32_t>::max();
    itsYmin = std::numeric_limits<std::int32_t>::max();
    itsXmax = std::numeric_limits<std::int32_t>::min();
    itsYmax = std::numeric_limits<std::int32_t>::min();

    collectRings(theGeom, piece.rings);

    if (piece.rings == 0)
      return;

    // No point on the right or top edge of the cell can be inside the piece
    // according to the crossing number test, hence the bounding box can
    // exclude them. Every point then belongs to the pieces of one cell only.
    itsXmax = static_cast<std::int32_t>(std::min<std::int64_t>(itsXmax, theCell.x2() - 1));
    itsYmax = static_cast<std::int32_t>(std::min<std::int64_t>(itsYmax, theCell.y2() - 1));
    itsOutput.pieces.push_back(piece);
    itsOutput.boxes.push_back({itsXmin, itsYmin, itsXmax, itsYmax});
  }

  // Clipping may also produce lines and points on the cell boundary, only
  // polygons are of interest.
  void collectRings(const GEOSGeometry* theGeom, std::uint32_t& theRings)
  {
    int type = GEOSGeomTypeId_r(h, theGeom);
    if (type == GEOS_POLYGON)
    {
      addRing(GEOSGetExteriorRing_r(h, theGeom), theRings);
      int holes = GEOSGetNumInteriorRings_r(h, theGeom);
      for (int i = 0; i < holes; i++)
        addRing(GEOSGetInteriorRingN_r(h, theGeom, i), theRings);
    }
    else if (type == GEOS_MULTIPOLYGON || type == GEOS_GEOMETRYCOLLECTION)
    {
      int n = GEOSGetNumGeometries_r(h, theGeom);
      for (int i = 0; i < n; i++)
        collectRings(GEOSGetGeometryN_r(h, theGeom, i), theRings);
    }
  }

  void addRing(const GEOSGeometry* theRing, std::uint32_t& theRings)
  {
    const GEOSCoordSequence* seq = GEOSGeom_getCoordSeq_r(h, theRing);
    unsigned int size = 0;
    GEOSCoordSeq_getSize_r(h, seq, &size);
    if (size < 4)
      return;

    auto& coords = itsOutput.coords;
    const std::size_t start = coords.size();
    for (unsigned int i = 0; i < size; i++)
    {
      double x = 0;
      double y = 0;
      GEOSCoordSeq_getXY_r(h, seq, i, &x, &y);
      auto qx = quantize(x);
      auto qy = quantize(y);
      // Clipping may create near-duplicates which become exact duplicates on the grid
      if (coords.size() > start && coords[coords.size() - 2] == qx && coords.back() == qy)
        continue;
      coords.push_back(qx);
      coords.push_back(qy);
      itsXmin = std::min(itsXmin, qx);
      itsYmin = std::min(itsYmin, qy);
      itsXmax = std::max(itsXmax, qx);
      itsYmax = std::max(itsYmax, qy);
    }

    // Closed ring with less than 3 distinct points has no interior
    if (coords.size() - start < 8)
    {
      coords.resize(start);
      return;
    }
    itsOutput.ring_offsets.push_back(static_cast<std::uint32_t>(coords.size() / 2));
    ++theRings;
  }

  GEOSContextHandle_t h;
  std::uint32_t itsZone;
  std::size_t itsMaxVertices;
  std::int64_t itsMinColumns;
  std::int64_t itsMinRows;
  FeaturePieces& itsOutput;
  std::int32_t itsXmin = 0;
  std::int32_t itsYmin = 0;
  std::int32_t itsXmax = 0;
  std::int32_t itsYmax = 0;
};

// ----------------------------------------------------------------------
/*!
 * \brief Validate and subdivide a single feature
 */
// ----------------------------------------------------------------------

FeaturePieces Subdivider::process(GEOSContextHandle_t h,
                                  const SourceFeature& theFeature,
                                  const TimeZoneFinder::Options& theOptions)
{
  FeaturePieces out;

  GeosPtr geom(GEOSGeomFromWKB_buf_r(h, theFeature.wkb.data(), theFeature.wkb.size()),
               GeosDeleter{h});
  if (!geom)
    throw Fmi::Exception(BCP, "Failed to convert timezone geometry to GEOS");

  if (GEOSisValid_r(h, geom.get()) != 1)
  {
    out.invalid = true;
    char* reason = GEOSisValidReason_r(h, geom.get());
    if (reason != nullptr)
    {
      out.reason = reason;
      GEOSFree_r(h, reason);
    }
    // Clipping requires valid input. The raw crossing number test would
    // still give even-odd answers for self-intersecting rings, but the cuts
    // made by GEOS on invalid input are not reliable.
    if (theOptions.make_valid)
    {
      GeosPtr valid(GEOSMakeValid_r(h, geom.get()), GeosDeleter{h});
      if (!valid)
        throw Fmi::Exception(BCP, "GEOSMakeValid failed").addParameter("Reason", out.reason);
      geom = std::move(valid);
    }
  }

  GEOSArea_r(h, geom.get(), &out.area);

  if (GEOSisEmpty_r(h, geom.get()) != 0)
    return out;

  // All zones are cut starting from the world rectangle so that the cells
  // of all zones nest, and the cells at the grid level are exactly the
  // search grid cells. Every piece then belongs to a single grid cell, or
  // is a full rectangle covering whole grid cells.
  Subdivider subdivider(h,
                        theFeature.zone,
                        std::max<std::size_t>(theOptions.max_vertices, 16),
                        grid_columns,
                        grid_rows,
                        out);
  subdivider.subdivide(geom.get(), {0, 1, 0, 1, 0});

  return out;
}

// Validate and normalize a coordinate and convert it to the integer grid.
// The half-open crossing rule excludes the top and right edges of the data,
// hence points on the north pole and on the antimeridian are moved by one
// grid unit so that they are found. lon=180 means the eastern hemisphere.
void to_grid(double lon, double lat, std::int32_t& x, std::int32_t& y)
{
  if (!std::isfinite(lon) || !std::isfinite(lat))
    throw Fmi::Exception(BCP, "Invalid coordinate for timezone search")
        .addParameter("lon", fmt::format("{}", lon))
        .addParameter("lat", fmt::format("{}", lat));

  if (lon < -180 || lon > 180)
    lon = std::remainder(lon, 360.0);
  lat = std::max(-90.0, std::min(90.0, lat));

  x = std::min(quantize(lon), max_x);
  y = std::min(quantize(lat), max_y);
}

// Restores the spatial filter of a layer on destruction
class SpatialFilterGuard
{
 public:
  explicit SpatialFilterGuard(OGRLayer& theLayer) : itsLayer(theLayer)
  {
    const OGRGeometry* old = theLayer.GetSpatialFilter();
    if (old != nullptr)
      itsOldFilter.reset(old->clone());
  }
  ~SpatialFilterGuard() { itsLayer.SetSpatialFilter(itsOldFilter.get()); }
  SpatialFilterGuard(const SpatialFilterGuard&) = delete;
  SpatialFilterGuard& operator=(const SpatialFilterGuard&) = delete;

 private:
  OGRLayer& itsLayer;
  std::unique_ptr<OGRGeometry> itsOldFilter;
};

// Open a GDAL/OGR vector source
std::shared_ptr<GDALDataset> open_source(const std::string& theSource)
{
  auto* ds = static_cast<GDALDataset*>(
      GDALOpenEx(theSource.c_str(), GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr));
  if (ds == nullptr)
    throw Fmi::Exception(BCP, "Failed to open timezone data source")
        .addParameter("Source", theSource);
  return {ds, [](GDALDataset* p) { GDALClose(p); }};
}

}  // namespace

// ----------------------------------------------------------------------
/*!
 * \brief Implementation details
 *
 * The polygons are recursively cut with world aligned dyadic rectangles
 * until each piece is at most one search grid cell in size and has at most
 * the given number of vertices. Rectangles completely inside a zone are
 * stored without vertices. The search grid stores either the zone of a
 * cell, or the short list of pieces intersecting the cell.
 *
 * Coordinates are stored as 32-bit integers in units of 1e-7 degrees,
 * which is the native OpenStreetMap resolution of the source data. The
 * conversion is hence lossless, and the point-in-polygon test is done
 * with exact integer arithmetic. Neighbouring zones share identical
 * edges, and the half-open crossing rule assigns a point on the edge to
 * exactly one of them.
 *
 * Some zones overlap on purpose (disputed areas). A point in an overlap is
 * assigned to the first zone in the preferred list, or otherwise to the
 * zone with the smallest total area.
 */
// ----------------------------------------------------------------------

class TimeZoneFinder::Impl
{
 public:
  Impl(OGRLayer& theLayer, const Options& theOptions);

  const std::string& zoneName(double lon, double lat) const;
  std::vector<std::string> zoneNames(double lon, double lat) const;
  bool contains(double lon, double lat) const;

  std::vector<std::string> itsZones;
  Statistics itsStatistics;

 private:
  void buildGrid(const std::vector<Value>& thePieceBoxes);
  bool inside(const Piece& thePiece, std::int32_t x, std::int32_t y) const;
  template <typename Callback>
  void matches(double lon, double lat, Callback&& theCallback) const;

  // Optional load area, inclusive integer bounds
  bool itsHasArea = false;
  Box itsArea{};

  std::vector<std::uint32_t> itsRank;  // lower rank wins in overlapping areas
  std::vector<std::string> itsNauticalZones;

  std::vector<Piece> itsPieces;
  std::vector<std::int32_t> itsCoords;
  std::vector<std::uint32_t> itsRingOffsets;
  std::vector<std::uint32_t> itsGrid;
  std::vector<std::uint32_t> itsListStart;  // list k is [start[k], start[k+1]) in itsListEntries
  std::vector<Value> itsListEntries;
};

// ----------------------------------------------------------------------
/*!
 * \brief Build the search structure from an OGR layer
 */
// ----------------------------------------------------------------------

TimeZoneFinder::Impl::Impl(OGRLayer& theLayer, const Options& theOptions)
    : itsNauticalZones(nautical_zones())
{
  try
  {
    // Read all features first, OGR layers are not thread safe

    std::map<std::string, std::uint32_t> zone_index;
    std::vector<SourceFeature> features;

    auto* defn = theLayer.GetLayerDefn();
    int field = defn->GetFieldIndex(theOptions.field.c_str());
    if (field < 0)
      throw Fmi::Exception(BCP, "Timezone name field not found in the timezone layer")
          .addParameter("Field", theOptions.field)
          .addParameter("Layer", theLayer.GetName());

    // Read only the polygons intersecting the load area. Every polygon
    // covering a point inside the area intersects the area, hence searches
    // inside the area give the same results as with the full data set.

    SpatialFilterGuard filter_guard(theLayer);
    if (theOptions.area)
    {
      const auto& area = *theOptions.area;
      if (!std::isfinite(area.west) || !std::isfinite(area.south) || !std::isfinite(area.east) ||
          !std::isfinite(area.north) || area.west > area.east || area.south > area.north ||
          area.west < -180 || area.east > 180 || area.south < -90 || area.north > 90)
        throw Fmi::Exception(BCP, "Invalid timezone load area")
            .addParameter("Area",
                          fmt::format("{},{},{},{}", area.west, area.south, area.east, area.north));

      itsHasArea = true;
      itsArea = {
          quantize(area.west), quantize(area.south), quantize(area.east), quantize(area.north)};
      theLayer.SetSpatialFilterRect(area.west, area.south, area.east, area.north);
    }

    theLayer.ResetReading();
    for (auto& feature : theLayer)
    {
      const OGRGeometry* geom = feature->GetGeometryRef();
      if (geom == nullptr || geom->IsEmpty())
        continue;

      std::string name = feature->GetFieldAsString(field);
      if (name.empty())
        throw Fmi::Exception(BCP, "Timezone feature has an empty zone name")
            .addParameter("FID", std::to_string(feature->GetFID()));

      auto pos = zone_index.find(name);
      if (pos == zone_index.end())
      {
        pos = zone_index.insert({name, static_cast<std::uint32_t>(itsZones.size())}).first;
        itsZones.push_back(name);
      }

      SourceFeature f;
      f.zone = pos->second;
      f.wkb.resize(geom->WkbSize());
      geom->exportToWkb(wkbNDR, f.wkb.data());
      features.push_back(std::move(f));
    }

    if (features.empty())
      throw Fmi::Exception(BCP,
                           itsHasArea ? "Timezone layer contains no polygons in the load area"
                                      : "Timezone layer contains no polygons")
          .addParameter("Layer", theLayer.GetName());

    // Subdivide in parallel. Features are handed out largest first so that
    // the huge zones (Russia, oceans) do not end up last on a single thread.

    std::vector<std::size_t> order(features.size());
    for (std::size_t i = 0; i < order.size(); i++)
      order[i] = i;
    std::sort(order.begin(),
              order.end(),
              [&](std::size_t a, std::size_t b)
              { return features[a].wkb.size() > features[b].wkb.size(); });

    std::vector<FeaturePieces> results(features.size());
    std::atomic<std::size_t> next{0};
    std::exception_ptr failure;
    std::mutex failure_mutex;

    unsigned int nthreads = theOptions.threads;
    if (nthreads == 0)
      nthreads = std::max(1U, std::thread::hardware_concurrency());
    nthreads = std::min<unsigned int>(nthreads, features.size());

    auto worker = [&]()
    {
      GeosContext context;
      while (true)
      {
        std::size_t i = next++;
        if (i >= order.size())
          break;
        try
        {
          results[order[i]] = Subdivider::process(context.get(), features[order[i]], theOptions);
        }
        catch (...)
        {
          std::lock_guard<std::mutex> lock(failure_mutex);
          if (!failure)
            failure = std::current_exception();
          next = order.size();
        }
      }
    };

    std::vector<std::thread> threads;
    for (unsigned int i = 0; i + 1 < nthreads; i++)
      threads.emplace_back(worker);
    worker();
    for (auto& t : threads)
      t.join();

    if (failure)
      std::rethrow_exception(failure);

    // Merge the results and rank the zones

    std::vector<double> zone_area(itsZones.size(), 0.0);
    std::vector<Value> values;

    itsRingOffsets.push_back(0);
    for (std::size_t i = 0; i < results.size(); i++)
    {
      auto& r = results[i];
      zone_area[features[i].zone] += r.area;
      if (r.invalid)
      {
        ++itsStatistics.invalid_features;
        if (theOptions.verbose)
          std::cerr << fmt::format("Warning: invalid timezone geometry for {}: {}{}\n",
                                   itsZones[features[i].zone],
                                   r.reason,
                                   theOptions.make_valid ? " (repaired)" : "");
      }

      const auto point_offset = static_cast<std::uint32_t>(itsCoords.size() / 2);
      const auto ring_offset = static_cast<std::uint32_t>(itsRingOffsets.size() - 1);

      for (std::size_t j = 0; j < r.pieces.size(); j++)
      {
        Piece piece = r.pieces[j];
        if (piece.rings > 0)
          piece.first_ring += ring_offset;
        values.emplace_back(r.boxes[j], static_cast<std::uint32_t>(itsPieces.size()));
        itsPieces.push_back(piece);
      }
      for (std::size_t j = 1; j < r.ring_offsets.size(); j++)
        itsRingOffsets.push_back(r.ring_offsets[j] + point_offset);
      itsCoords.insert(itsCoords.end(), r.coords.begin(), r.coords.end());

      r = FeaturePieces();  // release memory early
    }

    buildGrid(values);

    itsCoords.shrink_to_fit();
    itsRingOffsets.shrink_to_fit();
    itsPieces.shrink_to_fit();

    // Preferred zones first in the given order, then the rest by increasing area
    std::vector<std::uint32_t> order_by_rank(itsZones.size());
    for (std::uint32_t i = 0; i < order_by_rank.size(); i++)
      order_by_rank[i] = i;
    auto preference = [&](std::uint32_t zone) -> std::size_t
    {
      const auto& pref = theOptions.preferred;
      auto pos = std::find(pref.begin(), pref.end(), itsZones[zone]);
      return static_cast<std::size_t>(pos - pref.begin());
    };
    std::stable_sort(order_by_rank.begin(),
                     order_by_rank.end(),
                     [&](std::uint32_t a, std::uint32_t b)
                     {
                       auto pa = preference(a);
                       auto pb = preference(b);
                       if (pa != pb)
                         return pa < pb;
                       return zone_area[a] < zone_area[b];
                     });
    itsRank.resize(itsZones.size());
    for (std::uint32_t i = 0; i < order_by_rank.size(); i++)
      itsRank[order_by_rank[i]] = i;

    itsStatistics.features = features.size();
    itsStatistics.zones = itsZones.size();
    itsStatistics.pieces = itsPieces.size();
    itsStatistics.full_pieces = static_cast<std::size_t>(std::count_if(
        itsPieces.begin(), itsPieces.end(), [](const Piece& p) { return p.rings == 0; }));
    itsStatistics.vertices = itsCoords.size() / 2;
    itsStatistics.uniform_cells = static_cast<std::size_t>(
        std::count_if(itsGrid.begin(), itsGrid.end(), [](auto c) { return c < mixed_flag; }));
    itsStatistics.bytes =
        itsCoords.capacity() * sizeof(std::int32_t) +
        itsRingOffsets.capacity() * sizeof(std::uint32_t) + itsPieces.capacity() * sizeof(Piece) +
        itsGrid.capacity() * sizeof(std::uint32_t) +
        itsListStart.capacity() * sizeof(std::uint32_t) + itsListEntries.capacity() * sizeof(Value);

    if (theOptions.verbose)
      std::cout << fmt::format(
          "Timezones: {} features, {} zones, {} invalid, {} pieces ({} full cells), {} vertices, "
          "{:.1f}% uniform grid cells, {:.1f} MB\n",
          itsStatistics.features,
          itsStatistics.zones,
          itsStatistics.invalid_features,
          itsStatistics.pieces,
          itsStatistics.full_pieces,
          itsStatistics.vertices,
          100.0 * itsStatistics.uniform_cells / itsGrid.size(),
          itsStatistics.bytes / 1048576.0);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Failed to build timezone search structure");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Build the search grid
 *
 * A cell gets a zone only if a full rectangle of that zone contains every
 * integer point of the cell, and no other piece contains any of them. The
 * list search would then return exactly that zone for every point in the
 * cell. Other cells get the list of all pieces whose bounding box
 * intersects the cell.
 */
// ----------------------------------------------------------------------

void TimeZoneFinder::Impl::buildGrid(const std::vector<Value>& thePieceBoxes)
{
  if (itsZones.size() >= mixed_flag)
    throw Fmi::Exception(BCP, "Too many timezones")
        .addParameter("Zones", std::to_string(itsZones.size()));

  const std::uint32_t unset = mixed_flag - 1;  // not a valid zone index, checked above
  const std::uint32_t mixed = mixed_flag;
  itsGrid.assign(static_cast<std::size_t>(grid_columns) * grid_rows, unset);

  // Call f(cell index, covers) for each cell intersecting the piece box
  auto for_each_cell = [](const Box& box, auto&& f)
  {
    const std::int32_t x1 = box.xmin;
    const std::int32_t y1 = box.ymin;
    const std::int32_t x2 = box.xmax;
    const std::int32_t y2 = box.ymax;

    const int i1 = grid_index(x1, x_offset, grid_columns);
    const int i2 = grid_index(x2, x_offset, grid_columns);
    const int j1 = grid_index(y1, y_offset, grid_rows);
    const int j2 = grid_index(y2, y_offset, grid_rows);

    for (int j = j1; j <= j2; j++)
    {
      const auto cy1 = grid_start(j, y_offset, grid_rows);
      const auto cy2 = std::min<std::int64_t>(grid_start(j + 1, y_offset, grid_rows) - 1, max_y);
      for (int i = i1; i <= i2; i++)
      {
        const auto cx1 = grid_start(i, x_offset, grid_columns);
        const auto cx2 =
            std::min<std::int64_t>(grid_start(i + 1, x_offset, grid_columns) - 1, max_x);
        const bool covers = (x1 <= cx1 && cx2 <= x2 && y1 <= cy1 && cy2 <= y2);
        f(static_cast<std::size_t>(j) * grid_columns + i, covers);
      }
    }
  };

  // Pass 1: find the uniform cells

  for (const auto& value : thePieceBoxes)
  {
    const Piece& piece = itsPieces[value.second];
    for_each_cell(value.first,
                  [&](std::size_t theCell, bool theCovers)
                  {
                    auto& cell = itsGrid[theCell];
                    if (piece.rings > 0 || !theCovers)
                      cell = mixed;
                    else if (cell == unset)
                      cell = piece.zone;
                    else if (cell != piece.zone)
                      cell = mixed;
                  });
  }

  // Cells not touched by any piece are outside the data and get an empty list
  std::replace(itsGrid.begin(), itsGrid.end(), unset, mixed);

  // Pass 2: count the list sizes of the mixed cells

  std::vector<std::uint32_t> counts(itsGrid.size(), 0);
  for (const auto& value : thePieceBoxes)
    for_each_cell(value.first,
                  [&](std::size_t theCell, bool /* theCovers */)
                  {
                    if (itsGrid[theCell] == mixed)
                      ++counts[theCell];
                  });

  itsListStart.clear();
  itsListStart.push_back(0);
  for (std::size_t i = 0; i < itsGrid.size(); i++)
  {
    if (itsGrid[i] != mixed)
      continue;
    itsGrid[i] = mixed_flag | static_cast<std::uint32_t>(itsListStart.size() - 1);
    itsListStart.push_back(itsListStart.back() + counts[i]);
  }

  // Pass 3: fill the lists

  itsListEntries.resize(itsListStart.back());
  std::vector<std::uint32_t> fill(itsListStart.begin(), itsListStart.end() - 1);
  for (const auto& value : thePieceBoxes)
    for_each_cell(value.first,
                  [&](std::size_t theCell, bool /* theCovers */)
                  {
                    if (itsGrid[theCell] >= mixed_flag)
                      itsListEntries[fill[itsGrid[theCell] & ~mixed_flag]++] = value;
                  });
}

// ----------------------------------------------------------------------
/*!
 * \brief Exact crossing number test
 *
 * The half-open rule (y1 > y) != (y2 > y) together with the strict
 * comparison assigns points on an edge shared by two pieces to exactly
 * one of them. Differences fit in 33 bits, but their products need more
 * than 64 bits.
 */
// ----------------------------------------------------------------------

bool TimeZoneFinder::Impl::inside(const Piece& thePiece, std::int32_t x, std::int32_t y) const
{
  if (thePiece.rings == 0)
    return true;

  bool result = false;
  const std::int32_t* coords = itsCoords.data();

  for (std::uint32_t r = thePiece.first_ring; r < thePiece.first_ring + thePiece.rings; r++)
  {
    const std::int32_t* p = coords + 2 * static_cast<std::size_t>(itsRingOffsets[r]);
    const std::int32_t* end = coords + 2 * static_cast<std::size_t>(itsRingOffsets[r + 1]) - 2;

    for (; p < end; p += 2)
    {
      const std::int64_t x1 = p[0];
      const std::int64_t y1 = p[1];
      const std::int64_t x2 = p[2];
      const std::int64_t y2 = p[3];
      if ((y1 > y) != (y2 > y))
      {
        // Is the edge crossing strictly to the right of the point?
        const __int128 lhs = static_cast<__int128>(x - x1) * (y2 - y1);
        const __int128 rhs = static_cast<__int128>(y - y1) * (x2 - x1);
        if (y2 > y1 ? lhs < rhs : lhs > rhs)
          result = !result;
      }
    }
  }
  return result;
}

// ----------------------------------------------------------------------
/*!
 * \brief Call the callback with the zone index of every piece containing the point
 */
// ----------------------------------------------------------------------

template <typename Callback>
void TimeZoneFinder::Impl::matches(double lon, double lat, Callback&& theCallback) const
{
  std::int32_t x = 0;
  std::int32_t y = 0;
  to_grid(lon, lat, x, y);

  if (itsHasArea && (x < itsArea.xmin || x > itsArea.xmax || y < itsArea.ymin || y > itsArea.ymax))
    throw Fmi::Exception(BCP, "Coordinate is outside the loaded timezone area")
        .addParameter("lon", fmt::format("{}", lon))
        .addParameter("lat", fmt::format("{}", lat));

  const auto cell =
      itsGrid[static_cast<std::size_t>(grid_index(y, y_offset, grid_rows)) * grid_columns +
              grid_index(x, x_offset, grid_columns)];
  if (cell < mixed_flag)
  {
    theCallback(cell);
    return;
  }

  const auto list = cell & ~mixed_flag;
  const auto* entry = itsListEntries.data() + itsListStart[list];
  const auto* end = itsListEntries.data() + itsListStart[list + 1];
  for (; entry < end; ++entry)
  {
    const Box& box = entry->first;
    if (x < box.xmin || x > box.xmax || y < box.ymin || y > box.ymax)
      continue;
    const Piece& piece = itsPieces[entry->second];
    if (inside(piece, x, y))
      theCallback(piece.zone);
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Test whether searches are possible at the coordinate
 */
// ----------------------------------------------------------------------

bool TimeZoneFinder::Impl::contains(double lon, double lat) const
{
  std::int32_t x = 0;
  std::int32_t y = 0;
  to_grid(lon, lat, x, y);
  return (!itsHasArea ||
          (x >= itsArea.xmin && x <= itsArea.xmax && y >= itsArea.ymin && y <= itsArea.ymax));
}

// ----------------------------------------------------------------------
/*!
 * \brief Find the zone of a coordinate
 */
// ----------------------------------------------------------------------

const std::string& TimeZoneFinder::Impl::zoneName(double lon, double lat) const
{
  try
  {
    constexpr auto none = std::numeric_limits<std::uint32_t>::max();
    std::uint32_t best = none;

    matches(lon,
            lat,
            [&](std::uint32_t zone)
            {
              if (best == none || itsRank[zone] < itsRank[best])
                best = zone;
            });

    if (best != none)
      return itsZones[best];

    // Only possible if the data does not cover the globe
    if (lon < -180 || lon > 180)
      lon = std::remainder(lon, 360.0);
    auto offset = static_cast<int>(std::lround(lon / 15.0));
    return itsNauticalZones[offset + 12];
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Find all zones covering a coordinate, best zone first
 */
// ----------------------------------------------------------------------

std::vector<std::string> TimeZoneFinder::Impl::zoneNames(double lon, double lat) const
{
  try
  {
    std::vector<std::uint32_t> found;
    matches(lon,
            lat,
            [&](std::uint32_t zone)
            {
              if (std::find(found.begin(), found.end(), zone) == found.end())
                found.push_back(zone);
            });

    std::sort(found.begin(),
              found.end(),
              [this](std::uint32_t a, std::uint32_t b) { return itsRank[a] < itsRank[b]; });

    std::vector<std::string> ret;
    ret.reserve(found.size());
    for (auto zone : found)
      ret.push_back(itsZones[zone]);
    return ret;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Public interface
 */
// ----------------------------------------------------------------------

const char* const TimeZoneFinder::default_source =
    "/usr/share/smartmet/timezones/timezones-with-oceans.shp";

TimeZoneFinder::~TimeZoneFinder() = default;
TimeZoneFinder::TimeZoneFinder(TimeZoneFinder&& other) noexcept = default;
TimeZoneFinder& TimeZoneFinder::operator=(TimeZoneFinder&& other) noexcept = default;

TimeZoneFinder::TimeZoneFinder() : TimeZoneFinder(default_source, "", Options()) {}

TimeZoneFinder::TimeZoneFinder(const std::string& theSource)
    : TimeZoneFinder(theSource, "", Options())
{
}

TimeZoneFinder::TimeZoneFinder(const std::string& theSource, const Options& theOptions)
    : TimeZoneFinder(theSource, "", theOptions)
{
}

TimeZoneFinder::TimeZoneFinder(const std::string& theSource,
                               const std::string& theLayer,
                               const Options& theOptions)
{
  try
  {
    GDALAllRegister();  // cheap after the first call
    auto ds = open_source(theSource);
    OGRLayer* layer = (theLayer.empty() ? ds->GetLayer(0) : ds->GetLayerByName(theLayer.c_str()));
    if (layer == nullptr)
      throw Fmi::Exception(BCP, "Timezone layer not found")
          .addParameter("Source", theSource)
          .addParameter("Layer", theLayer);
    itsImpl = std::make_unique<Impl>(*layer, theOptions);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Failed to read timezones").addParameter("Source", theSource);
  }
}

TimeZoneFinder::TimeZoneFinder(OGRLayer& theLayer, const Options& theOptions)
    : itsImpl(std::make_unique<Impl>(theLayer, theOptions))
{
}

const std::string& TimeZoneFinder::zoneName(double theLongitude, double theLatitude) const
{
  return itsImpl->zoneName(theLongitude, theLatitude);
}

std::vector<std::string> TimeZoneFinder::zoneNames(double theLongitude, double theLatitude) const
{
  return itsImpl->zoneNames(theLongitude, theLatitude);
}

bool TimeZoneFinder::contains(double theLongitude, double theLatitude) const
{
  return itsImpl->contains(theLongitude, theLatitude);
}

const std::vector<std::string>& TimeZoneFinder::zones() const
{
  return itsImpl->itsZones;
}

const TimeZoneFinder::Statistics& TimeZoneFinder::statistics() const
{
  return itsImpl->itsStatistics;
}

}  // namespace Fmi

// ======================================================================
