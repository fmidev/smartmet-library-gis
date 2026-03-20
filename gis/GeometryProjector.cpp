// GeometryProjector.cpp

#include "GeometryProjector.h"
#include "OGR.h"

#include <gis/GeometryBuilder.h>
#include <gis/RectClipper.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cpl_conv.h>
#include <cpl_error.h>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <ogr_api.h>
#include <ogr_geometry.h>
#include <ogr_spatialref.h>
#include <stdexcept>
#include <utility>
#include <vector>

// #include <geos_c.h>
#include <iostream>

namespace Fmi
{

// ------------------------------ small helpers ------------------------------

namespace
{
constexpr double kDegToRad = M_PI / 180.0;

inline bool nearlyEqual(double a, double b, double tol)
{
  return std::abs(a - b) <= tol;
}

inline double ringEps(double minX, double minY, double maxX, double maxY)
{
  const double scale = std::max(maxX - minX, maxY - minY);
  return std::clamp(1e-7 * scale, 1e-8, 1e-4);
}

inline double boundaryTolMeters(double minX, double minY, double maxX, double maxY)
{
  const double scale = std::max(maxX - minX, maxY - minY);
  return std::max(0.5, 1e-6 * scale);
}

inline bool pointInBounds(
    double x, double y, double minX, double minY, double maxX, double maxY, double tol = 0.0)
{
  return (x >= minX - tol && x <= maxX + tol && y >= minY - tol && y <= maxY + tol);
}

// ------------------------------ geographic densification ------------------------------

double metersPerDegLat(double /*phiRad*/)
{
  return 111320.0;
}

double metersPerDegLon(double phiRad)
{
  return 111320.0 * std::cos(phiRad);
}

double approxSegmentMeters(double lon0, double lat0, double lon1, double lat1)
{
  const double phi = 0.5 * (lat0 + lat1) * kDegToRad;
  const double dx = (lon1 - lon0) * metersPerDegLon(phi);
  const double dy = (lat1 - lat0) * metersPerDegLat(phi);
  return std::sqrt(dx * dx + dy * dy);
}

// Densify in geographic CRS using km step (preserves curvature after projection)

void densifyGeographicKm(OGRLineString* line, double stepKm)
{
  if (!line || line->getNumPoints() < 2)
    return;
  if (stepKm <= 0.0)
    return;

  const double stepM = stepKm * 1000.0;

  OGRLineString out;
  for (int i = 0; i < line->getNumPoints() - 1; ++i)
  {
    const double lon0 = line->getX(i);
    const double lat0 = line->getY(i);
    const double lon1 = line->getX(i + 1);
    const double lat1 = line->getY(i + 1);

    out.addPoint(lon0, lat0);

    const double d = approxSegmentMeters(lon0, lat0, lon1, lat1);
    if (d <= stepM)
      continue;

    const int nseg = static_cast<int>(std::ceil(d / stepM));
    for (int s = 1; s < nseg; ++s)
    {
      const double t = static_cast<double>(s) / static_cast<double>(nseg);
      out.addPoint(lon0 + t * (lon1 - lon0), lat0 + t * (lat1 - lat0));
    }
  }
  out.addPoint(line->getX(line->getNumPoints() - 1), line->getY(line->getNumPoints() - 1));

  line->empty();
  for (int i = 0; i < out.getNumPoints(); ++i)
    line->addPoint(out.getX(i), out.getY(i));
}

// ------------------------------ ring helpers ------------------------------

// ---- boundary classification ----

enum class BEdge : std::uint8_t
{
  Bottom = 0,
  Right = 1,
  Top = 2,
  Left = 3,
  None = 4
};

struct ProjectionBoundary
{
  double minX, minY, maxX, maxY;
};

void snapToBoundaryPoint(OGRPoint& p, const ProjectionBoundary& b, double tol)
{
  if (std::abs(p.getX() - b.minX) <= tol)
    p.setX(b.minX);
  else if (std::abs(p.getX() - b.maxX) <= tol)
    p.setX(b.maxX);

  if (std::abs(p.getY() - b.minY) <= tol)
    p.setY(b.minY);
  else if (std::abs(p.getY() - b.maxY) <= tol)
    p.setY(b.maxY);
}

enum class Corner : std::uint8_t
{
  BL = 0,
  BR = 1,
  TR = 2,
  TL = 3
};

bool isOnBoundary(const OGRPoint& p, const ProjectionBoundary& b, double tol)
{
  return (std::abs(p.getX() - b.minX) <= tol || std::abs(p.getX() - b.maxX) <= tol ||
          std::abs(p.getY() - b.minY) <= tol || std::abs(p.getY() - b.maxY) <= tol);
}

// ------------------------------ Liang-Barsky helpers ------------------------------

struct ClipHit
{
  bool ok = false;
  OGRPoint a;
  OGRPoint b;
};

void finalizeCurrentRun(std::vector<std::unique_ptr<OGRLineString>>& runs,
                        std::unique_ptr<OGRLineString>& cur)
{
  if (cur && cur->getNumPoints() >= 2)
    runs.emplace_back(std::move(cur));
  cur.reset();
}

ClipHit clipSegmentLB(double x0, double y0, double x1, double y1, const ProjectionBoundary& b)
{
  ClipHit hit;
  std::array<double, 4> p;
  std::array<double, 4> q;
  p[0] = -(x1 - x0);
  q[0] = x0 - b.minX;
  p[1] = (x1 - x0);
  q[1] = b.maxX - x0;
  p[2] = -(y1 - y0);
  q[2] = y0 - b.minY;
  p[3] = (y1 - y0);
  q[3] = b.maxY - y0;

  double u1 = 0.0;
  double u2 = 1.0;
  for (int i = 0; i < 4; ++i)
  {
    if (std::abs(p[i]) < 1e-15)
    {
      if (q[i] < 0)
        return hit;
    }
    else
    {
      const double t = q[i] / p[i];
      if (p[i] < 0)
      {
        if (t > u2)
          return hit;
        u1 = std::max(t, u1);
      }
      else
      {
        if (t < u1)
          return hit;
        u2 = std::min(t, u2);
      }
    }
  }

  hit.ok = true;
  hit.a.setX(x0 + u1 * (x1 - x0));
  hit.a.setY(y0 + u1 * (y1 - y0));
  hit.b.setX(x0 + u2 * (x1 - x0));
  hit.b.setY(y0 + u2 * (y1 - y0));
  return hit;
}

void appendPointIfDifferent(OGRLineString& ls, const OGRPoint& p, double eps)
{
  const int n = ls.getNumPoints();
  if (n == 0)
  {
    ls.addPoint(&p);
    return;
  }
  if (!nearlyEqual(ls.getX(n - 1), p.getX(), eps) || !nearlyEqual(ls.getY(n - 1), p.getY(), eps))
    ls.addPoint(&p);
}

void appendSegmentToCurrentRun(std::unique_ptr<OGRLineString>& cur,
                               const OGRPoint& a,
                               const OGRPoint& c,
                               double eps)
{
  if (!cur)
    cur = std::make_unique<OGRLineString>();

  if (cur->getNumPoints() == 0)
    cur->addPoint(&a);
  else
    appendPointIfDifferent(*cur, a, eps);

  appendPointIfDifferent(*cur, c, eps);
}

// Special for rings: merge wrap-around segments if they connect (cyclic continuity)
void mergeCyclicRunsIfConnected(std::vector<std::unique_ptr<OGRLineString>>& runs, double eps)
{
  if (runs.size() < 2)
    return;

  auto& first = runs.front();
  auto& last = runs.back();
  if (!first || !last)
    return;

  const double fx0 = first->getX(0);
  const double fy0 = first->getY(0);
  const double lx1 = last->getX(last->getNumPoints() - 1);
  const double ly1 = last->getY(last->getNumPoints() - 1);

  if (nearlyEqual(fx0, lx1, eps) && nearlyEqual(fy0, ly1, eps))
  {
    auto merged = std::make_unique<OGRLineString>();
    merged->addSubLineString(last.get(), 0, last->getNumPoints() - 1);
    for (int i = 1; i < first->getNumPoints(); ++i)
      merged->addPoint(first->getX(i), first->getY(i));
    runs.front() = std::move(merged);
    runs.pop_back();
  }
}

std::vector<std::unique_ptr<OGRLineString>> clipProjectedLineToBounds(const OGRLineString& proj,
                                                                      const ProjectionBoundary& b,
                                                                      bool mergeCyclicRuns,
                                                                      bool detectJumps,
                                                                      double maxJumpMeters)
{
  std::vector<std::unique_ptr<OGRLineString>> runs;
  if (proj.getNumPoints() < 2)
    return runs;

  const double eps = ringEps(b.minX, b.minY, b.maxX, b.maxY);
  const double snapTol = boundaryTolMeters(b.minX, b.minY, b.maxX, b.maxY);

  std::unique_ptr<OGRLineString> cur;

  for (int i = 0; i < proj.getNumPoints() - 1; ++i)
  {
    const double x0 = proj.getX(i);
    const double y0 = proj.getY(i);
    const double x1 = proj.getX(i + 1);
    const double y1 = proj.getY(i + 1);
    if (detectJumps)
    {
      const double dx = x1 - x0;
      const double dy = y1 - y0;
      const double segLen = std::sqrt(dx * dx + dy * dy);
      if (!std::isfinite(segLen) || segLen > maxJumpMeters)
      {
        // Treat pathological jumps (typically from reprojection domain discontinuities) as run
        // breaks.
        if (cur)
          finalizeCurrentRun(runs, cur);
        // If p1 (the far end of the jump) is inside the bounding box, seed a new run there so
        // the re-entry point is not silently discarded.  Without this, a meridian that re-enters
        // the box with a large projected step on both its entry and exit side would produce zero
        // output because p1 would never be added to any run.
        if (pointInBounds(x1, y1, b.minX, b.minY, b.maxX, b.maxY))
        {
          if (!cur)
            cur = std::make_unique<OGRLineString>();
          cur->addPoint(x1, y1);
        }
        continue;
      }
    }

    const bool p1in = pointInBounds(x1, y1, b.minX, b.minY, b.maxX, b.maxY);

    const ClipHit hit = clipSegmentLB(x0, y0, x1, y1, b);
    if (!hit.ok)
    {
      if (cur)
        finalizeCurrentRun(runs, cur);
      continue;
    }

    OGRPoint a = hit.a;
    OGRPoint c = hit.b;
    snapToBoundaryPoint(a, b, snapTol);
    snapToBoundaryPoint(c, b, snapTol);

    appendSegmentToCurrentRun(cur, a, c, eps);

    if (!p1in)
      finalizeCurrentRun(runs, cur);
  }

  if (cur)
    finalizeCurrentRun(runs, cur);

  if (mergeCyclicRuns)
    mergeCyclicRunsIfConnected(runs, eps);

  return runs;
}

// ------------------------------ polygon splitting + hole cuts ------------------------------

// ---- open-hole cut merge helpers ----

struct BoundaryParams
{
  double tol;
  double eps;
  double w;
  double h;
  double per;
};

// ---- best-effort projection helpers ----

std::vector<std::unique_ptr<OGRLineString>> clipRunsToBounds(
    const std::vector<std::unique_ptr<OGRLineString>>& projectedRuns,
    const ProjectionBoundary& b,
    bool mergeCyclicRuns,
    bool detectJumps,
    double maxJumpMeters)
{
  std::vector<std::unique_ptr<OGRLineString>> out;
  for (const auto& pr : projectedRuns)
  {
    if (!pr || pr->getNumPoints() < 2)
      continue;

    auto clipped = clipProjectedLineToBounds(*pr, b, mergeCyclicRuns, detectJumps, maxJumpMeters);
    out.insert(out.end(),
               std::make_move_iterator(clipped.begin()),
               std::make_move_iterator(clipped.end()));
  }
  return out;
}

// ---- polygon assembly helpers ----

// Copy ring coordinates into a LineString.
// - If forceClose==true: ensure closed by appending first point (if needed).
// - If forceClose==false: preserve openness: do NOT append closure if the input is unclosed.
std::unique_ptr<OGRLineString> ringToLineStringPreserveClosure(const OGRLinearRing* r,
                                                               bool forceClose,
                                                               double eps)
{
  if (!r)
    return nullptr;

  const int n = r->getNumPoints();
  if (n == 0)
    return nullptr;

  auto ls = std::make_unique<OGRLineString>();

  // Copy all points as-is
  for (int i = 0; i < n; ++i)
    ls->addPoint(r->getX(i), r->getY(i));

  // Determine if input ring is already closed
  bool closed = false;
  if (n >= 2)
    closed = nearlyEqual(r->getX(0), r->getX(n - 1), eps) &&
             nearlyEqual(r->getY(0), r->getY(n - 1), eps);

  if (forceClose)
  {
    if (!closed)
      ls->addPoint(r->getX(0), r->getY(0));
  }
  else
  {
    // preserve: if unclosed, do nothing.
    // if closed already, keep it closed (fine).
  }

  return ls;
}

}  // namespace

// ------------------------------ PIMPL ------------------------------

class GeometryProjector::Impl
{
 public:
  Impl(OGRSpatialReference* sourceSRS, OGRSpatialReference* targetSRS);

