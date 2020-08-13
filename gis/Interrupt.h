#pragma once

#include "Box.h"
#include "SpatialReference.h"
#include <list>
#include <memory>

class OGRGeometry;
class OGREnvelope;

namespace Fmi
{
// Geographic interrupt geometry for the given spatial reference

struct Interrupt
{
  std::list<Box> cuts;
  std::shared_ptr<OGRGeometry> andGeometry;
  std::shared_ptr<OGRGeometry> cutGeometry;
};

Interrupt interruptGeometry(const SpatialReference& theSRS);

// Estimated envelope for interrupt generation
OGREnvelope interruptEnvelope(const SpatialReference& theSRS);

}  // namespace Fmi
