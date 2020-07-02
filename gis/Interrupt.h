#pragma once

#include "SpatialReference.h"
#include <memory>

class OGRGeometry;
class OGREnvelope;

namespace Fmi
{
// Get WGS84 interrupt geometry for the given spatial reference

struct Interrupt
{
  std::shared_ptr<OGRGeometry> andGeometry;
  std::shared_ptr<OGRGeometry> cutGeometry = nullptr;
};

Interrupt interruptGeometry(const SpatialReference& theSRS);

// Estimated envelope for interrupt generation
OGREnvelope interruptEnvelope(const SpatialReference& theSRS);

}  // namespace Fmi