  void setProjectedBounds(double minX, double minY, double maxX, double maxY);
  void setDensifyResolutionKm(double km);
  std::unique_ptr<OGRGeometry> projectGeometry(const OGRGeometry* geom);
  void setJumpThreshold(double threshold);

 private:
  struct SRSDeleter
  {
    void operator()(OGRSpatialReference* srs) const noexcept
    {
      if (srs)
        srs->Release();
    }
  };

  struct CTDeleter
  {
    void operator()(OGRCoordinateTransformation* ct) const noexcept
    {
      if (ct)
        OGRCoordinateTransformation::DestroyCT(ct);
    }
  };

  using SrsPtr = std::unique_ptr<OGRSpatialReference, SRSDeleter>;
  using CtPtr = std::unique_ptr<OGRCoordinateTransformation, CTDeleter>;

  CtPtr m_transform;
  CtPtr m_inverseTransform;
  SrsPtr m_sourceSRS;
  SrsPtr m_targetSRS;

  double m_jumpThreshold = 500e3;  // segment to max 500 km by default

  std::array<double, 4> m_projectedBounds{0, 0, 0, 0};
  double tol = 0;
  bool m_boundsSet = false;

  double m_densifyKm = 50.0;  // default densification is to 50 km

  // ---- core dispatch ----
  std::unique_ptr<OGRGeometry> projectPoint(const OGRPoint* point);
  std::unique_ptr<OGRGeometry> projectLineString(const OGRLineString* line);
  std::unique_ptr<OGRGeometry> projectPolygon(const OGRPolygon* polygon);
  std::unique_ptr<OGRGeometry> projectMultiGeometry(const OGRGeometryCollection* collection);

