// ======================================================================
/*!
 * \brief Tests for Fmi::PointGridding
 *
 * data/pointgridding contains an Alpine DEM window and synthetic stations analysed by
 * the Python prototype the algorithm was developed with, see data/pointgridding/README.
 */
// ======================================================================

#include <gtest/gtest.h>

#include "PointGridding.h"
#include <macgyver/Exception.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <functional>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

using namespace Fmi::PointGridding;

namespace
{
const std::string datadir = "data/pointgridding/";

const Grid reference_grid{3900000.0, 2350000.0, 2500.0, 240, 200};

std::vector<double> read_f32(const std::string& path, std::size_t n)
{
  std::ifstream in(path, std::ios::binary);
  if (!in)
    throw std::runtime_error("Failed to open " + path);
  std::vector<float> buffer(n);
  in.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(n * sizeof(float)));
  if (!in)
    throw std::runtime_error("Failed to read " + path);
  return {buffer.begin(), buffer.end()};
}

std::vector<std::vector<double>> read_csv(const std::string& path)
{
  std::ifstream in(path);
  if (!in)
    throw std::runtime_error("Failed to open " + path);
  std::vector<std::vector<double>> rows;
  std::string line;
  std::getline(in, line);  // header
  while (std::getline(in, line))
  {
    std::vector<double> row;
    std::stringstream ss(line);
    std::string cell;
    while (std::getline(ss, cell, ','))
      row.push_back(std::stod(cell));
    rows.push_back(row);
  }
  return rows;
}

const Terrain& reference_terrain()
{
  static const Terrain terrain(reference_grid,
                               read_f32(datadir + "dem.f32", reference_grid.size()));
  return terrain;
}

std::vector<Station> reference_stations()
{
  std::vector<Station> stations;
  for (const auto& row : read_csv(datadir + "stations.csv"))
    stations.push_back(Station{row.at(0), row.at(1), row.at(2), row.at(3)});
  return stations;
}

// Exact Gaussian weighted sums for comparison
double exact_sum(const Grid& grid,
                 std::size_t i,
                 std::size_t j,
                 const std::vector<double>& x,
                 const std::vector<double>& y,
                 const std::vector<double>& v,
                 double sigma)
{
  const double cx = grid.x0 + (i + 0.5) * grid.dx;
  const double cy = grid.y0 + (j + 0.5) * grid.dx;
  double sum = 0;
  for (std::size_t s = 0; s < x.size(); s++)
  {
    const double d2 = (x[s] - cx) * (x[s] - cx) + (y[s] - cy) * (y[s] - cy);
    sum += v[s] * std::exp(-0.5 * d2 / (sigma * sigma));
  }
  return sum;
}
}  // namespace

TEST(PointGridding, FieldSampling)
{
  const Grid grid{0, 0, 10, 3, 2};
  Field f(grid, std::vector<double>{0, 1, 2, 10, 11, 12});
  EXPECT_DOUBLE_EQ(f.sample(5, 5), 0);       // cell centre
  EXPECT_DOUBLE_EQ(f.sample(10, 5), 0.5);    // between cells
  EXPECT_DOUBLE_EQ(f.sample(15, 10), 6);     // centre of four cells
  EXPECT_DOUBLE_EQ(f.sample(-100, 5), 0);    // clamped to the edge
  EXPECT_DOUBLE_EQ(f.sample(100, 100), 12);  // clamped to the corner
}

