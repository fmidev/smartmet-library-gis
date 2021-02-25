#pragma once

#include <list>
#include <ogr_geometry.h>

/*
 * Collect polygons, linestrings and points, and build a minimal geometry from them.
 *  - multipolygon or polygon if no linestrings or points were added
 *  - multilinestring or linestring if no polygons or points were added
 *  - multipoint or point if no polygons or linestrings were added
 *  - geometrycollection otherwise
 *
 * Ownership of all features is passed on to the output geometry.
 */

namespace Fmi
{
class GeometryBuilder
{
 public:
  ~GeometryBuilder();
  GeometryBuilder() = default;
  GeometryBuilder(const GeometryBuilder &other) = delete;
  GeometryBuilder &operator=(const GeometryBuilder &other) = delete;

  void add(OGRPolygon *theGeom);
  void add(OGRLineString *theGeom);
  void add(OGRPoint *theGeom);

  OGRGeometry *build();

 private:
  std::list<OGRPolygon *> itsPolygons;
  std::list<OGRLineString *> itsLines;
  std::list<OGRPoint *> itsPoints;
};
}  // namespace Fmi