  // ---- helpers ----
  ProjectionBoundary getProjectionBoundary() const;
  bool isInsideBounds(double x, double y) const;

  std::unique_ptr<OGRPoint> projectSinglePoint(double x, double y, bool* success = nullptr) const;

  // Polygon splitting: exterior runs => polygon pieces; hole runs => interior rings or boundary
  // cuts
  std::unique_ptr<OGRGeometry> splitPolygonWithHolesFast(const OGRPolygon* polygon) const;

  // ---- best-effort projection helpers ----
  std::vector<std::unique_ptr<OGRLineString>> projectToProjectedRunsBestEffort(
      const OGRLineString& geo, bool spitAtFailures) const;
};

// ------------------------------ ctor/dtor ------------------------------

GeometryProjector::Impl::Impl(OGRSpatialReference* sourceSRS, OGRSpatialReference* targetSRS)
{
  if (!sourceSRS || !targetSRS)
    throw std::runtime_error("GeometryProjector: source/target SRS must be non-null");

  // Clone() returns a new ref-counted object (Release() is correct deleter)
  m_sourceSRS.reset(sourceSRS->Clone());
  m_targetSRS.reset(targetSRS->Clone());

  if (!m_sourceSRS || !m_targetSRS)
    throw std::runtime_error("GeometryProjector: failed to clone SRS");

  m_sourceSRS->SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
  m_targetSRS->SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);

