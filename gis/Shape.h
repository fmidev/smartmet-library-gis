#pragma once

#include <macgyver/Exception.h>
#include <iostream>
#include <ogr_geometry.h>

namespace Fmi
{
typedef std::list<OGRLineString *>::iterator LineIterator;

class ShapeClipper;

class Shape
{
 public:
  Shape();
  virtual ~Shape();

  virtual int clip(const OGRLineString *theGeom, ShapeClipper &theClipper, bool exterior) const;
  virtual int cut(const OGRLineString *theGeom, ShapeClipper &theClipper, bool exterior) const;

  virtual bool connectPoints_cw(OGRLinearRing &ring,
                                double x1,
                                double y1,
                                double x2,
                                double y2,
                                double theMaximumSegmentLength) const;
  virtual bool connectPoints_ccw(OGRLinearRing &ring,
                                 double x1,
                                 double y1,
                                 double x2,
                                 double y2,
                                 double theMaximumSegmentLength) const;

  virtual int getPosition(double x, double y) const;
  // virtual int             getLineIntersectionPoints(double aX, double aY, double bX, double
  // bY,double& pX1, double& pY1, double& pX2, double& pY2) const;

  virtual bool isInsideRing(const OGRLinearRing &theRing) const;
  // virtual bool            isOnEdge(double x, double y) const;
  virtual bool isRingInside(const OGRLinearRing &theRing) const;

  virtual OGRLineString *makeLineRing(double theMaximumSegmentLength) const;
  virtual OGRLinearRing *makeRing(double theMaximumSegmentLength) const;
  virtual OGRLinearRing *makeHole(double theMaximumSegmentLength) const;

  virtual LineIterator search_cw(OGRLinearRing *ring,
                                 std::list<OGRLineString *> &lines,
                                 double x1,
                                 double y1,
                                 double &x2,
                                 double &y2) const;
  virtual LineIterator search_ccw(OGRLinearRing *ring,
                                  std::list<OGRLineString *> &lines,
                                  double x1,
                                  double y1,
                                  double &x2,
                                  double &y2) const;

  virtual void print(std::ostream &stream);

  static bool all_not_inside(int position);
  static bool all_not_outside(int position);
  static bool all_only_inside(int position);
  static bool all_only_outside(int position);

  enum Position
  {
    Inside = 1,
    Outside = 2,
    Left = 4,
    Top = 8,
    Right = 16,
    Bottom = 32,
    TopLeft = Top | Left,         // 12
    TopRight = Top | Right,       // 24
    BottomLeft = Bottom | Left,   // 36
    BottomRight = Bottom | Right  // 48
  };
};

typedef std::shared_ptr<Shape> Shape_sptr;

}  // namespace Fmi
