#ifdef UNIX

#include "PostGIS.h"
#include "CoordinateTransformation.h"
#include <ogr_spatialref.h>
#include <ogrsf_frmts.h>
#include <stdexcept>

namespace Fmi
{
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
    out->assignSpatialReference(layer->GetSpatialRef());

    layer->ResetReading();
    while ((feature = layer->GetNextFeature()) != nullptr)
    {
      // owned by feature
      OGRGeometry* geometry = feature->GetGeometryRef();
      if (geometry != nullptr) out->addGeometry(geometry);  // clones geometry
    }
  }
  else
  {
    SpatialReference source(*layer->GetSpatialRef());
    CoordinateTransformation transformation(source, *theSR);
    out->assignSpatialReference(theSR->get()->Clone());

    layer->ResetReading();
    while ((feature = layer->GetNextFeature()) != nullptr)
    {
      // owned by feature
      OGRGeometry* geometry = feature->GetGeometryRef();
      if (geometry != nullptr)
      {
        auto* clone = geometry->clone();
        transformation.Transform(*clone);
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
    transformation.reset(
        new CoordinateTransformation(SpatialReference(*layer->GetSpatialRef()), *theSR));

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
        ret_item->geom.reset(geometry->clone());
      }
      else
      {
        auto* clone = geometry->clone();
        transformation->Transform(*clone);
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

      if (theFieldNames.find(fieldname) == theFieldNames.end()) continue;
      if (feature->IsFieldSet(iField) == 0)
      {
        ret_item->attributes.insert(make_pair(fieldname, ""));
        continue;
      }

      switch (poFieldDefn->GetType())
      {
        case OFTInteger:
          ret_item->attributes.insert(make_pair(fieldname, feature->GetFieldAsInteger(iField)));
          break;
        case OFTReal:
          ret_item->attributes.insert(make_pair(fieldname, feature->GetFieldAsDouble(iField)));
          break;
        case OFTString:
        {
          ret_item->attributes.insert(make_pair(fieldname, feature->GetFieldAsString(iField)));
        }
        break;
        case OFTDateTime:
        {
          int year, month, day, hour, min, sec, tzFlag;
          feature->GetFieldAsDateTime(iField, &year, &month, &day, &hour, &min, &sec, &tzFlag);
          boost::posix_time::ptime timestamp(boost::gregorian::date(year, month, day),
                                             boost::posix_time::time_duration(hour, min, sec));

          ret_item->attributes.insert(make_pair(fieldname, timestamp));
        }
        break;
        default:
          break;
      };
    }
    OGRFeature::DestroyFeature(feature);

    ret.push_back(ret_item);
  }

  return ret;
}
}  // namespace PostGIS
}  // namespace Fmi

#endif