  m_transform.reset(OGRCreateCoordinateTransformation(m_sourceSRS.get(), m_targetSRS.get()));
  m_inverseTransform.reset(OGRCreateCoordinateTransformation(m_targetSRS.get(), m_sourceSRS.get()));

  if (!m_transform || !m_inverseTransform)
    throw std::runtime_error("GeometryProjector: failed to create coordinate transformation");
}

// ------------------------------ public API ------------------------------

void GeometryProjector::Impl::setProjectedBounds(double minX, double minY, double maxX, double maxY)
{
  m_projectedBounds[0] = minX;
  m_projectedBounds[1] = minY;
  m_projectedBounds[2] = maxX;
  m_projectedBounds[3] = maxY;
  m_boundsSet = true;
  tol = boundaryTolMeters(minX, minY, maxX, maxY);
}

void GeometryProjector::Impl::setDensifyResolutionKm(double km)
{
  m_densifyKm = km;
}

std::unique_ptr<OGRGeometry> GeometryProjector::Impl::projectGeometry(const OGRGeometry* geom)
{
  if (!geom)
    return nullptr;
  if (!m_boundsSet)
    throw std::runtime_error(
        "GeometryProjector: setProjectedBounds must be called before projectGeometry");

  const auto gt = wkbFlatten(geom->getGeometryType());

  switch (gt)
  {
    case wkbPoint:
      return projectPoint(dynamic_cast<const OGRPoint*>(geom));
    case wkbLineString:
    case wkbLinearRing:
      return projectLineString(dynamic_cast<const OGRLineString*>(geom));
    case wkbPolygon:
      return projectPolygon(dynamic_cast<const OGRPolygon*>(geom));
    case wkbMultiPoint:
    case wkbMultiLineString:
    case wkbMultiPolygon:
    case wkbGeometryCollection:
      return projectMultiGeometry(geom->toGeometryCollection());
    default:
      return std::unique_ptr<OGRGeometry>(
          OGRGeometryFactory::createGeometry(wkbGeometryCollection));
  }
}

