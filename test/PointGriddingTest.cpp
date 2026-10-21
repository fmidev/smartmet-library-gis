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
