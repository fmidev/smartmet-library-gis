/*
 * \brief Utility container for the partial basic elements formed by clipping
 */

#pragma once

#include "Box.h"

#include <list>
#include <ogr_geometry.h>

namespace Fmi
{
class Box;
class GeometryBuilder;

class RectClipper
{
 public:
  RectClipper() = delete;
  RectClipper(const Box &theBox, bool keep_inside)
      : itsBox(theBox), itsKeepInsideFlag(keep_inside){};

  ~RectClipper();

  void addBox();  // add box as exterior or interior depending on KeepInsideFlag

  void addExterior(OGRLinearRing *theRing);
  void addExterior(OGRLineString *theLine);

  void addInterior(OGRLinearRing *theRing);
  void addInterior(OGRLineString *theLine);

  void reconnectWithBox(double theMaximumSegmentLength);
  void reconnectWithoutBox();
  void release(GeometryBuilder &theBuilder);
  void clear();

  bool empty() const;
  void reconnect();

 private:
  Box itsBox;
  bool itsKeepInsideFlag = false;
  bool itsAddBoxFlag = false;

  std::list<OGRLinearRing *> itsExteriorRings;
  std::list<OGRLineString *> itsExteriorLines;

  std::list<OGRLinearRing *> itsInteriorRings;
  std::list<OGRLineString *> itsInteriorLines;

  // Result from build()
  std::list<OGRPolygon *> itsPolygons;
};

}  // namespace Fmi
