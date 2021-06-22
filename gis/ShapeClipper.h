/*
 * \brief Utility container for the partial basic elements formed by clipping
 */

#pragma once

#include "Shape.h"
#include <list>
#include <ogr_geometry.h>

namespace Fmi
{
class GeometryBuilder;

class ShapeClipper
{
 public:
  ShapeClipper() = delete;
  ShapeClipper(Shape_sptr &theShape, bool keep_inside);
  ~ShapeClipper();

  void addShape();  // add a shape as exterior or interior depending on KeepInsideFlag

  void addExterior(OGRLinearRing *theRing);
  void addExterior(OGRLineString *theLine);

  void addInterior(OGRLinearRing *theRing);
  void addInterior(OGRLineString *theLine);

  void reconnectWithShape(double theMaximumSegmentLength);
  void reconnectWithoutShape();
  void release(GeometryBuilder &theBuilder);
  void clear();

  bool empty() const;
  void reconnect();

 private:
  void connectLines(std::list<OGRLinearRing *> &theRings,
                    std::list<OGRLineString *> &theLines,
                    double theMaximumSegmentLength,
                    bool keep_inside,
                    bool exterior);
  void reconnectLines(std::list<OGRLineString *> &lines, bool exterior);

  Shape_sptr itsShape;
  bool itsKeepInsideFlag;
  bool itsAddShapeFlag;

  std::list<OGRLinearRing *> itsExteriorRings;
  std::list<OGRLineString *> itsExteriorLines;

  std::list<OGRLinearRing *> itsInteriorRings;
  std::list<OGRLineString *> itsInteriorLines;

  std::list<OGRPolygon *> itsPolygons;  // Result from build()
};

}  // namespace Fmi
