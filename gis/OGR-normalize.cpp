#include "OGR.h"

#include <macgyver/DebugTools.h>
#include <macgyver/Exception.h>
#include <ogr_geometry.h>

namespace
{
// ----------------------------------------------------------------------
/*!
 * \brief Reverse given segment in a linestring
 */
// ----------------------------------------------------------------------

void reverse(OGRLineString &line, int start, int end)
{
  try
  {
    OGRPoint p1;
    OGRPoint p2;
    while (start < end)
    {
      line.getPoint(start, &p1);
      line.getPoint(end, &p2);
      line.setPoint(start, &p2);
      line.setPoint(end, &p1);
      ++start;
      --end;
    }
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// Forward declaration needed since two functions call each other
OGRGeometry *normalize_winding(const OGRGeometry *theGeom);

// ----------------------------------------------------------------------
/*!
 * \brief Handle Polygon
 */
// ----------------------------------------------------------------------

OGRPolygon *normalize_winding(const OGRPolygon *theGeom)
{
  try
  {
    if (theGeom == nullptr || theGeom->IsEmpty() != 0)
      return nullptr;

    auto *out = new OGRPolygon;

    // Preserve Z/M flags
    if (theGeom->Is3D())
      out->set3D(TRUE);
    if (theGeom->IsMeasured())
      out->setMeasured(TRUE);

    auto *exterior = dynamic_cast<OGRLinearRing *>(theGeom->getExteriorRing()->clone());

    if (!exterior->isClockwise())
      exterior->reversePoints();

    out->addRingDirectly(exterior);

    for (int i = 0, n = theGeom->getNumInteriorRings(); i < n; i++)
    {
      auto *geom = dynamic_cast<OGRLinearRing *>(theGeom->getInteriorRing(i)->clone());
      if (geom->isClockwise())
        geom->reversePoints();
      out->addRingDirectly(geom);
    }

    return out;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Handle MultiPolygon
 */
// ----------------------------------------------------------------------

OGRMultiPolygon *normalize_winding(const OGRMultiPolygon *theGeom)
{
  try
  {
    if (theGeom == nullptr || theGeom->IsEmpty() != 0)
      return nullptr;

    auto *out = new OGRMultiPolygon();

    // Preserve Z/M flags for the collection itself
    if (theGeom->Is3D())
      out->set3D(TRUE);
    if (theGeom->IsMeasured())
      out->setMeasured(TRUE);

    for (int i = 0, n = theGeom->getNumGeometries(); i < n; ++i)
    {
      auto *geom = normalize_winding(dynamic_cast<const OGRPolygon *>(theGeom->getGeometryRef(i)));
      if (geom != nullptr)
        out->addGeometryDirectly(geom);
    }
    return out;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Handle GeometryCollection
 */
// ----------------------------------------------------------------------

OGRGeometryCollection *normalize_winding(const OGRGeometryCollection *theGeom)
{
  try
  {
    if (theGeom == nullptr || theGeom->IsEmpty() != 0)
      return nullptr;

    auto *out = new OGRGeometryCollection();

    // Preserve Z/M flags for the collection itself
    if (theGeom->Is3D())
      out->set3D(TRUE);
    if (theGeom->IsMeasured())
      out->setMeasured(TRUE);

    for (int i = 0, n = theGeom->getNumGeometries(); i < n; ++i)
    {
      auto *geom = normalize_winding(theGeom->getGeometryRef(i));
      if (geom != nullptr)
        out->addGeometryDirectly(geom);
    }
    return out;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Normalize winding of the given geometry
 */
// ----------------------------------------------------------------------

OGRGeometry *normalize_winding(const OGRGeometry *theGeom)
{
  try
  {
    if (theGeom == nullptr)
      return nullptr;

    const OGRwkbGeometryType id = theGeom->getGeometryType();

    // Note the flattening call to ignore Z/M properties
    switch (wkbFlatten(id))
    {
      case wkbPoint:
      case wkbMultiPoint:
      case wkbMultiLineString:
      case wkbLineString:
        return theGeom->clone();
      case wkbPolygon:
        return normalize_winding(dynamic_cast<const OGRPolygon *>(theGeom));
      case wkbMultiPolygon:
        return normalize_winding(dynamic_cast<const OGRMultiPolygon *>(theGeom));
      case wkbGeometryCollection:
        return normalize_winding(dynamic_cast<const OGRGeometryCollection *>(theGeom));

      case wkbNone:
        throw Fmi::Exception::Trace(
            BCP,
            "Encountered a 'none' geometry component while changing winding order of an OGR "
            "geometry");
      default:
      {
        const char *pszName = OGRGeometryTypeToName(id);
        throw Fmi::Exception::Trace(BCP,
                                    "Encountered an unknown geometry component while normalizing "
                                    "winding order of an OGR geometry")
            .addParameter("Type", pszName);
      }
    }

    // NOT REACHED
    return nullptr;
  }
  catch (...)
  {
    auto error = Fmi::Exception::Trace(BCP, "Operation failed!");
    error.addParameter("GeometryType", std::to_string(theGeom->getGeometryType()));
    error.addParameter("GeometryWKBType", std::to_string(wkbFlatten(theGeom->getGeometryType())));
    error.addParameter("WKT", theGeom->exportToWkt());
    error.addParameter("C++ type", Fmi::demangle_cpp_type_name(typeid(*theGeom).name()));
    throw error;
  }
}

}  // namespace

// ----------------------------------------------------------------------
/*!
 * \brief Normalize the winding order
 */
// ----------------------------------------------------------------------

OGRGeometry *Fmi::OGR::normalizeWindingOrder(const OGRGeometry &theGeom)
{
  try
  {
    auto *geom = normalize_winding(&theGeom);

    if (geom != nullptr)
      geom->assignSpatialReference(theGeom.getSpatialReference());  // SR is ref. counter

    return geom;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Normalize a ring into lexicographic order
 *
 * This is strictly speaking not necessary, but it keeps the expected
 * test results stable.
 */
// ----------------------------------------------------------------------

void Fmi::OGR::normalize(OGRLinearRing &theRing)
{
  try
  {
    if (theRing.IsEmpty() != 0)
      return;

    // Find the "smallest" coordinate using lexicographic ordering

    int best_pos = 0;
    int n = theRing.getNumPoints();
    for (int pos = 0; pos < n; ++pos)
    {
      if ((theRing.getX(pos) < theRing.getX(best_pos)) ||
          (theRing.getX(pos) == theRing.getX(best_pos) &&
           theRing.getY(pos) < theRing.getY(best_pos)))
        best_pos = pos;
    }

    // Quick exit if the ring is already normalized
    if (best_pos == 0)
      return;

    // Flip hands -algorithm to the part without the
    // duplicate last coordinate at n-1:

    reverse(theRing, 0, best_pos - 1);
    reverse(theRing, best_pos, n - 2);
    reverse(theRing, 0, n - 2);

    // And make sure the ring is valid by duplicating the first coordinate
    // at the end:

    OGRPoint point;
    theRing.getPoint(0, &point);
    theRing.setPoint(n - 1, &point);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

void Fmi::OGR::normalize(OGRPolygon &thePoly)
{
  try
  {
    if (thePoly.IsEmpty() != 0)
      return;

    auto *exterior = thePoly.getExteriorRing();
    if (exterior != nullptr)
      normalize(*exterior);

    for (int i = 0, n = thePoly.getNumInteriorRings(); i < n; i++)
    {
      auto *hole = thePoly.getInteriorRing(i);
      if (hole != nullptr)
        normalize(*hole);
    }
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}
