#ifdef UNIX

#include "PostGIS.h"
#include "CoordinateTransformation.h"
#include "OGR.h"
#include "SpatialReference.h"
#include <gdal_version.h>
#include <iostream>
#include <ogrsf_frmts.h>
#include <stdexcept>

// Segment geometries by 1 degree accuracy when clipping/cutting to a rectangle
const double default_segmentation_length = 1.0;

namespace Fmi
{
#if 0
void print_orientation(OGRGeometry& geom);

void print_orientation(OGRLinearRing& geom)
{
  if (geom.isClockwise())
    std::cout << " linearring is CW\n";
  else
    std::cout << " linearring is CCW\n";
}

void print_orientation(OGRPolygon& geom)
{
  if (geom.IsEmpty()) return;
  std::cout << "Polygon exterior ";
  print_orientation(*geom.getExteriorRing());
  std::cout << "\tNumber of holes: " << geom.getNumInteriorRings() << "\n";
  for (int i = 0, n = geom.getNumInteriorRings(); i < n; ++i)
  {
    std::cout << "\thole " << i << " ";
    print_orientation(*geom.getInteriorRing(i));
  }
}

void print_orientation(OGRMultiPolygon& geom)
{
  if (geom.IsEmpty()) return;
  for (int i = 0, n = geom.getNumGeometries(); i < n; ++i)
  {
    std::cout << "Multipolygon:\n";
    print_orientation(dynamic_cast<OGRPolygon&>(*geom.getGeometryRef(i)));
  }
}

void print_orientation(OGRGeometryCollection& geom)
{
  if (geom.IsEmpty()) return;
  for (int i = 0, n = geom.getNumGeometries(); i < n; ++i)
  {
    std::cout << "Collection:\n";
    print_orientation(*geom.getGeometryRef(i));
  }
}

void print_orientation(OGRGeometry& geom)
{
  OGRwkbGeometryType id = geom.getGeometryType();

  switch (id)
  {
    case wkbPoint:
    case wkbPoint25D:
    case wkbLineString:
    case wkbLineString25D:
    case wkbMultiPoint:
    case wkbMultiPoint25D:
    case wkbMultiLineString:
    case wkbMultiLineString25D:
      break;
    case wkbLinearRing:
      return print_orientation(dynamic_cast<OGRLinearRing&>(geom));
    case wkbPolygon:
    case wkbPolygon25D:
      return print_orientation(dynamic_cast<OGRPolygon&>(geom));
    case wkbMultiPolygon:
    case wkbMultiPolygon25D:
      return print_orientation(dynamic_cast<OGRMultiPolygon&>(geom));
    case wkbGeometryCollection:
    case wkbGeometryCollection25D:
      return print_orientation(dynamic_cast<OGRGeometryCollection&>(geom));
    default:
      throw std::runtime_error("FOOBAR");
  }
}
#endif

namespace PostGIS
{
// ----------------------------------------------------------------------
/*!
 * \brief Fetch a shape from the database
 *
 * \param theName "schema.table"
 * \param theSR nullptr, if original SR in database is desired
 */
// ----------------------------------------------------------------------

OGRGeometryPtr read(const Fmi::SpatialReference* theSR,
                    const GDALDataPtr& theConnection,
                    const std::string& theName,
                    const boost::optional<std::string>& theWhereClause)
{
  // Get time column in UTC time
  theConnection->ExecuteSQL("SET TIME ZONE UTC", nullptr, nullptr);

  // Fetch the layer, which is owned by the data source
  OGRLayer* layer = theConnection->GetLayerByName(theName.c_str());
  if (layer == nullptr)
    throw std::runtime_error("Failed to read '" + theName + "' from the database");

  // Establish the filter

  if (theWhereClause && !theWhereClause->empty())
  {
    auto err = layer->SetAttributeFilter(theWhereClause->c_str());
    if (err != OGRERR_NONE)
      throw std::runtime_error("Failed to set filter '" + *theWhereClause + "' on '" + theName +
                               "'");
  }

  auto* out = new OGRGeometryCollection;  // NOLINT

  // This is owned by us
  OGRFeature* feature;

  if (theSR == nullptr)
  {
    layer->GetSpatialRef()->SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
    if (layer->GetSpatialRef() == nullptr)
    {
      auto* wgs84 = new OGRSpatialReference();
      wgs84->SetFromUserInput("WGS84");
      out->assignSpatialReference(wgs84);
    }
    else
      out->assignSpatialReference(layer->GetSpatialRef());

    layer->ResetReading();
    while ((feature = layer->GetNextFeature()) != nullptr)
    {
      // owned by feature
      OGRGeometry* geometry = feature->GetGeometryRef();
      if (geometry != nullptr)
        out->addGeometry(geometry);  // clones geometry
    }
  }
  else
  {
    std::unique_ptr<SpatialReference> source;
    if (layer->GetSpatialRef() == nullptr)
      source.reset(new SpatialReference("WGS84"));
    else
      source.reset(new SpatialReference(*layer->GetSpatialRef()));

    CoordinateTransformation transformation(*source, *theSR);
    out->assignSpatialReference(theSR->get()->Clone());

    layer->ResetReading();
    while ((feature = layer->GetNextFeature()) != nullptr)
    {
      // owned by feature
      OGRGeometry* geometry = feature->GetGeometryRef();
      if (geometry != nullptr)
      {
#if 1
        auto* clone = transformation.transformGeometry(*geometry, default_segmentation_length);
#else
        // This one timeouts WMS ice.get tests:
        // const char* const opts[] = {"WRAPDATELINE=YES", nullptr};

        // This one timeouts the same this:
        // const char* const opts[] = {nullptr};

        auto* clone = OGRGeometryFactory::transformWithOptions(
            geometry, transformation.get(), const_cast<char**>(opts));
#endif
        if (clone != nullptr)
          out->addGeometryDirectly(clone);  // takes ownership
      }
    }
  }

  OGRFeature::DestroyFeature(feature);
  return OGRGeometryPtr(out);
}

// ----------------------------------------------------------------------
/*!
 * \brief Fetch a shape from the database along with associated attributes
 *
 * \param theSR nullptr, if original SR in database is desired
 * \param theName "schema.table"
 * \param theFieldNames names of the fileds to be fetched
 * \return Features return value contains geometries and related attributes
 */
// ----------------------------------------------------------------------

Features read(const Fmi::SpatialReference* theSR,
              const GDALDataPtr& theConnection,
              const std::string& theName,
              const std::set<std::string>& theFieldNames,
              const boost::optional<std::string>& theWhereClause)
{
  // Get time column in UTC time
  theConnection->ExecuteSQL("SET TIME ZONE UTC", nullptr, nullptr);

  Features ret;

  // Fetch the layer, which is owned by the data source
  OGRLayer* layer = theConnection->GetLayerByName(theName.c_str());
  if (layer == nullptr)
    throw std::runtime_error("Failed to read '" + theName + "' from the database");

  // Establish the filter

  if (theWhereClause && !theWhereClause->empty())
  {
    auto err = layer->SetAttributeFilter(theWhereClause->c_str());
    if (err != OGRERR_NONE)
      throw std::runtime_error("Failed to set filter '" + *theWhereClause + "' on '" + theName +
                               "'");
  }

  // Establish coordinate transformation

  std::unique_ptr<CoordinateTransformation> transformation;

  if (theSR != nullptr)
  {
    if (layer->GetSpatialRef() == nullptr)
      transformation.reset(new CoordinateTransformation(SpatialReference("WGS84"), *theSR));
    else
      transformation.reset(
          new CoordinateTransformation(SpatialReference(*layer->GetSpatialRef()), *theSR));
  }

  // This is owned by us

  OGRFeature* feature;

  layer->ResetReading();
  while ((feature = layer->GetNextFeature()) != nullptr)
  {
    FeaturePtr ret_item(new Feature);
    // owned by feature
    OGRGeometry* geometry = feature->GetGeometryRef();
    if (geometry != nullptr)
    {
      if (transformation == nullptr)
      {
        auto* clone = geometry->clone();
        ret_item->geom.reset(clone);
      }
      else
      {
        auto* clone = transformation->transformGeometry(*geometry, default_segmentation_length);
        ret_item->geom.reset(clone);
        // Note: We clone the input SR since we have no lifetime guarantees for it
        ret_item->geom->assignSpatialReference(theSR->get()->Clone());
      }
    }
    else
    {
      ret_item->geom = nullptr;
    }

    // add attribute fields
    OGRFeatureDefn* poFDefn = layer->GetLayerDefn();
    int iField;
    for (iField = 0; iField < poFDefn->GetFieldCount(); iField++)
    {
      OGRFieldDefn* poFieldDefn = poFDefn->GetFieldDefn(iField);
      std::string fieldname(poFieldDefn->GetNameRef());

      if (theFieldNames.find(fieldname) == theFieldNames.end())
        continue;
      if (feature->IsFieldSet(iField) == 0)
      {
        ret_item->attributes.insert(make_pair(fieldname, ""));
        continue;
      }

      const auto ftype = poFieldDefn->GetType();

      switch (ftype)
      {
        case OFTInteger:
          ret_item->attributes.insert(make_pair(fieldname, feature->GetFieldAsInteger(iField)));
          break;
        case OFTInteger64:
          ret_item->attributes.insert(
              make_pair(fieldname, static_cast<int>(feature->GetFieldAsInteger64(iField))));
          break;
        case OFTReal:
        {
          ret_item->attributes.insert(make_pair(fieldname, feature->GetFieldAsDouble(iField)));
          break;
        }
        case OFTString:
          ret_item->attributes.insert(make_pair(fieldname, feature->GetFieldAsString(iField)));
          break;
        case OFTDateTime:
        {
          int year, month, day, hour, min, sec, tzFlag;
          feature->GetFieldAsDateTime(iField, &year, &month, &day, &hour, &min, &sec, &tzFlag);
          boost::posix_time::ptime timestamp(boost::gregorian::date(year, month, day),
                                             boost::posix_time::time_duration(hour, min, sec));

          ret_item->attributes.insert(make_pair(fieldname, timestamp));
          break;
        }
        default:
          break;
      };
    }
    ret.push_back(ret_item);
    OGRFeature::DestroyFeature(feature);
  }

  return ret;
}
}  // namespace PostGIS
}  // namespace Fmi

#endif