void GeometryProjector::Impl::setJumpThreshold(double threshold)
{
  m_jumpThreshold = threshold;
}

// ------------------------------ core dispatch helpers ------------------------------

std::unique_ptr<OGRGeometry> GeometryProjector::Impl::projectPoint(const OGRPoint* point)
{
  if (!point || point->IsEmpty())
    return nullptr;

  bool ok = false;
  auto p = projectSinglePoint(point->getX(), point->getY(), &ok);
  if (!ok || !p)
    return nullptr;

  if (!isInsideBounds(p->getX(), p->getY()))
    return nullptr;

  return std::unique_ptr<OGRGeometry>(p.release());
}

std::unique_ptr<OGRGeometry> GeometryProjector::Impl::projectLineString(const OGRLineString* line)
{
  if (!line || line->IsEmpty())
    return nullptr;

  ProjectionBoundary b = getProjectionBoundary();

  std::unique_ptr<OGRLineString> geo(line->clone());
  if (geo && m_densifyKm > 0.0)
    densifyGeographicKm(geo.get(), m_densifyKm);

  auto projRuns = projectToProjectedRunsBestEffort(*geo, /*splitAtFailures=*/true);
  const double maxJumpMeters =
      (m_densifyKm > 0.0) ? (m_densifyKm * 1000 * 10) : std::max(m_jumpThreshold, 1000 * 1e3);
  auto clippedRuns =
      clipRunsToBounds(projRuns, b, /*mergeCyclicRuns=*/false, /*detectJumps=*/true, maxJumpMeters);

  if (clippedRuns.empty())
    return nullptr;

  if (clippedRuns.size() == 1)
    return std::unique_ptr<OGRGeometry>(clippedRuns[0].release());

  auto* ml = new OGRMultiLineString();
  for (auto& r : clippedRuns)
    ml->addGeometry(r.get());
  return std::unique_ptr<OGRGeometry>(ml);
}

std::unique_ptr<OGRGeometry> GeometryProjector::Impl::projectPolygon(const OGRPolygon* polygon)
{
  if (!polygon || polygon->IsEmpty())
    return nullptr;

  return splitPolygonWithHolesFast(polygon);
}

std::unique_ptr<OGRGeometry> GeometryProjector::Impl::projectMultiGeometry(
    const OGRGeometryCollection* collection)
{
  if (!collection || collection->IsEmpty())
    return nullptr;

  const auto gt = wkbFlatten(collection->getGeometryType());

  OGRGeometryCollection* out = nullptr;
  if (gt == wkbMultiPoint)
    out = new OGRMultiPoint();
  else if (gt == wkbMultiPolygon)
    out = new OGRMultiPolygon();
  else
    out = new OGRGeometryCollection();

  // This calls itself recursively since a database call may return a multilinestring
  // containing multilinestrings, which is technically invalid, but GDAL accepts it.
  // Recursion flattens the structure.
  // Note: auto does not work easily for recursive lambdas.

  std::function<void(const OGRGeometry*)> addFlattened = [&](const OGRGeometry* geom)
  {
    if (!geom || geom->IsEmpty())
      return;
    const auto ggt = wkbFlatten(geom->getGeometryType());
    if (ggt == wkbMultiLineString || ggt == wkbMultiPolygon || ggt == wkbGeometryCollection)
    {
      const auto* coll = static_cast<const OGRGeometryCollection*>(geom);
      for (int j = 0; j < coll->getNumGeometries(); ++j)
        addFlattened(coll->getGeometryRef(j));
    }
    else
    {
      const OGRErr err = out->addGeometry(geom);
      if (err != OGRERR_NONE)
        std::cerr << "  addGeometry FAILED err=" << err
                  << " geomType=" << OGRGeometryTypeToName(geom->getGeometryType())
                  << " outType=" << OGRGeometryTypeToName(out->getGeometryType()) << "\n";
    }
  };

  for (int i = 0; i < collection->getNumGeometries(); ++i)
  {
    const OGRGeometry* g = collection->getGeometryRef(i);
    if (!g)
      continue;
    auto pg = projectGeometry(g);
    if (!pg || pg->IsEmpty())
      continue;
    // if (!pg->IsValid())  continue;  // drop invalid pieces before they can corrupt the collection
    addFlattened(pg.get());
  }

  return std::unique_ptr<OGRGeometry>(out);
}

