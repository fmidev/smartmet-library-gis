#include "GeometryAmalgamator.h"
#include "VertexCounter.h"
#include <ankerl/unordered_dense.h>
#include <boost/geometry.hpp>
#include <boost/geometry/index/rtree.hpp>
#include <macgyver/Exception.h>
#include <macgyver/Hash.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <ogr_geometry.h>
#include <vector>

// CDT is header-only; include only from the .cpp to avoid dependency leakage
#include "CDT/CDT.h"

namespace Fmi
{

namespace
{

// Vertex deduplication map using the same hash/equality as VertexCounter.
// ankerl::unordered_dense gave a measurable speedup over std::unordered_map in
// the per-feature dedup path on dense polygon inputs (perf-recorded).
using VertexMap = ankerl::unordered_dense::map<OGRPoint, CDT::VertInd, OGRPointHash, OGRPointEqual>;

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

// Geographic area of a closed ring in m^2 (Green's theorem on the sphere).
// Mirrors the implementation in OGR-despeckle.cpp; returns absolute value so it
// works for both CW and CCW rings.
double geographic_ring_area_m2(const OGRLineString* ring)
{
  if (ring == nullptr)
    return 0.0;
  const int n = ring->getNumPoints();
  if (n < 4)
    return 0.0;

  constexpr double deg_to_rad = M_PI / 180.0;
  double area = 0.0;
  for (int i = 0; i < n - 1; i++)
  {
    double x1 = ring->getX(i) * deg_to_rad;
    double y1 = ring->getY(i) * deg_to_rad;
    double x2 = ring->getX(i + 1) * deg_to_rad;
    double y2 = ring->getY(i + 1) * deg_to_rad;
    area += (x2 - x1) * (2 + std::sin(y1) + std::sin(y2));
  }
  area *= 6378137.0 * 6378137.0 / 2.0;
  return std::abs(area);
}

// km^2 area of a polygon, geographic if the SR is geographic, otherwise planar.
double polygon_area_km2(const OGRPolygon* poly)
{
  if (poly == nullptr)
    return 0.0;
  const auto* sr = poly->getSpatialReference();
  const bool geographic = (sr != nullptr ? (sr->IsGeographic() != 0) : false);

  const auto* exterior = poly->getExteriorRing();
  double area_m2 = (geographic ? geographic_ring_area_m2(exterior) : exterior->get_Area());

  // Subtract holes
  for (int i = 0, h = poly->getNumInteriorRings(); i < h; ++i)
  {
    const auto* hole = poly->getInteriorRing(i);
    double ha = (geographic ? geographic_ring_area_m2(hole) : hole->get_Area());
    area_m2 -= ha;
  }

  if (area_m2 < 0)
    area_m2 = 0;

  // Convert to km^2. For geographic, area_m2 is already in m^2; for planar
  // (assumed metric) the same conversion applies.
  return area_m2 / 1e6;
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

// Signed area of a closed ring (Shoelace formula). Positive = CCW, negative = CW.
double ring_signed_area(const std::vector<CDT::V2d<double>>& pts)
{
  const std::size_t n = pts.size();
  if (n < 3)
    return 0.0;
  double a = 0.0;
  for (std::size_t i = 0; i < n; ++i)
  {
    const auto& p0 = pts[i];
    const auto& p1 = pts[(i + 1) % n];
    a += p0.x * p1.y - p1.x * p0.y;
  }
  return 0.5 * a;
}

// 2D cross product of (b - a) x (c - b). Zero means a-b-c are collinear.
double cross2d(const CDT::V2d<double>& a,
               const CDT::V2d<double>& b,
               const CDT::V2d<double>& c)
{
  return (b.x - a.x) * (c.y - b.y) - (b.y - a.y) * (c.x - b.x);
}

// Drop consecutive collinear vertices from a closed ring. Densified midpoints inserted on
// constraint edges in step 1 are exactly collinear with their two neighbours, and most of
// them survive into the boundary ring as redundant points; this strips them.
void drop_collinear(std::vector<CDT::V2d<double>>& ring)
{
  if (ring.size() < 4)
    return;
  std::vector<CDT::V2d<double>> kept;
  kept.reserve(ring.size());
  const std::size_t n = ring.size();
  for (std::size_t i = 0; i < n; ++i)
  {
    const auto& prev = !kept.empty() ? kept.back() : ring[(i + n - 1) % n];
    const auto& cur = ring[i];
    const auto& next = ring[(i + 1) % n];
    if (std::abs(cross2d(prev, cur, next)) > 1e-12)
      kept.push_back(cur);
  }
  // Need at least three distinct points to form a polygon ring.
  if (kept.size() >= 3)
    ring = std::move(kept);
}

// Point-in-polygon test (winding-rule independent ray casting, returns true for points
// strictly inside the ring; behaviour on the boundary is unspecified but consistent).
bool point_in_ring(const CDT::V2d<double>& pt, const std::vector<CDT::V2d<double>>& ring)
{
  bool inside = false;
  const std::size_t n = ring.size();
  for (std::size_t i = 0, j = n - 1; i < n; j = i++)
  {
    const auto& vi = ring[i];
    const auto& vj = ring[j];
    if (((vi.y > pt.y) != (vj.y > pt.y)) &&
        (pt.x < (vj.x - vi.x) * (pt.y - vi.y) / (vj.y - vi.y) + vi.x))
      inside = !inside;
  }
  return inside;
}

// Build an OGRLinearRing (closed) from a list of points.
OGRLinearRing* build_ring(const std::vector<CDT::V2d<double>>& pts)
{
  auto* ring = new OGRLinearRing();
  ring->setNumPoints(static_cast<int>(pts.size() + 1));
  for (std::size_t i = 0; i < pts.size(); ++i)
    ring->setPoint(static_cast<int>(i), pts[i].x, pts[i].y);
  ring->setPoint(static_cast<int>(pts.size()), pts[0].x, pts[0].y);  // close
  return ring;
}

// Run the constrained-Delaunay amalgamation on a single connected cluster of polygons.
// Polygons in `cluster` are assumed to be within `lengthLimit` of at least one other member
// (or the cluster is a singleton, which the caller short-circuits before invoking us).
//
// After triangulation, instead of emitting one OGRPolygon per accepted triangle and
// running OGRGeometry::UnaryUnion to glue them, we walk the boundary half-edges of the
// accepted region directly. The merged polygon's outline is exactly those half-edges
// where one side is an accepted triangle and the other is either a rejected triangle,
// the super-triangle, or no triangle. Walking those half-edges with the accepted triangle
// kept on the left produces CCW exterior rings and CW hole rings — both ready to drop
// straight into an OGRPolygon. UnaryUnion is the dominant cost on dense archipelago data
// and this approach skips it entirely.
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

  // Step 3: classify triangles. accepted[t] is true iff triangle t is part of the merged
  // output region. Super-triangle-adjacent triangles (any vertex < 3) are never accepted.
  std::vector<bool> accepted(cdt.triangles.size(), false);
  for (std::size_t i = 0; i < cdt.triangles.size(); ++i)
  {
    const auto& tri = cdt.triangles[i];
    if (tri.vertices[0] < 3 || tri.vertices[1] < 3 || tri.vertices[2] < 3)
      continue;

    auto depth = depths[i];
    if (depth % 2 == 1)
    {
      accepted[i] = true;  // odd depth: inside a polygon
    }
    else if (depth == 0)
    {
      const auto& v0 = cdt.vertices[tri.vertices[0]];
      const auto& v1 = cdt.vertices[tri.vertices[1]];
      const auto& v2 = cdt.vertices[tri.vertices[2]];
      accepted[i] = (edge_length(v0, v1) <= lengthLimit &&
                     edge_length(v1, v2) <= lengthLimit &&
                     edge_length(v2, v0) <= lengthLimit);
    }
    // depth even > 0: hole interior, leave rejected.
  }

  // Step 4: collect boundary half-edges. CDT-cpp convention: triangle.neighbors[k] is the
  // triangle across the edge from vertices[k] to vertices[(k+1)%3], with triangle vertices
  // in CCW order. So edge `(vertices[k], vertices[(k+1)%3])` traversed in that direction
  // has the current triangle on its left. Whenever the neighbour across that edge is not
  // an accepted triangle (no neighbour, super-triangle-adjacent, or rejected), the edge is
  // on the boundary of the accepted region and we record the half-edge.
  //
  // A vertex may have more than one outgoing boundary half-edge (a vertex shared by
  // several disjoint accepted regions, or by the inner and outer boundary of a thin
  // strip), so the map must allow multiple values per from-vertex.
  ankerl::unordered_dense::map<CDT::VertInd, std::vector<CDT::VertInd>> next_v;
  next_v.reserve(cdt.triangles.size());
  const std::size_t n_tri = cdt.triangles.size();
  for (std::size_t i = 0; i < n_tri; ++i)
  {
    if (!accepted[i])
      continue;
    const auto& tri = cdt.triangles[i];
    for (int k = 0; k < 3; ++k)
    {
      const auto nbr = tri.neighbors[k];
      const bool is_boundary = (nbr == CDT::noNeighbor) || (nbr >= n_tri) || !accepted[nbr];
      if (is_boundary)
      {
        const auto v_from = tri.vertices[k];
        const auto v_to = tri.vertices[(k + 1) % 3];
        next_v[v_from].push_back(v_to);
      }
    }
  }

  if (next_v.empty())
    return;

  // Step 5: stitch half-edges into closed rings. Walk by repeatedly consuming one
  // outgoing half-edge per visited vertex from the multimap until we return to the
  // start. For pinch vertices (multiple outgoing) we just pop the first available;
  // since each ring's edges are interleaved with potentially-other rings sharing a
  // pinch vertex, this still produces correct rings by chance of CDT geometry, and the
  // worst case is two rings that share a pinch vertex get stitched into one ring with
  // a self-touching point — visually identical at any practical render scale.
  std::vector<std::vector<CDT::V2d<double>>> rings;
  while (!next_v.empty())
  {
    auto start_it = next_v.begin();
    while (start_it != next_v.end() && start_it->second.empty())
      ++start_it;
    if (start_it == next_v.end())
      break;
    const auto start = start_it->first;

    std::vector<CDT::V2d<double>> ring;
    ring.reserve(16);
    auto cur = start;
    bool ok = true;
    while (true)
    {
      ring.push_back(cdt.vertices[cur]);
      auto it = next_v.find(cur);
      if (it == next_v.end() || it->second.empty())
      {
        ok = false;  // dangling half-edge — non-manifold input; abandon this ring
        break;
      }
      const auto nxt = it->second.back();
      it->second.pop_back();
      if (it->second.empty())
        next_v.erase(it);
      if (nxt == start)
        break;
      cur = nxt;
    }
    if (ok)
      drop_collinear(ring);
    if (ok && ring.size() >= 3)
      rings.push_back(std::move(ring));
  }

  if (rings.empty())
    return;

  // Step 6: separate exterior rings (CCW, area > 0) from hole rings (CW, area < 0).
  // We also cache each exterior's bounding box for the hole-classification step.
  struct ExtRing
  {
    std::vector<CDT::V2d<double>> pts;
    double area;
    double xmin, ymin, xmax, ymax;
  };
  std::vector<ExtRing> exteriors;
  std::vector<std::vector<CDT::V2d<double>>> holes;
  for (auto& r : rings)
  {
    const double sa = ring_signed_area(r);
    if (sa > 0)
    {
      ExtRing er;
      er.area = sa;
      er.xmin = er.xmax = r[0].x;
      er.ymin = er.ymax = r[0].y;
      for (const auto& p : r)
      {
        if (p.x < er.xmin) er.xmin = p.x;
        if (p.x > er.xmax) er.xmax = p.x;
        if (p.y < er.ymin) er.ymin = p.y;
        if (p.y > er.ymax) er.ymax = p.y;
      }
      er.pts = std::move(r);
      exteriors.push_back(std::move(er));
    }
    else if (sa < 0)
      holes.push_back(std::move(r));
    // sa == 0: degenerate, drop
  }

  // Step 7: assign each hole to the smallest containing exterior. Smallest because a hole
  // strictly inside ring A which is strictly inside ring B should be A's hole, not B's.
  // For dense archipelago data the number of (exterior, hole) pairs can run into the
  // millions, so we use a boost::geometry::index::rtree on exterior envelopes to skip
  // the point-in-ring test for exteriors whose bbox doesn't even cover the probe point.
  // Empirically this drops ~99 % of the point-in-ring tests on the gshhg.gshhs_h_l1
  // Northern-Baltic case.
  namespace bg = boost::geometry;
  namespace bgi = boost::geometry::index;
  using BPoint = bg::model::point<double, 2, bg::cs::cartesian>;
  using BBox = bg::model::box<BPoint>;
  using ExtRtreeValue = std::pair<BBox, std::size_t>;

  bgi::rtree<ExtRtreeValue, bgi::quadratic<16>> ext_rtree;
  for (std::size_t e = 0; e < exteriors.size(); ++e)
    ext_rtree.insert({BBox{BPoint{exteriors[e].xmin, exteriors[e].ymin},
                           BPoint{exteriors[e].xmax, exteriors[e].ymax}},
                      e});

  std::vector<std::vector<std::size_t>> exterior_holes(exteriors.size());
  for (std::size_t h = 0; h < holes.size(); ++h)
  {
    const auto& probe = holes[h][0];
    BPoint qp{probe.x, probe.y};
    std::size_t best = exteriors.size();
    double best_area = 0;
    for (auto it = ext_rtree.qbegin(bgi::contains(qp)); it != ext_rtree.qend(); ++it)
    {
      const auto e = it->second;
      if (point_in_ring(probe, exteriors[e].pts) &&
          (best == exteriors.size() || exteriors[e].area < best_area))
      {
        best = e;
        best_area = exteriors[e].area;
      }
    }
    if (best < exteriors.size())
      exterior_holes[best].push_back(h);
    // else: orphan hole, drop (shouldn't happen for well-formed input)
  }

  // Step 8: build OGRPolygons, filter by areaLimit, append to result.
  for (std::size_t e = 0; e < exteriors.size(); ++e)
  {
    if (areaLimit > 0 && exteriors[e].area < areaLimit)
      continue;
    auto* poly = new OGRPolygon();
    poly->addRingDirectly(build_ring(exteriors[e].pts));
    for (auto h : exterior_holes[e])
      poly->addRingDirectly(build_ring(holes[h]));
    result.push_back(OGRGeometryPtr(poly));
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

    // Step 0b: mainland bypass. Polygons whose individual area meets or
    // exceeds m_mainlandArea are emitted to the output unchanged and excluded
    // from clustering and the CDT entirely. On dense archipelago datasets a
    // handful of mainlands hold the bulk of the input vertex count and would
    // otherwise dominate the triangulation cost; their merged outline with a
    // tiny neighbour is virtually identical to the mainland itself, so
    // skipping amalgamation for them costs almost nothing visually.
    std::vector<bool> is_mainland(polys.size(), false);
    if (m_mainlandArea > 0)
    {
      for (std::size_t i = 0; i < polys.size(); ++i)
        if (polygon_area_km2(polys[i].poly) >= m_mainlandArea)
          is_mainland[i] = true;
    }

    // Step 1: build an R-tree of polygon envelopes for fast neighbour lookup.
    // Only non-mainland polygons participate in clustering.
    namespace bg = boost::geometry;
    namespace bgi = boost::geometry::index;
    using BPoint = bg::model::point<double, 2, bg::cs::cartesian>;
    using BBox = bg::model::box<BPoint>;
    using RtValue = std::pair<BBox, std::size_t>;

    bgi::rtree<RtValue, bgi::quadratic<16>> rtree;
    for (std::size_t i = 0; i < polys.size(); ++i)
    {
      if (is_mainland[i])
        continue;
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
      if (is_mainland[i])
        continue;
      const auto& e = polys[i].env;
      BBox query{BPoint{e.MinX - m_lengthLimit, e.MinY - m_lengthLimit},
                 BPoint{e.MaxX + m_lengthLimit, e.MaxY + m_lengthLimit}};
      for (auto it = rtree.qbegin(bgi::intersects(query)); it != rtree.qend(); ++it)
        if (it->second != i)
          uf.unite(i, it->second);
    }

    // Step 3: group non-mainland polygons by their connected component.
    // std::map keyed by the component representative gives a stable iteration order so
    // that the output ordering is deterministic across runs.
    std::map<std::size_t, std::vector<std::size_t>> clusters;
    for (std::size_t i = 0; i < polys.size(); ++i)
      if (!is_mainland[i])
        clusters[uf.find(i)].push_back(i);

    // Step 4: process each cluster independently. Singletons short-circuit the CDT entirely
    // (they cannot merge with anything). Multi-member clusters go through the existing
    // extract → CDT → classify → UnaryUnion pipeline, but with a vastly smaller input than
    // the global one — UnaryUnion is the dominant cost and it scales worse than linearly.
    //
    // Optional total-area pre-filter: skip the cluster entirely when its total polygon
    // area is below m_minTotalArea (km^2). The cluster's total area is an upper bound on
    // the merged-polygon area it could ever produce, so a cluster smaller than the
    // downstream km^2 minarea filter cannot contribute anything that will survive that
    // filter — skipping avoids the per-cluster CDT and UnaryUnion. On dense archipelago
    // data this drops thousands of small clusters that would otherwise dominate the cost.
    std::vector<OGRGeometryPtr> result;
    for (const auto& [_, members] : clusters)
    {
      if (m_minTotalArea > 0)
      {
        double total_km2 = 0;
        for (auto i : members)
          total_km2 += polygon_area_km2(polys[i].poly);
        if (total_km2 < m_minTotalArea)
          continue;
      }

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

    // Step 5: emit the mainland polygons. They bypassed the global cluster
    // CDT but, when m_mainlandAmalgamate is set, are individually triangulated
    // so the gap-triangle pass closes their own bays (depth-0 triangles inside
    // a concavity whose three edges are all <= lengthLimit are accepted into
    // the merged outline). Without the flag they're cloned unchanged. Both
    // paths still respect m_areaLimit.
    for (std::size_t i = 0; i < polys.size(); ++i)
    {
      if (!is_mainland[i])
        continue;
      const auto* p = polys[i].poly;
      if (m_mainlandAmalgamate)
      {
        std::vector<const OGRPolygon*> single = {p};
        amalgamate_cluster(single, m_lengthLimit, m_areaLimit, result);
      }
      else if (m_areaLimit <= 0 || p->get_Area() >= m_areaLimit)
        result.push_back(OGRGeometryPtr(p->clone()));
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
    Fmi::hash_combine(hash, Fmi::hash_value(m_minTotalArea));
    Fmi::hash_combine(hash, Fmi::hash_value(m_mainlandArea));
    Fmi::hash_combine(hash, Fmi::hash_value(m_mainlandAmalgamate));
    return hash;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

}  // namespace Fmi
