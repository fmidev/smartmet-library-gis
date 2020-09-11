#pragma once

#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/variant.hpp>
#include <gdal_version.h>
#include <map>
#include <vector>

class OGRGeometry;

namespace geos
{
  namespace geom
  {
    class Geometry;
  }
}

using GeometryPtr = boost::shared_ptr<geos::geom::Geometry> ;
using OGRGeometryPtr = boost::shared_ptr<OGRGeometry> ;

#if GDAL_VERSION_MAJOR < 2
class OGRDataSource;
using GDALDataPtr = boost::shared_ptr<OGRDataSource>;
#else
class GDALDataset;
using GDALDataPtr = boost::shared_ptr<GDALDataset>;
#endif

namespace Fmi
{
using Attribute = boost::variant<int, double, std::string, boost::posix_time::ptime>;

struct Feature
{
  OGRGeometryPtr geom;
  std::map<std::string, Attribute> attributes;
};

using FeaturePtr = boost::shared_ptr<Feature>;

using Features = std::vector<FeaturePtr>;
}  // namespace Fmi
