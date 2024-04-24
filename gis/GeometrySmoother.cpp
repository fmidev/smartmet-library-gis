#include "GeometrySmoother.h"
#include "Box.h"
#include "VertexCounter.h"
#include <macgyver/Exception.h>
#include <macgyver/Hash.h>
#include <ogr_geometry.h>
#include <string>

namespace Fmi
{

namespace
{

using Weight = std::function<double(double dist)>;

// Factory method to generate a lambda function for the chosen weighting function
Weight create_weight_lambda(GeometrySmoother::Type type, double radius)
{
  switch (type)
  {
    case GeometrySmoother::Type::Tukey:
      return [radius](double dist)
      {
        if (dist >= radius)
          return 0.0;

        double normalizedDistance = dist / radius;
        double t = (1.0 - (normalizedDistance * normalizedDistance));
        return t * t;
      };

    case GeometrySmoother::Type::Linear:
      return [radius](double dist)
      {
        if (dist >= radius)
          return 0.0;
        return (radius - dist) / radius;
      };

    case GeometrySmoother::Type::Average:
      return [radius](double dist)
      {
        if (dist >= radius)
          return 0.0;
        return 1.0;
      };

    case GeometrySmoother::Type::Gaussian:
    default:
      return [radius](double dist)
      {
        if (dist >= radius)
          return 0.0;

        double sigma = 1.5 * radius;
        return std::exp(-(dist * dist) / (2 * sigma * sigma));
      };
  }
}

// Utility class to handle weighting by a distance based filter
class LineFilter
{
 private:
  std::unordered_map<OGRPoint, OGRPoint, OGRPointHash, OGRPointEqual> m_cache;  // cached results
  VertexCounter m_counter;          // how many times a vertex appears in the full geometry
  Weight m_filter;                  // filterer
  std::vector<double> m_distances;  // distances between adjacent vertices in current polyline
  std::vector<int> m_counts;        // occurrance counts for current polyline
  OGRPoint m_first_point{0, 0};
  OGRPoint m_prev_point{0, 0};
  int m_first_pos = 0;
  double m_path_length = 0;
  double m_sum_x = 0;
  double m_sum_y = 0;
  double m_total_weight = 0;
  bool m_closed = false;
  bool m_debug = false;

 public:
  LineFilter(GeometrySmoother::Type t, double r) : m_filter(create_weight_lambda(t, r)) {}

  void init(const OGRLineString* geom)
  {
    m_closed = geom->get_IsClosed();
    init_counts(geom);
    init_distances(geom);
  }

  // Start filtering from a point. Returns false if smoothing at the vertex is disabled
  bool reset(const OGRPoint* point, int i)
  {
    // Indicate filtering not accepted atleast yet for append()
    m_total_weight = 0;
    m_debug = false;
    m_first_pos = i;
    m_first_point = *point;

    if (!allowed(i))
      return false;

    // Cannot smoothen a point if when adjacent points are not allowed either or there would
    // be a gap beetween isobands where they diverge
    long n = m_counts.size();
    if (m_closed)
    {
      int prev = (i > 0 ? i - 1 : n - 2);  // yes, n-2 since first vertex is duplicated
      int next = (i < n - 2 ? i + 1 : 0);
      if (!allowed(prev) || !allowed(next))
        return false;
    }
    else
    {
      if (i > 0 && !allowed(i - 1))
        return false;
      if (i < n - 1 && !allowed(i + 1))
        return false;
    }

    // Caching guarantees there are no gaps between isobands simply due to the different winding
    // order of the polygons
    auto pos = m_cache.find(*point);
    if (pos != m_cache.end())
    {
      m_sum_x = pos->second.getX();  // Set result to be the cached value
      m_sum_y = pos->second.getY();
      m_total_weight = 1;
      return false;
    }

    m_prev_point = *point;
    m_path_length = 0;
    m_sum_x = 0;
    m_sum_y = 0;

    return true;
  }

