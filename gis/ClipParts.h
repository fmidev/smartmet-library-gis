/*
 * \brief Utility container for the partial basic elements formed by clipping
 */

#pragma once

#include <list>
#include <ogr_geometry.h>

namespace Fmi
{
class Box;

class ClipParts
{
 public:
  ClipParts() = delete;
  ClipParts(bool keep_inside) : itsKeepInsideFlag(keep_inside){};

  ~ClipParts();

  OGRGeometry *build();

  void reconnectPolygons(const Box &theBox);

  void reverseLines();
  void reconnect();
  void release(ClipParts &theParts);

  void addBox();  // add box as exterior or interior

  void addExterior(OGRPolygon *thePolygon);
  void addExterior(OGRLineString *theLine);

  void addInterior(OGRPolygon *thePolygon);
  void addInterior(OGRLineString *theLine);

  void add(OGRPoint *thePoint);

  bool empty() const;

  void dump() const;

 private:
  void clear();
  OGRGeometry *internalBuild() const;

  bool itsKeepInsideFlag;

  bool itsAddBoxFlag;

  std::list<OGRPolygon *> itsExteriorPolygons;
  std::list<OGRLineString *> itsExteriorLines;

  std::list<OGRPolygon *> itsInteriorPolygons;
  std::list<OGRLineString *> itsInteriorLines;

  std::list<OGRPoint *> itsPoints;
};

}  // namespace Fmi
