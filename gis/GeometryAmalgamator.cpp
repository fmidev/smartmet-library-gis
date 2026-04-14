#include "GeometryAmalgamator.h"
#include "VertexCounter.h"
#include <macgyver/Exception.h>
#include <macgyver/Hash.h>
#include <cmath>
#include <ogr_geometry.h>
#include <unordered_map>
#include <vector>

// CDT is header-only; include only from the .cpp to avoid dependency leakage
#include "CDT/CDT.h"

namespace Fmi
{

namespace
{

// Vertex deduplication map using the same hash/equality as VertexCounter
using VertexMap = std::unordered_map<OGRPoint, CDT::VertInd, OGRPointHash, OGRPointEqual>;

// Add a vertex to the deduplication map and CDT vertex list, returning its index
CDT::VertInd add_vertex(const OGRPoint& pt,
                        VertexMap& vertex_map,
                        std::vector<CDT::V2d<double>>& vertices)
{
  auto it = vertex_map.find(pt);
  if (it != vertex_map.end())
    return it->second;

  auto idx = static_cast<CDT::VertInd>(vertices.size());
  vertices.push_back(CDT::V2d<double>(pt.getX(), pt.getY()));
  vertex_map.insert({pt, idx});
  return idx;
}

// Extract vertices and constraint edges from a ring, densifying edges longer than maxLen
void extract_ring(const OGRLineString* ring,
                  double maxLen,
                  VertexMap& vertex_map,
                  std::vector<CDT::V2d<double>>& vertices,
                  std::vector<CDT::Edge>& edges)
{
  if (ring == nullptr)
    return;

  const int n = ring->getNumPoints();
  if (n < 4)  // minimum valid ring: 3 unique + closing duplicate
    return;

  // Process edges. For closed rings, last point == first point, so iterate n-1 edges.
  OGRPoint pt1;
  OGRPoint pt2;
  ring->getPoint(0, &pt1);
  auto prev_idx = add_vertex(pt1, vertex_map, vertices);

  for (int i = 1; i < n; i++)
  {
    ring->getPoint(i, &pt2);

    // Densify: if the edge is longer than maxLen, add intermediate points
    double len = std::hypot(pt2.getX() - pt1.getX(), pt2.getY() - pt1.getY());
    if (maxLen > 0 && len > maxLen)
    {
      int segments = static_cast<int>(std::ceil(len / maxLen));
      for (int s = 1; s < segments; s++)
      {
        double t = static_cast<double>(s) / segments;
        OGRPoint mid(pt1.getX() + t * (pt2.getX() - pt1.getX()),
                     pt1.getY() + t * (pt2.getY() - pt1.getY()));
        auto mid_idx = add_vertex(mid, vertex_map, vertices);
        if (prev_idx != mid_idx)
          edges.push_back(CDT::Edge(prev_idx, mid_idx));
        prev_idx = mid_idx;
      }
    }

    auto cur_idx = add_vertex(pt2, vertex_map, vertices);
    if (prev_idx != cur_idx)
      edges.push_back(CDT::Edge(prev_idx, cur_idx));
    prev_idx = cur_idx;
    pt1 = pt2;
  }
}

// Extract vertices and edges from a polygon (exterior + holes)
void extract_polygon(const OGRPolygon* poly,
                     double maxLen,
                     VertexMap& vertex_map,
                     std::vector<CDT::V2d<double>>& vertices,
                     std::vector<CDT::Edge>& edges)
{
  if (poly == nullptr)
    return;

  extract_ring(poly->getExteriorRing(), maxLen, vertex_map, vertices, edges);

  for (int i = 0, n = poly->getNumInteriorRings(); i < n; ++i)
    extract_ring(poly->getInteriorRing(i), maxLen, vertex_map, vertices, edges);
}

// Extract from any geometry type (recurses into collections)
void extract_geometry(const OGRGeometry* geom,
                      double maxLen,
                      VertexMap& vertex_map,
                      std::vector<CDT::V2d<double>>& vertices,
                      std::vector<CDT::Edge>& edges)
{
  if (geom == nullptr)
    return;

  switch (geom->getGeometryType())
  {
    case wkbPolygon:
      extract_polygon(dynamic_cast<const OGRPolygon*>(geom), maxLen, vertex_map, vertices, edges);
      break;
    case wkbMultiPolygon:
    {
      const auto* mp = dynamic_cast<const OGRMultiPolygon*>(geom);
      for (int i = 0, n = mp->getNumGeometries(); i < n; ++i)
        extract_geometry(mp->getGeometryRef(i), maxLen, vertex_map, vertices, edges);
      break;
    }
    case wkbGeometryCollection:
    {
      const auto* gc = dynamic_cast<const OGRGeometryCollection*>(geom);
      for (int i = 0, n = gc->getNumGeometries(); i < n; ++i)
        extract_geometry(gc->getGeometryRef(i), maxLen, vertex_map, vertices, edges);
      break;
    }
    default:
      break;  // skip non-polygon types
  }
}

// Euclidean distance between two CDT vertices
double edge_length(const CDT::V2d<double>& a, const CDT::V2d<double>& b)
{
  return std::hypot(b.x - a.x, b.y - a.y);
}

// Decompose a geometry into individual polygons, filtering by area
void decompose(const OGRGeometry* geom, double area_limit, std::vector<OGRGeometryPtr>& result)
{
  if (geom == nullptr)
    return;

  switch (geom->getGeometryType())
  {
    case wkbPolygon:
    {
      if (area_limit <= 0 || geom->toPolygon()->get_Area() >= area_limit)
        result.push_back(OGRGeometryPtr(geom->clone()));
      break;
    }
    case wkbMultiPolygon:
    {
      const auto* mp = geom->toMultiPolygon();
      for (int i = 0, n = mp->getNumGeometries(); i < n; ++i)
        decompose(mp->getGeometryRef(i), area_limit, result);
      break;
    }
    case wkbGeometryCollection:
    {
      const auto* gc = dynamic_cast<const OGRGeometryCollection*>(geom);
      for (int i = 0, n = gc->getNumGeometries(); i < n; ++i)
        decompose(gc->getGeometryRef(i), area_limit, result);
      break;
    }
    default:
      break;
  }
}

}  // namespace

void GeometryAmalgamator::apply(std::vector<OGRGeometryPtr>& geoms) const
{
  try
  {
    if (m_lengthLimit <= 0 && m_areaLimit <= 0)
      return;

    // If only area filtering, no amalgamation needed
    if (m_lengthLimit <= 0)
    {
      std::vector<OGRGeometryPtr> result;
      for (const auto& geom : geoms)
        if (geom && !geom->IsEmpty())
          decompose(geom.get(), m_areaLimit, result);
      geoms = std::move(result);
      return;
    }

    // Step 1: Extract all vertices and constraint edges
    VertexMap vertex_map;
    std::vector<CDT::V2d<double>> vertices;
    std::vector<CDT::Edge> edges;

    // Densify polygon edges to half the length limit so that the Delaunay triangulation
    // produces small triangles. This ensures gap triangle edges (including diagonals across
    // the gap) stay within the length limit check.
    const double maxEdgeLen = m_lengthLimit / 2.0;
    for (const auto& geom : geoms)
      if (geom && !geom->IsEmpty())
        extract_geometry(geom.get(), maxEdgeLen, vertex_map, vertices, edges);

    if (vertices.size() < 3)
    {
      geoms.clear();
      return;
    }

    // Step 2: Constrained Delaunay triangulation
    // CDT places super-triangle vertices at indices 0,1,2; user vertices start at index 3.
    CDT::Triangulation<double> cdt;
    cdt.insertVertices(vertices);
    cdt.insertEdges(edges);

    auto depths = cdt.calculateTriangleDepths();

    // Step 3: Classify triangles and build accepted triangle polygons
    auto* collection = new OGRGeometryCollection();

    for (std::size_t i = 0; i < cdt.triangles.size(); i++)
    {
      const auto& tri = cdt.triangles[i];

      // Skip super-triangle-adjacent triangles (super-tri vertices are at indices 0, 1, 2)
      if (tri.vertices[0] < 3 || tri.vertices[1] < 3 || tri.vertices[2] < 3)
        continue;

      bool accept = false;
      auto depth = depths[i];

      if (depth % 2 == 1)
      {
        // Odd depth: inside a polygon
        accept = true;
      }
      else if (depth == 0)
      {
        // Gap triangle: accept if all edges are short enough.
        // Constraint edges are short due to densification, so checking all edges
        // works the same way as in the shapetools amalgamate algorithm.
        const auto& v0 = cdt.vertices[tri.vertices[0]];
        const auto& v1 = cdt.vertices[tri.vertices[1]];
        const auto& v2 = cdt.vertices[tri.vertices[2]];

        accept = (edge_length(v0, v1) <= m_lengthLimit && edge_length(v1, v2) <= m_lengthLimit &&
                  edge_length(v2, v0) <= m_lengthLimit);
      }
      // depth even > 0: hole interior, reject

      if (accept)
      {
        const auto& v0 = cdt.vertices[tri.vertices[0]];
        const auto& v1 = cdt.vertices[tri.vertices[1]];
        const auto& v2 = cdt.vertices[tri.vertices[2]];

        auto* ring = new OGRLinearRing();
        ring->addPoint(v0.x, v0.y);
        ring->addPoint(v1.x, v1.y);
        ring->addPoint(v2.x, v2.y);
        ring->closeRings();

        auto* poly = new OGRPolygon();
        poly->addRingDirectly(ring);
        collection->addGeometryDirectly(poly);
      }
    }

    // Step 4: Union all accepted triangles
    OGRGeometryPtr unified(collection->UnaryUnion());
    delete collection;

    if (!unified || unified->IsEmpty())
    {
      geoms.clear();
      return;
    }

    // Step 5: Decompose into individual polygons and filter by area
    std::vector<OGRGeometryPtr> result;
    decompose(unified.get(), m_areaLimit, result);
    geoms = std::move(result);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

std::size_t GeometryAmalgamator::hash_value() const
{
  try
  {
    auto hash = Fmi::hash_value(m_lengthLimit);
    Fmi::hash_combine(hash, Fmi::hash_value(m_areaLimit));
    return hash;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

}  // namespace Fmi