// ------------------------------ basic helpers ------------------------------

ProjectionBoundary GeometryProjector::Impl::getProjectionBoundary() const
{
  ProjectionBoundary b;
  b.minX = m_projectedBounds[0];
  b.minY = m_projectedBounds[1];
  b.maxX = m_projectedBounds[2];
  b.maxY = m_projectedBounds[3];

  return b;
}

bool GeometryProjector::Impl::isInsideBounds(double x, double y) const
{
  ProjectionBoundary b = getProjectionBoundary();
  return pointInBounds(x, y, b.minX, b.minY, b.maxX, b.maxY);
}

std::unique_ptr<OGRPoint> GeometryProjector::Impl::projectSinglePoint(double x,
                                                                      double y,
                                                                      bool* success) const
{
  double px = x;
  double py = y;
  if (m_transform->Transform(1, &px, &py) == FALSE || !std::isfinite(px) || !std::isfinite(py))
  {
    if (success)
      *success = false;
    return nullptr;
  }

  // Reject points that are implausibly far outside the bounding box —
  // these are PROJ sentinel values or degenerate projections that would
  // corrupt polygon rings. Allow a generous margin (10x box size) to
  // avoid rejecting legitimate far-outside points that will be clipped.
  const double boxW = m_projectedBounds[2] - m_projectedBounds[0];
  const double boxH = m_projectedBounds[3] - m_projectedBounds[1];
  const double margin = 10.0 * std::max(boxW, boxH);
  const double centerX = 0.5 * (m_projectedBounds[0] + m_projectedBounds[2]);
  const double centerY = 0.5 * (m_projectedBounds[1] + m_projectedBounds[3]);
  if (std::abs(px - centerX) > margin || std::abs(py - centerY) > margin)
  {
    if (success)
      *success = false;
    return nullptr;
  }

  if (success)
    *success = true;
  return std::make_unique<OGRPoint>(px, py);
}

std::vector<std::unique_ptr<OGRLineString>>
GeometryProjector::Impl::projectToProjectedRunsBestEffort(const OGRLineString& geo,
                                                          bool splitAtFailures) const
{
  std::vector<std::unique_ptr<OGRLineString>> runs;
  auto cur = std::make_unique<OGRLineString>();

  auto flush = [&]()
  {
    if (cur && cur->getNumPoints() >= 2)
      runs.push_back(std::move(cur));
    cur = std::make_unique<OGRLineString>();
  };

  for (int i = 0; i < geo.getNumPoints(); ++i)
  {
    bool ok = false;
    auto p = projectSinglePoint(geo.getX(i), geo.getY(i), &ok);
    if (!ok || !p)
    {
      if (splitAtFailures)
        flush();
      continue;
    }
    cur->addPoint(p->getX(), p->getY());
  }
  flush();  // always flush at end regardless of splitAtFailures
  return runs;
}

// ------------------------------ splitPolygonWithHolesFast ------------------------------

