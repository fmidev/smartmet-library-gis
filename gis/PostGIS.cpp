#ifdef UNIX

#include "PostGIS.h"
#include "CoordinateTransformation.h"
#include "OGR.h"
#include "SpatialReference.h"
#include <gdal_version.h>
#include <iostream>
#include <ogrsf_frmts.h>
#include <stdexcept>

#include "Box.h"
#include <geos_c.h>

// Segment geometries by 1 degree accuracy when clipping/cutting to a rectangle
const double default_segmentation_length = 1.0;

namespace
{

// Force the last point of a polygon ring to be exactly equal to the first when the gap
// between them is a small floating-point error.  Polygon rings must always be closed by
// definition; any non-zero gap is a data-quality issue that can cause rendering artefacts
// (spurious lines from the polygon interior to the bounding-box boundary).
//
// We only close gaps that are smaller than 1e-6 × the coordinate magnitude — i.e., gaps
// that are pure floating-point noise and not a genuine geometry defect.  For typical
// WGS-84 coordinates (lon ≈ 25, lat ≈ 65) this is about 6.5e-5 degrees ≈ 7 m; for
// ETRS-TM35FIN coordinates (y ≈ 7 000 000 m) it is about 7 m as well.
void forceCloseRingIfNeeded(OGRLinearRing* ring)
{
  if (!ring)
    return;
  const int n = ring->getNumPoints();
  if (n < 2)
    return;

  const double x0 = ring->getX(0);
  const double y0 = ring->getY(0);
  const double xn = ring->getX(n - 1);
  const double yn = ring->getY(n - 1);

  if (x0 == xn && y0 == yn)
    return;  // already exactly closed

  const double dx = x0 - xn;
  const double dy = y0 - yn;
  const double gap = std::sqrt(dx * dx + dy * dy);

  // Threshold: 1e-6 × coordinate magnitude, floored at 1e-10 to handle near-zero coords.
  const double mag =
      std::max({std::abs(x0), std::abs(y0), std::abs(xn), std::abs(yn), 1.0});
  if (gap < mag * 1e-6)
    ring->setPoint(n - 1, x0, y0);
}

void forceClosePolygonRings(OGRGeometry* geom)
{
  if (!geom)
    return;

  switch (wkbFlatten(geom->getGeometryType()))
  {
    case wkbPolygon:
    {
      auto* poly = static_cast<OGRPolygon*>(geom);
      forceCloseRingIfNeeded(poly->getExteriorRing());
      for (int i = 0; i < poly->getNumInteriorRings(); ++i)
        forceCloseRingIfNeeded(poly->getInteriorRing(i));
      break;
    }
    case wkbMultiPolygon:
    case wkbGeometryCollection:
    {
      auto* coll = static_cast<OGRGeometryCollection*>(geom);
      for (int i = 0; i < coll->getNumGeometries(); ++i)
        forceClosePolygonRings(coll->getGeometryRef(i));
      break;
    }
    default:
      break;
  }
}

}  // namespace

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
                    const std::optional<std::string>& theWhereClause)
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

  const auto next_feature = [layer]()
  {
    return std::shared_ptr<OGRFeature>(
        layer->GetNextFeature(), [](OGRFeature* feature) { OGRFeature::DestroyFeature(feature); });
  };

  std::shared_ptr<OGRFeature> feature;

  if (theSR == nullptr)
  {
    // layer->GetSpatialRef()->SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
    if (layer->GetSpatialRef() == nullptr)
    {
      auto* wgs84 = new OGRSpatialReference();
      wgs84->SetFromUserInput("WGS84");
      out->assignSpatialReference(wgs84);
    }
    else
    {
      std::unique_ptr<OGRSpatialReference, OGRSpatialReferenceReleaser> crs(
          layer->GetSpatialRef()->Clone(), OGRSpatialReferenceReleaser());
      crs->SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
      out->assignSpatialReference(crs.get());
    }

    layer->ResetReading();
    while ((feature = next_feature()))
    {
      // owned by feature
      OGRGeometry* geometry = feature->GetGeometryRef();
      if (geometry != nullptr)
      {
        auto* clone = geometry->clone();
        if (clone)
        {
          forceClosePolygonRings(clone);
          out->addGeometryDirectly(clone);  // takes ownership
        }
      }
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
    std::shared_ptr<OGRSpatialReference> tmp(theSR->get()->Clone(),
                                             [](OGRSpatialReference* sr) { sr->Release(); });
    out->assignSpatialReference(tmp.get());

    layer->ResetReading();
    while ((feature = next_feature()))
    {
      const auto* defn = feature->GetDefnRef();
      int fieldIndex = defn->GetFieldIndex("iso_a2");
      bool hasfield = (fieldIndex >= 0 && feature->IsFieldSetAndNotNull(fieldIndex));
      std::string name;
      if (hasfield)
        name = feature->GetFieldAsString(fieldIndex);

      // owned by feature
      OGRGeometry* geometry = feature->GetGeometryRef();
      if (geometry != nullptr)
      {
        auto* clone = transformation.transformGeometry(*geometry, default_segmentation_length);
        if (clone != nullptr)
        {
#if 0
          if (!clone->IsValid())
          {
            std::cerr << "'" << name << "' NOT valid\n";
            // delete clone;
            // clone = nullptr;
          }
#endif
        }
        if (clone != nullptr)
        {
          forceClosePolygonRings(clone);
          out->addGeometryDirectly(clone);  // takes ownership
        }
      }
    }
  }

  return {out, [](OGRGeometry* g) { OGRGeometryFactory::destroyGeometry(g); }};
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
              const std::optional<std::string>& theWhereClause)
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

  const auto next_feature = [layer]()
  {
    return std::shared_ptr<OGRFeature>(
        layer->GetNextFeature(), [](OGRFeature* feature) { OGRFeature::DestroyFeature(feature); });
  };

  std::shared_ptr<OGRFeature> feature;

  layer->ResetReading();
  while ((feature = next_feature()))
  {
    FeaturePtr ret_item(new Feature);
    // owned by feature
    OGRGeometry* geometry = feature->GetGeometryRef();
    if (geometry != nullptr)
    {
      if (transformation == nullptr)
      {
        auto* clone = geometry->clone();
        forceClosePolygonRings(clone);
        ret_item->geom.reset(clone);
      }
      else
      {
        auto* clone = transformation->transformGeometry(*geometry, default_segmentation_length);
        forceClosePolygonRings(clone);
        ret_item->geom.reset(clone);
        // Note: We clone the input SR since we have no lifetime guarantees for it
        std::shared_ptr<OGRSpatialReference> tmp(theSR->get()->Clone(),
                                                 [](OGRSpatialReference* sr) { sr->Release(); });
        ret_item->geom->assignSpatialReference(tmp.get());
      }
    }
    else
    {
      ret_item->geom = nullptr;
    }

    // add attribute fields
    OGRFeatureDefn* poFDefn = layer->GetLayerDefn();
    int iField = 0;
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
          int year = 0;
          int month = 0;
          int day = 0;
          int hour = 0;
          int min = 0;
          int sec = 0;
          int tzFlag = 0;
          feature->GetFieldAsDateTime(iField, &year, &month, &day, &hour, &min, &sec, &tzFlag);
          Fmi::DateTime timestamp(Fmi::Date(year, month, day), Fmi::TimeDuration(hour, min, sec));

          ret_item->attributes.insert(make_pair(fieldname, timestamp));
          break;
        }
        default:
          break;
      }
    }
    ret.push_back(ret_item);
  }

  return ret;
}
}  // namespace PostGIS
}  // namespace Fmi

#endif