TEST(PointGridding, GaussianSumsApproximateExactWeights)
{
  const Grid grid{0, 0, 2500, 200, 160};
  std::mt19937 gen(1);
  std::uniform_real_distribution<double> ux(0, 500e3);
  std::uniform_real_distribution<double> uy(0, 400e3);
  std::normal_distribution<double> nz(0, 3);
  std::vector<double> x;
  std::vector<double> y;
  std::vector<double> z;
  for (int s = 0; s < 300; s++)
  {
    x.push_back(ux(gen));
    y.push_back(uy(gen));
    z.push_back(nz(gen));
  }
  const double sigma = 40e3;
  const auto sums =
      gaussianSums(grid, x, y, {std::vector<double>(x.size(), 1.0), z}, sigma, 2);

  // Compare away from the edges. The box filter approximation of the Gaussian and the
  // bilinear splatting make the weights differ by a few percent.
  double max_weight = 0;
  double max_error = 0;
  double max_value_error = 0;
  for (std::size_t j = 30; j < grid.ny - 30; j += 7)
    for (std::size_t i = 30; i < grid.nx - 30; i += 7)
    {
      const double w = exact_sum(grid, i, j, x, y, std::vector<double>(x.size(), 1.0), sigma);
      const double wz = exact_sum(grid, i, j, x, y, z, sigma);
      max_weight = std::max(max_weight, w);
      max_error = std::max(max_error, std::abs(sums[0](i, j) - w));
      max_value_error = std::max(max_value_error, std::abs(sums[1](i, j) / sums[0](i, j) - wz / w));
    }
  EXPECT_LT(max_error / max_weight, 0.1);
  EXPECT_LT(max_value_error, 0.15);
}

TEST(PointGridding, ConstantFieldIsReproduced)
{
  const Grid grid{0, 0, 5000, 120, 100};
  const Terrain terrain(grid, std::vector<double>(grid.size(), 300.0));
  std::vector<Station> stations;
  std::mt19937 gen(2);
  std::uniform_real_distribution<double> ux(10e3, 590e3);
  std::uniform_real_distribution<double> uy(10e3, 490e3);
  for (int s = 0; s < 100; s++)
    stations.push_back(Station{ux(gen), uy(gen), 300.0, 7.5});

  Options options;
  options.predictors.clear();
  options.bilateralSigmaH.reset();
  options.qc = false;
  const Analysis analysis(terrain, stations, options);
  const auto f = analysis.grid();
  for (double v : f.values())
    ASSERT_NEAR(v, 7.5, 1e-9);
}

TEST(PointGridding, StationsOutsideTheGridAreIgnored)
{
  const Grid grid{0, 0, 5000, 40, 40};
  const Terrain terrain(grid, std::vector<double>(grid.size(), 0.0));
  const std::vector<Station> stations{{100e3, 100e3, 0, 1.0},
                                      {-5e3, 100e3, 0, 50.0},
                                      {100e3, 100e3, 0, std::nan("")}};
  Options options;
  options.qc = false;
  const Analysis analysis(terrain, stations, options);
  ASSERT_EQ(analysis.status().size(), 3U);
  EXPECT_EQ(analysis.status()[0], StationStatus::Used);
  EXPECT_EQ(analysis.status()[1], StationStatus::Outside);
  EXPECT_EQ(analysis.status()[2], StationStatus::Outside);
  EXPECT_NEAR(analysis.predict(100e3, 100e3, 0), 1.0, 1e-9);
}

TEST(PointGridding, MatchesPrototype)
{
  const auto& terrain = reference_terrain();
  const auto stations = reference_stations();
  const auto expected = read_csv(datadir + "expected_stations.csv");
  ASSERT_EQ(stations.size(), expected.size());

  // The prototype uses the Barnes residual passes
  Options options;
  options.residuals = Residuals::Barnes;
  const auto t0 = std::chrono::steady_clock::now();
  const Analysis analysis(terrain, stations, options);
  const auto f = analysis.grid();
  const auto t1 = std::chrono::steady_clock::now();
  std::cout << "Analysis of " << stations.size() << " stations on " << reference_grid.nx << "x"
            << reference_grid.ny << " grid: "
            << std::chrono::duration<double, std::milli>(t1 - t0).count() << " ms\n";

  // Gross errors
  for (std::size_t s = 0; s < stations.size(); s++)
  {
    const bool rejected = (analysis.status()[s] == StationStatus::Rejected);
    EXPECT_EQ(rejected, expected[s].at(1) != 0) << "station " << s;
  }

  // Values at the stations
  double max_station_error = 0;
  for (std::size_t s = 0; s < stations.size(); s++)
  {
    const auto& st = stations[s];
    max_station_error = std::max(
        max_station_error, std::abs(analysis.predict(st.x, st.y, st.elevation) - expected[s][0]));
  }
  EXPECT_LT(max_station_error, 1e-4);

  // The grid, stored as float32
  const auto grid = read_f32(datadir + "expected_grid.f32", reference_grid.size());
  double max_grid_error = 0;
  for (std::size_t n = 0; n < grid.size(); n++)
    max_grid_error = std::max(max_grid_error, std::abs(f.data()[n] - grid[n]));
  EXPECT_LT(max_grid_error, 1e-4);
}