  void append(OGRLineString* geom)
  {
    if (m_total_weight == 0)
      geom->addPoint(m_first_point.getX(), m_first_point.getY());  // filtering was not allowed
    else
    {
      auto x = m_sum_x / m_total_weight;
      auto y = m_sum_y / m_total_weight;
      geom->addPoint(x, y);

      m_cache.insert(std::make_pair(m_first_point, OGRPoint(x, y)));
    }
  }

  // Reset accumulated path length
  void resetPathLength()
  {
    m_path_length = 0;
    m_prev_point = m_first_point;
  }

  // Add a point
  bool add(const OGRLineString* geom, int j, int dist_pos)
  {
    const auto n = geom->getNumPoints();
    if (j < 0)
      j = n + j - 1;  // j=-1 becomes n-2, the last point is skipped as a duplicate of the 1st one
    else if (j >= n)
      j = j - n + 1;  // j=n becomes 1, 1st point is skipped as a duplicate of the last one

    OGRPoint pt;
    geom->getPoint(j, &pt);
    if (!allowed(j))
      return false;

    if (dist_pos < 0)
      dist_pos = n + dist_pos - 1;
    else if (dist_pos >= n)
      dist_pos = dist_pos - n + 1;

    double dist = 0.0;
    if (j != m_first_pos)
      dist = distance(dist_pos);

    double weight = m_filter(dist);
    if (weight == 0)
      return false;

    m_sum_x += weight * pt.getX();
    m_sum_y += weight * pt.getY();
    m_total_weight += weight;
    m_path_length = dist;
    m_prev_point = pt;

    return true;
  }

  // Test whether the vertex is allowed in smoothing
  bool allowed(int j)
  {
    // n=0: isoline, since counting is then disabled
    // n=1: unshared isoband edge, in practise grid edges or etc
    // n=2: shared isoband edge
    // n=4: shared isoband corner at a grid cell vertex
    auto n = m_counts[j];
    return (n == 0 || n == 2);
  }

  // Distance metric
  double distance(int dist_pos) const { return m_path_length + m_distances[dist_pos]; }

  // Function to calculate the Euclidean distance between two 2D vertices
  static double distance(const OGRPoint& v1, const OGRPoint& v2)
  {
    double x = v1.getX() - v2.getX();
    double y = v1.getY() - v2.getY();
    return std::hypot(x, y);
  }

  void count(const OGRGeometry* geom) { m_counter.add(geom); }

  // Initialize occurrance counts
  void init_counts(const OGRLineString* geom)
  {
    const auto n = geom->getNumPoints();
    m_counts.clear();
    m_counts.reserve(n);

    OGRPoint pt;
    for (int i = 0; i < n; i++)
    {
      geom->getPoint(i, &pt);
      auto count = m_counter.getCount(pt);
      m_counts.push_back(count);
    }
  }

