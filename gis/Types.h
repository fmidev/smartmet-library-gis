#pragma once

#include <geos/geom/Geometry.h>
#include <gdal/ogr_geometry.h>
#include <gdal/ogrsf_frmts.h>
#include <boost/shared_ptr.hpp>
#include <boost/variant.hpp>
#include <map>

typedef boost::shared_ptr<geos::geom::Geometry> GeometryPtr;

typedef boost::shared_ptr<OGRGeometry> OGRGeometryPtr;
typedef boost::shared_ptr<OGRDataSource> OGRDataSourcePtr;

namespace Fmi
{
typedef boost::variant<int, double, std::string> Attribute;

struct Feature
{
  OGRGeometryPtr geom;
  std::map<std::string, Attribute> attributes;
};

typedef boost::shared_ptr<Feature> FeaturePtr;

typedef std::vector<FeaturePtr> Features;
}