TEST(PointGridding, ThreadCountDoesNotChangeTheResult)
{
  const auto& terrain = reference_terrain();
  const auto stations = reference_stations();
  for (auto residuals : {Residuals::Barnes, Residuals::OI})
  {
    Options one;
    one.residuals = residuals;
    one.threads = 1;
    Options many = one;
    many.threads = 7;
    const auto a = Analysis(terrain, stations, one).grid();
    const auto b = Analysis(terrain, stations, many).grid();
    for (std::size_t n = 0; n < a.values().size(); n++)
      ASSERT_EQ(a.values()[n], b.values()[n]);
  }
}

namespace
{
// A flat or sloping test area with stations on its western half only
struct BackgroundCase
{
  Grid grid{0, 0, 5000, 120, 80};
  std::vector<double> dem;
  std::vector<Station> stations;

  explicit BackgroundCase(double slope = 0.0)
  {
    for (std::size_t j = 0; j < grid.ny; j++)
      for (std::size_t i = 0; i < grid.nx; i++)
        dem.push_back(slope * static_cast<double>(i));
  }

  double height(double x) const { return std::max(0.0, x / grid.dx - 0.5) * dem.at(1); }

  void addStations(const std::function<double(double, double, double)>& truth)
  {
    std::mt19937 gen(3);
    std::uniform_real_distribution<double> ux(10e3, 280e3);
    std::uniform_real_distribution<double> uy(10e3, 390e3);
    for (int s = 0; s < 150; s++)
    {
      const double x = ux(gen);
      const double y = uy(gen);
      const double h = height(x);
      stations.push_back(Station{x, y, h, truth(x, y, h)});
    }
  }
};

// The test area is only 600 km wide, so the scales are reduced to match
Options background_options(double weight)
{
  Options options;
  options.qc = false;
  options.backgroundWeight = weight;
  options.regionalSigma = 100e3;
  options.localSigma = 40e3;
  return options;
}
}  // namespace

TEST(PointGridding, BackgroundBiasIsCorrectedNearStationsOnly)
{
  // The background is 2 degrees too warm everywhere, the observations are right
  BackgroundCase c;
  c.addStations([](double, double, double) { return 5.0; });
  const Terrain terrain(c.grid, c.dem);
  const Background bg{Field(c.grid, 7.0), Field(c.grid, 0.0)};
  const Analysis analysis(terrain, c.stations, bg, background_options(1.0));

  // Among the stations the bias is gone, far from them the background is kept
  EXPECT_NEAR(analysis.predict(150e3, 200e3, 0), 5.0, 0.05);
  EXPECT_NEAR(analysis.predict(595e3, 200e3, 0), 7.0, 0.3);
  const auto f = analysis.grid();
  for (double v : f.values())
  {
    ASSERT_GE(v, 5.0 - 0.01);
    ASSERT_LE(v, 7.0 + 0.01);
  }
}

