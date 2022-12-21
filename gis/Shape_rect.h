#pragma once

#include "Shape.h"

namespace Fmi
{
class Shape_rect : public Shape
{
 public:
  Shape_rect(double theX1, double theY1, double theX2, double theY2);
  ~Shape_rect() override;

  Shape_rect(const Shape_rect &other) = delete;
  Shape_rect &operator=(const Shape_rect &other) = delete;
  Shape_rect(Shape_rect &&other) = delete;
  Shape_rect &operator=(Shape_rect &&other) = delete;

  int clip(const OGRLineString *theGeom, ShapeClipper &theClipper, bool exterior) const override;
  int cut(const OGRLineString *theGeom, ShapeClipper &theClipper, bool exterior) const override;

  bool connectPoints_cw(OGRLinearRing &ring,
                        double x1,
                        double y1,
                        double x2,
                        double y2,
                        double theMaximumSegmentLength) const override;
  bool connectPoints_ccw(OGRLinearRing &ring,
                         double x1,
                         double y1,
                         double x2,
                         double y2,
                         double theMaximumSegmentLength) const override;

  int getPosition(double x, double y) const override;

  bool isInsideRing(const OGRLinearRing &theRing) const override;
  // bool            isOnEdge(double x, double y) const override;
  bool isRingInside(const OGRLinearRing &theRing) const override;

  OGRLinearRing *makeRing(double theMaximumSegmentLength) const override;
  OGRLinearRing *makeHole(double theMaximumSegmentLength) const override;

  LineIterator search_cw(OGRLinearRing *ring,
                         std::list<OGRLineString *> &lines,
                         double x1,
                         double y1,
                         double &x2,
                         double &y2) const override;
  LineIterator search_ccw(OGRLinearRing *ring,
                          std::list<OGRLineString *> &lines,
                          double x1,
                          double y1,
                          double &x2,
                          double &y2) const override;

  void print(std::ostream &stream) override;

 protected:
  int getLineIntersectionPoints(double aX,
                                double aY,
                                double bX,
                                double bY,
                                double &pX1,
                                double &pY1,
                                double &pX2,
                                double &pY2) const;

  void clip_to_edges(double &x1, double &y1, double x2, double y2) const;

  static bool onEdge(int pos);
  static bool onSameEdge(int pos1, int pos2);
  static int nextEdge(int pos);
  static bool different(double x1, double y1, double x2, double y2);

 private:
  double itsX1;  // bottom left coordinates
  double itsY1;
  double itsX2;  // top right coordinates
  double itsY2;

  double itsXMin;  // clipping rectangle
  double itsYMin;
  double itsXMax;
  double itsYMax;
};

}  // namespace Fmi
