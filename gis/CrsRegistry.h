// ======================================================================
/*!
 * \brief Internal to the gis library: the single store of everything known
 *        about a CRS definition string.
 *
 * There used to be two caches keyed by the same definition string - one in
 * OGRSpatialReferenceFactory holding a parsed OGRSpatialReference, one in
 * SpatialReference holding the values derived from it - with different size
 * limits, independent eviction and two separate statistics lines. One could
 * evict while the other retained, so a SpatialReference that still had its
 * derived values could be forced to re-parse the CRS to produce an object.
 *
 * They are now one entry per definition string, holding both, with the derived
 * values computed lazily: a caller that only wants an object (the download
 * plugin, OGRCreateCoordinateTransformation) never pays for the derivation,
 * which is by far the expensive half.
 *
 * Not part of the public API. Declared in a header only because
 * SpatialReference and the factory are separate translation units.
 */
// ======================================================================

#pragma once

#include "ProjInfo.h"

#include <macgyver/Cache.h>

#include <memory>
#include <optional>
#include <string>

class OGRSpatialReference;

namespace Fmi
{
namespace CrsRegistry
{
// Everything derived from a CRS, all of it immutable once computed. Shared by
// every SpatialReference built from the same definition string, which is what
// makes copying one free and its accessors instant.
struct Derived
{
  std::size_t hashvalue = 0;
  bool is_geographic = false;
  bool is_axis_swapped = false;
  bool epsg_treats_as_lat_long = false;
  std::optional<int> epsg;
  std::string wkt;
  ProjInfo projinfo;
};

// The derived values for a definition string, computed once on first request.
//
// The computation runs on a private clone, so it needs no lock: several of the
// GDAL calls involved are not the pure reads their const signatures suggest
// (GetRoot() builds the node tree on demand; EPSGTreatsAsLatLong() demotes and
// undemotes a BoundCRS, swapping the underlying PJ* out), which is exactly why
// they must not run on an object anybody else can see.
std::shared_ptr<const Derived> derived(const std::string& desc);

// The derived values for a caller-supplied object, which has no definition
// string to cache under and so cannot be shared. This is the expensive path:
// it re-runs exportToWkt, exportToProj, a ProjInfo parse and the GetRoot walk.
// Prefer building from the definition string wherever one exists.
std::shared_ptr<const Derived> derive(const OGRSpatialReference& crs);

// Statistics and sizing for the single store. Measured in definition strings.
Cache::CacheStats getCacheStats();
void SetCacheSize(std::size_t newMaxSize);

}  // namespace CrsRegistry
}  // namespace Fmi
