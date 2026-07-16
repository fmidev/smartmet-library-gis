// ======================================================================
/*!
 * \brief Benchmark for GeometrySmoother, mimicking the WMS isoline/isoband
 *        smoothing that dominates cold-tile latency for on-the-fly contour
 *        layers (see opengeoweb/PRESET-LAYER-USAGE.md).
 *
 * Not a unit test: named *Benchmark (not *Test) so `make test` does not run it.
 * Build and run manually:
 *     cd test && make GeometrySmootherBenchmark && ./GeometrySmootherBenchmark
 *
 * All four weight kernels walk an identical stencil with an identical
 * dist>=radius break, so the ONLY difference between them is the arithmetic in
 * the weight function. Gaussian is the kernel every FMI WMS filter uses, and it
 * is the only one that used std::exp in the hot loop. Comparing Gaussian against
 * Tukey (same stencil, cheap polynomial weight) therefore isolates the exp cost.
 * With the constexpr lookup-table replacement in GeometrySmoother.cpp the
 * Gaussian time now tracks Tukey; before it, Gaussian was markedly slower,
 * especially on older glibc (RHEL8 std::exp measured ~3x slower than a LUT).
 */
// ======================================================================

#include "GeometrySmoother.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ogr_geometry.h>
#include <vector>

using namespace Fmi;
using Clock = std::chrono::steady_clock;

namespace
{
double secs(Clock::time_point a, Clock::time_point b)
{
  return std::chrono::duration<double>(b - a).count();
}

// Wiggly, densely-sampled polylines resembling marching-squares contour output:
// ~1 px vertex spacing, so a radius-R Gaussian pulls ~2R neighbours into every
// vertex's stencil.
std::vector<OGRGeometryPtr> make_contours(int nlines, int nverts)
{
  std::vector<OGRGeometryPtr> geoms;
  geoms.reserve(nlines);
  for (int l = 0; l < nlines; ++l)
  {
    auto* ls = new OGRLineString;
    double phase = 0.13 * l;
    double y0 = 3.0 * l;
    for (int i = 0; i < nverts; ++i)
    {
      double x = i * 1.0;
      double y = y0 + 6.0 * std::sin(0.05 * i + phase) + 2.0 * std::sin(0.17 * i + 2 * phase);
      ls->addPoint(x, y);
    }
    geoms.emplace_back(ls);
  }
  return geoms;
}

std::vector<OGRGeometryPtr> clone(const std::vector<OGRGeometryPtr>& in)
{
  std::vector<OGRGeometryPtr> out;
  out.reserve(in.size());
  for (const auto& g : in)
    out.emplace_back(g->clone());
  return out;
}

double run(const std::vector<OGRGeometryPtr>& base,
           GeometrySmoother::Type type,
           double radius,
           int reps)
{
  double best = 1e30;
  for (int r = 0; r < reps; ++r)
  {
    auto geoms = clone(base);
    GeometrySmoother s;
    s.type(type);
    s.radius(radius);
    s.iterations(1);
    auto t0 = Clock::now();
    s.apply(geoms, false);
    best = std::min(best, secs(t0, Clock::now()));
  }
  return best;
}
}  // namespace

int main()
{
  const int nlines = 300;
  const int nverts = 600;
  const int reps = 7;
  auto base = make_contours(nlines, nverts);

  long verts = 0;
  for (const auto& g : base)
    verts += static_cast<const OGRLineString*>(g.get())->getNumPoints();
  printf("GeometrySmoother benchmark: %d lines x %d verts = %ld vertices, %d reps (best of)\n\n",
         nlines,
         nverts,
         verts,
         reps);
  printf("%-8s %10s %10s %10s %10s   %s\n",
         "radius",
         "Average",
         "Linear",
         "Tukey",
         "Gaussian",
         "Gaussian vs Tukey (= exp cost)");

  for (double radius : {8.0, 16.0, 20.0})
  {
    double a = run(base, GeometrySmoother::Type::Average, radius, reps);
    double l = run(base, GeometrySmoother::Type::Linear, radius, reps);
    double t = run(base, GeometrySmoother::Type::Tukey, radius, reps);
    double g = run(base, GeometrySmoother::Type::Gaussian, radius, reps);
    printf(
        "%-8.0f %9.4fs %9.4fs %9.4fs %9.4fs   %+.1f%%\n", radius, a, l, t, g, 100.0 * (g - t) / t);
  }
  return 0;
}