std::unique_ptr<OGRGeometry> GeometryProjector::Impl::splitPolygonWithHolesFast(
    const OGRPolygon* polygon) const
{
  ProjectionBoundary b = getProjectionBoundary();
  const double eps = ringEps(b.minX, b.minY, b.maxX, b.maxY);
  const Fmi::Box box(b.minX, b.minY, b.maxX, b.maxY);
  Fmi::RectClipper clipper(box, /*keep_inside=*/true);

  // Helper: project a ring and feed its runs into the clipper
  auto projectRingIntoClipper = [&](const OGRLinearRing* ring, bool isExterior) -> bool
  {
    if (!ring || ring->getNumPoints() < 4)
      return false;

    auto geo = ringToLineStringPreserveClosure(ring, /*forceClose=*/true, eps);

    // Normalize winding before densification so that jump-detection runs are produced
    // in the order the CCW reconnection algorithm (connectLines/search_ccw) expects.
    // For exterior rings: CCW so the bottom edge is traversed first, seeding Run 1 at
    // the bottom-right corner and Run 2 at the top-left corner.
    // For interior rings: CW (the opposite).
    // OGRLinearRing::isClockwise()==1 means CW (negative signed area in y-up coordinates).
    {
      int cw = ring->isClockwise();
      bool doReverse = isExterior ? (cw != 0) : (cw == 0);
      if (doReverse)
        geo->reversePoints();
    }

    if (m_densifyKm > 0.0)
      densifyGeographicKm(geo.get(), m_densifyKm);

    // Jump threshold: 20× the densification step.
    // Pole-line jumps (lat=±90 traversed at constant latitude) project to 10-30 million m,
    // far above this threshold.  ECK3's steeply-curved meridians near the poles produce
    // legitimate segments up to ~900 km at 50 km densification; the 10× multiplier (500 km)
    // was too low for those.  20× (1000 km at 50 km densification) safely clears all known
    // legitimate segments while remaining far below any pole-line jump.
    const double maxJumpMeters =
        (m_densifyKm > 0.0) ? (m_densifyKm * 1000.0 * 20.0) : m_jumpThreshold;

    auto projRuns = projectToProjectedRunsBestEffort(*geo, /*splitAtFailures=*/false);

#if 0
    std::cerr << "  projRuns=" << projRuns.size();
    int totalPts = 0;
    for (const auto& r : projRuns)
      if (r)
        totalPts += r->getNumPoints();
    std::cerr << " totalPts=" << totalPts << " geoPts=" << geo->getNumPoints();
    if (totalPts < geo->getNumPoints())
      std::cerr << " *** PROJECTION FAILURES: " << (geo->getNumPoints() - totalPts)
                << " points lost";
    std::cerr << "\n";
    if (!projRuns.empty())
    {
      const auto& first = projRuns.front();
      const auto& last = projRuns.back();
      if (first)
        std::cerr << "  first run front=(" << first->getX(0) << "," << first->getY(0) << ")"
                  << " back=(" << first->getX(first->getNumPoints() - 1) << ","
                  << first->getY(first->getNumPoints() - 1) << ")\n";
      if (projRuns.size() > 1 && last)
        std::cerr << "  last run front=(" << last->getX(0) << "," << last->getY(0) << ")"
                  << " back=(" << last->getX(last->getNumPoints() - 1) << ","
                  << last->getY(last->getNumPoints() - 1) << ")\n";
    }
#endif

    if (projRuns.empty())
      return false;

    // Cyclic merge: if first run starts off-boundary and last run ends
    // off-boundary, they are two halves of a split at a projection failure.
    if (projRuns.size() > 1)
    {
      auto& first = projRuns.front();
      auto& last = projRuns.back();
      if (first && last)
      {
        OGRPoint firstFront(first->getX(0), first->getY(0));
        OGRPoint lastBack(last->getX(last->getNumPoints() - 1),
                          last->getY(last->getNumPoints() - 1));
        if (!isOnBoundary(firstFront, b, tol) && !isOnBoundary(lastBack, b, tol))
        {
          const double dx = lastBack.getX() - firstFront.getX();
          const double dy = lastBack.getY() - firstFront.getY();
          const double dist = std::sqrt(dx * dx + dy * dy);
          if (dist <= m_densifyKm * 1000.0 * 3.0)
          {
            auto merged = std::make_unique<OGRLineString>();
            for (int i = 0; i < last->getNumPoints(); ++i)
              merged->addPoint(last->getX(i), last->getY(i));
            for (int i = 1; i < first->getNumPoints(); ++i)
              merged->addPoint(first->getX(i), first->getY(i));
            projRuns.front() = std::move(merged);
            projRuns.pop_back();
          }
        }
      }
    }

    // Clip runs to bounding box with jump detection, then feed into clipper
    auto clippedRuns = clipRunsToBounds(
        projRuns, b, /*mergeCyclicRuns=*/true, /*detectJumps=*/true, maxJumpMeters);

    auto snapToExactBoundary = [&](OGRLineString* line)
    {
      if (!line || line->getNumPoints() < 2)
        return;
      auto snapCoord = [&](int idx)
      {
        double x = line->getX(idx);
        double y = line->getY(idx);
        if (std::abs(x - b.minX) < tol)
          x = b.minX;
        else if (std::abs(x - b.maxX) < tol)
          x = b.maxX;
        if (std::abs(y - b.minY) < tol)
          y = b.minY;
        else if (std::abs(y - b.maxY) < tol)
          y = b.maxY;
        line->setPoint(idx, x, y);
      };

      snapCoord(0);
      snapCoord(line->getNumPoints() - 1);
    };

    for (auto& run : clippedRuns)
      if (run)
        snapToExactBoundary(run.get());

    for (auto& run : clippedRuns)
    {
      if (!run || run->getNumPoints() < 2)
        continue;

      const int n = run->getNumPoints();
      const bool isOpen = (run->getX(0) != run->getX(n - 1) || run->getY(0) != run->getY(n - 1));

#if 0
      std::cerr << (isExterior ? "  exterior" : "  interior") << " run: pts=" << n
                << " isOpen=" << isOpen << " front=(" << run->getX(0) << "," << run->getY(0) << ")"
                << " back=(" << run->getX(n - 1) << "," << run->getY(n - 1) << ")\n";
#endif

      if (isOpen)
      {
        // Genuinely clipped by boundary - pass as linestring
        auto* line = static_cast<OGRLineString*>(run->clone());
        if (isExterior)
          clipper.addExterior(line);
        else
          clipper.addInterior(line);
      }
      else
      {
        // Naturally closed ring - pass directly as ring
        auto* ring = new OGRLinearRing;
        ring->addSubLineString(run.get());
        if (isExterior)
          clipper.addExterior(ring);
        else
          clipper.addInterior(ring);
      }
    }

    return true;
  };

  // Project exterior ring
  const OGRLinearRing* ext = polygon->getExteriorRing();
  if (!ext || ext->getNumPoints() < 4)
    return nullptr;

  projectRingIntoClipper(ext, /*isExterior=*/true);

  // Project holes
  for (int i = 0; i < polygon->getNumInteriorRings(); ++i)
    projectRingIntoClipper(polygon->getInteriorRing(i), /*isExterior=*/false);

  if (clipper.empty())
    return nullptr;

  clipper.reconnect();
  clipper.reconnectWithBox(/*theMaximumSegmentLength=*/0.0);

  GeometryBuilder builder;
  clipper.release(builder);
  return std::unique_ptr<OGRGeometry>(builder.build());
}

