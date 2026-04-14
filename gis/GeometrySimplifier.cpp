#include "GeometrySimplifier.h"
#include "Box.h"
#include "VertexCounter.h"
#include <macgyver/Exception.h>
#include <macgyver/Hash.h>
#include <algorithm>
#include <ogr_geometry.h>
#include <queue>
#include <vector>

namespace Fmi
{

namespace
{

// Perpendicular distance from point p to line segment a-b
double perpendicular_distance(const OGRPoint& p, const OGRPoint& a, const OGRPoint& b)
{
  double dx = b.getX() - a.getX();
  double dy = b.getY() - a.getY();
  double len_sq = dx * dx + dy * dy;

  if (len_sq == 0.0)
    return std::hypot(p.getX() - a.getX(), p.getY() - a.getY());

  double t = ((p.getX() - a.getX()) * dx + (p.getY() - a.getY()) * dy) / len_sq;
  t = std::clamp(t, 0.0, 1.0);

  double proj_x = a.getX() + t * dx;
  double proj_y = a.getY() + t * dy;
  return std::hypot(p.getX() - proj_x, p.getY() - proj_y);
}

// Triangle area using the shoelace formula
double triangle_area(const OGRPoint& a, const OGRPoint& b, const OGRPoint& c)
{
  return 0.5 * std::abs((b.getX() - a.getX()) * (c.getY() - a.getY()) -
                        (c.getX() - a.getX()) * (b.getY() - a.getY()));
}

// Compare two OGRPoints lexicographically (x first, then y)
bool point_less(const OGRPoint& a, const OGRPoint& b)
{
  if (a.getX() != b.getX())
    return a.getX() < b.getX();
  return a.getY() < b.getY();
}

// Douglas-Peucker on a segment of points. Marks which points to keep.
void simplify_dp(
    const std::vector<OGRPoint>& pts, int start, int end, double tolerance, std::vector<bool>& keep)
{
  if (end - start < 2)
    return;

  double max_dist = 0;
  int max_idx = start;

  for (int i = start + 1; i < end; i++)
  {
    double d = perpendicular_distance(pts[i], pts[start], pts[end]);
    if (d > max_dist)
    {
      max_dist = d;
      max_idx = i;
    }
  }

  if (max_dist > tolerance)
  {
    keep[max_idx] = true;
    simplify_dp(pts, start, max_idx, tolerance, keep);
    simplify_dp(pts, max_idx, end, tolerance, keep);
  }
}

// Visvalingam-Whyatt on a segment of points. Returns indices to keep.
std::vector<bool> simplify_vw(const std::vector<OGRPoint>& pts, double tolerance)
{
  const int n = pts.size();
  std::vector<bool> keep(n, true);

  if (n < 3)
    return keep;

  // Linked list of active neighbors
  std::vector<int> prev(n);
  std::vector<int> next(n);
  for (int i = 0; i < n; i++)
  {
    prev[i] = i - 1;
    next[i] = i + 1;
  }

  // Current effective areas
  std::vector<double> areas(n, std::numeric_limits<double>::max());
  for (int i = 1; i < n - 1; i++)
    areas[i] = triangle_area(pts[prev[i]], pts[i], pts[next[i]]);

  // Min-heap: (area, index)
  using Entry = std::pair<double, int>;
  std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> heap;

  for (int i = 1; i < n - 1; i++)
    heap.push({areas[i], i});

  double last_removed_area = 0;

  while (!heap.empty())
  {
    auto [area, idx] = heap.top();
    heap.pop();

    if (!keep[idx])
      continue;

    // Stale entry — area was updated after this entry was pushed
    if (area != areas[idx])
      continue;

    if (area >= tolerance)
      break;

    // Remove this vertex
    keep[idx] = false;
    last_removed_area = area;

    // Update neighbors
    int p = prev[idx];
    int nx = next[idx];

    next[p] = nx;
    prev[nx] = p;

    // Update area of previous neighbor (if it's an interior point)
    if (p > 0 && p < n - 1 && keep[p])
    {
      double new_area = triangle_area(pts[prev[p]], pts[p], pts[next[p]]);
      areas[p] = std::max(new_area, last_removed_area);  // monotonicity rule
      heap.push({areas[p], p});
    }

    // Update area of next neighbor (if it's an interior point)
    if (nx > 0 && nx < n - 1 && keep[nx])
    {
      double new_area = triangle_area(pts[prev[nx]], pts[nx], pts[next[nx]]);
      areas[nx] = std::max(new_area, last_removed_area);  // monotonicity rule
      heap.push({areas[nx], nx});
    }
  }

  return keep;
}

// Utility class to handle topology-aware simplification
class LineSimplifier
{
 private:
  VertexCounter m_counter;
  GeometrySimplifier::Type m_type;
  double m_tolerance;
  std::vector<int> m_counts;

