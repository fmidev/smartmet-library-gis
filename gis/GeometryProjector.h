// GeometryProjector.h
#pragma once

#include <memory>
#include <ogr_geometry.h>
#include <ogr_spatialref.h>
#include <vector>

class GeometryProjector
{
 public:
  GeometryProjector(OGRSpatialReference* sourceSRS, OGRSpatialReference* targetSRS);
  ~GeometryProjector() = default;

  GeometryProjector(const GeometryProjector&) = delete;
  GeometryProjector& operator=(const GeometryProjector&) = delete;
  GeometryProjector(GeometryProjector&&) noexcept = default;
  GeometryProjector& operator=(GeometryProjector&&) noexcept = default;

  void setProjectedBounds(double minX, double minY, double maxX, double maxY);

  // Default 50km. Set <=0 to disable geographic densification.
  void setDensifyResolutionKm(double km);

  // Project + clip to bounds. Returns nullptr only if input geom is nullptr.
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

  double m_densifyKm = 50.0;

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

  // Insert a boundary point into a ring along an existing boundary edge segment (if not already a
  // vertex).
  // Returns the vertex index of the inserted (or existing) point, or -1 on failure.
  int ensureBoundaryVertex(OGRLinearRing& ring,
                           const OGRPoint& pOnBoundary,
                           const ProjectionBoundary& b,
                           double tol) const;

  bool boundaryEdgeContainsS(double s0, double s1, double s, double per) const;

  // ---- best-effort projection helpers ----

  // Project a geographic line to projected "valid runs" (split at projection failure).
  std::vector<std::unique_ptr<OGRLineString>> projectToProjectedRunsBestEffort(
      const OGRLineString& geo) const;

  // Clip a set of projected runs to bounds and concatenate the results.
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

  // Return the next/prev corner "s" position on the perimeter given a perimeter coordinate s.
  double nextCornerS(double s, const ProjectionBoundary& b, double tol) const;
  double prevCornerS(double s, const ProjectionBoundary& b, double tol) const;

  // Append a corner point based on its exact cornerS value.
  void appendCornerByS(double s,
                       const ProjectionBoundary& b,
                       double tol,
                       std::vector<OGRPoint>& out) const;

  // Core boundary-walk (directed). boundaryPathShortest becomes a thin wrapper.
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

  // Copy + normalize an exterior ring to a mutable ring (closed, exact closure).
  static OGRLinearRing copyExteriorRing(const OGRLinearRing& exterior);

  // Snap endpoints of an open run to boundary and validate they are on boundary.
  bool getSnappedRunEndpointsOnBoundary(const OGRLineString& holeRun,
                                        const ProjectionBoundary& b,
                                        const BoundaryParams& bp,
                                        OGRPoint& hs,
                                        OGRPoint& he) const;

  // Ensure hs/he exist as vertices in ext; return their indices (in [0..m-1]).
  bool ensureEndpointsAsBoundaryVertices(OGRLinearRing& ext,
                                         const ProjectionBoundary& b,
                                         const BoundaryParams& bp,
                                         const OGRPoint& hs,
                                         const OGRPoint& he,
                                         int& iHs,
                                         int& iHe) const;

  // Choose which arc (forward vs backward from hs to he) is the "boundary-following" arc
  // (i.e. advances along boundaryS in the increasing-s sense), and should be replaced.
  bool chooseReplaceForwardArc(const OGRLinearRing& ext,
                               int iHs,
                               int iHe,
                               const ProjectionBoundary& b,
                               const BoundaryParams& bp) const;

  // Append reversed hole run (he->hs) into an output ring, skipping duplicates.
  void appendReversedHoleRun(OGRLinearRing& out,
                             const OGRLineString& holeRun,
                             const BoundaryParams& bp,
                             const OGRPoint& heV) const;

  // Append the "kept" exterior arc from hs to he into out.
  void appendKeptExteriorArc(OGRLinearRing& out,
                             const OGRLinearRing& ext,
                             int iHs,
                             int iHe,
                             bool replaceForward,
                             const BoundaryParams& bp) const;

  // Signed delta in boundaryS direction normalized to (-per/2, +per/2], positive means increasing.
  double deltaSInc(double s1, double s2, double per) const;

  // Accumulate how much of an arc advances along increasing boundaryS (weighted by boundary segment
  // length).
  double increasingBoundaryAdvanceScore(const OGRLinearRing& ext,
                                        int from,
                                        int to,
                                        bool forward,
                                        const ProjectionBoundary& b,
                                        const BoundaryParams& bp) const;

  // Total boundary length along an arc (only counts segments fully on boundary).
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
