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
 *  3. Optimal interpolation (or damped Barnes passes) of the residuals, optionally
 *     height-aware. Far from stations the residual relaxes to zero, so the result falls back
 *     to the terrain trend.
 *  4. The smooth fields are combined with the full-resolution terrain.
 *
 * Gaussian weighting is approximated by repeated extended box filters (Gwosdek et al.
 * 2011), as in the fast Barnes interpolation of Zürcher (2023). The cost of the gridding is
 * O(stations + cells) independent of the length scales. The only neighbour search is that of
 * the optimal interpolation weights, which is over the stations, not the grid.
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
  Height,  // (h - referenceHeight) in km, with a background (h - model height) in km
  TPI      // topographic position index (h - smoothed terrain) in km
};

// How the residuals from the terrain trend are interpolated
enum class Residuals : std::uint8_t
{
  Barnes,  // damped Barnes passes over Options::residualSigmas
  OI       // optimal interpolation (simple kriging) with a fixed covariance
};

// A first guess such as a forecast, given on the terrain grid. The observations then correct
// the background instead of being gridded on their own: the background is moved from the
// model orography to the actual heights with Options::backgroundLapseRate, and the differences
// of the observations from it are gridded. Far from the stations the result relaxes to the
// background, which is what is wanted over the seas.
struct Background
{
  Field value;      // the background on the terrain grid, NaN where it is missing
  Field elevation;  // the model orography on the terrain grid in metres, NaN where missing
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
  double localHeightRidge = 9 * 0.3 * 0.3;
  double localTPIRidge = 9 * 0.1 * 0.1;

  double levelPrior = 1.0;  // pseudo stations pulling the local level toward the regional

  // Residual analysis. Optimal interpolation solves the station weights alpha from
  // (C + oiErrorRatio I) alpha = r, C the covariances of the residuals r at the stations, and
  // the estimate is sum_i alpha_i c(d_i). Unlike Barnes it accounts for the redundancy of
  // clustered stations, and it relaxes to the trend far from the stations without damping.
  // The covariance is the mean of Gaussians of oiScales, times a height term of scale
  // oiSigmaH when set. In cross-validation it was slightly more accurate than the Barnes
  // passes (temperature 1.572 K vs 1.602 K) at about the same cost; the terrain trend
  // matters far more than either.
  Residuals residuals = Residuals::OI;
  std::vector<double> oiScales{120e3, 50e3};  // m
  std::optional<double> oiSigmaH = 500.0;     // m
  double oiErrorRatio = 0.5;  // observation error variance relative to the residual variance

  // Barnes residual passes. With bilateralSigmaH set the passes are height-aware:
  // w = exp(-d^2 / 2 sigma^2) * exp(-dh^2 / 2 sigmaH^2)
  std::vector<double> residualSigmas{60e3, 25e3};
  std::optional<double> bilateralSigmaH = 300.0;  // m
  double layerSpacing = 250.0;                    // m, bilateral grid layers
  double maxHeight = 4000.0;                      // m, highest bilateral layer

  double damping = 0.25;  // Barnes: station equivalents, residuals relax to zero far away

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

  // Background. The weight is that of the corrected background among the stations, the rest
  // comes from the analysis of the observations alone. A 25 km forecast does not resolve the
  // Alps, and the blend 0.7 gave the best cross-validation scores for temperature (1.46 K, the
  // corrected background alone 1.49 K, the observations alone 1.62 K). Far from the stations
  // the weight rises to 1 with the station support S/(S+1), S the station equivalents within
  // localSigma.
  double backgroundLapseRate = -6.5;  // K/km, from the model orography to the actual height
  double backgroundWeight = 0.7;
  // Where the background is missing the observations alone are used. The weight of the
  // background fades to zero over about this distance from the edges of its data, so that
  // the result has no seam there.
  double backgroundTaper = 50e3;  // m

  // Physical range of the analysed quantity, for example 0...100 for relative humidity or a
  // lower limit of 0 for wind speed. With a background the limits apply to the values, not to
  // the corrections. The trend and every residual pass are limited before the next pass sees
  // them, so a station at a limit does not spread a spurious residual around it, and the gross
  // error check compares the stations with limited predictions. Station values out of range by
  // at most qcMinThreshold are clamped, values farther out are rejected when qc is enabled.
  std::optional<double> lowerLimit;
  std::optional<double> upperLimit;

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
  Rejected  // failed the gross error check, or far outside the limits of the values
};

class Analysis
{
 public:
  Analysis(const Terrain& terrain, const std::vector<Station>& stations, const Options& options);

  // Correct a background with the observations. The background must be on the terrain grid.
  Analysis(const Terrain& terrain,
           const std::vector<Station>& stations,
           const Background& background,
           const Options& options);

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

  // Fitted height coefficient (K/km) on the coarse grid, if height is a predictor. With a
  // background this is the correction to backgroundLapseRate.
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
