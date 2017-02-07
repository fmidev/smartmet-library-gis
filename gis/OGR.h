#pragma once
#include <gdal/ogr_geometry.h>
#include <geos/geom/Geometry.h>
#include <string>
#include <list>

namespace Fmi
{
class Box;

namespace OGR
{
std::string exportToWkt(const OGRGeometry& theGeom);

std::string exportToSvg(const OGRGeometry& theGeom, const Box& theBox, int thePrecision = 1);

// We would prefert to use a const reference here but const is
// not possible due to SR reference counting
OGRGeometry* importFromGeos(const geos::geom::Geometry& theGeom, OGRSpatialReference* theSR);

// Clip to rectangle, polygons may break into polylines
OGRGeometry* lineclip(const OGRGeometry& theGeom, const Box& theBox);

// Clip to rectangle, polygons are preserved
OGRGeometry* polyclip(const OGRGeometry& theGeom, const Box& theBox);

// Filter out small polygons
OGRGeometry* despeckle(const OGRGeometry& theGeom, double theAreaLimit);

// Is the given coordinate inside a shape?
bool inside(const OGRGeometry& theGeom, double theX, double theY);
bool inside(const OGRPolygon& theGeom, double theX, double theY);

typedef std::list<std::pair<double, double> > CoordinatePoints;
// OGRGeometry object is constructed from list of coordinates
// wkbPoint, wkbLineString, wkbLinearRing, wkbPolygon types are supported
// spatial reference with theEPSGNumber is assigned to the geometry
OGRGeometry* constructGeometry(const CoordinatePoints& theCoordinates,
                               OGRwkbGeometryType theGeometryType,
                               unsigned int theEPSGNumber);
// OGRGeometry is expanded by theRadiusInMeters meters
OGRGeometry* expandGeometry(const OGRGeometry* theGeom, double theRadiusInMeters);

}  // namespace OGR
}  // namespace Fmi