TEST(PointGridding, BackgroundIsMovedToTheActualHeight)
{
  // The terrain rises eastwards, the model orography is flat, the truth follows the standard
  // atmosphere: the background moved with the lapse rate needs no correction at all
  BackgroundCase c(10.0);
  const auto truth = [](double, double, double h) { return 10.0 - 6.5 * h / 1000.0; };
  c.addStations(truth);
  const Terrain terrain(c.grid, c.dem);
  const Background bg{Field(c.grid, 10.0), Field(c.grid, 0.0)};
  const Analysis analysis(terrain, c.stations, bg, background_options(1.0));

  const auto f = analysis.grid();
  for (std::size_t j = 0; j < c.grid.ny; j += 7)
    for (std::size_t i = 0; i < c.grid.nx; i += 7)
      ASSERT_NEAR(f(i, j), truth(0, 0, c.dem[j * c.grid.nx + i]), 1e-6);
}

TEST(PointGridding, BackgroundBlendAndMissingValues)
{
  BackgroundCase c;
  c.addStations([](double x, double, double) { return 5.0 + x / 100e3; });
  const Terrain terrain(c.grid, c.dem);
  Field value(c.grid, 3.0);
  for (std::size_t j = 0; j < c.grid.ny; j++)
    for (std::size_t i = 100; i < c.grid.nx; i++)
      value(i, j) = std::nan("");  // the eastern edge is outside the model
  const Background bg{value, Field(c.grid, 0.0)};

  const Analysis corrected(terrain, c.stations, bg, background_options(1.0));
  const Analysis blended(terrain, c.stations, bg, background_options(0.7));
  const Analysis plain(terrain, c.stations, background_options(1.0));

  // Among the stations the blend is close to 0.7 * corrected + 0.3 * plain, and away from them
  // and from the edge of the background data at 500 km the corrected background is kept
  for (double x : {100e3, 150e3, 200e3})
  {
    const double c0 = corrected.predict(x, 200e3, 0);
    const double p0 = plain.predict(x, 200e3, 0);
    EXPECT_NEAR(blended.predict(x, 200e3, 0), 0.7 * c0 + 0.3 * p0, 0.05 * std::abs(c0 - p0) + 1e-6)
        << "x=" << x;
  }
  EXPECT_NEAR(blended.predict(410e3, 200e3, 0), corrected.predict(410e3, 200e3, 0), 0.05);

  // Where the background is missing the plain analysis is used, and there is no seam at the
  // edge: the largest step between neighbouring cells is no larger than elsewhere
  for (const auto* a : {&corrected, &blended})
  {
    const auto f = a->grid();
    const auto fp = plain.grid();
    EXPECT_DOUBLE_EQ(f(110, 40), fp(110, 40));
    double edge_step = 0;
    double inner_step = 0;
    for (std::size_t i = 1; i < c.grid.nx; i++)
    {
      const double step = std::abs(f(i, 40) - f(i - 1, 40));
      if (i >= 95 && i <= 105)
        edge_step = std::max(edge_step, step);
      else if (i >= 60 && i <= 90)
        inner_step = std::max(inner_step, step);
    }
    EXPECT_LT(edge_step, 2 * inner_step + 0.02);
  }
}

TEST(PointGridding, BackgroundMustBeOnTheTerrainGrid)
{
  BackgroundCase c;
  const Terrain terrain(c.grid, c.dem);
  const Grid other{0, 0, 10000, 60, 40};
  const Background bg{Field(other, 0.0), Field(other, 0.0)};
  EXPECT_THROW(Analysis(terrain, c.stations, bg, Options()), std::exception);
}

namespace
{
// Stations all over a test area whose terrain rises eastwards by slope metres per cell, with
// hills added as needed
struct LimitCase
{
  Grid grid{0, 0, 5000, 120, 80};
  std::vector<double> dem;
  std::vector<Station> stations;

  explicit LimitCase(double slope)
  {
    for (std::size_t j = 0; j < grid.ny; j++)
      for (std::size_t i = 0; i < grid.nx; i++)
        dem.push_back(slope * static_cast<double>(i));
  }

