#include "OGRCoordinateTransformationFactory.h"
#include "OGRSpatialReferenceFactory.h"
#include <boost/thread.hpp>
#include <fmt/format.h>
#include <macgyver/Exception.h>
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

// Deleter stores the hash
Deleter::Deleter(std::size_t theHash) : itsHash(theHash) {}

void Deleter::operator()(OGRCoordinateTransformation *ptr)
{
  try
  {
    Delete(itsHash, std::unique_ptr<OGRCoordinateTransformation>{ptr});
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

void SetMaxSize(std::size_t theMaxSize)
{
  try
  {
    WriteLock lock(gMutex);
    gMaxSize = theMaxSize;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

void Delete(std::size_t theHash, std::unique_ptr<OGRCoordinateTransformation> theTransformation)
{
  try
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
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

Ptr Create(const std::string &theSource, const std::string &theTarget)
{
  try
  {
    auto hash = Fmi::hash_value(theSource);
    Fmi::hash_combine(hash, Fmi::hash_value(theTarget));

    // Return transformation object from the pool if there is one
    {
      WriteLock lock(gMutex);

      auto pos =
          std::find_if(gPool.begin(),
                       gPool.end(),
                       [hash](const CacheElement &element) { return hash == element.first; });

      if (pos != gPool.end())
      {
        // Take ownership from the CacheElement and return it to the user
        Ptr ret(pos->second, Deleter(hash));
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
      return Ptr(ptr, Deleter(hash));

    throw Fmi::Exception::Trace(BCP,
                                "Failed to create coordinate transformation from '" + theSource +
                                    "' to '" + theTarget + "'");
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

Ptr Create(int theSource, const std::string &theTarget)
{
  try
  {
    return Create(fmt::format("EPSG:{}", theSource), theTarget);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

Ptr Create(const std::string &theSource, int theTarget)
{
  try
  {
    return Create(theSource, fmt::format("EPSG:{}", theTarget));
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

Ptr Create(int theSource, int theTarget)
{
  try
  {
    return Create(fmt::format("EPSG:{}", theSource), fmt::format("EPSG:{}", theTarget));
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

}  // namespace OGRCoordinateTransformationFactory
}  // namespace Fmi
