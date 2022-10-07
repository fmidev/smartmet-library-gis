#include "OGRCoordinateTransformationFactory.h"
#include "OGRSpatialReferenceFactory.h"
#include <boost/thread.hpp>
#include <fmt/format.h>
#include <macgyver/Exception.h>
#include <macgyver/Hash.h>
#include <list>
#include <ogr_spatialref.h>

namespace Fmi
{
namespace OGRCoordinateTransformationFactory
{
namespace
{
class OGRCoordinateTransformationPool
{
 public:
  // The object is stored along with its hash value
  using CacheElement = std::pair<std::size_t, OGRCoordinateTransformation *>;

  ~OGRCoordinateTransformationPool()
  {
    // This leads to segfault in PROJ.7. Proj 9.1 seems to have fixed this, possibly even earlier
    // versions for (auto &elem : m_cache) delete elem.second;
  }

  void SetMaxSize(std::size_t theMaxSize)
  {
    WriteLock lock(m_mutex);
    m_maxsize = theMaxSize;
  }

  void Add(std::size_t theHash, std::unique_ptr<OGRCoordinateTransformation> &theTransformation)
  {
    WriteLock lock(m_mutex);

    // Add as the most recently used to the start
    m_cache.push_front(std::make_pair(theHash, theTransformation.release()));

    // Pop the least recently used if the cache is too big
    if (m_cache.size() > m_maxsize)
    {
      delete m_cache.back().second;
      m_cache.pop_back();
    }
  }

  Ptr Find(std::size_t hash)
  {
    WriteLock lock(m_mutex);
    auto pos = std::find_if(m_cache.begin(),
                            m_cache.end(),
                            [hash](const CacheElement &element) { return hash == element.first; });

    if (pos == m_cache.end())
      return Ptr(nullptr, Deleter(0));

    // Take ownership from the CacheElement and return it to the user
    Ptr ret(pos->second, Deleter(hash));
    m_cache.erase(pos);
    return ret;
  }

 private:
  using MutexType = boost::shared_mutex;
  using ReadLock = boost::shared_lock<MutexType>;
  using WriteLock = boost::unique_lock<MutexType>;

  MutexType m_mutex;
  std::list<CacheElement> m_cache;
  std::size_t m_maxsize = 40 * 40;
};

// Actual objet pool
OGRCoordinateTransformationPool gPool;

}  // namespace

// Deleter stores the hash
Deleter::Deleter(std::size_t hash) : m_hash(hash) {}

void Deleter::operator()(OGRCoordinateTransformation *ptr) const
{
  Delete(m_hash, std::unique_ptr<OGRCoordinateTransformation>{ptr});
}

void SetMaxSize(std::size_t theMaxSize)
{
  gPool.SetMaxSize(theMaxSize);
}

void Delete(std::size_t theHash, std::unique_ptr<OGRCoordinateTransformation> theTransformation)
{
  gPool.Add(theHash, theTransformation);
}

Ptr Create(const std::string &theSource, const std::string &theTarget)
{
  try
  {
    auto hash = Fmi::hash_value(theSource);
    Fmi::hash_combine(hash, Fmi::hash_value(theTarget));

    auto trans = gPool.Find(hash);
    if (trans)
      return trans;

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