 public:
  LineSimplifier(GeometrySimplifier::Type t, double tol) : m_type(t), m_tolerance(tol) {}

  void count(const OGRGeometry* geom) { m_counter.add(geom); }

  void init_counts(const OGRLineString* geom)
  {
    const int n = geom->getNumPoints();
    m_counts.clear();
    m_counts.reserve(n);

    OGRPoint pt;
    for (int i = 0; i < n; i++)
    {
      geom->getPoint(i, &pt);
      m_counts.push_back(m_counter.getCount(pt));
    }
  }

  // An anchor point must not be removed. Inverse of smoother's allowed():
  // count 0 = isoline (counting disabled) -> not an anchor, can remove
  // count 1 = unshared edge -> anchor, must keep
  // count 2 = shared edge -> not an anchor, can remove
  // count 4 = shared corner -> anchor, must keep
  bool is_anchor(int j) const
  {
    auto n = m_counts[j];
    return (n != 0 && n != 2);
  }

  // Simplify a segment of points between two anchors using the chosen algorithm.
  // First and last points are always kept.
  void simplify_segment(std::vector<OGRPoint>& pts, std::vector<bool>& keep) const
  {
    if (pts.size() < 3)
      return;

    // Canonicalize segment direction for deterministic results on shared edges
    bool reversed = point_less(pts.back(), pts.front());
    if (reversed)
      std::reverse(pts.begin(), pts.end());

    if (m_type == GeometrySimplifier::Type::DouglasPeucker)
    {
      std::vector<bool> seg_keep(pts.size(), false);
      seg_keep.front() = true;
      seg_keep.back() = true;
      simplify_dp(pts, 0, static_cast<int>(pts.size()) - 1, m_tolerance, seg_keep);

      if (reversed)
        std::reverse(seg_keep.begin(), seg_keep.end());

      keep.insert(keep.end(), seg_keep.begin(), seg_keep.end());
    }
    else
    {
      auto seg_keep = simplify_vw(pts, m_tolerance);

      if (reversed)
        std::reverse(seg_keep.begin(), seg_keep.end());

      keep.insert(keep.end(), seg_keep.begin(), seg_keep.end());
    }
  }

