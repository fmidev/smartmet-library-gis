// GeometryProjector.cpp

#include "GeometryProjector.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

#include <cpl_conv.h>
#include <cpl_error.h>
#include <ogr_api.h>
#include <ogr_geometry.h>
#include <ogr_spatialref.h>

// ------------------------------ small helpers ------------------------------

namespace
{
constexpr double kDegToRad = M_PI / 180.0;

inline double sqr(double x)
{
  return x * x;
}

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

inline void forceExactClosure(OGRLinearRing& r)
{
  if (r.getNumPoints() < 2)
    return;
  OGRPoint first, last;
  r.getPoint(0, &first);
  r.getPoint(r.getNumPoints() - 1, &last);
  if (std::abs(first.getX() - last.getX()) > 1e-10 || std::abs(first.getY() - last.getY()) > 1e-10)
  {
    r.closeRings();
    r.setPoint(r.getNumPoints() - 1, first.getX(), first.getY());
  }
}

// Copy ring coordinates into a LineString.
// - If forceClose==true: ensure closed by appending first point (if needed).
// - If forceClose==false: preserve openness: do NOT append closure if the input is unclosed.
static std::unique_ptr<OGRLineString> ringToLineStringPreserveClosure(const OGRLinearRing* r,
                                                                      bool forceClose,
                                                                      double eps)
{
  auto ls = std::make_unique<OGRLineString>();
  if (!r)
    return ls;

  const int n = r->getNumPoints();
  if (n == 0)
    return ls;

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
  void setPoleHandling(bool enable);

 private:
  struct ProjectionBoundary
  {
    double minX, minY, maxX, maxY;
  };

  struct ClipHit
  {
    bool ok = false;
    OGRPoint a, b;
  };

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

  double m_jumpThreshold = 0.0;
  bool m_autoThreshold = true;
  bool m_handlePoles = true;

  double m_projectedBounds[4] = {0, 0, 0, 0};
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

  // Densify in geographic CRS using km step (preserves curvature after projection)
  void densifyGeographicKm(OGRLineString* line, double stepKm) const;
  static double metersPerDegLat(double phiRad);
  static double metersPerDegLon(double phiRad);
  static double approxSegmentMeters(double lon0, double lat0, double lon1, double lat1);

  // Liang–Barsky
  ClipHit clipSegmentLB(
      double x0, double y0, double x1, double y1, const ProjectionBoundary& b) const;
  static void snapToBoundaryPoint(OGRPoint& p, const ProjectionBoundary& b, double tol);

  static void appendPointIfDifferent(OGRLineString& ls, const OGRPoint& p, double eps);

  // Clip a projected polyline to bounds => inside runs (LineStrings)
  std::vector<std::unique_ptr<OGRLineString>> clipProjectedLineToBounds(
      const OGRLineString& proj, const ProjectionBoundary& b) const;

  // Special for rings: merge wrap-around segments if they connect (cyclic continuity)
  static void mergeCyclicRunsIfConnected(std::vector<std::unique_ptr<OGRLineString>>& runs,
                                         double eps);

  // Ring closure
  static bool isRingClosed(const OGRLineString& ls, double eps = 1e-8);
  static std::unique_ptr<OGRLinearRing> toClosedRing(const OGRLineString& ls);

  // Boundary traversal
  bool isOnBoundary(const OGRPoint& p, const ProjectionBoundary& b, double tol) const;
  double boundaryS(const OGRPoint& p, const ProjectionBoundary& b, double tol) const;
  std::vector<OGRPoint> boundaryPathShortest(const OGRPoint& startOnB,
                                             const OGRPoint& endOnB,
                                             const ProjectionBoundary& b) const;

  // Directed boundary walk (goCW=true follows increasing boundaryS: BL->BR->TR->TL->BL)
  std::vector<OGRPoint> boundaryPathDirected(const OGRPoint& startOnB,
                                             const OGRPoint& endOnB,
                                             const ProjectionBoundary& b,
                                             bool goCW) const;

  // Build a closed ring from an open run by walking bbox boundary from last->first
  std::unique_ptr<OGRLinearRing> closeRunAlongBoundary(const OGRLineString& run,
                                                       const ProjectionBoundary& b,
                                                       bool goCW) const;

  // Polygon splitting: exterior runs => polygon pieces; hole runs => interior rings or boundary
  // cuts
  std::unique_ptr<OGRGeometry> splitPolygonWithHolesFast(const OGRPolygon* polygon) const;

  // Merge open hole run as a cut into exterior ring (boundary-parameter insertion)
  std::unique_ptr<OGRLinearRing> mergeOpenHoleRunAsCut(const OGRLinearRing& exterior,
                                                       const OGRLineString& holeRun,
                                                       const ProjectionBoundary& b) const;

  int ensureBoundaryVertex(OGRLinearRing& ring,
                           const OGRPoint& pOnBoundary,
                           const ProjectionBoundary& b,
                           double tol) const;

  bool boundaryEdgeContainsS(double s0, double s1, double s, double per) const;

  // ---- best-effort projection helpers ----
  std::vector<std::unique_ptr<OGRLineString>> projectToProjectedRunsBestEffort(
      const OGRLineString& geo) const;

  std::vector<std::unique_ptr<OGRLineString>> clipRunsToBounds(
      const std::vector<std::unique_ptr<OGRLineString>>& projectedRuns,
      const ProjectionBoundary& b) const;

  // ---- polygon assembly helpers ----
  std::vector<std::unique_ptr<OGRPolygon>> buildShellsFromExteriorRuns(
      std::vector<std::unique_ptr<OGRLineString>>& extRuns, const ProjectionBoundary& b) const;

  std::vector<std::unique_ptr<OGRLinearRing>> cloneInteriorRings(const OGRPolygon& shell) const;

  void attachClosedHoleRingToShells(const OGRLinearRing& holeRing,
                                    std::vector<std::unique_ptr<OGRPolygon>>& shells) const;

  void applyOpenHoleRunAsCut(const OGRLineString& openRun,
                             const ProjectionBoundary& b,
                             std::vector<std::unique_ptr<OGRPolygon>>& shells) const;

  // ---- boundary traversal refactor ----
  enum class Corner : int
  {
    BL = 0,
    BR = 1,
    TR = 2,
    TL = 3
  };

  OGRPoint cornerPoint(Corner c, const ProjectionBoundary& b) const;
  double cornerS(Corner c, const ProjectionBoundary& b) const;
  double nextCornerS(double s, const ProjectionBoundary& b, double tol) const;
  double prevCornerS(double s, const ProjectionBoundary& b, double tol) const;

  void appendCornerByS(double s,
                       const ProjectionBoundary& b,
                       double tol,
                       std::vector<OGRPoint>& out) const;

  std::vector<OGRPoint> boundaryPathCore(const OGRPoint& startOnB,
                                         const OGRPoint& endOnB,
                                         const ProjectionBoundary& b,
                                         bool goCW) const;

  // ---- open-hole cut merge helpers ----
  struct BoundaryParams
  {
    double tol;
    double eps;
    double w;
    double h;
    double per;
  };

  BoundaryParams boundaryParams(const ProjectionBoundary& b) const;

  static OGRLinearRing copyExteriorRing(const OGRLinearRing& exterior);

  bool getSnappedRunEndpointsOnBoundary(const OGRLineString& holeRun,
                                        const ProjectionBoundary& b,
                                        const BoundaryParams& bp,
                                        OGRPoint& hs,
                                        OGRPoint& he) const;

  bool ensureEndpointsAsBoundaryVertices(OGRLinearRing& ext,
                                         const ProjectionBoundary& b,
                                         const BoundaryParams& bp,
                                         const OGRPoint& hs,
                                         const OGRPoint& he,
                                         int& iHs,
                                         int& iHe) const;

  bool chooseReplaceForwardArc(const OGRLinearRing& ext,
                               int iHs,
                               int iHe,
                               const ProjectionBoundary& b,
                               const BoundaryParams& bp) const;

  void appendReversedHoleRun(OGRLinearRing& out,
                             const OGRLineString& holeRun,
                             const BoundaryParams& bp,
                             const OGRPoint& heV) const;

  void appendKeptExteriorArc(OGRLinearRing& out,
                             const OGRLinearRing& ext,
                             int iHs,
                             int iHe,
                             bool replaceForward,
                             const BoundaryParams& bp) const;

  double deltaSInc(double s1, double s2, double per) const;

  double increasingBoundaryAdvanceScore(const OGRLinearRing& ext,
                                        int from,
                                        int to,
                                        bool forward,
                                        const ProjectionBoundary& b,
                                        const BoundaryParams& bp) const;

  double boundaryLengthOnArc(const OGRLinearRing& ext,
                             int from,
                             int to,
                             bool forward,
                             const ProjectionBoundary& b,
                             const BoundaryParams& bp) const;

  // ---- boundary classification helpers ----
  enum class BEdge : int
  {
    Bottom = 0,
    Right = 1,
    Top = 2,
    Left = 3,
    None = 4
  };

  BEdge classifyBoundaryEdge(const OGRPoint& p, const ProjectionBoundary& b, double tol) const;
  bool pointOnEdge(const OGRPoint& p, BEdge e, const ProjectionBoundary& b, double tol) const;
  bool segmentOnSameEdge(
      const OGRPoint& a, const OGRPoint& c, BEdge e, const ProjectionBoundary& b, double tol) const;

  static bool betweenInclusive(double a, double c, double v, double tol);

  int findInsertAfterIndexOnBoundaryEdge(const OGRLinearRing& ring,
                                         const OGRPoint& pOnBoundary,
                                         BEdge edge,
                                         const ProjectionBoundary& b,
                                         double tol) const;

  // ---- clipping helpers ----
  void finalizeCurrentRun(std::vector<std::unique_ptr<OGRLineString>>& runs,
                          std::unique_ptr<OGRLineString>& cur) const;

  void appendSegmentToCurrentRun(std::unique_ptr<OGRLineString>& cur,
                                 const OGRPoint& a,
                                 const OGRPoint& c,
                                 double eps) const;
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

  if (m_autoThreshold)
  {
    const double w = maxX - minX;
    if (w > 0)
      m_jumpThreshold = 0.5 * w;
  }
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
      return projectMultiGeometry(dynamic_cast<const OGRGeometryCollection*>(geom));
    default:
      return std::unique_ptr<OGRGeometry>(
          OGRGeometryFactory::createGeometry(wkbGeometryCollection));
  }
}

