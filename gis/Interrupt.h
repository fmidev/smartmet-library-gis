#pragma once

#include "SpatialReference.h"

class OGRMultiPolygon;

namespace Fmi
{
// Get WGS84 interrupt geometry for the given spatial reference

struct Interrupt
{
  OGRMultiPolygon* andGeometry = nullptr;
  OGRMultiPolygon* cutGeometry = nullptr;

  ~Interrupt();
};

Interrupt interruptGeometry(const SpatialReference& theSRS);

}  // namespace Fmi
