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
#include <chrono>
#include <functional>
#include <cmath>
#include <fstream>
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

  const auto t0 = std::chrono::steady_clock::now();
  const Analysis analysis(terrain, stations, Options());
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
  Options one;
  one.threads = 1;
  Options many;
  many.threads = 7;
  const auto a = Analysis(terrain, stations, one).grid();
  const auto b = Analysis(terrain, stations, many).grid();
  for (std::size_t n = 0; n < a.values().size(); n++)
    ASSERT_EQ(a.values()[n], b.values()[n]);
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

  // The blend is a weighted mean of the corrected background and the plain analysis
  for (double x : {50e3, 150e3, 300e3, 450e3})
  {
    const double expected = 0.7 * corrected.predict(x, 200e3, 0) + 0.3 * plain.predict(x, 200e3, 0);
    EXPECT_NEAR(blended.predict(x, 200e3, 0), expected, 1e-9) << "x=" << x;
  }

  // Where the background is missing the plain analysis is used
  const auto fb = blended.grid();
  const auto fp = plain.grid();
  EXPECT_DOUBLE_EQ(fb(110, 40), fp(110, 40));
  EXPECT_TRUE(std::isfinite(corrected.grid()(110, 40)));
}

TEST(PointGridding, BackgroundMustBeOnTheTerrainGrid)
{
  BackgroundCase c;
  const Terrain terrain(c.grid, c.dem);
  const Grid other{0, 0, 10000, 60, 40};
  const Background bg{Field(other, 0.0), Field(other, 0.0)};
  EXPECT_THROW(Analysis(terrain, c.stations, bg, Options()), std::exception);
}
