#pragma once
#include <list>
#include <memory>
#include <string>

class OGRCoordinateTransformation;

namespace Fmi
{
namespace OGRCoordinateTransformationFactory
{
// The object is stored along with its hash value
using CacheElement = std::pair<std::size_t, std::unique_ptr<OGRCoordinateTransformation>>;
using OGRCoordinateTransformationPool = std::list<CacheElement>;

// Deleter stores the hash and a reference to cache to which the object is to be returned
class Deleter
{
 public:
  explicit Deleter(std::size_t theHash, OGRCoordinateTransformationPool *thePool);
  void operator()(OGRCoordinateTransformation *ptr);

 private:
  std::size_t itsHash;
  OGRCoordinateTransformationPool *itsPool;
};

using Ptr = std::unique_ptr<OGRCoordinateTransformation, Deleter>;

// Get transformation object, creating it if necessary using the engine spatial for creating
// the needed spatial references
Ptr Create(const std::string &theSource, const std::string &theTarget);

// Set maximum cache size
void SetMaxSize(std::size_t theMaxSize);

// Deleter returns object to the cache using this one
void Delete(std::size_t theHash, std::unique_ptr<OGRCoordinateTransformation> theTransformation);

}  // namespace OGRCoordinateTransformationFactory
}  // namespace Fmi
