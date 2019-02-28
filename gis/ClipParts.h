/*
 * \brief Utility container for the partial basic elements formed by clipping
 */

#pragma once

#include <gdal/ogr_geometry.h>
#include <list>

namespace Fmi
{
class Box;

class ClipParts
{
 public:
  ~ClipParts();

  OGRGeometry *build();

  void reconnectPolygons(const Box &theBox, bool add_exterior);

  void reverseLines();
  void reconnect();
  void release(ClipParts &theParts);

  void add(OGRPolygon *thePolygon);
  void add(OGRLineString *theLine);
  void add(OGRPoint *thePoint);

  bool empty() const;

 private:
  void clear();
  OGRGeometry *internalBuild() const;

  std::list<OGRPolygon *> itsPolygons;
  std::list<OGRLineString *> itsLines;
  std::list<OGRPoint *> itsPoints;
};

}  // namespace Fmi
