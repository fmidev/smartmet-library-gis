#pragma once

#include "SpatialReference.h"

class OGRGeometry;

namespace Fmi
{
// Get WGS84 interrupt geometry for the given spatial reference

struct Interrupt
{
  OGRGeometry* andGeometry = nullptr;
  OGRGeometry* cutGeometry = nullptr;

  ~Interrupt();
};

Interrupt interruptGeometry(const SpatialReference& theSRS);

}  // namespace Fmi