#if 0
  // Disabled since does not seem to cause problems
  for (size_t i = 0; i < shells.size(); ++i)
  {
    if (!shells[i]->IsValid())
    {
      // Get the GEOS handle from the geometry
      GEOSContextHandle_t hGEOSCtx = OGRGeometry::createGEOSContext();
      GEOSGeom hGEOSGeom = shells[i]->exportToGEOS(hGEOSCtx);

      char* pszReason = GEOSisValidReason_r(hGEOSCtx, hGEOSGeom);

      std::cerr << "    shell[" << i << "] valid=false"
                << " reason=" << (pszReason ? pszReason : "?") << "\n";

      GEOSFree(pszReason);
      GEOSGeom_destroy_r(hGEOSCtx, hGEOSGeom);
      OGRGeometry::freeGEOSContext(hGEOSCtx);
    }
  }
#endif

// ------------------------------ GeometryProjector (public) ------------------------------

GeometryProjector::GeometryProjector(OGRSpatialReference* sourceSRS, OGRSpatialReference* targetSRS)
    : m_impl(std::make_unique<Impl>(sourceSRS, targetSRS))
{
}

GeometryProjector::~GeometryProjector() = default;

GeometryProjector::GeometryProjector(GeometryProjector&&) noexcept = default;
GeometryProjector& GeometryProjector::operator=(GeometryProjector&&) noexcept = default;

void GeometryProjector::setProjectedBounds(double minX, double minY, double maxX, double maxY)
{
  m_impl->setProjectedBounds(minX, minY, maxX, maxY);
}

void GeometryProjector::setDensifyResolutionKm(double km)
{
  m_impl->setDensifyResolutionKm(km);
}

std::unique_ptr<OGRGeometry> GeometryProjector::projectGeometry(const OGRGeometry* geom)
{
  return m_impl->projectGeometry(geom);
}

void GeometryProjector::setJumpThreshold(double threshold)
{
  m_impl->setJumpThreshold(threshold);
}

}  // namespace Fmi