void GeometryProjector::Impl::setJumpThreshold(double threshold)
{
  m_jumpThreshold = threshold;
  m_autoThreshold = false;
}

void GeometryProjector::Impl::setPoleHandling(bool enable)
{
  m_handlePoles = enable;
}

// ------------------------------ core dispatch helpers ------------------------------

std::unique_ptr<OGRGeometry> GeometryProjector::Impl::projectPoint(const OGRPoint* point)
{
  if (!point || point->IsEmpty())
    return std::unique_ptr<OGRGeometry>(OGRGeometryFactory::createGeometry(wkbPoint));

  bool ok = false;
  auto p = projectSinglePoint(point->getX(), point->getY(), &ok);
  if (!ok || !p)
    return std::unique_ptr<OGRGeometry>(OGRGeometryFactory::createGeometry(wkbPoint));

  if (!isInsideBounds(p->getX(), p->getY()))
    return std::unique_ptr<OGRGeometry>(OGRGeometryFactory::createGeometry(wkbPoint));

  return std::unique_ptr<OGRGeometry>(p.release());
}

std::unique_ptr<OGRGeometry> GeometryProjector::Impl::projectLineString(const OGRLineString* line)
{
  if (!line || line->IsEmpty())
    return std::unique_ptr<OGRGeometry>(OGRGeometryFactory::createGeometry(wkbLineString));

  ProjectionBoundary b = getProjectionBoundary();

  std::unique_ptr<OGRLineString> geo(dynamic_cast<OGRLineString*>(line->clone()));
  if (geo && m_densifyKm > 0.0)
    densifyGeographicKm(geo.get(), m_densifyKm);

  auto projRuns = projectToProjectedRunsBestEffort(*geo);
  auto clippedRuns = clipRunsToBounds(projRuns, b);

  if (clippedRuns.empty())
    return std::unique_ptr<OGRGeometry>(OGRGeometryFactory::createGeometry(wkbLineString));

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
    return std::unique_ptr<OGRGeometry>(OGRGeometryFactory::createGeometry(wkbPolygon));

  return splitPolygonWithHolesFast(polygon);
}

