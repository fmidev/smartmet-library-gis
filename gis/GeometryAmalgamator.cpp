#include "GeometryAmalgamator.h"
#include "VertexCounter.h"
#include <boost/geometry.hpp>
#include <boost/geometry/index/rtree.hpp>
#include <macgyver/Exception.h>
#include <macgyver/Hash.h>
#include <algorithm>
#include <cmath>
#include <map>
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

// Flatten any OGR geometry containing polygons into a list of OGRPolygon* with cached envelopes.
struct FlatPoly
{
  const OGRPolygon* poly;
  OGREnvelope env;
};

void flatten(const OGRGeometry* g, std::vector<FlatPoly>& out)
{
  if (g == nullptr)
    return;

  switch (g->getGeometryType())
  {
    case wkbPolygon:
    {
      const auto* p = dynamic_cast<const OGRPolygon*>(g);
      if (p != nullptr && p->IsEmpty() == 0)
      {
        FlatPoly fp;
        fp.poly = p;
        p->getEnvelope(&fp.env);
        out.push_back(fp);
      }
      break;
    }
    case wkbMultiPolygon:
    {
      const auto* mp = dynamic_cast<const OGRMultiPolygon*>(g);
      for (int i = 0, n = mp->getNumGeometries(); i < n; ++i)
        flatten(mp->getGeometryRef(i), out);
      break;
    }
    case wkbGeometryCollection:
    {
      const auto* gc = dynamic_cast<const OGRGeometryCollection*>(g);
      for (int i = 0, n = gc->getNumGeometries(); i < n; ++i)
        flatten(gc->getGeometryRef(i), out);
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

// Lightweight union-find with path compression and union by rank.
class UnionFind
{
 public:
  explicit UnionFind(std::size_t n) : m_parent(n), m_rank(n, 0)
  {
    for (std::size_t i = 0; i < n; ++i)
      m_parent[i] = i;
  }

  std::size_t find(std::size_t x)
  {
    while (m_parent[x] != x)
    {
      m_parent[x] = m_parent[m_parent[x]];  // halving
      x = m_parent[x];
    }
    return x;
  }

  void unite(std::size_t a, std::size_t b)
  {
    a = find(a);
    b = find(b);
    if (a == b)
      return;
    if (m_rank[a] < m_rank[b])
      std::swap(a, b);
    m_parent[b] = a;
    if (m_rank[a] == m_rank[b])
      ++m_rank[a];
  }

 private:
  std::vector<std::size_t> m_parent;
  std::vector<std::uint8_t> m_rank;
};

// Run the constrained-Delaunay amalgamation on a single connected cluster of polygons.
// Polygons in `cluster` are assumed to be within `lengthLimit` of at least one other member
// (or the cluster is a singleton, which the caller short-circuits before invoking us).
void amalgamate_cluster(const std::vector<const OGRPolygon*>& cluster,
                        double lengthLimit,
                        double areaLimit,
                        std::vector<OGRGeometryPtr>& result)
{
  // Step 1: extract vertices + constraint edges with edge densification
  VertexMap vertex_map;
  std::vector<CDT::V2d<double>> vertices;
  std::vector<CDT::Edge> edges;
  const double maxEdgeLen = lengthLimit / 2.0;
  for (const auto* p : cluster)
    extract_polygon(p, maxEdgeLen, vertex_map, vertices, edges);

  if (vertices.size() < 3)
    return;

  // Step 2: constrained Delaunay triangulation
  CDT::Triangulation<double> cdt;
  cdt.insertVertices(vertices);
  cdt.insertEdges(edges);
  auto depths = cdt.calculateTriangleDepths();

  // Step 3: classify triangles and build accepted-triangle polygons
  auto* collection = new OGRGeometryCollection();

  for (std::size_t i = 0; i < cdt.triangles.size(); i++)
  {
    const auto& tri = cdt.triangles[i];

    // Skip super-triangle-adjacent triangles (super-tri vertices live at indices 0,1,2)
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
      const auto& v0 = cdt.vertices[tri.vertices[0]];
      const auto& v1 = cdt.vertices[tri.vertices[1]];
      const auto& v2 = cdt.vertices[tri.vertices[2]];
      accept = (edge_length(v0, v1) <= lengthLimit && edge_length(v1, v2) <= lengthLimit &&
                edge_length(v2, v0) <= lengthLimit);
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

  // Step 4: union all accepted triangles for this cluster
  OGRGeometryPtr unified(collection->UnaryUnion());
  delete collection;

  if (!unified || unified->IsEmpty())
    return;

  // Step 5: decompose with the area filter
  decompose(unified.get(), areaLimit, result);
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

    // Step 0: flatten the input down to individual polygons + their envelopes.
    // Clustering happens at this granularity rather than at the input vector
    // entry level because typical callers pass a single MultiPolygon containing
    // every island as a member, not one OGRGeometryPtr per island.
    std::vector<FlatPoly> polys;
    for (const auto& gp : geoms)
      if (gp && !gp->IsEmpty())
        flatten(gp.get(), polys);

    if (polys.empty())
    {
      geoms.clear();
      return;
    }

    // Step 1: build an R-tree of polygon envelopes for fast neighbour lookup.
    namespace bg = boost::geometry;
    namespace bgi = boost::geometry::index;
    using BPoint = bg::model::point<double, 2, bg::cs::cartesian>;
    using BBox = bg::model::box<BPoint>;
    using RtValue = std::pair<BBox, std::size_t>;

    bgi::rtree<RtValue, bgi::quadratic<16>> rtree;
    for (std::size_t i = 0; i < polys.size(); ++i)
    {
      const auto& e = polys[i].env;
      rtree.insert({BBox{BPoint{e.MinX, e.MinY}, BPoint{e.MaxX, e.MaxY}}, i});
    }

    // Step 2: union-find polygons whose envelopes are within lengthLimit of each other.
    // Bbox-distance is conservative: polygons with overlapping expanded envelopes might
    // actually be > lengthLimit apart by their geometries, but we'll then just process
    // them in the same CDT (slower than optimal, still correct). False negatives —
    // missing a real neighbour — are not possible.
    UnionFind uf(polys.size());
    for (std::size_t i = 0; i < polys.size(); ++i)
    {
      const auto& e = polys[i].env;
      BBox query{BPoint{e.MinX - m_lengthLimit, e.MinY - m_lengthLimit},
                 BPoint{e.MaxX + m_lengthLimit, e.MaxY + m_lengthLimit}};
      for (auto it = rtree.qbegin(bgi::intersects(query)); it != rtree.qend(); ++it)
        if (it->second != i)
          uf.unite(i, it->second);
    }

    // Step 3: group polygons by their connected component.
    // std::map keyed by the component representative gives a stable iteration order so
    // that the output ordering is deterministic across runs.
    std::map<std::size_t, std::vector<std::size_t>> clusters;
    for (std::size_t i = 0; i < polys.size(); ++i)
      clusters[uf.find(i)].push_back(i);

    // Step 4: process each cluster independently. Singletons short-circuit the CDT entirely
    // (they cannot merge with anything). Multi-member clusters go through the existing
    // extract → CDT → classify → UnaryUnion pipeline, but with a vastly smaller input than
    // the global one — UnaryUnion is the dominant cost and it scales worse than linearly.
    std::vector<OGRGeometryPtr> result;
    for (const auto& [_, members] : clusters)
    {
      if (members.size() == 1)
      {
        const auto* p = polys[members[0]].poly;
        if (m_areaLimit <= 0 || p->get_Area() >= m_areaLimit)
          result.push_back(OGRGeometryPtr(p->clone()));
      }
      else
      {
        std::vector<const OGRPolygon*> cluster_polys;
        cluster_polys.reserve(members.size());
        for (auto i : members)
          cluster_polys.push_back(polys[i].poly);
        amalgamate_cluster(cluster_polys, m_lengthLimit, m_areaLimit, result);
      }
    }
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
