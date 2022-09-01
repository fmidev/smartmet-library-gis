#pragma once
#include <memory>
#include <string>

class OGRCoordinateTransformation;

namespace Fmi
{
namespace OGRCoordinateTransformationFactory
{
// Deleter stores the hash and a reference to cache to which the object is to be returned
class Deleter
{
 public:
  explicit Deleter(std::size_t hash);
  void operator()(OGRCoordinateTransformation *ptr) const;

 private:
  std::size_t m_hash;
};

using Ptr = std::unique_ptr<OGRCoordinateTransformation, Deleter>;

// Get transformation object, creating it if necessary using the engine spatial for creating
// the needed spatial references
Ptr Create(const std::string &theSource, const std::string &theTarget);
Ptr Create(int theSource, int theTarget);
Ptr Create(int theSource, const std::string &theTarget);
Ptr Create(const std::string &theSource, int theTarget);

// Set maximum cache size
void SetMaxSize(std::size_t theMaxSize);

// Deleter returns object to the cache using this one
void Delete(std::size_t theHash, std::unique_ptr<OGRCoordinateTransformation> theTransformation);

}  // namespace OGRCoordinateTransformationFactory
}  // namespace Fmi