std::unique_ptr<OGRGeometry> GeometryProjector::Impl::projectMultiGeometry(
    const OGRGeometryCollection* collection)
{
  if (!collection || collection->IsEmpty())
    return std::unique_ptr<OGRGeometry>(OGRGeometryFactory::createGeometry(wkbGeometryCollection));

  const auto gt = wkbFlatten(collection->getGeometryType());

  OGRGeometryCollection* out = nullptr;
  if (gt == wkbMultiPoint)
    out = new OGRMultiPoint();
  else if (gt == wkbMultiLineString)
    out = new OGRMultiLineString();
  else if (gt == wkbMultiPolygon)
    out = new OGRMultiPolygon();
  else
    out = new OGRGeometryCollection();

  for (int i = 0; i < collection->getNumGeometries(); ++i)
  {
    const OGRGeometry* g = collection->getGeometryRef(i);
    if (!g)
      continue;
    auto pg = projectGeometry(g);
    if (pg && !pg->IsEmpty())
      out->addGeometry(pg.get());
  }

  return std::unique_ptr<OGRGeometry>(out);
}

// ------------------------------ basic helpers ------------------------------

GeometryProjector::Impl::ProjectionBoundary GeometryProjector::Impl::getProjectionBoundary() const
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
  double px = x, py = y;
  if (m_transform->Transform(1, &px, &py) == FALSE || !std::isfinite(px) || !std::isfinite(py))
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
GeometryProjector::Impl::projectToProjectedRunsBestEffort(const OGRLineString& geo) const
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
      flush();  // split at failure (no bridging)
      continue;
    }
    cur->addPoint(p->getX(), p->getY());
  }
  flush();

  return runs;
}

std::vector<std::unique_ptr<OGRLineString>> GeometryProjector::Impl::clipRunsToBounds(
    const std::vector<std::unique_ptr<OGRLineString>>& projectedRuns,
    const ProjectionBoundary& b) const
{
  std::vector<std::unique_ptr<OGRLineString>> out;
  for (const auto& pr : projectedRuns)
  {
    if (!pr || pr->getNumPoints() < 2)
      continue;

    auto clipped = clipProjectedLineToBounds(*pr, b);
    for (auto& r : clipped)
      out.push_back(std::move(r));
  }
  return out;
}

// ------------------------------ polygon assembly helpers ------------------------------

std::vector<std::unique_ptr<OGRPolygon>> GeometryProjector::Impl::buildShellsFromExteriorRuns(
    std::vector<std::unique_ptr<OGRLineString>>& extRuns, const ProjectionBoundary& b) const
{
  std::vector<std::unique_ptr<OGRPolygon>> shells;
  shells.reserve(extRuns.size());

  for (auto& run : extRuns)
  {
    if (!run || run->getNumPoints() < 2)
      continue;

    auto shellRing = closeRunAlongBoundary(*run, b, /*goCW=*/true);
    if (!shellRing || shellRing->getNumPoints() < 4)
      continue;

    forceExactClosure(*shellRing);

    auto poly = std::make_unique<OGRPolygon>();
    poly->addRing(shellRing.get());
    shells.emplace_back(std::move(poly));
  }

  return shells;
}

std::vector<std::unique_ptr<OGRLinearRing>> GeometryProjector::Impl::cloneInteriorRings(
    const OGRPolygon& shell) const
{
  std::vector<std::unique_ptr<OGRLinearRing>> keep;
  keep.reserve(shell.getNumInteriorRings());

  for (int k = 0; k < shell.getNumInteriorRings(); ++k)
  {
    const OGRLinearRing* r = shell.getInteriorRing(k);
    if (!r)
      continue;

    auto c = std::make_unique<OGRLinearRing>();
    c->addSubLineString(r, 0, r->getNumPoints() - 1);
    c->closeRings();
    forceExactClosure(*c);
    keep.emplace_back(std::move(c));
  }
  return keep;
}

void GeometryProjector::Impl::attachClosedHoleRingToShells(
    const OGRLinearRing& holeRing, std::vector<std::unique_ptr<OGRPolygon>>& shells) const
{
  OGRPolygon holePoly;
  holePoly.addRing(&holeRing);

  for (auto& shell : shells)
  {
    if (!shell || shell->IsEmpty())
      continue;

    if (shell->Contains(&holePoly) || shell->Intersects(&holePoly))
    {
      shell->addRing(&holeRing);
      return;
    }
  }
}

void GeometryProjector::Impl::applyOpenHoleRunAsCut(
    const OGRLineString& openRun,
    const ProjectionBoundary& b,
    std::vector<std::unique_ptr<OGRPolygon>>& shells) const
{
  for (auto& shell : shells)
  {
    if (!shell || shell->IsEmpty())
      continue;

    if (!shell->Intersects(&openRun))
      continue;

    const OGRLinearRing* curExt = shell->getExteriorRing();
    if (!curExt)
      continue;

    auto newExt = mergeOpenHoleRunAsCut(*curExt, openRun, b);
    if (!newExt)
      continue;

    forceExactClosure(*newExt);

    auto keep = cloneInteriorRings(*shell);

    shell->empty();
    shell->addRing(newExt.get());
    for (auto& r : keep)
      shell->addRing(r.get());

    return;  // applied
  }
}

// ------------------------------ corner utilities + core walk ------------------------------

OGRPoint GeometryProjector::Impl::cornerPoint(Corner c, const ProjectionBoundary& b) const
{
  switch (c)
  {
    case Corner::BL:
      return OGRPoint(b.minX, b.minY);
    case Corner::BR:
      return OGRPoint(b.maxX, b.minY);
    case Corner::TR:
      return OGRPoint(b.maxX, b.maxY);
    case Corner::TL:
      return OGRPoint(b.minX, b.maxY);
  }
  return OGRPoint(b.minX, b.minY);
}

double GeometryProjector::Impl::cornerS(Corner c, const ProjectionBoundary& b) const
{
  const double w = b.maxX - b.minX;
  const double h = b.maxY - b.minY;
  switch (c)
  {
    case Corner::BL:
      return 0.0;
    case Corner::BR:
      return w;
    case Corner::TR:
      return w + h;
    case Corner::TL:
      return w + h + w;
  }
  return 0.0;
}

