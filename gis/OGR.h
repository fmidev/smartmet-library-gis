#pragma once

#include <boost/optional.hpp>
#include <list>
#include <ogr_geometry.h>
#include <string>

class OGRGeometry;
class OGRPolygon;
class OGRMultiPolygon;
class OGRSpatialReference;

// cannot forward declare OGR similarly since OGRwkbGeometryType is an enum

namespace geos
{
namespace geom
{
class Geometry;
}
}  // namespace geos

namespace Fmi
{
class CoordinateTransformation;
class Box;
class SpatialReference;
class GeometryBuilder;
class Shape;

using Shape_sptr = std::shared_ptr<Shape>;

namespace OGR
{
std::string exportToWkt(const OGRSpatialReference& theSRS);
std::string exportToWkt(const OGRGeometry& theGeom, int precision);
std::string exportToPrettyWkt(const OGRSpatialReference& theSRS);
std::string exportToProj(const OGRSpatialReference& theSRS);
std::string exportToWkt(const OGRGeometry& theGeom);
std::string exportToSvg(const OGRGeometry& theGeom, const Box& theBox, double thePrecision);

// We would prefert to use a const reference here but const is
// not possible due to SR reference counting
OGRGeometry* importFromGeos(const geos::geom::Geometry& theGeom, OGRSpatialReference* theSRS);

// Transform to box
void transform(OGRGeometry& theGeom, const Box& theBox);

// Translate the geometry
void translate(OGRGeometry& theGeom, double dx, double dy);
void translate(OGRGeometry* theGeom, double dx, double dy);

// Clip to rectangle, polygons may break into polylines.
OGRGeometry* lineclip(const OGRGeometry& theGeom, const Box& theBox);

// Clip to rectangle, polygons are preserved. Optional maximum length for new edges along the box
// boundaries.
OGRGeometry* polyclip(const OGRGeometry& theGeom,
                      const Box& theBox,
                      double theMaxSegmentLength = 0);

// Cut rectangle out, polygons may break into polylines
OGRGeometry* linecut(const OGRGeometry& theGeom, const Box& theBox);

// Cut rectangle out, polygons are preserved. Optional maximum length for new edges along the box
// boundaries.
OGRGeometry* polycut(const OGRGeometry& theGeom, const Box& theBox, double theMaxSegmentLength = 0);

// Filter out small polygons
OGRGeometry* despeckle(const OGRGeometry& theGeom, double theAreaLimit);

// Normalize rings to lexicographic order, mostly to consistent test results
void normalize(OGRPolygon& thePoly);
void normalize(OGRLinearRing& theRing);

// Normalize winding order: exterior=CW, interior=CCW
OGRGeometry* normalizeWindingOrder(const OGRGeometry& theGeom);

// Renormalize winding order after a coordinate transformation - some rings may have reverted order
OGRGeometry* renormalizeWindingOrder(const OGRGeometry& theGeom);

// Reverse winding order
OGRGeometry* reverseWindingOrder(const OGRGeometry& theGeom);

// Is the given coordinate inside a shape?
bool inside(const OGRGeometry& theGeom, double theX, double theY);
bool inside(const OGRPolygon& theGeom, double theX, double theY);

using CoordinatePoints = std::list<std::pair<double, double>>;

// OGRGeometry object is constructed from list of coordinates
// wkbPoint, wkbLineString, wkbLinearRing, wkbPolygon types are supported
// spatial reference with theEPSGNumber is assigned to the geometry

OGRGeometry* constructGeometry(const CoordinatePoints& theCoordinates,
                               int theGeometryType,  // OGRwkbGeometryType
                               unsigned int theEPSGNumber);

// OGRGeometry is expanded by theRadiusInMeters meters
OGRGeometry* expandGeometry(const OGRGeometry* theGeom, double theRadiusInMeters);

// Direction of north in the spatial reference given a WGS84 -> GEOM transformation
boost::optional<double> gridNorth(const CoordinateTransformation& theTransformation,
                                  double theLon,
                                  double theLat);

// Create OGRGeometry from WKT-string, if theEPSGNumber > 0 assign spatial reference to geometry
OGRGeometry* createFromWkt(const std::string& wktString, unsigned int theEPSGNumber = 0);

// ### Clipping and cutting for different shapes (box, circle, etc)

OGRGeometry* lineclip(const OGRGeometry& theGeom, Shape_sptr& theShape);
OGRGeometry* linecut(const OGRGeometry& theGeom, Shape_sptr& theShape);
OGRGeometry* polycut(const OGRGeometry& theGeom,
                     Shape_sptr& theShape,
                     double theMaxSegmentLength = 0);
OGRGeometry* polyclip(const OGRGeometry& theGeom,
                      Shape_sptr& theShape,
                      double theMaxSegmentLength = 0);

// Clipping and cutting results returned in the GeometryBuilder
void polycut(GeometryBuilder& builder,
             const OGRGeometry& theGeom,
             Shape_sptr& theShape,
             double theMaxSegmentLength);
void polyclip(GeometryBuilder& builder,
              const OGRGeometry& theGeom,
              Shape_sptr& theShape,
              double theMaxSegmentLength);

}  // namespace OGR
}  // namespace Fmi
