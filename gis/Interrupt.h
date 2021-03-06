#pragma once

#include "Box.h"
#include "Shape.h"
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
  std::list<Shape_sptr> shapeClips;
  std::list<Shape_sptr> shapeCuts;
};

Interrupt interruptGeometry(const SpatialReference& theSRS);

// Estimated envelope for interrupt generation
OGREnvelope interruptEnvelope(const SpatialReference& theSRS);

}  // namespace Fmi
