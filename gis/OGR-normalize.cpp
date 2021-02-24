#include "OGR.h"

#include <ogr_geometry.h>
#include <stdexcept>

namespace
{
// ----------------------------------------------------------------------
/*!
 * \brief Reverse given segment in a linestring
 */
// ----------------------------------------------------------------------

void reverse(OGRLineString &line, int start, int end)
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

// Forward declaration needed since two functions call each other
OGRGeometry *normalize_winding(const OGRGeometry *theGeom);

// ----------------------------------------------------------------------
/*!
 * \brief Handle Polygon
 */
// ----------------------------------------------------------------------

OGRPolygon *normalize_winding(const OGRPolygon *theGeom)
{
  if (theGeom == nullptr || theGeom->IsEmpty() != 0)
    return nullptr;

  auto *out = new OGRPolygon;

  auto *exterior = dynamic_cast<OGRLinearRing *>(theGeom->getExteriorRing()->clone());

  if (!exterior->isClockwise())
    exterior->reverseWindingOrder();

  out->addRingDirectly(exterior);

  for (int i = 0, n = theGeom->getNumInteriorRings(); i < n; i++)
  {
    auto *geom = dynamic_cast<OGRLinearRing *>(theGeom->getInteriorRing(i)->clone());
    if (geom->isClockwise())
      geom->reverseWindingOrder();
    out->addRingDirectly(geom);
  }

  return out;
}

// ----------------------------------------------------------------------
/*!
 * \brief Handle MultiPolygon
 */
// ----------------------------------------------------------------------

OGRMultiPolygon *normalize_winding(const OGRMultiPolygon *theGeom)
{
  if (theGeom == nullptr || theGeom->IsEmpty() != 0)
    return nullptr;

  auto *out = new OGRMultiPolygon();

  for (int i = 0, n = theGeom->getNumGeometries(); i < n; ++i)
  {
    auto *geom = normalize_winding(dynamic_cast<const OGRPolygon *>(theGeom->getGeometryRef(i)));
    if (geom != nullptr)
      out->addGeometryDirectly(geom);
  }
  return out;
}

// ----------------------------------------------------------------------
/*!
 * \brief Handle GeometryCollection
 */
// ----------------------------------------------------------------------

OGRGeometryCollection *normalize_winding(const OGRGeometryCollection *theGeom)
{
  if (theGeom == nullptr || theGeom->IsEmpty() != 0)
    return nullptr;

  auto *out = new OGRGeometryCollection();

  for (int i = 0, n = theGeom->getNumGeometries(); i < n; ++i)
  {
    auto *geom = normalize_winding(theGeom->getGeometryRef(i));
    if (geom != nullptr)
      out->addGeometryDirectly(geom);
  }
  return out;
}

// ----------------------------------------------------------------------
/*!
 * \brief Normalize winding of the given geometry
 */
// ----------------------------------------------------------------------

OGRGeometry *normalize_winding(const OGRGeometry *theGeom)
{
  if (theGeom == nullptr)
    return nullptr;

  OGRwkbGeometryType id = theGeom->getGeometryType();

  switch (id)
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
      throw std::runtime_error(
          "Encountered a 'none' geometry component while changing winding order of an OGR "
          "geometry");
    default:
      throw std::runtime_error(
          "Encountered an unknown geometry component while changing winding order of an OGR "
          "geometry");
  }

  // NOT REACHED
  return nullptr;
}

}  // namespace

// ----------------------------------------------------------------------
/*!
 * \brief Normalize the winding order
 */
// ----------------------------------------------------------------------

OGRGeometry *Fmi::OGR::normalizeWindingOrder(const OGRGeometry &theGeom)
{
  auto *geom = normalize_winding(&theGeom);

  if (geom != nullptr)
    geom->assignSpatialReference(theGeom.getSpatialReference());  // SR is ref. counter

  return geom;
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
  if (theRing.IsEmpty() != 0)
    return;

  // Find the "smallest" coordinate

  int best_pos = 0;
  int n = theRing.getNumPoints();
  for (int pos = 0; pos < n; ++pos)
  {
    if (theRing.getX(pos) < theRing.getX(best_pos))
      best_pos = pos;
    else if (theRing.getX(pos) == theRing.getX(best_pos) &&
             theRing.getY(pos) < theRing.getY(best_pos))
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

void Fmi::OGR::normalize(OGRPolygon &thePoly)
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