  void addHill(double x0, double y0, double height, double sigma)
  {
    for (std::size_t j = 0; j < grid.ny; j++)
      for (std::size_t i = 0; i < grid.nx; i++)
      {
        const double dx = (static_cast<double>(i) + 0.5) * grid.dx - x0;
        const double dy = (static_cast<double>(j) + 0.5) * grid.dx - y0;
        dem[j * grid.nx + i] += height * std::exp(-(dx * dx + dy * dy) / (2 * sigma * sigma));
      }
  }

  double height(double x, double y) const { return Field(grid, dem).sample(x, y); }

  // Stations at the heights of the terrain
  void addStations(const std::function<double(double, double, double)>& truth, int count = 200)
  {
    const Field elevation(grid, dem);
    std::mt19937 gen(5);
    std::uniform_real_distribution<double> ux(10e3, 590e3);
    std::uniform_real_distribution<double> uy(10e3, 390e3);
    for (int s = 0; s < count; s++)
    {
      const double x = ux(gen);
      const double y = uy(gen);
      const double h = elevation.sample(x, y);
      stations.push_back(Station{x, y, h, truth(x, y, h)});
    }
  }

  // RMS error against the truth over the grid cells for which where(i) holds
  double rmse(
      const Field& f,
      const std::function<double(double, double, double)>& truth,
      const std::function<bool(std::size_t)>& where = [](std::size_t) { return true; }) const
  {
    double sum = 0;
    std::size_t n = 0;
    for (std::size_t j = 0; j < grid.ny; j++)
      for (std::size_t i = 0; i < grid.nx; i++)
      {
        if (!where(i))
          continue;
        const double x = grid.x0 + (static_cast<double>(i) + 0.5) * grid.dx;
        const double y = grid.y0 + (static_cast<double>(j) + 0.5) * grid.dx;
        const double e = f(i, j) - truth(x, y, dem[j * grid.nx + i]);
        sum += e * e;
        n++;
      }
    return std::sqrt(sum / static_cast<double>(n));
  }
};

// LimitsWithOptimalInterpolation checks the limits for optimal interpolation of the residuals
Options limit_options(Residuals residuals = Residuals::Barnes)
{
  Options options;
  options.residuals = residuals;
  options.regionalSigma = 100e3;
  options.localSigma = 40e3;
  options.priorLapseRate = 0;
  return options;
}

std::pair<double, double> range_of(const Field& f)
{
  const auto [lo, hi] = std::minmax_element(f.values().begin(), f.values().end());
  return {*lo, *hi};
}
}  // namespace

TEST(PointGridding, LimitsKeepSaturatedStations)
{
  // Relative humidity rising with height and saturating above 500 m: the height regression
  // through the saturated stations overshoots 100 % on the slopes, and without limits the
  // gross error check rejects saturated stations for disagreeing with it
  LimitCase c(10.0);
  const auto truth = [](double, double, double h) { return std::min(100.0, 60.0 + 0.08 * h); };
  c.addStations(truth);
  const Terrain terrain(c.grid, c.dem);

  Options options = limit_options();
  const Analysis unlimited(terrain, c.stations, options);
  options.lowerLimit = 0.0;
  options.upperLimit = 100.0;
  const Analysis limited(terrain, c.stations, options);

  const auto fu = unlimited.grid();
  const auto fl = limited.grid();
  EXPECT_GT(range_of(fu).second, 100.5);
  EXPECT_LE(range_of(fl).second, 100.0);
  EXPECT_GE(range_of(fl).first, 0.0);

  for (std::size_t s = 0; s < c.stations.size(); s++)
    EXPECT_EQ(limited.status()[s], StationStatus::Used) << "station " << s;

  // Better than clamping the unlimited analysis afterwards, also on the saturated slopes
  Field clamped = fu;
  for (std::size_t n = 0; n < clamped.values().size(); n++)
    clamped.data()[n] = std::clamp(clamped.data()[n], 0.0, 100.0);
  const auto saturated = [](std::size_t i) { return i >= 60; };
  EXPECT_LT(c.rmse(fl, truth), c.rmse(clamped, truth));
  EXPECT_LE(c.rmse(fl, truth, saturated), c.rmse(clamped, truth, saturated));

  // Points and the grid agree
  for (std::size_t i = 5; i < c.grid.nx; i += 10)
  {
    const double x = (static_cast<double>(i) + 0.5) * c.grid.dx;
    EXPECT_NEAR(limited.predict(x, 200e3 + 2500, c.dem[i]), fl(i, 40), 1e-6) << "i=" << i;
  }
}