double GeometryProjector::Impl::nextCornerS(double s, const ProjectionBoundary& b, double tol) const
{
  const double w = b.maxX - b.minX;
  const double h = b.maxY - b.minY;
  const double per = 2.0 * (w + h);

  if (s < w - tol)
    return w;
  if (s < w + h - tol)
    return w + h;
  if (s < w + h + w - tol)
    return w + h + w;
  return per;
}

double GeometryProjector::Impl::prevCornerS(double s, const ProjectionBoundary& b, double tol) const
{
  const double w = b.maxX - b.minX;
  const double h = b.maxY - b.minY;

  if (s > w + h + w + tol)
    return w + h + w;
  if (s > w + h + tol)
    return w + h;
  if (s > w + tol)
    return w;
  return 0.0;
}

void GeometryProjector::Impl::appendCornerByS(double s,
                                              const ProjectionBoundary& b,
                                              double tol,
                                              std::vector<OGRPoint>& out) const
{
  if (nearlyEqual(s, cornerS(Corner::BL, b), tol))
    out.push_back(cornerPoint(Corner::BL, b));
  else if (nearlyEqual(s, cornerS(Corner::BR, b), tol))
    out.push_back(cornerPoint(Corner::BR, b));
  else if (nearlyEqual(s, cornerS(Corner::TR, b), tol))
    out.push_back(cornerPoint(Corner::TR, b));
  else if (nearlyEqual(s, cornerS(Corner::TL, b), tol))
    out.push_back(cornerPoint(Corner::TL, b));
}

std::vector<OGRPoint> GeometryProjector::Impl::boundaryPathCore(const OGRPoint& startOnB,
                                                                const OGRPoint& endOnB,
                                                                const ProjectionBoundary& b,
                                                                bool goCW) const
{
  const double tol = boundaryTolMeters(b.minX, b.minY, b.maxX, b.maxY);
  const double w = b.maxX - b.minX;
  const double h = b.maxY - b.minY;
  const double per = 2.0 * (w + h);

  OGRPoint a = startOnB, c = endOnB;
  snapToBoundaryPoint(a, b, tol);
  snapToBoundaryPoint(c, b, tol);

  const double sA = boundaryS(a, b, tol);
  const double sC = boundaryS(c, b, tol);

  if (std::abs(sA - sC) <= tol)
    return {c};

  std::vector<OGRPoint> out;
  double s = sA;

  if (goCW)
  {
    for (int guard = 0; guard < 16; ++guard)
    {
      const double nc = nextCornerS(s, b, tol);

      double distToC = sC - s;
      if (distToC < 0)
        distToC += per;

      double distToCorner = nc - s;
      if (distToCorner < 0)
        distToCorner += per;

      if (distToCorner + tol >= distToC)
        break;

      if (nc >= per - tol)
      {
        out.push_back(cornerPoint(Corner::BL, b));  // wrap
        s = 0.0;
      }
      else
      {
        appendCornerByS(nc, b, tol, out);
        s = nc;
      }
    }
  }
  else
  {
    for (int guard = 0; guard < 16; ++guard)
    {
      const double pc = prevCornerS(s, b, tol);

      double distToC = s - sC;
      if (distToC < 0)
        distToC += per;

      double distToCorner = s - pc;
      if (distToCorner < 0)
        distToCorner += per;

      if (distToCorner + tol >= distToC)
        break;

      appendCornerByS(pc, b, tol, out);
      s = pc;
    }
  }

  out.push_back(c);
  return out;
}

// ------------------------------ geographic densification ------------------------------

double GeometryProjector::Impl::metersPerDegLat(double /*phiRad*/)
{
  return 111320.0;
}

double GeometryProjector::Impl::metersPerDegLon(double phiRad)
{
  return 111320.0 * std::cos(phiRad);
}

double GeometryProjector::Impl::approxSegmentMeters(double lon0,
                                                    double lat0,
                                                    double lon1,
                                                    double lat1)
{
  const double phi = 0.5 * (lat0 + lat1) * kDegToRad;
  const double dx = (lon1 - lon0) * metersPerDegLon(phi);
  const double dy = (lat1 - lat0) * metersPerDegLat(phi);
  return std::sqrt(dx * dx + dy * dy);
}

void GeometryProjector::Impl::densifyGeographicKm(OGRLineString* line, double stepKm) const
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

// ------------------------------ clipping (Liang–Barsky) ------------------------------

