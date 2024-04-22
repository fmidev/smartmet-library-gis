#include "VertexCounter.h"
#include <macgyver/Exception.h>

namespace Fmi
{

void VertexCounter::processGeometry(const OGRGeometry* geometry)
{
  if (geometry == nullptr)
    return;

  switch (geometry->getGeometryType())
  {
    case wkbMultiPoint:
    {
      const auto* multiPoint = dynamic_cast<const OGRMultiPoint*>(geometry);
      // Iterate through each point in the OGRMultiPoint
      for (int i = 0; i < multiPoint->getNumGeometries(); ++i)
      {
        const auto* point = multiPoint->getGeometryRef(i);
        // Increment the count for the OGRPoint
        m_counts[*point]++;
      }
      break;
    }
    case wkbGeometryCollection:
    {
      const auto* geometryCollection = dynamic_cast<const OGRGeometryCollection*>(geometry);
      // Iterate through each geometry in the collection
      for (int i = 0; i < geometryCollection->getNumGeometries(); ++i)
      {
        const auto* subGeometry = geometryCollection->getGeometryRef(i);
        processGeometry(subGeometry);  // Recursive call for each sub-geometry
      }
      break;
    }
    case wkbPolygon:
    {
      const auto* polygon = dynamic_cast<const OGRPolygon*>(geometry);
      // Process the exterior ring of the polygon
      processGeometry(polygon->getExteriorRing());

      // Process each interior ring (hole) of the polygon
      for (int i = 0; i < polygon->getNumInteriorRings(); ++i)
        processGeometry(polygon->getInteriorRing(i));
      break;
    }
    case wkbMultiPolygon:
    {
      const auto* multiPolygon = dynamic_cast<const OGRMultiPolygon*>(geometry);
      // Iterate through each polygon in the OGRMultiPolygon
      for (int i = 0; i < multiPolygon->getNumGeometries(); ++i)
      {
        const auto* subGeometry = multiPolygon->getGeometryRef(i);
        processGeometry(subGeometry);  // Recursive call for each sub-geometry
      }
      break;
    }
    case wkbMultiLineString:
    {
      const auto* multiLineString = dynamic_cast<const OGRMultiLineString*>(geometry);
      // Iterate through each line in the OGRMultiLineString
      for (int i = 0; i < multiLineString->getNumGeometries(); ++i)
      {
        const OGRGeometry* subGeometry = multiLineString->getGeometryRef(i);
        processGeometry(subGeometry);  // Recursive call for each sub-geometry
      }
      break;
    }
    case wkbLineString:
    {
      const auto* lineString = dynamic_cast<const OGRLineString*>(geometry);
      // Process each unique point in the OGRLineString
      int numPoints = lineString->getNumPoints();
      int startPos = lineString->get_IsClosed() ? 1 : 0;
      OGRPoint point;
      for (int i = startPos; i < numPoints; ++i)
      {
        lineString->getPoint(i, &point);
        m_counts[point]++;
      }
      break;
    }
    case wkbLinearRing:
    {
      const auto* linearRing = dynamic_cast<const OGRLinearRing*>(geometry);
      // Process each unique point in the OGRLinearRing
      int numPoints = linearRing->getNumPoints();
      OGRPoint point;
      for (int i = 1; i < numPoints; ++i)
      {
        linearRing->getPoint(i, &point);
        m_counts[point]++;
      }
      break;
    }
    default:
      throw Fmi::Exception(BCP, "Unsupported geometry type passed to VertexCounter");
  }
}

void VertexCounter::add(const OGRGeometry* geometry)
{
  processGeometry(geometry);
}

int VertexCounter::getCount(const OGRPoint& point) const
{
  if (m_counts.empty())
    return 0;
  auto it = m_counts.find(point);
  if (it != m_counts.end())
    return it->second;

  return 0;
}

}  // namespace Fmi