TEST(PointGridding, SummitStationAtTheLimitIsKept)
{
  // Humidity rising with height up to saturation at 1000 m, and a station in the cloud on a
  // 1700 m summit. The height trend extrapolates far beyond 100 % there, and without limits
  // the gross error check rejects the summit for disagreeing with it.
  LimitCase c(5.0);
  const double px = 300e3;
  const double py = 200e3;
  c.addHill(px, py, 1500.0, 10e3);
  const auto truth = [](double, double, double h) { return std::min(100.0, 50.0 + 0.05 * h); };
  c.addStations(truth);
  const std::size_t summit = c.stations.size();
  const Terrain terrain(c.grid, c.dem);
  const double h = c.dem[40 * c.grid.nx + 60];
  c.stations.push_back(Station{px + 2500, py + 2500, h, 100.0});
  const auto rejected = [](const Analysis& a)
  { return std::count(a.status().begin(), a.status().end(), StationStatus::Rejected); };

  Options options = limit_options();
  const Analysis unlimited(terrain, c.stations, options);
  options.upperLimit = 100.0;
  const Analysis limited(terrain, c.stations, options);

  EXPECT_EQ(unlimited.status()[summit], StationStatus::Rejected);
  EXPECT_EQ(limited.status()[summit], StationStatus::Used);
  EXPECT_LE(rejected(limited), rejected(unlimited));
  EXPECT_NEAR(limited.predict(px + 2500, py + 2500, h), 100.0, 0.1);
  EXPECT_LE(range_of(limited.grid()).second, 100.0);
}

TEST(PointGridding, CalmStationsDoNotSpreadWind)
{
  // Wind speed rising with height over hills, calm below 500 m. The height trend through the
  // calm valleys goes negative, the calm stations get positive residuals, and without limits
  // the residual passes spread them as wind into the neighbouring calm cells.
  LimitCase c(0.0);
  for (std::size_t j = 0; j < c.grid.ny; j++)
    for (std::size_t i = 0; i < c.grid.nx; i++)
    {
      const double x = (static_cast<double>(i) + 0.5) * c.grid.dx;
      const double y = (static_cast<double>(j) + 0.5) * c.grid.dx;
      c.dem[j * c.grid.nx + i] = 500.0 + 400.0 * std::sin(x / 40e3) * std::cos(y / 50e3);
    }
  const Terrain terrain(c.grid, c.dem);
  const auto truth = [](double, double, double h) { return std::max(0.0, (h - 500.0) / 50.0); };
  std::mt19937 gen(7);
  std::uniform_real_distribution<double> ux(10e3, 590e3);
  std::uniform_real_distribution<double> uy(10e3, 390e3);
  for (int s = 0; s < 300; s++)
  {
    const double x = ux(gen);
    const double y = uy(gen);
    const double h = terrain.elevation().sample(x, y);
    c.stations.push_back(Station{x, y, h, truth(x, y, h)});
  }

  Options options = limit_options();
  const Analysis unlimited(terrain, c.stations, options);
  options.lowerLimit = 0.0;
  const Analysis limited(terrain, c.stations, options);

  const auto fu = unlimited.grid();
  const auto fl = limited.grid();
  EXPECT_GE(range_of(fl).first, 0.0);

  // Compared with clamping the unlimited analysis afterwards, in the calm cells
  Field clamped = fu;
  for (std::size_t n = 0; n < clamped.values().size(); n++)
    clamped.data()[n] = std::max(clamped.data()[n], 0.0);
  double calm_limited = 0;
  double calm_clamped = 0;
  std::size_t n_calm = 0;
  for (std::size_t n = 0; n < c.dem.size(); n++)
    if (c.dem[n] < 450.0)
    {
      calm_limited += fl.data()[n];
      calm_clamped += clamped.data()[n];
      n_calm++;
    }
  calm_limited /= static_cast<double>(n_calm);
  calm_clamped /= static_cast<double>(n_calm);
  EXPECT_LT(calm_limited, calm_clamped);
  EXPECT_LT(c.rmse(fl, truth), c.rmse(clamped, truth));
}

