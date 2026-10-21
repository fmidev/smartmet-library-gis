// ======================================================================
/*!
 * \brief Fast terrain-aware gridding of point observations
 *
 * Interpolates station observations onto a regular grid without a background field, using
 * only the terrain:
 *
 *  1. Gross error check: each station is compared with the leave-one-out estimate of its
 *     neighbours at similar heights.
 *  2. Local regression value = a + sum_k beta_k * p_k with predictors such as height and
 *     the topographic position index (height relative to the smoothed terrain), fitted at
 *     every cell of a coarse grid from Gaussian-weighted station moments. A regional fit
 *     acts as the prior of the local fit, and the regional fit is pulled toward the
 *     standard atmosphere lapse rate, so the coefficients stay sensible where the stations
 *     do not span enough height.
 *  3. Damped Barnes passes (optionally height-aware) for the residuals. Far from stations
 *     the residual relaxes to zero, so the result falls back to the terrain trend.
 *  4. The smooth fields are combined with the full-resolution terrain.
 *
 * Gaussian weighting is approximated by repeated extended box filters (Gwosdek et al.
 * 2011), as in the fast Barnes interpolation of Zürcher (2023). The cost is
 * O(stations + cells) independent of the length scales, and no neighbour searches are
 * needed.
 *
 * All coordinates are in a projected, preferably equal-area, coordinate system in metres.
 * The grid should be fixed per area (Europe, Africa...) so that all requests for the same
 * time share the same field.
 */
// ======================================================================

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace Fmi
{
namespace PointGridding
{
// Regular grid. Cell centres are at x0 + (i + 0.5) * dx, y0 + (j + 0.5) * dx, and the data
// is stored row by row with j growing northwards.
struct Grid
{
  double x0 = 0;
  double y0 = 0;
  double dx = 0;
  std::size_t nx = 0;
  std::size_t ny = 0;

  std::size_t size() const { return nx * ny; }
  Grid coarsen(std::size_t factor) const;
  bool contains(double x, double y) const;
  bool operator==(const Grid& other) const;
};

class Field
{
 public:
  Field() = default;
  explicit Field(const Grid& grid, double value = 0.0);
  Field(const Grid& grid, std::vector<double> values);

  const Grid& grid() const { return itsGrid; }
  double& operator()(std::size_t i, std::size_t j) { return itsData[j * itsGrid.nx + i]; }
  double operator()(std::size_t i, std::size_t j) const { return itsData[j * itsGrid.nx + i]; }
  double* data() { return itsData.data(); }
  const double* data() const { return itsData.data(); }
  const std::vector<double>& values() const { return itsData; }

  // Bilinear interpolation, clamped to the grid edges
  double sample(double x, double y) const;

  // Bilinear resampling onto another grid
  Field resample(const Grid& target) const;

 private:
  Grid itsGrid;
  std::vector<double> itsData;
};

struct Station
{
  double x = 0;
  double y = 0;
  double elevation = 0;  // metres, the station's own altitude, not the grid cell mean
  double value = 0;
};

enum class Predictor : std::uint8_t
{
  Height,  // (h - referenceHeight) in km
  TPI      // topographic position index (h - smoothed terrain) in km
};

struct Options
{
  // Terrain regression. No predictors means a smooth level only, which with no fixed lapse
  // rate either makes the whole method a damped multi-scale Barnes interpolation (suitable
  // for pressure, wind components and other fields without a height dependency).
  std::vector<Predictor> predictors{Predictor::Height, Predictor::TPI};
  std::optional<double> fixedLapseRate;  // K/km, applied instead of a fitted height term

  double referenceHeight = 500.0;  // m
  double priorLapseRate = -6.5;    // K/km

  double regionalSigma = 600e3;  // m
  double localSigma = 150e3;     // m

  // Ridge penalties in station * km^2: a coefficient stays at its prior until the local
  // stations span enough of that predictor.
  double regionalHeightRidge = 20 * 0.3 * 0.3;
  double regionalTPIRidge = 20 * 0.1 * 0.1;
  double localHeightRidge = 3 * 0.3 * 0.3;
  double localTPIRidge = 3 * 0.1 * 0.1;

  double levelPrior = 1.0;  // pseudo stations pulling the local level toward the regional

  // Residual passes. With bilateralSigmaH set the passes are height-aware:
  // w = exp(-d^2 / 2 sigma^2) * exp(-dh^2 / 2 sigmaH^2)
  std::vector<double> residualSigmas{40e3};
  std::optional<double> bilateralSigmaH = 300.0;  // m
  double layerSpacing = 250.0;                    // m, bilateral grid layers
  double maxHeight = 4000.0;                      // m, highest bilateral layer

  double damping = 0.5;  // station equivalents, residuals relax to zero far from stations

  std::size_t coarsen = 4;  // smooth fields are computed on a grid this much coarser

  // Gross error check
  bool qc = true;
  double qcSigma = 40e3;           // m, comparison scale
  double qcSigmaH = 300.0;         // m, height scale of the comparison
  double qcFallbackSigma = 80e3;   // m, horizontal comparison for isolated heights
  double qcSpreadSigma = 150e3;    // m, scale of the local innovation spread
  double qcMinThreshold = 6.0;     // minimum rejection threshold (value units)
  double qcThreshold = 4.0;        // rejection threshold in local spreads
  double qcMinSupport = 1.0;       // station equivalents needed to judge a station
  unsigned int qcIterations = 3;

  unsigned int threads = 0;  // 0 = hardware concurrency
};

// Static terrain fields for one area, computed once and shared by all analyses
class Terrain
{
 public:
  // Elevations in metres on grid, negative values (bathymetry) are treated as sea level
  Terrain(const Grid& grid, std::vector<double> elevation, double tpiSigma = 10e3);

  const Grid& grid() const { return itsElevation.grid(); }
  const Field& elevation() const { return itsElevation; }
  const Field& smoothElevation() const { return itsSmoothElevation; }

 private:
  Field itsElevation;
  Field itsSmoothElevation;
};

enum class StationStatus : std::uint8_t
{
  Used,
  Outside,  // outside the grid or a non-finite value
  Rejected  // failed the gross error check
};

class Analysis
{
 public:
  Analysis(const Terrain& terrain, const std::vector<Station>& stations, const Options& options);
  ~Analysis();
  Analysis(const Analysis& other) = delete;
  Analysis& operator=(const Analysis& other) = delete;
  Analysis(Analysis&& other) noexcept;
  Analysis& operator=(Analysis&& other) = delete;

  // The analysed field on the terrain grid
  Field grid() const;

  // The analysis at a point with the given altitude
  double predict(double x, double y, double elevation) const;

  // Status of each input station
  const std::vector<StationStatus>& status() const;

  // Fitted height coefficient (K/km) on the coarse grid, if height is a predictor
  std::optional<Field> lapseRate() const;

  class Impl;

 private:
  std::unique_ptr<Impl> impl;
};

// Gaussian-weighted sums sum_i w(d_i) * values_i at every cell of grid, with w(0) = 1. The
// values are given per quantity: quantities[k][i] is quantity k of station i. Exposed for
// testing and for other gridding needs.
std::vector<Field> gaussianSums(const Grid& grid,
                                const std::vector<double>& x,
                                const std::vector<double>& y,
                                const std::vector<std::vector<double>>& quantities,
                                double sigma,
                                unsigned int threads = 0);

}  // namespace PointGridding
}  // namespace Fmi