GeometryProjector::Impl::ClipHit GeometryProjector::Impl::clipSegmentLB(
    double x0, double y0, double x1, double y1, const ProjectionBoundary& b) const
{
  ClipHit hit;
  double p[4], q[4];
  p[0] = -(x1 - x0);
  q[0] = x0 - b.minX;
  p[1] = (x1 - x0);
  q[1] = b.maxX - x0;
  p[2] = -(y1 - y0);
  q[2] = y0 - b.minY;
  p[3] = (y1 - y0);
  q[3] = b.maxY - y0;

  double u1 = 0.0, u2 = 1.0;
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
        if (t > u1)
          u1 = t;
      }
      else
      {
        if (t < u1)
          return hit;
        if (t < u2)
          u2 = t;
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

void GeometryProjector::Impl::snapToBoundaryPoint(OGRPoint& p,
                                                  const ProjectionBoundary& b,
                                                  double tol)
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

void GeometryProjector::Impl::appendPointIfDifferent(OGRLineString& ls,
                                                     const OGRPoint& p,
                                                     double eps)
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

std::vector<std::unique_ptr<OGRLineString>> GeometryProjector::Impl::clipProjectedLineToBounds(
    const OGRLineString& proj, const ProjectionBoundary& b) const
{
  std::vector<std::unique_ptr<OGRLineString>> runs;
  if (proj.getNumPoints() < 2)
    return runs;

  const double eps = ringEps(b.minX, b.minY, b.maxX, b.maxY);
  const double snapTol = boundaryTolMeters(b.minX, b.minY, b.maxX, b.maxY);

  std::unique_ptr<OGRLineString> cur;

  for (int i = 0; i < proj.getNumPoints() - 1; ++i)
  {
    const double x0 = proj.getX(i), y0 = proj.getY(i);
    const double x1 = proj.getX(i + 1), y1 = proj.getY(i + 1);

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

  mergeCyclicRunsIfConnected(runs, eps);
  return runs;
}

void GeometryProjector::Impl::mergeCyclicRunsIfConnected(
    std::vector<std::unique_ptr<OGRLineString>>& runs, double eps)
{
  if (runs.size() < 2)
    return;

  auto& first = runs.front();
  auto& last = runs.back();
  if (!first || !last)
    return;

  const double fx0 = first->getX(0), fy0 = first->getY(0);
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

// ------------------------------ ring helpers ------------------------------

bool GeometryProjector::Impl::isRingClosed(const OGRLineString& ls, double eps)
{
  const int n = ls.getNumPoints();
  if (n < 4)
    return false;
  return nearlyEqual(ls.getX(0), ls.getX(n - 1), eps) &&
         nearlyEqual(ls.getY(0), ls.getY(n - 1), eps);
}

std::unique_ptr<OGRLinearRing> GeometryProjector::Impl::toClosedRing(const OGRLineString& ls)
{
  auto r = std::make_unique<OGRLinearRing>();
  for (int i = 0; i < ls.getNumPoints(); ++i)
    r->addPoint(ls.getX(i), ls.getY(i));
  r->closeRings();
  forceExactClosure(*r);
  return r;
}

// ------------------------------ boundary traversal ------------------------------

bool GeometryProjector::Impl::isOnBoundary(const OGRPoint& p,
                                           const ProjectionBoundary& b,
                                           double tol) const
{
  return (std::abs(p.getX() - b.minX) <= tol || std::abs(p.getX() - b.maxX) <= tol ||
          std::abs(p.getY() - b.minY) <= tol || std::abs(p.getY() - b.maxY) <= tol);
}

double GeometryProjector::Impl::boundaryS(const OGRPoint& pin,
                                          const ProjectionBoundary& b,
                                          double tol) const
{
  OGRPoint p = pin;
  snapToBoundaryPoint(p, b, tol);

  const double w = b.maxX - b.minX;
  const double h = b.maxY - b.minY;

  if (std::abs(p.getY() - b.minY) <= tol)
    return std::clamp(p.getX() - b.minX, 0.0, w);

  if (std::abs(p.getX() - b.maxX) <= tol)
    return w + std::clamp(p.getY() - b.minY, 0.0, h);

  if (std::abs(p.getY() - b.maxY) <= tol)
    return w + h + std::clamp(b.maxX - p.getX(), 0.0, w);

  return w + h + w + std::clamp(b.maxY - p.getY(), 0.0, h);
}

std::vector<OGRPoint> GeometryProjector::Impl::boundaryPathDirected(const OGRPoint& startOnB,
                                                                    const OGRPoint& endOnB,
                                                                    const ProjectionBoundary& b,
                                                                    bool goCW) const
{
  return boundaryPathCore(startOnB, endOnB, b, goCW);
}

std::vector<OGRPoint> GeometryProjector::Impl::boundaryPathShortest(
    const OGRPoint& startOnB, const OGRPoint& endOnB, const ProjectionBoundary& b) const
{
  const double tol = boundaryTolMeters(b.minX, b.minY, b.maxX, b.maxY);
  const double w = b.maxX - b.minX;
  const double h = b.maxY - b.minY;
  const double per = 2.0 * (w + h);

  OGRPoint a = startOnB, c = endOnB;
  snapToBoundaryPoint(a, b, tol);
  snapToBoundaryPoint(c, b, tol);

  const double sA = boundaryS(a, b, tol);
  const double sC = boundaryS(c, b, tol);

  double cw = sC - sA;
  if (cw < 0)
    cw += per;
  const double ccw = per - cw;
  const bool goCW = (cw <= ccw);

  return boundaryPathCore(a, c, b, goCW);
}

std::unique_ptr<OGRLinearRing> GeometryProjector::Impl::closeRunAlongBoundary(
    const OGRLineString& run, const ProjectionBoundary& b, bool goCW) const
{
  const double tol = boundaryTolMeters(b.minX, b.minY, b.maxX, b.maxY);
  const double eps = ringEps(b.minX, b.minY, b.maxX, b.maxY);

  if (run.getNumPoints() < 2)
    return nullptr;

  OGRPoint first(run.getX(0), run.getY(0));
  OGRPoint last(run.getX(run.getNumPoints() - 1), run.getY(run.getNumPoints() - 1));

  snapToBoundaryPoint(first, b, tol);
  snapToBoundaryPoint(last, b, tol);

  auto ring = std::make_unique<OGRLinearRing>();
  for (int i = 0; i < run.getNumPoints(); ++i)
    ring->addPoint(run.getX(i), run.getY(i));

  if (!nearlyEqual(first.getX(), last.getX(), eps) || !nearlyEqual(first.getY(), last.getY(), eps))
  {
    if (isOnBoundary(last, b, tol) && isOnBoundary(first, b, tol))
    {
      auto path = boundaryPathDirected(last, first, b, goCW);
      for (const auto& p : path)
        ring->addPoint(&p);
    }
    else
    {
      ring->addPoint(&first);
    }
  }

  ring->closeRings();
  forceExactClosure(*ring);
  if (ring->getNumPoints() < 4)
    return nullptr;
  return ring;
}

// ------------------------------ polygon splitting + hole cuts ------------------------------

bool GeometryProjector::Impl::boundaryEdgeContainsS(double s0,
                                                    double s1,
                                                    double s,
                                                    double per) const
{
  if (s0 <= s1)
    return (s >= s0 && s <= s1);
  return (s >= s0 && s < per) || (s >= 0.0 && s <= s1);
}

// ------------------------------ boundary classification helpers ------------------------------

GeometryProjector::Impl::BEdge GeometryProjector::Impl::classifyBoundaryEdge(
    const OGRPoint& pin, const ProjectionBoundary& b, double tol) const
{
  OGRPoint p = pin;
  snapToBoundaryPoint(p, b, tol);

  if (std::abs(p.getY() - b.minY) <= tol)
    return BEdge::Bottom;
  if (std::abs(p.getX() - b.maxX) <= tol)
    return BEdge::Right;
  if (std::abs(p.getY() - b.maxY) <= tol)
    return BEdge::Top;
  if (std::abs(p.getX() - b.minX) <= tol)
    return BEdge::Left;
  return BEdge::None;
}

bool GeometryProjector::Impl::pointOnEdge(const OGRPoint& pin,
                                          BEdge e,
                                          const ProjectionBoundary& b,
                                          double tol) const
{
  OGRPoint p = pin;
  snapToBoundaryPoint(p, b, tol);

  switch (e)
  {
    case BEdge::Bottom:
      return std::abs(p.getY() - b.minY) <= tol;
    case BEdge::Right:
      return std::abs(p.getX() - b.maxX) <= tol;
    case BEdge::Top:
      return std::abs(p.getY() - b.maxY) <= tol;
    case BEdge::Left:
      return std::abs(p.getX() - b.minX) <= tol;
    default:
      return false;
  }
}

bool GeometryProjector::Impl::segmentOnSameEdge(
    const OGRPoint& a, const OGRPoint& c, BEdge e, const ProjectionBoundary& b, double tol) const
{
  return pointOnEdge(a, e, b, tol) && pointOnEdge(c, e, b, tol);
}

bool GeometryProjector::Impl::betweenInclusive(double a, double c, double v, double tol)
{
  const double lo = std::min(a, c) - tol;
  const double hi = std::max(a, c) + tol;
  return v >= lo && v <= hi;
}

int GeometryProjector::Impl::findInsertAfterIndexOnBoundaryEdge(const OGRLinearRing& ring,
                                                                const OGRPoint& pOnBoundary,
                                                                BEdge edge,
                                                                const ProjectionBoundary& b,
                                                                double tol) const
{
  const int n = ring.getNumPoints();
  if (n < 4)
    return -1;

  const int m = n - 1;  // last duplicates first

  // test membership along the varying coordinate depending on edge
  auto fitsBetween = [&](const OGRPoint& a, const OGRPoint& c) -> bool
  {
    switch (edge)
    {
      case BEdge::Right:
      case BEdge::Left:
        return betweenInclusive(a.getY(), c.getY(), pOnBoundary.getY(), tol);
      case BEdge::Bottom:
      case BEdge::Top:
        return betweenInclusive(a.getX(), c.getX(), pOnBoundary.getX(), tol);
      default:
        return false;
    }
  };

  for (int i = 0; i < m; ++i)
  {
    const int j = (i + 1) % m;

    OGRPoint a(ring.getX(i), ring.getY(i));
    OGRPoint c(ring.getX(j), ring.getY(j));

    if (!segmentOnSameEdge(a, c, edge, b, tol))
      continue;

    // Ensure p is on the same edge too
    if (!pointOnEdge(pOnBoundary, edge, b, tol))
      continue;

    if (fitsBetween(a, c))
      return i;
  }

  return -1;
}

// ------------------------------ clipping helpers ------------------------------

void GeometryProjector::Impl::finalizeCurrentRun(std::vector<std::unique_ptr<OGRLineString>>& runs,
                                                 std::unique_ptr<OGRLineString>& cur) const
{
  if (cur && cur->getNumPoints() >= 2)
    runs.emplace_back(std::move(cur));
  cur.reset();
}

void GeometryProjector::Impl::appendSegmentToCurrentRun(std::unique_ptr<OGRLineString>& cur,
                                                        const OGRPoint& a,
                                                        const OGRPoint& c,
                                                        double eps) const
{
  if (!cur)
    cur = std::make_unique<OGRLineString>();

  if (cur->getNumPoints() == 0)
    cur->addPoint(&a);
  else
    appendPointIfDifferent(*cur, a, eps);

  appendPointIfDifferent(*cur, c, eps);
}

int GeometryProjector::Impl::ensureBoundaryVertex(OGRLinearRing& ring,
                                                  const OGRPoint& pOnBoundaryIn,
                                                  const ProjectionBoundary& b,
                                                  double tol) const
{
  ring.closeRings();
  forceExactClosure(ring);

  OGRPoint p = pOnBoundaryIn;
  snapToBoundaryPoint(p, b, tol);

  // Must lie on some boundary edge
  const BEdge edge = classifyBoundaryEdge(p, b, tol);
  if (edge == BEdge::None)
    return -1;

  // 1) If already a vertex, return existing index
  for (int i = 0; i < ring.getNumPoints(); ++i)
  {
    if (nearlyEqual(ring.getX(i), p.getX(), tol) && nearlyEqual(ring.getY(i), p.getY(), tol))
      return i;
  }

  const int n = ring.getNumPoints();
  if (n < 4)
    return -1;

  // 2) Find boundary edge segment where insertion is valid
  const int insertAfter = findInsertAfterIndexOnBoundaryEdge(ring, p, edge, b, tol);
  if (insertAfter < 0)
    return -1;

  // 3) Rebuild ring with inserted point after insertAfter (excluding closure duplicate)
  const int m = n - 1;
  std::vector<OGRPoint> pts;
  pts.reserve(m + 2);

  for (int i = 0; i < m; ++i)
  {
    pts.emplace_back(ring.getX(i), ring.getY(i));
    if (i == insertAfter)
      pts.emplace_back(p.getX(), p.getY());
  }
  pts.push_back(pts.front());  // closure

  ring.empty();
  for (const auto& q : pts)
    ring.addPoint(q.getX(), q.getY());

  ring.closeRings();
  forceExactClosure(ring);

  return insertAfter + 1;
}

GeometryProjector::Impl::BoundaryParams GeometryProjector::Impl::boundaryParams(
    const ProjectionBoundary& b) const
{
  BoundaryParams bp;
  bp.tol = boundaryTolMeters(b.minX, b.minY, b.maxX, b.maxY);
  bp.eps = ringEps(b.minX, b.minY, b.maxX, b.maxY);
  bp.w = b.maxX - b.minX;
  bp.h = b.maxY - b.minY;
  bp.per = 2.0 * (bp.w + bp.h);
  return bp;
}

OGRLinearRing GeometryProjector::Impl::copyExteriorRing(const OGRLinearRing& exterior)
{
  OGRLinearRing ext;
  ext.addSubLineString(&exterior, 0, exterior.getNumPoints() - 1);
  ext.closeRings();
  forceExactClosure(ext);
  return ext;
}

bool GeometryProjector::Impl::getSnappedRunEndpointsOnBoundary(const OGRLineString& holeRun,
                                                               const ProjectionBoundary& b,
                                                               const BoundaryParams& bp,
                                                               OGRPoint& hs,
                                                               OGRPoint& he) const
{
  if (holeRun.getNumPoints() < 2)
    return false;

  hs = OGRPoint(holeRun.getX(0), holeRun.getY(0));
  he = OGRPoint(holeRun.getX(holeRun.getNumPoints() - 1), holeRun.getY(holeRun.getNumPoints() - 1));

  snapToBoundaryPoint(hs, b, bp.tol);
  snapToBoundaryPoint(he, b, bp.tol);

  return isOnBoundary(hs, b, bp.tol) && isOnBoundary(he, b, bp.tol);
}

bool GeometryProjector::Impl::ensureEndpointsAsBoundaryVertices(OGRLinearRing& ext,
                                                                const ProjectionBoundary& b,
                                                                const BoundaryParams& bp,
                                                                const OGRPoint& hs,
                                                                const OGRPoint& he,
                                                                int& iHs,
                                                                int& iHe) const
{
  iHs = ensureBoundaryVertex(ext, hs, b, bp.tol);
  iHe = ensureBoundaryVertex(ext, he, b, bp.tol);

  if (iHs < 0 || iHe < 0 || iHs == iHe)
    return false;

  ext.closeRings();
  forceExactClosure(ext);

  const int n = ext.getNumPoints();
  if (n < 4)
    return false;

  // ensureBoundaryVertex returns an index in the current ring; but we also use m=n-1 for wrap
  // logic. iHs/iHe should be within [0..m-1] already; guard anyway.
  const int m = n - 1;
  if (iHs < 0 || iHs >= m || iHe < 0 || iHe >= m)
    return false;

  return true;
}

double GeometryProjector::Impl::deltaSInc(double s1, double s2, double per) const
{
  double d = s2 - s1;
  if (d > 0.5 * per)
    d -= per;
  else if (d < -0.5 * per)
    d += per;
  return d;  // positive => increasing boundaryS
}

double GeometryProjector::Impl::increasingBoundaryAdvanceScore(const OGRLinearRing& ext,
                                                               int from,
                                                               int to,
                                                               bool forward,
                                                               const ProjectionBoundary& b,
                                                               const BoundaryParams& bp) const
{
  const int n = ext.getNumPoints();
  const int m = n - 1;
  auto nextIdx = [&](int i) { return (i + 1) % m; };
  auto prevIdx = [&](int i) { return (i - 1 + m) % m; };

  auto segLen = [&](int i, int j)
  {
    const double dx = ext.getX(j) - ext.getX(i);
    const double dy = ext.getY(j) - ext.getY(i);
    return std::sqrt(dx * dx + dy * dy);
  };

  double score = 0.0;
  int i = from;
  while (i != to)
  {
    const int j = forward ? nextIdx(i) : prevIdx(i);

    OGRPoint a(ext.getX(i), ext.getY(i));
    OGRPoint c(ext.getX(j), ext.getY(j));

    if (isOnBoundary(a, b, bp.tol) && isOnBoundary(c, b, bp.tol))
    {
      const double sA = boundaryS(a, b, bp.tol);
      const double sC = boundaryS(c, b, bp.tol);
      const double ds = deltaSInc(sA, sC, bp.per);

      if (ds >= -bp.tol)  // allow tiny numeric noise
        score += segLen(i, j);
    }

    i = j;
  }

  return score;
}

double GeometryProjector::Impl::boundaryLengthOnArc(const OGRLinearRing& ext,
                                                    int from,
                                                    int to,
                                                    bool forward,
                                                    const ProjectionBoundary& b,
                                                    const BoundaryParams& bp) const
{
  const int n = ext.getNumPoints();
  const int m = n - 1;

  if (m == 0)
    return 0.0;

  auto nextIdx = [&](int i) { return (i + 1) % m; };
  auto prevIdx = [&](int i) { return (i - 1 + m) % m; };

  auto segLen = [&](int i, int j)
  {
    const double dx = ext.getX(j) - ext.getX(i);
    const double dy = ext.getY(j) - ext.getY(i);
    return std::sqrt(dx * dx + dy * dy);
  };

  double len = 0.0;
  int i = from;
  while (i != to)
  {
    const int j = forward ? nextIdx(i) : prevIdx(i);

    OGRPoint a(ext.getX(i), ext.getY(i));
    OGRPoint c(ext.getX(j), ext.getY(j));

    if (isOnBoundary(a, b, bp.tol) && isOnBoundary(c, b, bp.tol))
      len += segLen(i, j);

    i = j;
  }

  return len;
}

bool GeometryProjector::Impl::chooseReplaceForwardArc(const OGRLinearRing& ext,
                                                      int iHs,
                                                      int iHe,
                                                      const ProjectionBoundary& b,
                                                      const BoundaryParams& bp) const
{
  const double incF = increasingBoundaryAdvanceScore(ext, iHs, iHe, /*forward=*/true, b, bp);
  const double incB = increasingBoundaryAdvanceScore(ext, iHs, iHe, /*forward=*/false, b, bp);

  if (std::abs(incF - incB) > bp.tol)
    return (incF > incB);

  const double lenF = boundaryLengthOnArc(ext, iHs, iHe, /*forward=*/true, b, bp);
  const double lenB = boundaryLengthOnArc(ext, iHs, iHe, /*forward=*/false, b, bp);
  return (lenF >= lenB);
}

void GeometryProjector::Impl::appendReversedHoleRun(OGRLinearRing& out,
                                                    const OGRLineString& holeRun,
                                                    const BoundaryParams& bp,
                                                    const OGRPoint& heV) const
{
  const int nRun = holeRun.getNumPoints();
  if (nRun < 2)
    return;

  const bool dupFirst = nearlyEqual(holeRun.getX(nRun - 1), heV.getX(), bp.tol) &&
                        nearlyEqual(holeRun.getY(nRun - 1), heV.getY(), bp.tol);

  int start = nRun - 1;
  if (dupFirst)
    start = nRun - 2;

  for (int i = start; i >= 0; --i)
    out.addPoint(holeRun.getX(i), holeRun.getY(i));
}

void GeometryProjector::Impl::appendKeptExteriorArc(OGRLinearRing& out,
                                                    const OGRLinearRing& ext,
                                                    int iHs,
                                                    int iHe,
                                                    bool replaceForward,
                                                    const BoundaryParams& /*bp*/) const
{
  const int n = ext.getNumPoints();
  const int m = n - 1;
  auto nextIdx = [&](int i) { return (i + 1) % m; };
  auto prevIdx = [&](int i) { return (i - 1 + m) % m; };

  if (replaceForward)
  {
    // keep backward arc from hs -> he
    int i = iHs;
    while (i != iHe)
    {
      i = prevIdx(i);
      out.addPoint(ext.getX(i), ext.getY(i));
    }
  }
  else
  {
    // keep forward arc from hs -> he
    int i = iHs;
    while (i != iHe)
    {
      i = nextIdx(i);
      out.addPoint(ext.getX(i), ext.getY(i));
    }
  }
}

std::unique_ptr<OGRLinearRing> GeometryProjector::Impl::mergeOpenHoleRunAsCut(
    const OGRLinearRing& exterior, const OGRLineString& holeRun, const ProjectionBoundary& b) const
{
  // Assumes OGC-valid input: exterior CCW, holes CW.
  // Produces CCW exterior output by replacing the boundary-following arc with the reversed hole
  // run.

  const BoundaryParams bp = boundaryParams(b);

  if (holeRun.getNumPoints() < 2)
    return nullptr;

  // 1) Endpoints of hole run snapped to boundary
  OGRPoint hs, he;
  if (!getSnappedRunEndpointsOnBoundary(holeRun, b, bp, hs, he))
    return nullptr;

  // 2) Copy/normalize exterior and ensure hs/he are explicit boundary vertices
  OGRLinearRing ext = copyExteriorRing(exterior);

  int iHs = -1, iHe = -1;
  if (!ensureEndpointsAsBoundaryVertices(ext, b, bp, hs, he, iHs, iHe))
    return nullptr;

  const int n = ext.getNumPoints();
  const int m = n - 1;
  if (m < 3)
    return nullptr;

  const OGRPoint hsV(ext.getX(iHs), ext.getY(iHs));
  const OGRPoint heV(ext.getX(iHe), ext.getY(iHe));

  // 3) Decide which arc hs->he is boundary-following (increasing boundaryS) and should be replaced
  const bool replaceForward = chooseReplaceForwardArc(ext, iHs, iHe, b, bp);

  // 4) Build output: start at he, traverse reversed hole run to hs, then traverse kept exterior arc
  // hs->he
  auto out = std::make_unique<OGRLinearRing>();
  out->addPoint(heV.getX(), heV.getY());

  appendReversedHoleRun(*out, holeRun, bp, heV);

  // Ensure we land exactly on hsV (avoid tiny drift)
  {
    const int k = out->getNumPoints();
    if (k == 0)
      return nullptr;
    if (!nearlyEqual(out->getX(k - 1), hsV.getX(), bp.eps) ||
        !nearlyEqual(out->getY(k - 1), hsV.getY(), bp.eps))
      out->addPoint(hsV.getX(), hsV.getY());
  }

  appendKeptExteriorArc(*out, ext, iHs, iHe, replaceForward, bp);

  out->closeRings();
  forceExactClosure(*out);

  if (out->getNumPoints() < 4)
    return nullptr;

  return out;
}

// ------------------------------ splitPolygonWithHolesFast ------------------------------

std::unique_ptr<OGRGeometry> GeometryProjector::Impl::splitPolygonWithHolesFast(
    const OGRPolygon* polygon) const
{
  ProjectionBoundary b = getProjectionBoundary();
  const double eps = ringEps(b.minX, b.minY, b.maxX, b.maxY);

  const OGRLinearRing* ext = polygon->getExteriorRing();
  if (!ext || ext->getNumPoints() < 4)
    return std::unique_ptr<OGRGeometry>(OGRGeometryFactory::createGeometry(wkbPolygon));

  auto extGeo = ringToLineStringPreserveClosure(ext, /*forceClose=*/true, eps);
  if (m_densifyKm > 0.0)
    densifyGeographicKm(extGeo.get(), m_densifyKm);

  auto extProjRuns = projectToProjectedRunsBestEffort(*extGeo);
  auto extRuns = clipRunsToBounds(extProjRuns, b);

  auto shells = buildShellsFromExteriorRuns(extRuns, b);
  if (shells.empty())
    return std::unique_ptr<OGRGeometry>(OGRGeometryFactory::createGeometry(wkbPolygon));

  for (int h = 0; h < polygon->getNumInteriorRings(); ++h)
  {
    const OGRLinearRing* hole = polygon->getInteriorRing(h);
    if (!hole || hole->getNumPoints() < 3)
      continue;

    auto holeGeo = ringToLineStringPreserveClosure(hole, /*forceClose=*/false, eps);
    if (m_densifyKm > 0.0)
      densifyGeographicKm(holeGeo.get(), m_densifyKm);

    auto holeProjRuns = projectToProjectedRunsBestEffort(*holeGeo);
    auto holeRuns = clipRunsToBounds(holeProjRuns, b);

    for (auto& hr : holeRuns)
    {
      if (!hr || hr->getNumPoints() < 2)
        continue;

      if (isRingClosed(*hr, eps))
      {
        auto holeRing = toClosedRing(*hr);
        if (!holeRing)
          continue;
        forceExactClosure(*holeRing);
        attachClosedHoleRingToShells(*holeRing, shells);
      }
      else
      {
        applyOpenHoleRunAsCut(*hr, b, shells);
      }
    }
  }

  if (shells.size() == 1)
    return std::unique_ptr<OGRGeometry>(shells[0].release());

  auto* mp = new OGRMultiPolygon();
  for (auto& s : shells)
    mp->addGeometry(s.get());
  return std::unique_ptr<OGRGeometry>(mp);
}

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

void GeometryProjector::setPoleHandling(bool enable)
{
  m_impl->setPoleHandling(enable);
}