TEST(PointGridding, LimitsApplyToTheBackground)
{
  // A humid background moved up the slope with a positive lapse rate goes past saturation.
  // The limits apply to the values, so the field stays at 100 % where the moved background
  // would exceed it, also where there are no stations.
  LimitCase c(10.0);
  c.addStations([](double, double, double) { return 95.0; }, 0);
  c.stations.push_back(Station{50e3, 200e3, c.height(50e3, 200e3), 96.0});
  c.stations.push_back(Station{100e3, 100e3, c.height(100e3, 100e3), 97.0});
  const Terrain terrain(c.grid, c.dem);
  const Background bg{Field(c.grid, 95.0), Field(c.grid, 0.0)};

  Options options = limit_options();
  options.qc = false;
  options.backgroundLapseRate = 10.0;
  options.upperLimit = 100.0;
  const Analysis analysis(terrain, c.stations, bg, options);
  const auto f = analysis.grid();
  EXPECT_LE(range_of(f).second, 100.0);
  EXPECT_DOUBLE_EQ(f(119, 40), 100.0);
  EXPECT_DOUBLE_EQ(analysis.predict(590e3, 200e3, 1200.0), 100.0);
}

TEST(PointGridding, StationValuesOutsideTheLimits)
{
  // Slightly out of range values are clamped, gross ones rejected when qc is enabled
  const Grid grid{0, 0, 5000, 40, 40};
  const Terrain terrain(grid, std::vector<double>(grid.size(), 0.0));
  const std::vector<Station> stations{
      {50e3, 50e3, 0, 98.0}, {100e3, 100e3, 0, 103.0}, {150e3, 150e3, 0, 150.0}};
  Options options;
  options.residuals = Residuals::Barnes;
  options.predictors.clear();
  options.lowerLimit = 0.0;
  options.upperLimit = 100.0;
  options.qcMinThreshold = 6.0;
  const Analysis checked(terrain, stations, options);
  EXPECT_EQ(checked.status()[0], StationStatus::Used);
  EXPECT_EQ(checked.status()[1], StationStatus::Used);
  EXPECT_EQ(checked.status()[2], StationStatus::Rejected);
  EXPECT_LE(range_of(checked.grid()).second, 100.0);

  options.qc = false;
  const Analysis unchecked(terrain, stations, options);
  EXPECT_EQ(unchecked.status()[2], StationStatus::Used);
  EXPECT_NEAR(unchecked.predict(150e3, 150e3, 0), 100.0, 0.1);
}

TEST(PointGridding, LimitsWithOptimalInterpolation)
{
  // The saturated slopes of LimitsKeepSaturatedStations: in range, no station rejected, and
  // the points agree with the grid
  LimitCase c(10.0);
  const auto truth = [](double, double, double h) { return std::min(100.0, 60.0 + 0.08 * h); };
  c.addStations(truth);
  const Terrain terrain(c.grid, c.dem);
  Options options = limit_options(Residuals::OI);
  options.lowerLimit = 0.0;
  options.upperLimit = 100.0;
  const Analysis limited(terrain, c.stations, options);
  const auto fl = limited.grid();
  EXPECT_LE(range_of(fl).second, 100.0);
  EXPECT_GE(range_of(fl).first, 0.0);
  for (std::size_t s = 0; s < c.stations.size(); s++)
    EXPECT_EQ(limited.status()[s], StationStatus::Used) << "station " << s;
  for (std::size_t i = 5; i < c.grid.nx; i += 10)
  {
    const double x = (static_cast<double>(i) + 0.5) * c.grid.dx;
    EXPECT_NEAR(limited.predict(x, 200e3 + 2500, c.dem[i]), fl(i, 40), 1e-6) << "i=" << i;
  }
  // The truth is noise free, which favours the near exact Barnes passes (1.04 %) over the
  // smoothing of the observation error in optimal interpolation (1.70 %)
  EXPECT_LT(c.rmse(fl, truth), 2.0);
}