  // Calculate distances between vertices in OGRLineString/OGRLinearRing
  void init_distances(const OGRLineString* geom)
  {
    const auto n = geom->getNumPoints();

    m_distances.clear();
    m_distances.reserve(n);

    for (int i = 0; i < n - 1; i++)
    {
      OGRPoint v1;
      OGRPoint v2;
      geom->getPoint(i, &v1);
      geom->getPoint(i + 1, &v2);
      m_distances.push_back(LineFilter::distance(v1, v2));
    }

    // To simplify subsequent processing
    if (geom->get_IsClosed())
      m_distances.push_back(m_distances.front());
  }
};

// Forward declaration needed since two functions call each other
OGRGeometry* apply_filter(const OGRGeometry* theGeom, LineFilter& filter, uint iterations);

// Linestrings may be closed, in which case we handle them like linearrings
OGRLineString* apply_filter(const OGRLineString* geom, LineFilter& filter, uint iterations)
{
  try
  {
    if (geom == nullptr || geom->IsEmpty() != 0)
      return nullptr;

    const bool closed = (geom->get_IsClosed() == 1);

    // To avoid repeated recalculation and unordered_map fetches
    filter.init(geom);

    const OGRLineString* g = geom;
    OGRLineString* out = nullptr;

    OGRPoint v1;

    for (uint iter = 0; iter < iterations; iter++)
    {
      if (iter > 0)
        g = out;                // replace geom with filtered linestring
      out = new OGRLineString;  // new filtered result

      const int n = g->getNumPoints();
      const int max_closed_length = n / 4;  // do not process closed rings too much
      const int imax = (closed ? n - 2 : n - 1);
      for (int i = 0; i <= imax; i++)
      {
        g->getPoint(i, &v1);

        if (filter.reset(&v1, i))
        {
          // Make sure the filter has a symmetric number of points to prevent excessive isoline
          // shrinkage at the ends if the number of iterations is > 1. The effective smoothing
          // radius thus shrinks when we get close to the isoline end points.

          // Closed isoline stencil is always symmetric
          int jmin = i - max_closed_length;
          int jmax = i + max_closed_length;

          if (!closed)
          {
            // Shorter of lengths to start (0) and end vertices (n-1)
            auto max_offset = std::min(i - 0, n - 1 - i);

            jmin = i - max_offset;  // symmetric stencil
            jmax = i + max_offset;
          }

          // Iterate backwards until radius is reached
          for (int j = i; j >= jmin; --j)
            if (!filter.add(g, j, j))
              break;

          // Iterate forwards until maxdistance is reached
          filter.resetPathLength();
          for (int j = i + 1; j <= jmax; ++j)
            if (!filter.add(g, j, j - 1))
              break;
        }
        filter.append(out);
      }
      if (closed)
        out->closeRings();  // Make sure the ring is exactly closed, rounding errors may be
                            // present

      if (iter > 1)
        delete g;
    }

    return out;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// Linearrings must be filtered with wraparound. There is a risk that if the specified radius
// is larger than the size of the ring, an averaging filter would transform all coordinates to
// the same value.
OGRLinearRing* apply_filter(const OGRLinearRing* geom, LineFilter& filter, uint iterations)
{
  try
  {
    if (geom == nullptr || geom->IsEmpty() != 0)
      return nullptr;

    // To avoid repeated recalculation and unordered_map fetches
    filter.init(geom);

    const OGRLinearRing* g = geom;
    OGRLinearRing* out = nullptr;

    OGRPoint v1;

    for (uint iter = 0; iter < iterations; iter++)
    {
      if (iter > 0)
        g = out;                // replace geom with filtered linestring
      out = new OGRLinearRing;  // new filtered result

      const int n = g->getNumPoints();
      const int max_closed_length = n / 4;  // do not process closed rings too much
      for (int i = 0; i < n - 1; i++)       // do not process the last point, duplicate it later
      {
        g->getPoint(i, &v1);

        if (filter.reset(&v1, i))
        {
          // Iterate backwards until max radius is reached
          const int jmin = i - max_closed_length;
          for (int j = i; j >= jmin; --j)
            if (!filter.add(g, j, j))
              break;

          // Iterate forwards until max radius is reached

          filter.resetPathLength();
          const int jmax = i + max_closed_length;
          for (int j = i + 1; j <= jmax; ++j)
            if (!filter.add(g, j, j - 1))
              break;
        }
        filter.append(out);
      }

      out->closeRings();  // Make sure the ring is exactly closed, rounding errors may be present

      if (iter > 1)
        delete g;
    }

    return out;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

OGRMultiPolygon* apply_filter(const OGRMultiPolygon* geom, LineFilter& filter, uint iterations)
{
  try
  {
    if (geom == nullptr || geom->IsEmpty() != 0)
      return nullptr;

    auto* out = new OGRMultiPolygon();

    for (int i = 0, n = geom->getNumGeometries(); i < n; ++i)
    {
      const auto* g = geom->getGeometryRef(i);
      auto* new_g = apply_filter(g, filter, iterations);
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

OGRGeometryCollection* apply_filter(const OGRGeometryCollection* geom,
                                    LineFilter& filter,
                                    uint iterations)
{
  try
  {
    if (geom == nullptr || geom->IsEmpty() != 0)
      return nullptr;

    auto* out = new OGRGeometryCollection();

    for (int i = 0, n = geom->getNumGeometries(); i < n; ++i)
    {
      const auto* g = geom->getGeometryRef(i);
      auto* new_g = apply_filter(g, filter, iterations);
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

OGRPolygon* apply_filter(const OGRPolygon* geom, LineFilter& filter, uint iterations)
{
  try
  {
    if (geom == nullptr || geom->IsEmpty() != 0)
      return nullptr;

    auto* out = new OGRPolygon();
    const auto* exterior = dynamic_cast<const OGRLinearRing*>(geom->getExteriorRing());
    auto* new_exterior = apply_filter(exterior, filter, iterations);
    out->addRingDirectly(new_exterior);

    for (int i = 0, n = geom->getNumInteriorRings(); i < n; ++i)
    {
      const auto* hole = dynamic_cast<const OGRLinearRing*>(geom->getInteriorRing(i));
      auto* new_hole = apply_filter(hole, filter, iterations);
      out->addRingDirectly(new_hole);
    }

    return out;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

OGRGeometry* apply_filter(const OGRGeometry* geom, LineFilter& filter, uint iterations)
{
  try
  {
    OGRwkbGeometryType id = geom->getGeometryType();

    switch (id)
    {
      case wkbMultiLineString:
        return apply_filter(dynamic_cast<const OGRMultiLineString*>(geom), filter, iterations);
      case wkbLineString:
        return apply_filter(dynamic_cast<const OGRLineString*>(geom), filter, iterations);
      case wkbLinearRing:
        return apply_filter(dynamic_cast<const OGRLinearRing*>(geom), filter, iterations);
      case wkbPolygon:
        return apply_filter(dynamic_cast<const OGRPolygon*>(geom), filter, iterations);
      case wkbMultiPolygon:
        return apply_filter(dynamic_cast<const OGRMultiPolygon*>(geom), filter, iterations);
      case wkbGeometryCollection:
        return apply_filter(dynamic_cast<const OGRGeometryCollection*>(geom), filter, iterations);
      default:
        throw Fmi::Exception::Trace(
            BCP, "Encountered an unknown geometry component while filtering an isoline/isoband");
    }
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// Apply the filter to a single geometry
void apply_filter(OGRGeometryPtr& geom_ptr, LineFilter& filter, uint iterations)
{
  try
  {
    geom_ptr.reset(apply_filter(geom_ptr.get(), filter, iterations));
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

}  // namespace

// Apply the filter
void GeometrySmoother::apply(std::vector<OGRGeometryPtr>& geoms, bool preserve_topology) const
{
  try
  {
    if (m_type == GeometrySmoother::Type::None || m_iterations == 0 || m_radius <= 0)
      return;

    LineFilter filter(m_type, m_radius);

    if (preserve_topology)
      for (const auto& geom_ptr : geoms)
        if (geom_ptr && !geom_ptr->IsEmpty())
          filter.count(geom_ptr.get());

    for (auto& geom_ptr : geoms)
      if (geom_ptr && !geom_ptr->IsEmpty())
        apply_filter(geom_ptr, filter, m_iterations);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// Alter the pixel radius to a metric one based on a BBOX
void GeometrySmoother::bbox(const Fmi::Box& box)
{
  double x1 = 0;
  double y1 = 0;
  double x2 = m_radius;
  double y2 = 0;
  box.itransform(x1, y1);
  box.itransform(x2, y2);
  m_radius = std::hypot(x2 - x1, y2 - y1);
}

// Hash value for caching purposes
std::size_t GeometrySmoother::hash_value() const
{
  try
  {
    auto hash = Fmi::hash_value(static_cast<unsigned int>(m_type));
    Fmi::hash_combine(hash, Fmi::hash_value(m_radius));
    Fmi::hash_combine(hash, Fmi::hash_value(m_iterations));
    return hash;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

}  // namespace Fmi
