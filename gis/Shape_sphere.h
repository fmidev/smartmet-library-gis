#pragma once

#include "Shape.h"

namespace Fmi
{
class Shape_sphere : public Shape
{
 public:
  Shape_sphere(double theX, double theY, double theRadius);
  ~Shape_sphere() override;

  int clip(const OGRLineString* theGeom, ShapeClipper& theClipper, bool exterior) const override;
  int cut(const OGRLineString* theGeom, ShapeClipper& theClipper, bool exterior) const override;

  bool connectPoints_cw(OGRLinearRing& ring,
                        double x1,
                        double y1,
                        double x2,
                        double y2,
                        double theMaximumSegmentLength) const override;
  bool connectPoints_ccw(OGRLinearRing& ring,
                         double x1,
                         double y1,
                         double x2,
                         double y2,
                         double theMaximumSegmentLength) const override;

  int getPosition(double x, double y) const override;

  bool isInsideRing(const OGRLinearRing& theRing) const override;
  bool isRingInside(const OGRLinearRing& theRing) const override;

  OGRLineString* makeLineRing(double theMaximumSegmentLength) const override;
  OGRLinearRing* makeRing(double theMaximumSegmentLength) const override;
  OGRLinearRing* makeHole(double theMaximumSegmentLength) const override;

  LineIterator search_cw(OGRLinearRing* ring,
                         std::list<OGRLineString*>& lines,
                         double x1,
                         double y1,
                         double& x2,
                         double& y2) const override;
  LineIterator search_ccw(OGRLinearRing* ring,
                          std::list<OGRLineString*>& lines,
                          double x1,
                          double y1,
                          double& x2,
                          double& y2) const override;

  void print(std::ostream& stream) override;

 protected:
  int getPositionByMetricCoordinates(double x, double y) const;
  int getLineIntersectionPoints(double aX,
                                double aY,
                                double bX,
                                double bY,
                                double& pX1,
                                double& pY1,
                                double& pX2,
                                double& pY2) const;

  double angleDistance_cw(double a, double b) const;
  double angleDistance_ccw(double a, double b) const;
  double distance(double a, double b) const;
  double getAngle(double x, double y) const;
  void getLatLonCoordinates(double x, double y, double& lon, double& lat) const;
  void getMetricCoordinates(double lon, double lan, double& x, double& y) const;
  void getMetricPointByAngle(double angle, double& x, double& y) const;
  void getLatLonPointByAngle(double angle, double& x, double& y) const;
  bool isOnEdge(double xx, double yy) const;
  bool isOnEdge(double xx, double yy, double& angle) const;
  double x() const { return itsX; }
  double y() const { return itsY; }
  double radius() const { return itsRadius; }
  double radius2() const { return itsRadius2; }
  void setBorderStep(double theBorderStep);
  void setRadius(double theRadius);

 private:
  double itsX;  // Center coordinates (latlon)
  double itsY;
  double itsXX;  // Center coordinates (azimuthal equidistant)
  double itsYY;
  double itsXDelta;  // Shifted center coordinates (=> positive calculations)
  double itsYDelta;
  double itsXXDelta;  // Shifted center coordinates (=> positive calculations)
  double itsYYDelta;
  double itsRadius;   // radius
  double itsRadius2;  // radius * radius
  double itsXXMin;
  double itsYYMin;
  double itsXXMax;
  double itsYYMax;
  double itsBorderStep;
  double itsBorderAngleStep;

  OGRSpatialReference sr_latlon;
  OGRSpatialReference sr;
  OGRCoordinateTransformation* transformation;
  OGRCoordinateTransformation* reverseTransformation;
};

}  // namespace Fmi