TEST(PointGridding, OptimalInterpolationDeclustersStations)
{
  // Ten stations at almost the same place west of a point and one station as far east of it.
  // Barnes counts the cluster as ten votes, optimal interpolation as little more than one, so
  // its estimate halfway is much nearer the mean of the two sides.
  const Grid grid{0, 0, 5000, 80, 40};
  const Terrain terrain(grid, std::vector<double>(grid.size(), 0.0));
  std::vector<Station> stations;
  for (int k = 0; k < 10; k++)
    stations.push_back(Station{150e3 + 500.0 * k, 100e3, 0, 0.0});
  stations.push_back(Station{250e3, 100e3, 0, 11.0});

  Options options;
  options.residuals = Residuals::OI;
  options.predictors.clear();
  options.bilateralSigmaH.reset();
  options.qc = false;
  options.localSigma = 1e7;  // the trend is the mean 1 everywhere
  options.regionalSigma = 1e7;
  const Analysis oi(terrain, stations, options);
  options.residuals = Residuals::Barnes;
  const Analysis barnes(terrain, stations, options);

  const double x = 202250;  // halfway between the centre of the cluster and the station
  const double mid = 5.5;
  EXPECT_LT(std::abs(oi.predict(x, 100e3, 0) - mid),
            0.6 * std::abs(barnes.predict(x, 100e3, 0) - mid));
  EXPECT_LT(barnes.predict(x, 100e3, 0), 3.0);

  // Near the stations each side keeps its own value, the single station shrunk toward the
  // trend by the observation error
  EXPECT_NEAR(oi.predict(152e3, 100e3, 0), 0.0, 1.0);
  EXPECT_GT(oi.predict(250e3, 100e3, 0), 6.0);
}

TEST(PointGridding, InvalidOptimalInterpolationOptions)
{
  const Grid grid{0, 0, 5000, 20, 20};
  const Terrain terrain(grid, std::vector<double>(grid.size(), 0.0));
  const std::vector<Station> stations{{50e3, 50e3, 0, 1.0}, {60e3, 50e3, 0, 2.0}};
  Options options;
  options.residuals = Residuals::OI;
  options.qc = false;
  options.oiScales.clear();
  EXPECT_THROW(Analysis(terrain, stations, options), Fmi::Exception);
  options.oiScales = {50e3, 0.0};
  EXPECT_THROW(Analysis(terrain, stations, options), Fmi::Exception);
  options.oiScales = {50e3};
  options.oiErrorRatio = 0;
  EXPECT_THROW(Analysis(terrain, stations, options), Fmi::Exception);
  options.oiErrorRatio = 0.5;
  options.oiSigmaH = 0.0;
  EXPECT_THROW(Analysis(terrain, stations, options), Fmi::Exception);
}

TEST(PointGridding, InvalidLimits)
{
  const Grid grid{0, 0, 5000, 20, 20};
  const Terrain terrain(grid, std::vector<double>(grid.size(), 0.0));
  const std::vector<Station> stations{{50e3, 50e3, 0, 1.0}};
  Options options;
  options.lowerLimit = 1.0;
  options.upperLimit = 1.0;
  EXPECT_THROW(Analysis(terrain, stations, options), std::exception);
  options.upperLimit = std::nan("");
  EXPECT_THROW(Analysis(terrain, stations, options), std::exception);
}
