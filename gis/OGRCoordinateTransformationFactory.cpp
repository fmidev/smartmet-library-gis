#include "OGRCoordinateTransformationFactory.h"
#include "OGRSpatialReferenceFactory.h"
#include <boost/thread.hpp>
#include <fmt/format.h>
#include <macgyver/Hash.h>
#include <ogr_spatialref.h>

namespace Fmi
{
namespace OGRCoordinateTransformationFactory
{
namespace
{
using MutexType = boost::shared_mutex;
using ReadLock = boost::shared_lock<MutexType>;
using WriteLock = boost::unique_lock<MutexType>;

// Object pool components
MutexType gMutex;
OGRCoordinateTransformationPool gPool;
std::size_t gMaxSize = 40 * 40;

}  // namespace

// Deleter stores the hash and a reference to cache to which the object is to be returned
Deleter::Deleter(std::size_t theHash, OGRCoordinateTransformationPool *thePool)
    : itsHash(theHash), itsPool(thePool)
{
}

void Deleter::operator()(OGRCoordinateTransformation *ptr)
{
  Delete(itsHash, std::unique_ptr<OGRCoordinateTransformation>{ptr});
}

void SetMaxSize(std::size_t theMaxSize)
{
  WriteLock lock(gMutex);
  gMaxSize = theMaxSize;
}

void Delete(std::size_t theHash, std::unique_ptr<OGRCoordinateTransformation> theTransformation)
{
  WriteLock lock(gMutex);

  // Add as the most recently used to the start
  gPool.push_front(std::make_pair(theHash, theTransformation.release()));

  // Pop the least recently used if the cache is too big
  if (gPool.size() > gMaxSize)
  {
    delete gPool.back().second;
    gPool.pop_back();
  }
}

Ptr Create(const std::string &theSource, const std::string &theTarget)
{
  auto hash = Fmi::hash_value(theSource);
  Fmi::hash_combine(hash, Fmi::hash_value(theTarget));

  // Return transformation object from the pool if there is one
  {
    WriteLock lock(gMutex);

    auto pos = std::find_if(gPool.begin(),
                            gPool.end(),
                            [hash](const CacheElement &element) { return hash == element.first; });

    if (pos != gPool.end())
    {
      // Take ownership from the CacheElement and return it to the user
      Ptr ret(pos->second, Deleter(hash, &gPool));
      gPool.erase(pos);
      return ret;
    }
  }

  // Lock no longer needed since we create a new coordinate transformation object and return it
  // to the user directly. The deleter will return the object back to the pool..

  auto src = OGRSpatialReferenceFactory::Create(theSource);
  auto tgt = OGRSpatialReferenceFactory::Create(theTarget);

  auto *ptr = OGRCreateCoordinateTransformation(src.get(), tgt.get());

  if (ptr != nullptr)
    return Ptr(ptr, Deleter(hash, &gPool));

  throw std::runtime_error("Failed to create coordinate transformation from '" + theSource +
                           "' to '" + theTarget + "'");
}

Ptr Create(int theSource, const std::string &theTarget)
{
  return Create(fmt::format("EPSG:{}", theSource), theTarget);
}

Ptr Create(const std::string &theSource, int theTarget)
{
  return Create(theSource, fmt::format("EPSG:{}", theTarget));
}

Ptr Create(int theSource, int theTarget)
{
  return Create(fmt::format("EPSG:{}", theSource), fmt::format("EPSG:{}", theTarget));
}

}  // namespace OGRCoordinateTransformationFactory
}  // namespace Fmi
