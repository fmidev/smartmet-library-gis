#pragma once

#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <boost/variant.hpp>
#include <gdal_version.h>
#include <map>
#include <memory>
#include <vector>

class OGRGeometry;

namespace geos
{
namespace geom
{
class Geometry;
}
}  // namespace geos

using GeometryPtr = std::shared_ptr<geos::geom::Geometry>;
using OGRGeometryPtr = std::shared_ptr<OGRGeometry>;

#if GDAL_VERSION_MAJOR < 2
class OGRDataSource;
using GDALDataPtr = std::shared_ptr<OGRDataSource>;
#else
class GDALDataset;
using GDALDataPtr = std::shared_ptr<GDALDataset>;
#endif

namespace Fmi
{
using Attribute = boost::variant<int, double, std::string, boost::posix_time::ptime>;

struct Feature
{
  OGRGeometryPtr geom;
  std::map<std::string, Attribute> attributes;
};

using FeaturePtr = std::shared_ptr<Feature>;

using Features = std::vector<FeaturePtr>;
}  // namespace Fmi
