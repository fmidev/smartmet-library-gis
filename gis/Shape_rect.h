#pragma once

#include "Shape.h"

namespace Fmi
{

class Shape_rect : public Shape
{
  public:

                    Shape_rect(double theX1, double theY1, double theX2, double theY2);
    virtual         ~Shape_rect();

    int             clip(const OGRLineString *theGeom, ShapeClipper &theClipper, bool exterior) const;
    int             cut(const OGRLineString *theGeom, ShapeClipper &theClipper, bool exterior) const;

    bool            connectPoints_cw(OGRLinearRing& ring,double x1,double y1,double x2,double y2,double theMaximumSegmentLength) const;
    bool            connectPoints_ccw(OGRLinearRing& ring,double x1,double y1,double x2,double y2,double theMaximumSegmentLength) const;

    int             getPosition(double x, double y) const;
    int             getLineIntersectionPoints(double aX, double aY, double bX, double bY,double& pX1, double& pY1, double& pX2, double& pY2) const;

    bool            isInsideRing(const OGRLinearRing &theRing) const;
    bool            isOnEdge(double x, double y) const;
    bool            isRingInside(const OGRLinearRing& theRing) const;

    OGRLinearRing*  makeRing(double theMaximumSegmentLength) const;
    OGRLinearRing*  makeHole(double theMaximumSegmentLength) const;

    LineIterator    search_cw(OGRLinearRing *ring,std::list<OGRLineString *> &lines,double x1,double y1,double &x2,double &y2) const;
    LineIterator    search_ccw(OGRLinearRing *ring,std::list<OGRLineString *> &lines,double x1,double y1,double &x2,double &y2) const;

    void            print(std::ostream& stream);

  protected:

    void            clip_one_edge(double &x1, double &y1, double x2, double y2, double limit) const;
    void            clip_to_edges(double &x1, double &y1, double x2, double y2) const;

    static bool     onEdge(int pos);
    static bool     onSameEdge(int pos1, int pos2);
    static int      nextEdge(int pos);
    static bool     different(double x1, double y1, double x2, double y2);

  private:

    double          itsX1;  // bottom left coordinates
    double          itsY1;
    double          itsX2;  // top right coordinates
    double          itsY2;

    double          itsXMin;  // clipping rectangle
    double          itsYMin;
    double          itsXMax;
    double          itsYMax;

};


}  // namespace Fmi
