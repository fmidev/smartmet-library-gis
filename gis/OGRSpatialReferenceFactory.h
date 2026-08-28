#include <macgyver/Cache.h>
#include <memory>
#include <mutex>
#include <string>

class OGRSpatialReference;

namespace Fmi
{
// The raw-object door of the gis library.
//
// Use this ONLY when a mutable OGRSpatialReference has to be handed to GDAL
// itself - OGRCreateCoordinateTransformation(), OGRGeometry::
// assignSpatialReference(), a GDAL dataset - or when GDAL's own API is being
// called on the object directly.
//
// For everything else construct an Fmi::SpatialReference from the definition
// string. It shares its derived values (WKT, PROJ string, EPSG code, axis
// flags) with every other SpatialReference built from the same string, so
// copying one is free and its accessors never enter GDAL at all. Deriving those
// values from a raw OGRSpatialReference instead costs about 1.7 ms every time,
// against 0.2 us for the cached path, because it re-runs exportToWkt,
// exportToProj, a ProjInfo parse and a GetRoot() walk - so a caller that starts
// from a raw object and wraps it later pays that on every construction.
//
// Both this and Fmi::SpatialReference are served by the same single store
// (CrsRegistry), keyed by definition string; getCacheStats() below and
// SpatialReference::getCacheStats() therefore describe the same thing.
namespace OGRSpatialReferenceFactory
{
// Hand the caller its own spatial reference: a fresh clone that it owns
// outright and may read, mutate, attach to a geometry, or keep for as long as
// it likes, with no effect on any other caller. Nothing is shared and nothing
// is handed back.
std::shared_ptr<OGRSpatialReference> Create(const std::string& theDesc);
std::shared_ptr<OGRSpatialReference> Create(int epsg);
// Resize the master store: one parsed object per definition string, kept only
// so that copies can be made with Clone() instead of re-parsing.
void SetCacheSize(std::size_t newMaxSize);

// How many distinct CRSes each thread keeps a private sample of, so that its
// clones need no lock. Small by design: a thread only touches the CRSes of the
// requests it serves. Exceeding it costs one extra clone-under-lock, never an
// error. Default 64.
void SetSampleStoreSize(std::size_t newMaxSize);
std::size_t getSampleStoreSize();

// Drop this thread's private samples.
//
// Worth calling from a worker thread just before it exits, if the application
// has such a hook. Every thread that touches PROJ acquires a PJ_CONTEXT (and
// with it a PROJ database context, prepared statements and ten LRU caches - see
// ../docs/proj-gdal-thread-safety.md section 3.7). GDAL destroys that context
// through its own thread_local, and if our samples outlive it their destructors
// make GDAL create a replacement that nothing will ever free. Releasing the
// samples while the thread is still alive avoids that entirely.
//
// Purely an optimisation for thread teardown: it is always safe to skip, and
// safe to call more than once. Samples are recreated on demand.
void ReleaseThreadSamples();

// Master store statistics
Cache::CacheStats getCacheStats();

// Global lock serialising operations on the factory's *internal* master
// objects: parsing a definition string (SetFromUserInput, which queries proj.db
// through SQLite and could deadlock worker threads against each other) and
// cloning a master to seed a thread's private sample.
//
// Callers do NOT need this lock to use an object returned by Create(): that
// object is a clone belonging to the caller alone, which is exactly what GDAL
// and PROJ require. Two callers asking for the same CRS get two different
// objects, and either may modify its own freely.
//
// Lock order is strictly one-way: this mutex, then PROJ's internal locks. It is
// a plain non-recursive std::mutex, so nothing holding it may call back into
// Create(). Steady-state Create() never takes it at all.
std::mutex& mutex();

}  // namespace OGRSpatialReferenceFactory
}  // namespace Fmi