  // Simplify a linestring, respecting anchor points
  OGRLineString* simplify(const OGRLineString* geom) const
  {
    if (geom == nullptr || geom->IsEmpty() != 0)
      return nullptr;

    const int n = geom->getNumPoints();
    if (n < 3)
      return dynamic_cast<OGRLineString*>(geom->clone());

    const bool closed = geom->get_IsClosed();

    // Build list of all points
    std::vector<OGRPoint> all_pts(n);
    for (int i = 0; i < n; i++)
      geom->getPoint(i, &all_pts[i]);

    // Identify anchor indices. For open linestrings, first and last are always anchors.
    std::vector<bool> anchor(n, false);
    if (!m_counts.empty())
    {
      for (int i = 0; i < n; i++)
        anchor[i] = is_anchor(i);
    }

    if (!closed)
    {
      anchor[0] = true;
      anchor[n - 1] = true;
    }

    // For closed rings: work with indices 0..n-2 (last point is duplicate of first)
    const int effective_n = closed ? n - 1 : n;

    // Collect anchor indices
    std::vector<int> anchor_indices;
    for (int i = 0; i < effective_n; i++)
      if (anchor[i])
        anchor_indices.push_back(i);

    // If no anchors at all (e.g. standalone ring with no topology), simplify the whole thing
    if (anchor_indices.empty())
    {
      std::vector<OGRPoint> pts(all_pts.begin(), all_pts.begin() + effective_n);
      std::vector<bool> keep;

      if (m_type == GeometrySimplifier::Type::DouglasPeucker)
      {
        // Find two most distant points as synthetic anchors
        int best_i = 0;
        int best_j = 0;
        double best_dist = 0;
        for (int i = 0; i < effective_n; i++)
          for (int j = i + 1; j < effective_n; j++)
          {
            double d = std::hypot(pts[i].getX() - pts[j].getX(), pts[i].getY() - pts[j].getY());
            if (d > best_dist)
            {
              best_dist = d;
              best_i = i;
              best_j = j;
            }
          }

        // Simplify two segments: best_i..best_j and best_j..best_i (wrapping)
        keep.assign(effective_n, false);
        keep[best_i] = true;
        keep[best_j] = true;

        // Segment best_i..best_j
        if (best_j - best_i > 1)
        {
          std::vector<OGRPoint> seg(pts.begin() + best_i, pts.begin() + best_j + 1);
          std::vector<bool> seg_keep;

          // Canonicalize
          bool reversed = point_less(seg.back(), seg.front());
          if (reversed)
            std::reverse(seg.begin(), seg.end());

          std::vector<bool> sk(seg.size(), false);
          sk.front() = true;
          sk.back() = true;
          simplify_dp(seg, 0, static_cast<int>(seg.size()) - 1, m_tolerance, sk);

          if (reversed)
            std::reverse(sk.begin(), sk.end());

          for (int i = 0; i < static_cast<int>(sk.size()); i++)
            if (sk[i])
              keep[best_i + i] = true;
        }

        // Segment best_j..best_i (wrapping around)
        std::vector<OGRPoint> seg2;
        std::vector<int> seg2_idx;
        for (int i = best_j; i != best_i; i = (i + 1) % effective_n)
        {
          seg2.push_back(pts[i]);
          seg2_idx.push_back(i);
        }
        seg2.push_back(pts[best_i]);
        seg2_idx.push_back(best_i);

        if (seg2.size() > 2)
        {
          bool reversed = point_less(seg2.back(), seg2.front());
          if (reversed)
            std::reverse(seg2.begin(), seg2.end());

          std::vector<bool> sk(seg2.size(), false);
          sk.front() = true;
          sk.back() = true;
          simplify_dp(seg2, 0, static_cast<int>(seg2.size()) - 1, m_tolerance, sk);

          if (reversed)
            std::reverse(sk.begin(), sk.end());

          for (int i = 0; i < static_cast<int>(sk.size()); i++)
            if (sk[i])
              keep[seg2_idx[reversed ? seg2.size() - 1 - i : i]] = true;
        }
      }
      else
      {
        // Visvalingam-Whyatt on a closed ring: process as linear sequence with wraparound
        // Duplicate some points for wraparound context, then trim results
        keep = simplify_vw(pts, m_tolerance);

        // Ensure at least 3 vertices survive
        int kept = 0;
        for (bool k : keep)
          if (k)
            kept++;
        if (kept < 3)
          keep.assign(effective_n, true);  // keep all
      }

      // Build output
      auto* out = closed ? static_cast<OGRLineString*>(new OGRLinearRing) : new OGRLineString;
      for (int i = 0; i < effective_n; i++)
        if (keep[i])
          out->addPoint(all_pts[i].getX(), all_pts[i].getY());

      if (closed)
      {
        out->closeRings();
        if (out->getNumPoints() < 4)
        {
          delete out;
          return dynamic_cast<OGRLineString*>(geom->clone());
        }
      }
      return out;
    }

    // Segment-based simplification between consecutive anchors
    std::vector<bool> keep(effective_n, false);

    // Mark all anchors as kept
    for (int idx : anchor_indices)
      keep[idx] = true;

    const int num_anchors = anchor_indices.size();

    if (closed)
    {
      // Process segments between consecutive anchors, wrapping around
      for (int a = 0; a < num_anchors; a++)
      {
        int start = anchor_indices[a];
        int end = anchor_indices[(a + 1) % num_anchors];

        // Collect segment points
        std::vector<OGRPoint> seg;
        std::vector<int> seg_indices;

        for (int i = start;; i = (i + 1) % effective_n)
        {
          seg.push_back(all_pts[i]);
          seg_indices.push_back(i);
          if (i == end)
            break;
        }

        if (seg.size() < 3)
          continue;

        std::vector<bool> seg_keep;
        // Use a temporary copy so simplify_segment can canonicalize
        auto seg_pts = seg;
        simplify_segment(seg_pts, seg_keep);

        for (std::size_t i = 0; i < seg_keep.size(); i++)
          if (seg_keep[i])
            keep[seg_indices[i]] = true;
      }
    }
    else
    {
      // Process segments between consecutive anchors
      for (int a = 0; a + 1 < num_anchors; a++)
      {
        int start = anchor_indices[a];
        int end = anchor_indices[a + 1];

        if (end - start < 2)
          continue;

        std::vector<OGRPoint> seg(all_pts.begin() + start, all_pts.begin() + end + 1);
        std::vector<bool> seg_keep;
        simplify_segment(seg, seg_keep);

        for (int i = 0; i < static_cast<int>(seg_keep.size()); i++)
          if (seg_keep[i])
            keep[start + i] = true;
      }
    }

    // Build output
    auto* out = closed ? static_cast<OGRLineString*>(new OGRLinearRing) : new OGRLineString;
    for (int i = 0; i < effective_n; i++)
      if (keep[i])
        out->addPoint(all_pts[i].getX(), all_pts[i].getY());

    if (closed)
    {
      out->closeRings();
      if (out->getNumPoints() < 4)
      {
        delete out;
        return dynamic_cast<OGRLineString*>(geom->clone());
      }
    }

    return out;
  }
};

// Forward declaration
OGRGeometry* apply_simplify(const OGRGeometry* theGeom, LineSimplifier& simplifier);

OGRLineString* apply_simplify(const OGRLineString* geom, LineSimplifier& simplifier)
{
  try
  {
    if (geom == nullptr || geom->IsEmpty() != 0)
      return nullptr;

    simplifier.init_counts(geom);
    return simplifier.simplify(geom);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

OGRLinearRing* apply_simplify(const OGRLinearRing* geom, LineSimplifier& simplifier)
{
  try
  {
    if (geom == nullptr || geom->IsEmpty() != 0)
      return nullptr;

    simplifier.init_counts(geom);
    auto* result = simplifier.simplify(geom);
    if (result == nullptr)
      return nullptr;

    // The simplify method returns the correct type (OGRLinearRing) for closed input
    return dynamic_cast<OGRLinearRing*>(result);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

OGRPolygon* apply_simplify(const OGRPolygon* geom, LineSimplifier& simplifier)
{
  try
  {
    if (geom == nullptr || geom->IsEmpty() != 0)
      return nullptr;

    auto* out = new OGRPolygon();
    const auto* exterior = geom->getExteriorRing();
    auto* new_exterior = apply_simplify(exterior, simplifier);
    if (new_exterior == nullptr)
    {
      delete out;
      return nullptr;
    }
    out->addRingDirectly(new_exterior);

    for (int i = 0, n = geom->getNumInteriorRings(); i < n; ++i)
    {
      const auto* hole = geom->getInteriorRing(i);
      auto* new_hole = apply_simplify(hole, simplifier);
      if (new_hole != nullptr)
        out->addRingDirectly(new_hole);
    }

    return out;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

OGRMultiPolygon* apply_simplify(const OGRMultiPolygon* geom, LineSimplifier& simplifier)
{
  try
  {
    if (geom == nullptr || geom->IsEmpty() != 0)
      return nullptr;

    auto* out = new OGRMultiPolygon();

    for (int i = 0, n = geom->getNumGeometries(); i < n; ++i)
    {
      const auto* g = geom->getGeometryRef(i);
      auto* new_g = apply_simplify(g, simplifier);
      if (new_g != nullptr)
        out->addGeometryDirectly(new_g);
    }

    return out;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

OGRGeometryCollection* apply_simplify(const OGRGeometryCollection* geom, LineSimplifier& simplifier)
{
  try
  {
    if (geom == nullptr || geom->IsEmpty() != 0)
      return nullptr;

    auto* out = new OGRGeometryCollection();

    for (int i = 0, n = geom->getNumGeometries(); i < n; ++i)
    {
      const auto* g = geom->getGeometryRef(i);
      auto* new_g = apply_simplify(g, simplifier);
      if (new_g != nullptr)
        out->addGeometryDirectly(new_g);
    }

    return out;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

OGRGeometry* apply_simplify(const OGRGeometry* geom, LineSimplifier& simplifier)
{
  try
  {
    OGRwkbGeometryType id = geom->getGeometryType();

    switch (id)
    {
      case wkbMultiLineString:
        return apply_simplify(dynamic_cast<const OGRMultiLineString*>(geom), simplifier);
      case wkbLineString:
        return apply_simplify(dynamic_cast<const OGRLineString*>(geom), simplifier);
      case wkbLinearRing:
        return apply_simplify(dynamic_cast<const OGRLinearRing*>(geom), simplifier);
      case wkbPolygon:
        return apply_simplify(dynamic_cast<const OGRPolygon*>(geom), simplifier);
      case wkbMultiPolygon:
        return apply_simplify(dynamic_cast<const OGRMultiPolygon*>(geom), simplifier);
      case wkbGeometryCollection:
        return apply_simplify(dynamic_cast<const OGRGeometryCollection*>(geom), simplifier);
      default:
        throw Fmi::Exception::Trace(
            BCP, "Encountered an unknown geometry component while simplifying an isoline/isoband");
    }
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

void apply_simplify(OGRGeometryPtr& geom_ptr, LineSimplifier& simplifier)
{
  try
  {
    geom_ptr.reset(apply_simplify(geom_ptr.get(), simplifier));
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

}  // namespace

void GeometrySimplifier::apply(std::vector<OGRGeometryPtr>& geoms, bool preserve_topology) const
{
  try
  {
    if (m_type == Type::None || m_tolerance <= 0)
      return;

    LineSimplifier simplifier(m_type, m_tolerance);

    if (preserve_topology)
      for (const auto& geom_ptr : geoms)
        if (geom_ptr && !geom_ptr->IsEmpty())
          simplifier.count(geom_ptr.get());

    for (auto& geom_ptr : geoms)
      if (geom_ptr && !geom_ptr->IsEmpty())
        apply_simplify(geom_ptr, simplifier);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

void GeometrySimplifier::bbox(const Fmi::Box& box)
{
  double x1 = 0;
  double y1 = 0;
  double x2 = m_tolerance;
  double y2 = 0;
  box.itransform(x1, y1);
  box.itransform(x2, y2);
  double pixel_size = std::hypot(x2 - x1, y2 - y1);

  if (m_type == Type::VisvalingamWhyatt)
    m_tolerance = pixel_size * pixel_size;  // area tolerance
  else
    m_tolerance = pixel_size;  // distance tolerance
}

std::size_t GeometrySimplifier::hash_value() const
{
  try
  {
    auto hash = Fmi::hash_value(static_cast<unsigned int>(m_type));
    Fmi::hash_combine(hash, Fmi::hash_value(m_tolerance));
    return hash;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

}  // namespace Fmi
