#pragma once

#include <list>
#include <ogr_geometry.h>
#include <optional>
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
std::string exportToSimpleWkt(const OGRSpatialReference& theSRS);
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

// Isoperimetric compactness of a polygon: 4πA/L².
//   - 1.0 for a circle, π/4 ≈ 0.785 for a square,
//   - → 0 for thin or fractal shapes.
// For polygons in a geographic CRS (lat/lon) area and length are computed
// on the WGS84 sphere; for projected CRS GDAL's planar get_Area / get_Length
// are used (assuming a metric CRS). The result is dimensionless either way.
//
// CompactnessMode::Exterior — exterior ring only (typical "shape" measure).
// CompactnessMode::Net      — net area minus holes, perimeter sums all rings;
//                             penalises Swiss-cheese polygons.
enum class CompactnessMode
{
  Exterior,
  Net
};
double compactness(const OGRPolygon& thePoly,
                   CompactnessMode theMode = CompactnessMode::Exterior);

// Scalar form: 4πA/L² from raw area / perimeter values. Useful for callers
// that have these as plain numbers — typically because the source format
// stores polygons as bin-local segments and the per-polygon perimeter is
// aggregated outside any OGR object (e.g. the gshhg-gmt-nc4 binned NetCDF
// reader in qdless). Returns 0 for non-positive inputs. Both arguments
// must be in matching units; the ratio is dimensionless.
double compactness(double theArea, double thePerimeter);

// Drop polygons whose area (km²) is below theMinArea or whose compactness
// is below theMinCompactness. Multipolygons / collections are walked
// recursively; non-polygon parts pass through unchanged. Returns nullptr
// if nothing survives. Caller takes ownership.
OGRGeometry* filterByCompactness(const OGRGeometry& theGeom,
                                  double theMinCompactness,
                                  double theMinArea = 0,
                                  CompactnessMode theMode = CompactnessMode::Exterior);

// Normalize rings to lexicographic order, mostly to consistent test results
void normalize(OGRPolygon& thePoly);
void normalize(OGRLinearRing& theRing);

// Normalize winding order: exterior=CCW, interior=CW (RFC 7946)
void normalizeWindingOrder(OGRGeometry* theGeom);

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
std::optional<double> gridNorth(const CoordinateTransformation& theTransformation,
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
