#include "PointGridding.h"
#include <macgyver/Exception.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <exception>
#include <limits>
#include <mutex>
#include <thread>

namespace Fmi
{
namespace PointGridding
{
namespace
{
const int box_passes = 4;

// ----------------------------------------------------------------------
/*!
 * \brief Run fn(begin, end) over [0, n) split into contiguous chunks
 */
// ----------------------------------------------------------------------

void parallel_for(std::size_t n,
                  unsigned int threads,
                  const std::function<void(std::size_t, std::size_t)>& fn)
{
  if (threads == 0)
    threads = std::max(1U, std::thread::hardware_concurrency());
  const std::size_t chunks = std::min<std::size_t>(threads, n);
  if (chunks <= 1)
  {
    fn(0, n);
    return;
  }

  std::vector<std::thread> workers;
  workers.reserve(chunks);
  std::exception_ptr error;
  std::mutex error_mutex;
  for (std::size_t k = 0; k < chunks; k++)
  {
    const std::size_t begin = n * k / chunks;
    const std::size_t end = n * (k + 1) / chunks;
    workers.emplace_back(
        [&, begin, end]()
        {
          try
          {
            fn(begin, end);
          }
          catch (...)
          {
            std::lock_guard<std::mutex> lock(error_mutex);
            if (!error)
              error = std::current_exception();
          }
        });
  }
  for (auto& w : workers)
    w.join();
  if (error)
    std::rethrow_exception(error);
}

// ----------------------------------------------------------------------
/*!
 * \brief Extended box filter parameters (Gwosdek et al. 2011)
 *
 * The filter has weight c on offsets -r...r and c*alpha on offsets +-(r+1), and passes
 * repetitions of it have variance sigma^2 exactly.
 */
// ----------------------------------------------------------------------

struct BoxParams
{
  std::size_t r = 0;
  double alpha = 0;
  double c = 0;
};

BoxParams box_params(double sigma, int passes)
{
  const double s2 = sigma * sigma / passes;
  BoxParams p;
  p.r = static_cast<std::size_t>(std::floor(0.5 * std::sqrt(12.0 * s2 + 1.0) - 0.5));
  const auto r = static_cast<double>(p.r);
  p.alpha = (2 * r + 1) * (r * (r + 1) - 3.0 * s2) / (6.0 * (s2 - (r + 1) * (r + 1)));
  p.c = 1.0 / (2.0 * p.alpha + 2 * r + 1);
  return p;
}

// One extended box pass on a line with a zero boundary condition
void box_line(const double* f, double* g, std::size_t n, const BoxParams& p)
{
  const std::size_t r = p.r;
  double sum = 0;
  for (std::size_t k = 0; k <= r && k < n; k++)
    sum += f[k];

  for (std::size_t i = 0; i < n; i++)
  {
    double ends = 0;
    if (i >= r + 1)
      ends += f[i - r - 1];
    if (i + r + 1 < n)
      ends += f[i + r + 1];
    g[i] = p.c * (sum + p.alpha * ends);
    if (i + r + 1 < n)
      sum += f[i + r + 1];
    if (i >= r)
      sum -= f[i - r];
  }
}

// One extended box pass along the columns [col0, col1) of a row-major field. Rows outside
// the field are read from a zero row so that the inner loops are branch free.
void box_columns(const double* src,
                 double* dst,
                 std::size_t nx,
                 std::size_t ny,
                 std::size_t col0,
                 std::size_t col1,
                 const BoxParams& p,
                 std::vector<double>& sum,
                 const std::vector<double>& zero)
{
  const std::size_t r = p.r;
  const std::size_t w = col1 - col0;
  const double c = p.c;
  const double alpha = p.alpha;
  sum.assign(w, 0.0);
  for (std::size_t k = 0; k <= r && k < ny; k++)
  {
    const double* row = src + k * nx + col0;
    for (std::size_t i = 0; i < w; i++)
      sum[i] += row[i];
  }

  double* s = sum.data();
  for (std::size_t j = 0; j < ny; j++)
  {
    const double* before = (j >= r + 1 ? src + (j - r - 1) * nx + col0 : zero.data());
    const double* after = (j + r + 1 < ny ? src + (j + r + 1) * nx + col0 : zero.data());
    const double* leaving = (j >= r ? src + (j - r) * nx + col0 : zero.data());
    double* out = dst + j * nx + col0;
    for (std::size_t i = 0; i < w; i++)
    {
      out[i] = c * (s[i] + alpha * (before[i] + after[i]));
      s[i] += after[i] - leaving[i];
    }
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Separable n-pass extended box blur, an approximate Gaussian
 */
// ----------------------------------------------------------------------

void blur(Field& field, double sigma_cells, unsigned int threads)
{
  const auto p = box_params(sigma_cells, box_passes);
  const std::size_t nx = field.grid().nx;
  const std::size_t ny = field.grid().ny;
  double* data = field.data();

  parallel_for(ny,
               threads,
               [&](std::size_t j0, std::size_t j1)
               {
                 std::vector<double> a(nx);
                 std::vector<double> b(nx);
                 for (std::size_t j = j0; j < j1; j++)
                 {
                   double* row = data + j * nx;
                   std::copy(row, row + nx, a.begin());
                   for (int pass = 0; pass < box_passes; pass++)
                   {
                     box_line(a.data(), b.data(), nx, p);
                     std::swap(a, b);
                   }
                   std::copy(a.begin(), a.end(), row);
                 }
               });

  std::vector<double> scratch(nx * ny);
  const std::vector<double> zero(nx, 0.0);
  parallel_for(nx,
               threads,
               [&](std::size_t c0, std::size_t c1)
               {
                 std::vector<double> sum;
                 double* src = data;
                 double* dst = scratch.data();
                 for (int pass = 0; pass < box_passes; pass++)
                 {
                   box_columns(src, dst, nx, ny, c0, c1, p, sum, zero);
                   std::swap(src, dst);
                 }
                 // an even number of passes ends in the original buffer
                 static_assert(box_passes % 2 == 0, "box_passes must be even");
               });
}

// The 1D kernel of the blur at offsets 0, 1, 2... normalised to 1 at offset 0, together
// with the 2D peak value of the unnormalised kernel.
struct Kernel
{
  std::vector<double> profile;
  double peak2d = 0;
};

Kernel kernel(double sigma_cells)
{
  const auto p = box_params(sigma_cells, box_passes);
  const std::size_t n = 2 * box_passes * (p.r + 1) + 1;
  std::vector<double> a(n, 0.0);
  std::vector<double> b(n, 0.0);
  a[n / 2] = 1.0;
  for (int pass = 0; pass < box_passes; pass++)
  {
    box_line(a.data(), b.data(), n, p);
    std::swap(a, b);
  }
  Kernel k;
  k.peak2d = a[n / 2] * a[n / 2];
  for (std::size_t i = n / 2; i < n; i++)
    k.profile.push_back(a[i] / a[n / 2]);
  return k;
}

// ----------------------------------------------------------------------
/*!
 * \brief Bilinear splatting setup for a point
 */
// ----------------------------------------------------------------------

struct Bilinear
{
  long i0 = 0;
  long j0 = 0;
  double tx = 0;
  double ty = 0;
};

Bilinear bilinear(const Grid& grid, double x, double y)
{
  const double fx = (x - grid.x0) / grid.dx - 0.5;
  const double fy = (y - grid.y0) / grid.dx - 0.5;
  Bilinear b;
  b.i0 = static_cast<long>(std::floor(fx));
  b.j0 = static_cast<long>(std::floor(fy));
  b.tx = fx - b.i0;
  b.ty = fy - b.j0;
  return b;
}

// Deposit station quantities onto the four surrounding cell centres
std::vector<Field> splat(const Grid& grid,
                         const std::vector<double>& x,
                         const std::vector<double>& y,
                         const std::vector<std::vector<double>>& quantities)
{
  std::vector<Field> out(quantities.size(), Field(grid));
  const auto nx = static_cast<long>(grid.nx);
  const auto ny = static_cast<long>(grid.ny);
  for (std::size_t s = 0; s < x.size(); s++)
  {
    const auto b = bilinear(grid, x[s], y[s]);
    const std::array<long, 4> is{b.i0, b.i0 + 1, b.i0, b.i0 + 1};
    const std::array<long, 4> js{b.j0, b.j0, b.j0 + 1, b.j0 + 1};
    const std::array<double, 4> ws{
        (1 - b.tx) * (1 - b.ty), b.tx * (1 - b.ty), (1 - b.tx) * b.ty, b.tx * b.ty};
    for (int c = 0; c < 4; c++)
    {
      if (is[c] < 0 || is[c] >= nx || js[c] < 0 || js[c] >= ny)
        continue;
      const auto i = static_cast<std::size_t>(is[c]);
      const auto j = static_cast<std::size_t>(js[c]);
      for (std::size_t k = 0; k < quantities.size(); k++)
        out[k](i, j) += ws[c] * quantities[k][s];
    }
  }
  return out;
}

// Weight of a station on its own value at its own location along one axis, when splatted
// linearly with fractions (1-t, t) and read back the same way
double self_weight(double t, double k1)
{
  return (1 - t) * (1 - t) + t * t + 2 * t * (1 - t) * k1;
}

double median(std::vector<double> values)
{
  if (values.empty())
    return 0.0;
  const std::size_t n = values.size();
  std::nth_element(values.begin(), values.begin() + n / 2, values.end());
  const double hi = values[n / 2];
  if (n % 2 == 1)
    return hi;
  const double lo = *std::max_element(values.begin(), values.begin() + n / 2);
  return 0.5 * (lo + hi);
}

// ----------------------------------------------------------------------
/*!
 * \brief Internal station representation
 */
// ----------------------------------------------------------------------

struct Obs
{
  std::vector<double> x;
  std::vector<double> y;
  std::vector<double> h;
  std::vector<double> z;
  std::vector<double> smooth;  // smoothed terrain height at the station
  std::vector<double> hm;      // model orography at the station, empty without a background
  std::vector<std::size_t> index;  // index in the input station list

  std::size_t size() const { return x.size(); }

  Obs subset(const std::vector<bool>& keep) const
  {
    Obs out;
    for (std::size_t i = 0; i < size(); i++)
    {
      if (!keep[i])
        continue;
      out.x.push_back(x[i]);
      out.y.push_back(y[i]);
      out.h.push_back(h[i]);
      out.z.push_back(z[i]);
      out.smooth.push_back(smooth[i]);
      if (!hm.empty())
        out.hm.push_back(hm[i]);
      out.index.push_back(index[i]);
    }
    return out;
  }
};

// The reference height of the height predictor is the model orography with a background
double predictor_value(Predictor p, double h, double smooth, double href)
{
  switch (p)
  {
    case Predictor::Height:
      return (h - href) / 1000.0;
    case Predictor::TPI:
      return (h - smooth) / 1000.0;
  }
  return 0.0;
}

// ----------------------------------------------------------------------
/*!
 * \brief Gaussian-weighted local regression from moment fields
 *
 * Fits z = a + sum_k beta_k * p_k at every cell. The coefficients are ridge-regressed
 * toward the prior ones: beta = (C + R)^-1 (c + R beta0), where C and c are the centred
 * weighted cross products, so a coefficient stays at its prior until the local stations span
 * enough of the predictor. The level is pulled toward the prior relation by levelWeight
 * pseudo stations at the local mean predictor values.
 */
// ----------------------------------------------------------------------

// Solve the k x k system A x = rhs in place (rhs becomes x) by Gaussian elimination with
// partial pivoting. k is tiny and A is positive definite thanks to the ridge.
void solve_small(std::vector<double>& A, std::vector<double>& rhs, std::size_t k)
{
  for (std::size_t col = 0; col < k; col++)
  {
    std::size_t piv = col;
    for (std::size_t row = col + 1; row < k; row++)
      if (std::abs(A[row * k + col]) > std::abs(A[piv * k + col]))
        piv = row;
    if (piv != col)
    {
      for (std::size_t c = 0; c < k; c++)
        std::swap(A[col * k + c], A[piv * k + c]);
      std::swap(rhs[col], rhs[piv]);
    }
    for (std::size_t row = col + 1; row < k; row++)
    {
      const double f = A[row * k + col] / A[col * k + col];
      for (std::size_t c = col; c < k; c++)
        A[row * k + c] -= f * A[col * k + c];
      rhs[row] -= f * rhs[col];
    }
  }
  for (std::size_t col = k; col-- > 0;)
  {
    double v = rhs[col];
    for (std::size_t c = col + 1; c < k; c++)
      v -= A[col * k + c] * rhs[c];
    rhs[col] = v / A[col * k + col];
  }
}

using Pairs = std::vector<std::pair<std::size_t, std::size_t>>;

// Station quantities whose Gaussian sums give the regression moments, in the order
// 1, z, p_i, p_i * z, p_i * p_j (i <= j)
std::vector<std::vector<double>> regression_quantities(
    const std::vector<std::vector<double>>& predictors, const std::vector<double>& z, Pairs& pairs)
{
  const std::size_t k = predictors.size();
  const std::size_t n = z.size();
  pairs.clear();
  for (std::size_t i = 0; i < k; i++)
    for (std::size_t j = i; j < k; j++)
      pairs.emplace_back(i, j);

  std::vector<std::vector<double>> quantities;
  quantities.emplace_back(n, 1.0);
  quantities.push_back(z);
  for (std::size_t i = 0; i < k; i++)
    quantities.push_back(predictors[i]);
  for (std::size_t i = 0; i < k; i++)
  {
    std::vector<double> q(n);
    for (std::size_t s = 0; s < n; s++)
      q[s] = predictors[i][s] * z[s];
    quantities.push_back(std::move(q));
  }
  for (const auto& ij : pairs)
  {
    std::vector<double> q(n);
    for (std::size_t s = 0; s < n; s++)
      q[s] = predictors[ij.first][s] * predictors[ij.second][s];
    quantities.push_back(std::move(q));
  }
  return quantities;
}

struct Regression
{
  Field a;
  std::vector<Field> beta;
};

Regression local_regression(const Grid& grid,
                            const Obs& obs,
                            const std::vector<std::vector<double>>& predictors,
                            const std::vector<double>& z,
                            double sigma,
                            const std::function<double(std::size_t)>& prior_level,
                            const std::function<double(std::size_t, std::size_t)>& prior_coef,
                            double level_weight,
                            const std::vector<double>& ridge,
                            unsigned int threads)
{
  const std::size_t k = predictors.size();
  Pairs pairs;
  const auto m = gaussianSums(
      grid, obs.x, obs.y, regression_quantities(predictors, z, pairs), sigma, threads);

  Regression out;
  out.a = Field(grid);
  out.beta.assign(k, Field(grid));

  parallel_for(
      grid.size(),
      threads,
      [&](std::size_t n0, std::size_t n1)
      {
        std::vector<double> swp(k);
        std::vector<double> A(k * k);
        std::vector<double> rhs(k);
        std::vector<double> beta0(k);
        for (std::size_t n = n0; n < n1; n++)
        {
          const double sw = m[0].data()[n];
          const double swz = m[1].data()[n];
          const double swsafe = std::max(sw, 1e-9);
          for (std::size_t i = 0; i < k; i++)
          {
            swp[i] = m[2 + i].data()[n];
            beta0[i] = prior_coef(i, n);
          }
          for (std::size_t p = 0; p < pairs.size(); p++)
          {
            const auto i = pairs[p].first;
            const auto j = pairs[p].second;
            const double cij = m[2 + 2 * k + p].data()[n] - swp[i] * swp[j] / swsafe;
            A[i * k + j] = A[j * k + i] = cij;
          }
          for (std::size_t i = 0; i < k; i++)
          {
            A[i * k + i] += ridge[i];
            rhs[i] = m[2 + k + i].data()[n] - swp[i] * swz / swsafe + ridge[i] * beta0[i];
          }

          solve_small(A, rhs, k);

          double zprior = prior_level(n);
          double fitted = 0;
          for (std::size_t i = 0; i < k; i++)
          {
            const double pbar = swp[i] / swsafe;
            zprior += beta0[i] * pbar;
            fitted += rhs[i] * pbar;
            out.beta[i].data()[n] = rhs[i];
          }
          const double zbar = (swz + level_weight * zprior) / (sw + level_weight);
          out.a.data()[n] = zbar - fitted;
        }
      });
  return out;
}

// ----------------------------------------------------------------------
/*!
 * \brief Residual passes
 */
// ----------------------------------------------------------------------

class Stage
{
 public:
  virtual ~Stage() = default;
  virtual double at(double x, double y, double h) const = 0;
  virtual void addTo(Field& target, const Field& elevation, unsigned int threads) const = 0;
};

// Damped Barnes: sum(w r) / (sum(w) + damping)
class BarnesStage : public Stage
{
 public:
  BarnesStage(const Grid& grid,
              const Obs& obs,
              const std::vector<double>& r,
              double sigma,
              double damping,
              unsigned int threads)
      : itsDamping(damping)
  {
    std::vector<std::vector<double>> q{std::vector<double>(obs.size(), 1.0), r};
    auto m = gaussianSums(grid, obs.x, obs.y, q, sigma, threads);
    itsSw = std::move(m[0]);
    itsSwr = std::move(m[1]);
    itsValue = Field(grid);
    for (std::size_t n = 0; n < grid.size(); n++)
      itsValue.data()[n] = itsSwr.data()[n] / (itsSw.data()[n] + damping);
    itsK1 = kernel(sigma / grid.dx).profile.at(1);
  }

  double at(double x, double y, double /* h */) const override { return itsValue.sample(x, y); }

  void addTo(Field& target, const Field& /* elevation */, unsigned int /* threads */) const override
  {
    const auto f = itsValue.resample(target.grid());
    for (std::size_t n = 0; n < target.grid().size(); n++)
      target.data()[n] += f.data()[n];
  }

  // Leave-one-out estimate and remaining support at the stations that built the stage
  void loo(const Obs& obs,
           const std::vector<double>& r,
           std::vector<double>& estimate,
           std::vector<double>& support) const
  {
    const Grid& grid = itsSw.grid();
    estimate.resize(obs.size());
    support.resize(obs.size());
    for (std::size_t s = 0; s < obs.size(); s++)
    {
      const auto b = bilinear(grid, obs.x[s], obs.y[s]);
      const double wself = self_weight(b.tx, itsK1) * self_weight(b.ty, itsK1);
      const double sw = itsSw.sample(obs.x[s], obs.y[s]) - wself;
      const double swr = itsSwr.sample(obs.x[s], obs.y[s]) - wself * r[s];
      estimate[s] = swr / (sw + itsDamping);
      support[s] = sw;
    }
  }

 private:
  Field itsSw;
  Field itsSwr;
  Field itsValue;
  double itsDamping = 0;
  double itsK1 = 0;
};

// Height-aware damped Barnes via a bilateral grid: stations are splatted linearly into
// height layers, each layer is blurred horizontally, the layer stack is blurred vertically
// with a small discrete Gaussian and the result is read back at (x, y, h).
class BilateralStage : public Stage
{
 public:
  BilateralStage(const Grid& grid,
                 const Obs& obs,
                 const std::vector<double>& r,
                 double sigma,
                 double layer_spacing,
                 double sigma_h,
                 double max_height,
                 double damping,
                 unsigned int threads)
      : itsLayers(static_cast<std::size_t>(std::ceil(max_height / layer_spacing)) + 1),
        itsDz(layer_spacing),
        itsDamping(damping),
        itsK1(kernel(sigma / grid.dx).profile.at(1))
  {
    std::vector<Field> sw;
    std::vector<Field> swr;
    splat_layers(grid, obs, r, sigma, threads, sw, swr);
    vertical_blur(grid, sw, swr, sigma_h / layer_spacing, threads);
  }

  double at(double x, double y, double h) const override { return interp(itsValue, x, y, h); }

  void addTo(Field& target, const Field& elevation, unsigned int threads) const override
  {
    // The bilinear setup from target cells to the layer grid is shared by all layers and
    // separable, so compute it once per column and per row
    const Grid& g = target.grid();
    const Grid& lg = itsValue.front().grid();
    const auto axis = [](double x0, double dx, std::size_t n, double lx0, double ldx, std::size_t ln,
                         std::vector<std::size_t>& index, std::vector<double>& frac)
    {
      index.resize(n);
      frac.resize(n);
      const auto maxi = static_cast<long>(ln) - 2;
      for (std::size_t i = 0; i < n; i++)
      {
        const double f = (x0 + (static_cast<double>(i) + 0.5) * dx - lx0) / ldx - 0.5;
        const long i0 = std::min(std::max(static_cast<long>(std::floor(f)), 0L), maxi);
        index[i] = static_cast<std::size_t>(i0);
        frac[i] = std::min(std::max(f - static_cast<double>(i0), 0.0), 1.0);
      }
    };
    std::vector<std::size_t> ci;
    std::vector<double> cx;
    std::vector<std::size_t> cj;
    std::vector<double> cy;
    axis(g.x0, g.dx, g.nx, lg.x0, lg.dx, lg.nx, ci, cx);
    axis(g.y0, g.dx, g.ny, lg.y0, lg.dx, lg.ny, cj, cy);

    const std::size_t lnx = lg.nx;
    parallel_for(g.ny,
                 threads,
                 [&](std::size_t j0, std::size_t j1)
                 {
                   for (std::size_t j = j0; j < j1; j++)
                   {
                     const double ty = cy[j];
                     const std::size_t row = cj[j] * lnx;
                     for (std::size_t i = 0; i < g.nx; i++)
                     {
                       std::size_t l0 = 0;
                       double tz = 0;
                       layer_weights(elevation(i, j), l0, tz);
                       const double tx = cx[i];
                       const std::size_t n = row + ci[i];
                       const auto bil = [&](const double* f)
                       {
                         return (1 - tx) * (1 - ty) * f[n] + tx * (1 - ty) * f[n + 1] +
                                (1 - tx) * ty * f[n + lnx] + tx * ty * f[n + lnx + 1];
                       };
                       target(i, j) += (1 - tz) * bil(itsValue[l0].data()) +
                                       tz * bil(itsValue[l0 + 1].data());
                     }
                   }
                 });
  }

  void loo(const Obs& obs,
           const std::vector<double>& r,
           std::vector<double>& estimate,
           std::vector<double>& support) const
  {
    const Grid& grid = itsSw.front().grid();
    estimate.resize(obs.size());
    support.resize(obs.size());
    for (std::size_t s = 0; s < obs.size(); s++)
    {
      const auto b = bilinear(grid, obs.x[s], obs.y[s]);
      std::size_t l0 = 0;
      double tz = 0;
      layer_weights(obs.h[s], l0, tz);
      const double wself = self_weight(b.tx, itsK1) * self_weight(b.ty, itsK1) *
                           self_weight(tz, itsKv1);
      const double sw = interp(itsSw, obs.x[s], obs.y[s], obs.h[s]) - wself;
      const double swr = interp(itsSwr, obs.x[s], obs.y[s], obs.h[s]) - wself * r[s];
      estimate[s] = swr / (sw + itsDamping);
      support[s] = sw;
    }
  }

 private:
  void layer_weights(double h, std::size_t& l0, double& t) const
  {
    const double top = static_cast<double>(itsLayers) - 1 - 1e-9;
    const double fl = std::min(std::max(h / itsDz, 0.0), top);
    l0 = static_cast<std::size_t>(std::floor(fl));
    t = fl - static_cast<double>(l0);
  }

  // Horizontally blurred sums of the layer weights and weighted residuals of each layer
  void splat_layers(const Grid& grid,
                    const Obs& obs,
                    const std::vector<double>& r,
                    double sigma,
                    unsigned int threads,
                    std::vector<Field>& sw,
                    std::vector<Field>& swr) const
  {
    sw.assign(itsLayers, Field(grid));
    swr.assign(itsLayers, Field(grid));
    std::vector<std::size_t> l0(obs.size());
    std::vector<double> t(obs.size());
    for (std::size_t s = 0; s < obs.size(); s++)
      layer_weights(obs.h[s], l0[s], t[s]);

    parallel_for(itsLayers,
                 threads,
                 [&](std::size_t first, std::size_t last)
                 {
                   for (std::size_t layer = first; layer < last; layer++)
                     splat_layer(grid, obs, r, sigma, l0, t, layer, sw, swr);
                 });
  }

  // Blurred sums of one layer
  void splat_layer(const Grid& grid,
                   const Obs& obs,
                   const std::vector<double>& r,
                   double sigma,
                   const std::vector<std::size_t>& l0,
                   const std::vector<double>& t,
                   std::size_t layer,
                   std::vector<Field>& sw,
                   std::vector<Field>& swr) const
  {
    std::vector<double> xs;
    std::vector<double> ys;
    std::vector<double> w;
    std::vector<double> wr;
    for (std::size_t s = 0; s < obs.size(); s++)
    {
      const double weight =
          (l0[s] == layer ? 1 - t[s] : 0.0) + (l0[s] + 1 == layer ? t[s] : 0.0);
      if (weight <= 0)
        continue;
      xs.push_back(obs.x[s]);
      ys.push_back(obs.y[s]);
      w.push_back(weight);
      wr.push_back(weight * r[s]);
    }
    if (xs.empty())
      return;
    auto f = gaussianSums(grid, xs, ys, {w, wr}, sigma, 1);
    sw[layer] = std::move(f[0]);
    swr[layer] = std::move(f[1]);
  }

  // Discrete Gaussian across the layers, s is the height scale in layers
  void vertical_blur(const Grid& grid,
                     const std::vector<Field>& sw,
                     const std::vector<Field>& swr,
                     double s,
                     unsigned int threads)
  {
    const long reach = static_cast<long>(3 * s + 1);
    itsKv1 = std::exp(-0.5 / (s * s));
    itsSw.assign(itsLayers, Field(grid));
    itsSwr.assign(itsLayers, Field(grid));
    itsValue.assign(itsLayers, Field(grid));
    const auto nl = static_cast<long>(itsLayers);

    // layers without any stations contribute nothing
    std::vector<bool> empty(itsLayers);
    for (std::size_t l = 0; l < itsLayers; l++)
      empty[l] = std::all_of(sw[l].values().begin(),
                             sw[l].values().end(),
                             [](double v) { return v == 0.0; });

    parallel_for(itsLayers,
                 threads,
                 [&](std::size_t first, std::size_t last)
                 {
                   for (auto l = static_cast<long>(first); l < static_cast<long>(last); l++)
                   {
                     double* vsw = itsSw[l].data();
                     double* vswr = itsSwr[l].data();
                     for (long src = std::max(0L, l - reach); src <= std::min(nl - 1, l + reach);
                          src++)
                     {
                       if (empty[src])
                         continue;
                       const double o = static_cast<double>(src - l);
                       const double kv = std::exp(-0.5 * (o / s) * (o / s));
                       const double* a = sw[src].data();
                       const double* b = swr[src].data();
                       for (std::size_t n = 0; n < grid.size(); n++)
                       {
                         vsw[n] += kv * a[n];
                         vswr[n] += kv * b[n];
                       }
                     }
                     double* value = itsValue[l].data();
                     for (std::size_t n = 0; n < grid.size(); n++)
                       value[n] = vswr[n] / (vsw[n] + itsDamping);
                   }
                 });
  }

  double interp(const std::vector<Field>& stack, double x, double y, double h) const
  {
    std::size_t l0 = 0;
    double t = 0;
    layer_weights(h, l0, t);
    return (1 - t) * stack[l0].sample(x, y) + t * stack[l0 + 1].sample(x, y);
  }

  std::size_t itsLayers = 0;
  double itsDz = 0;
  double itsDamping = 0;
  double itsK1 = 0;
  double itsKv1 = 0;
  std::vector<Field> itsSw;
  std::vector<Field> itsSwr;
  std::vector<Field> itsValue;
};

// ----------------------------------------------------------------------
/*!
 * \brief Terrain trend plus residual stages fitted to a set of stations
 */
// ----------------------------------------------------------------------

class Model
{
 public:
  // With a background the observations are the corrections to it, and the height predictor
  // is the height above the model orography given on the terrain grid
  Model(const Terrain& terrain,
        const Options& options,
        const Obs& obs,
        bool with_residuals,
        unsigned int threads,
        const Field* model_elevation = nullptr)
      : itsTerrain(terrain),
        itsOptions(options),
        itsCoarse(terrain.grid().coarsen(options.coarsen)),
        itsModelElevation(model_elevation)
  {
    fit_trend(obs, threads);
    if (!with_residuals)
      return;
    for (double sigma : options.residualSigmas)
    {
      std::vector<double> r(obs.size());
      for (std::size_t s = 0; s < obs.size(); s++)
        r[s] = obs.z[s] - predict(obs.x[s], obs.y[s], obs.h[s], obs.smooth[s]);
      if (options.bilateralSigmaH)
        itsStages.emplace_back(new BilateralStage(itsCoarse,
                                                  obs,
                                                  r,
                                                  sigma,
                                                  options.layerSpacing,
                                                  *options.bilateralSigmaH,
                                                  options.maxHeight,
                                                  options.damping,
                                                  threads));
      else
        itsStages.emplace_back(
            new BarnesStage(itsCoarse, obs, r, sigma, options.damping, threads));
    }
  }

  double trend(double x, double y, double h, double smooth) const
  {
    double t = itsA.sample(x, y);
    const double href =
        (itsModelElevation ? itsModelElevation->sample(x, y) : itsOptions.referenceHeight);
    for (std::size_t k = 0; k < itsBeta.size(); k++)
      t += itsBeta[k].sample(x, y) * predictor_value(itsOptions.predictors[k], h, smooth, href);
    if (itsOptions.fixedLapseRate)
      t += *itsOptions.fixedLapseRate * (h - itsOptions.referenceHeight) / 1000.0;
    return t;
  }

  double predict(double x, double y, double h, double smooth) const
  {
    double t = trend(x, y, h, smooth);
    for (const auto& stage : itsStages)
      t += stage->at(x, y, h);
    return t;
  }

  Field grid(unsigned int threads) const
  {
    const Grid& g = itsTerrain.grid();
    const Field& dem = itsTerrain.elevation();
    const Field& smooth = itsTerrain.smoothElevation();
    const Field a = itsA.resample(g);
    std::vector<Field> beta;
    for (const auto& b : itsBeta)
      beta.push_back(b.resample(g));

    Field out(g);
    const double href0 = itsOptions.referenceHeight;
    parallel_for(g.size(),
                 threads,
                 [&](std::size_t n0, std::size_t n1)
                 {
                   for (std::size_t n = n0; n < n1; n++)
                   {
                     const double h = dem.data()[n];
                     const double href =
                         (itsModelElevation ? itsModelElevation->data()[n] : href0);
                     double t = a.data()[n];
                     for (std::size_t k = 0; k < beta.size(); k++)
                       t += beta[k].data()[n] * predictor_value(itsOptions.predictors[k],
                                                                h,
                                                                smooth.data()[n],
                                                                href);
                     if (itsOptions.fixedLapseRate)
                       t += *itsOptions.fixedLapseRate * (h - href0) / 1000.0;
                     out.data()[n] = t;
                   }
                 });
    for (const auto& stage : itsStages)
      stage->addTo(out, dem, threads);
    return out;
  }

  std::optional<Field> coefficient(Predictor p) const
  {
    for (std::size_t k = 0; k < itsOptions.predictors.size(); k++)
      if (itsOptions.predictors[k] == p && k < itsBeta.size())
        return itsBeta[k];
    return {};
  }

  const Grid& coarse() const { return itsCoarse; }

 private:
  void fit_trend(const Obs& obs, unsigned int threads)
  {
    const Options& opt = itsOptions;
    const double href = opt.referenceHeight;

    std::vector<double> z = obs.z;
    if (opt.fixedLapseRate)
      for (std::size_t s = 0; s < obs.size(); s++)
        z[s] -= *opt.fixedLapseRate * (obs.h[s] - href) / 1000.0;

    const std::size_t k = opt.predictors.size();
    // Far from the stations the level relaxes to the mean of the observations, or with a
    // background to no correction at all
    const bool background = (itsModelElevation != nullptr);

    if (k == 0)
    {
      double level = 0;
      if (!background)
      {
        for (double v : z)
          level += v;
        level /= std::max<std::size_t>(1, z.size());
      }
      std::vector<std::vector<double>> q{std::vector<double>(obs.size(), 1.0), z};
      const auto m = gaussianSums(itsCoarse, obs.x, obs.y, q, opt.localSigma, threads);
      itsA = Field(itsCoarse);
      for (std::size_t n = 0; n < itsCoarse.size(); n++)
        itsA.data()[n] =
            (m[1].data()[n] + opt.levelPrior * level) / (m[0].data()[n] + opt.levelPrior);
      return;
    }

    std::vector<std::vector<double>> pv(k, std::vector<double>(obs.size()));
    std::vector<double> prior(k);
    std::vector<double> ridge0(k);
    std::vector<double> ridge1(k);
    for (std::size_t i = 0; i < k; i++)
    {
      const Predictor p = opt.predictors[i];
      for (std::size_t s = 0; s < obs.size(); s++)
        pv[i][s] =
            predictor_value(p, obs.h[s], obs.smooth[s], itsModelElevation ? obs.hm[s] : href);
      const bool height = (p == Predictor::Height);
      // The background has already been moved to the actual heights
      prior[i] = (height && !itsModelElevation) ? opt.priorLapseRate : 0.0;
      ridge0[i] = height ? opt.regionalHeightRidge : opt.regionalTPIRidge;
      ridge1[i] = height ? opt.localHeightRidge : opt.localTPIRidge;
    }

    double level = 0;
    if (!background)
    {
      for (std::size_t s = 0; s < obs.size(); s++)
      {
        double v = z[s];
        for (std::size_t i = 0; i < k; i++)
          v -= prior[i] * pv[i][s];
        level += v;
      }
      level /= std::max<std::size_t>(1, obs.size());
    }

    const auto regional = local_regression(
        itsCoarse,
        obs,
        pv,
        z,
        opt.regionalSigma,
        [level](std::size_t) { return level; },
        [&prior](std::size_t i, std::size_t) { return prior[i]; },
        1.0,
        ridge0,
        threads);

    auto local = local_regression(
        itsCoarse,
        obs,
        pv,
        z,
        opt.localSigma,
        [&regional](std::size_t n) { return regional.a.data()[n]; },
        [&regional](std::size_t i, std::size_t n) { return regional.beta[i].data()[n]; },
        opt.levelPrior,
        ridge1,
        threads);

    itsA = std::move(local.a);
    itsBeta = std::move(local.beta);
  }

  const Terrain& itsTerrain;
  const Options& itsOptions;
  Grid itsCoarse;
  const Field* itsModelElevation = nullptr;
  Field itsA;
  std::vector<Field> itsBeta;
  std::vector<std::unique_ptr<Stage>> itsStages;
};

// ----------------------------------------------------------------------
/*!
 * \brief Gross error check
 *
 * Each station's residual from the terrain trend is compared with the leave-one-out
 * estimate of its neighbours at similar heights (horizontally at a larger scale if there
 * are too few of them). The threshold scales with the leave-one-out local spread of the
 * innovations, so legitimately rough fields such as mountain inversions are not mistaken
 * for errors. Returns the rejection flags.
 */
// ----------------------------------------------------------------------

std::vector<bool> quality_control(const Terrain& terrain,
                                  const Options& options,
                                  const Obs& obs,
                                  unsigned int threads,
                                  const Field* model_elevation)
{
  Options trend_only = options;
  trend_only.residualSigmas.clear();
  trend_only.qc = false;
  const Grid coarse = terrain.grid().coarsen(options.coarsen);

  std::vector<bool> rejected(obs.size(), false);
  for (unsigned int iter = 0; iter < options.qcIterations; iter++)
  {
    std::vector<bool> keep(obs.size());
    for (std::size_t i = 0; i < obs.size(); i++)
      keep[i] = !rejected[i];
    const Obs sub = obs.subset(keep);
    std::vector<std::size_t> where;
    for (std::size_t i = 0; i < obs.size(); i++)
      if (keep[i])
        where.push_back(i);

    const Model trend(terrain, trend_only, sub, false, threads, model_elevation);
    std::vector<double> r(sub.size());
    for (std::size_t s = 0; s < sub.size(); s++)
      r[s] = sub.z[s] - trend.predict(sub.x[s], sub.y[s], sub.h[s], sub.smooth[s]);

    std::vector<double> loo;
    std::vector<double> support;
    BilateralStage(coarse,
                   sub,
                   r,
                   options.qcSigma,
                   options.layerSpacing,
                   options.qcSigmaH,
                   options.maxHeight,
                   options.damping,
                   threads)
        .loo(sub, r, loo, support);

    std::vector<double> loo2;
    std::vector<double> support2;
    BarnesStage(coarse, sub, r, options.qcFallbackSigma, options.damping, threads)
        .loo(sub, r, loo2, support2);

    std::vector<double> innov(sub.size());
    std::vector<double> cap(sub.size());
    const double maxcap = 4 * options.qcMinThreshold * options.qcMinThreshold;
    for (std::size_t s = 0; s < sub.size(); s++)
    {
      if (support[s] < options.qcMinSupport)
      {
        loo[s] = loo2[s];
        support[s] = support2[s];
      }
      innov[s] = r[s] - loo[s];
      cap[s] = std::min(innov[s] * innov[s], maxcap);
    }

    std::vector<double> loovar;
    std::vector<double> sw;
    BarnesStage(coarse, sub, cap, options.qcSpreadSigma, 1.0, threads).loo(sub, cap, loovar, sw);
    const double prior_var = median(cap);

    bool any = false;
    for (std::size_t s = 0; s < sub.size(); s++)
    {
      const double swcap = loovar[s] * (sw[s] + 1.0);
      const double var = (swcap + prior_var) / (sw[s] + 1.0);
      const double threshold =
          std::max(options.qcMinThreshold, options.qcThreshold * std::sqrt(std::max(var, 0.0)));
      if (std::abs(innov[s]) > threshold && support[s] >= options.qcMinSupport)
      {
        rejected[where[s]] = true;
        any = true;
      }
    }
    if (!any)
      break;
  }
  return rejected;
}

}  // namespace

// ----------------------------------------------------------------------
// Grid and Field
// ----------------------------------------------------------------------

Grid Grid::coarsen(std::size_t factor) const
{
  if (factor == 0)
    throw Fmi::Exception(BCP, "Grid coarsening factor must be positive");
  Grid g = *this;
  g.dx = dx * static_cast<double>(factor);
  g.nx = nx / factor;
  g.ny = ny / factor;
  if (g.nx < 2 || g.ny < 2)
    throw Fmi::Exception(BCP, "Grid too small to be coarsened")
        .addParameter("factor", std::to_string(factor));
  return g;
}

bool Grid::contains(double x, double y) const
{
  return x > x0 && x < x0 + dx * static_cast<double>(nx) && y > y0 &&
         y < y0 + dx * static_cast<double>(ny);
}

bool Grid::operator==(const Grid& other) const
{
  return x0 == other.x0 && y0 == other.y0 && dx == other.dx && nx == other.nx &&
         ny == other.ny;
}

Field::Field(const Grid& grid, double value) : itsGrid(grid), itsData(grid.size(), value) {}

Field::Field(const Grid& grid, std::vector<double> values)
    : itsGrid(grid), itsData(std::move(values))
{
  if (itsData.size() != grid.size())
    throw Fmi::Exception(BCP, "Field size does not match the grid size");
}

double Field::sample(double x, double y) const
{
  const double fx = (x - itsGrid.x0) / itsGrid.dx - 0.5;
  const double fy = (y - itsGrid.y0) / itsGrid.dx - 0.5;
  const auto maxi = static_cast<long>(itsGrid.nx) - 2;
  const auto maxj = static_cast<long>(itsGrid.ny) - 2;
  const long i0 = std::min(std::max(static_cast<long>(std::floor(fx)), 0L), maxi);
  const long j0 = std::min(std::max(static_cast<long>(std::floor(fy)), 0L), maxj);
  const double tx = std::min(std::max(fx - static_cast<double>(i0), 0.0), 1.0);
  const double ty = std::min(std::max(fy - static_cast<double>(j0), 0.0), 1.0);
  const auto i = static_cast<std::size_t>(i0);
  const auto j = static_cast<std::size_t>(j0);
  const Field& f = *this;
  return (1 - tx) * (1 - ty) * f(i, j) + tx * (1 - ty) * f(i + 1, j) + (1 - tx) * ty * f(i, j + 1) +
         tx * ty * f(i + 1, j + 1);
}

Field Field::resample(const Grid& target) const
{
  if (target == itsGrid)
    return *this;
  Field out(target);
  for (std::size_t j = 0; j < target.ny; j++)
  {
    const double y = target.y0 + (static_cast<double>(j) + 0.5) * target.dx;
    for (std::size_t i = 0; i < target.nx; i++)
      out(i, j) = sample(target.x0 + (static_cast<double>(i) + 0.5) * target.dx, y);
  }
  return out;
}

// ----------------------------------------------------------------------

std::vector<Field> gaussianSums(const Grid& grid,
                                const std::vector<double>& x,
                                const std::vector<double>& y,
                                const std::vector<std::vector<double>>& quantities,
                                double sigma,
                                unsigned int threads)
{
  try
  {
    const double sigma_cells = sigma / grid.dx;
    auto fields = splat(grid, x, y, quantities);
    const double peak = kernel(sigma_cells).peak2d;
    auto finish = [&](Field& f, unsigned int blur_threads)
    {
      blur(f, sigma_cells, blur_threads);
      for (std::size_t n = 0; n < grid.size(); n++)
        f.data()[n] /= peak;
    };
    // Independent fields parallelize better than the rows of a single field
    if (fields.size() > 1 && threads != 1)
      parallel_for(fields.size(),
                   threads,
                   [&](std::size_t k0, std::size_t k1)
                   {
                     for (std::size_t k = k0; k < k1; k++)
                       finish(fields[k], 1);
                   });
    else
      for (auto& f : fields)
        finish(f, threads);
    return fields;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Gaussian sums failed");
  }
}

// ----------------------------------------------------------------------
// Terrain
// ----------------------------------------------------------------------

Terrain::Terrain(const Grid& grid, std::vector<double> elevation, double tpiSigma)
    : itsElevation(grid, std::move(elevation))
{
  try
  {
    for (std::size_t n = 0; n < grid.size(); n++)
    {
      double& h = itsElevation.data()[n];
      if (!std::isfinite(h) || h < 0)
        h = 0;  // sea surface
    }

    // Blur with a zero boundary condition, renormalised near the edges
    itsSmoothElevation = itsElevation;
    Field ones(grid, 1.0);
    blur(itsSmoothElevation, tpiSigma / grid.dx, 0);
    blur(ones, tpiSigma / grid.dx, 0);
    for (std::size_t n = 0; n < grid.size(); n++)
      itsSmoothElevation.data()[n] /= ones.data()[n];
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Failed to initialize gridding terrain");
  }
}

// ----------------------------------------------------------------------
// Analysis
// ----------------------------------------------------------------------

class Analysis::Impl
{
 public:
  Impl(const Terrain& terrain,
       const std::vector<Station>& stations,
       const Background* background,
       const Options& options)
      : itsTerrain(terrain), itsOptions(options), itsStatus(stations.size(), StationStatus::Used)
  {
    const Grid& grid = terrain.grid();
    if (background)
    {
      if (!(background->value.grid() == grid) || !(background->elevation.grid() == grid))
        throw Fmi::Exception(BCP, "The background must be given on the terrain grid");
      itsBackground = *background;
    }

    // With a background obs holds the corrections to it, raw the observations themselves
    Obs obs;
    std::vector<double> raw;
    for (std::size_t i = 0; i < stations.size(); i++)
    {
      const auto& s = stations[i];
      double base = 0;
      double hm = 0;
      if (itsBackground && grid.contains(s.x, s.y))
      {
        hm = itsBackground->elevation.sample(s.x, s.y);
        base = background_at(s.x, s.y, s.elevation, hm);
      }
      if (!grid.contains(s.x, s.y) || !std::isfinite(s.value) || !std::isfinite(s.elevation) ||
          !std::isfinite(base))
      {
        itsStatus[i] = StationStatus::Outside;
        continue;
      }
      obs.x.push_back(s.x);
      obs.y.push_back(s.y);
      obs.h.push_back(s.elevation);
      obs.z.push_back(s.value - base);
      obs.smooth.push_back(terrain.smoothElevation().sample(s.x, s.y));
      if (itsBackground)
        obs.hm.push_back(hm);
      obs.index.push_back(i);
      raw.push_back(s.value);
    }

    const Field* model_elevation = (itsBackground ? &itsBackground->elevation : nullptr);

    std::vector<bool> keep(obs.size(), true);
    if (options.qc && obs.size() > 0)
    {
      const auto rejected =
          quality_control(terrain, itsOptions, obs, options.threads, model_elevation);
      for (std::size_t s = 0; s < obs.size(); s++)
      {
        keep[s] = !rejected[s];
        if (rejected[s])
          itsStatus[obs.index[s]] = StationStatus::Rejected;
      }
    }

    // The analysis of the observations alone, for blending and where the background is missing
    const bool missing = (itsBackground && (has_missing(itsBackground->value) ||
                                            has_missing(itsBackground->elevation)));
    if (missing)
      itsTaper = taper(*itsBackground, options);
    if (itsBackground && (options.backgroundWeight < 1 || missing))
    {
      Obs plain = obs;
      plain.z = raw;
      plain.hm.clear();
      plain = plain.subset(keep);
      itsPlain = std::make_unique<Model>(terrain, itsOptions, plain, true, options.threads);

      // Station equivalents S within the local scale: the analysis of the observations alone
      // is blended in by S / (S + 1), since far from the stations it knows nothing
      const Grid coarse = grid.coarsen(options.coarsen);
      itsSupport = gaussianSums(coarse,
                                plain.x,
                                plain.y,
                                {std::vector<double>(plain.size(), 1.0)},
                                options.localSigma,
                                options.threads)[0];
      for (std::size_t n = 0; n < coarse.size(); n++)
        itsSupport.data()[n] /= itsSupport.data()[n] + 1.0;
    }

    itsModel = std::make_unique<Model>(
        terrain, itsOptions, obs.subset(keep), true, options.threads, model_elevation);
  }

  // The background moved from the model orography hm to the height h
  double background_at(double x, double y, double h, double hm) const
  {
    return itsBackground->value.sample(x, y) +
           itsOptions.backgroundLapseRate * (h - hm) / 1000.0;
  }

  // Combine the correction, the background and the plain analysis. The plain analysis is
  // blended in only as far as there are stations behind it (support 0...1), and fully where
  // the background fades out (taper 0).
  double combine(
      double correction, double base, double plain, double taper, double support) const
  {
    if (!std::isfinite(base))
      return plain;
    const double corrected = base + correction;
    if (!itsPlain)
      return corrected;
    const double w = taper * (1 - (1 - itsOptions.backgroundWeight) * support);
    return w * corrected + (1 - w) * plain;
  }

  // Weight multiplier of the background: 1 well inside its data, falling continuously to 0 at
  // the edges of the data. A blurred validity mask, normalised by the blurred ones so that the
  // edges of the grid itself do not count, is 1/2 at the edge of the data.
  static Field taper(const Background& theBackground, const Options& theOptions)
  {
    const Grid& g = theBackground.value.grid();
    Field valid(g);
    for (std::size_t n = 0; n < g.size(); n++)
      valid.data()[n] = (std::isfinite(theBackground.value.data()[n]) &&
                         std::isfinite(theBackground.elevation.data()[n]))
                            ? 1.0
                            : 0.0;
    Field result = valid;
    Field norm(g, 1.0);
    const double sigma_cells = theOptions.backgroundTaper / g.dx;
    blur(result, sigma_cells, theOptions.threads);
    blur(norm, sigma_cells, theOptions.threads);
    for (std::size_t n = 0; n < g.size(); n++)
    {
      const double t = (norm.data()[n] > 0 ? result.data()[n] / norm.data()[n] : 0.0);
      result.data()[n] = (valid.data()[n] > 0 ? std::clamp(2 * t - 1, 0.0, 1.0) : 0.0);
    }
    return result;
  }

  double taper_at(std::size_t n) const
  {
    return itsTaper.values().empty() ? 1.0 : itsTaper.data()[n];
  }

  Field grid() const
  {
    Field out = itsModel->grid(itsOptions.threads);
    if (!itsBackground)
      return out;

    const Field plain = (itsPlain ? itsPlain->grid(itsOptions.threads) : Field());
    const Field support = (itsPlain ? itsSupport.resample(out.grid()) : Field());
    const Field& dem = itsTerrain.elevation();
    const double lapse = itsOptions.backgroundLapseRate;
    parallel_for(out.values().size(),
                 itsOptions.threads,
                 [&](std::size_t n0, std::size_t n1)
                 {
                   for (std::size_t n = n0; n < n1; n++)
                   {
                     const double base =
                         itsBackground->value.data()[n] +
                         lapse * (dem.data()[n] - itsBackground->elevation.data()[n]) / 1000.0;
                     out.data()[n] = combine(out.data()[n],
                                             base,
                                             itsPlain ? plain.data()[n] : 0.0,
                                             taper_at(n),
                                             itsPlain ? support.data()[n] : 0.0);
                   }
                 });
    return out;
  }

  double predict(double x, double y, double elevation) const
  {
    const double smooth = itsTerrain.smoothElevation().sample(x, y);
    const double correction = itsModel->predict(x, y, elevation, smooth);
    if (!itsBackground)
      return correction;
    const double hm = itsBackground->elevation.sample(x, y);
    const double plain = (itsPlain ? itsPlain->predict(x, y, elevation, smooth) : 0.0);
    const double taper = (itsTaper.values().empty() ? 1.0 : itsTaper.sample(x, y));
    const double support = (itsPlain ? itsSupport.sample(x, y) : 0.0);
    return combine(correction, background_at(x, y, elevation, hm), plain, taper, support);
  }

  static bool has_missing(const Field& f)
  {
    for (double v : f.values())
      if (!std::isfinite(v))
        return true;
    return false;
  }

  const Terrain& itsTerrain;
  const Options itsOptions;
  std::optional<Background> itsBackground;
  Field itsTaper;    // empty when the background has no missing values
  Field itsSupport;  // station support of the plain analysis on the coarse grid
  std::vector<StationStatus> itsStatus;
  std::unique_ptr<Model> itsModel;
  std::unique_ptr<Model> itsPlain;
};

Analysis::Analysis(const Terrain& terrain,
                   const std::vector<Station>& stations,
                   const Options& options)
try : impl(std::make_unique<Impl>(terrain, stations, nullptr, options))
{
}
catch (...)
{
  throw Fmi::Exception::Trace(BCP, "Point gridding failed");
}

Analysis::Analysis(const Terrain& terrain,
                   const std::vector<Station>& stations,
                   const Background& background,
                   const Options& options)
try : impl(std::make_unique<Impl>(terrain, stations, &background, options))
{
}
catch (...)
{
  throw Fmi::Exception::Trace(BCP, "Point gridding with a background failed");
}

Analysis::~Analysis() = default;
Analysis::Analysis(Analysis&& other) noexcept = default;

Field Analysis::grid() const
{
  try
  {
    return impl->grid();
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Point gridding failed");
  }
}

double Analysis::predict(double x, double y, double elevation) const
{
  return impl->predict(x, y, elevation);
}

const std::vector<StationStatus>& Analysis::status() const
{
  return impl->itsStatus;
}

std::optional<Field> Analysis::lapseRate() const
{
  return impl->itsModel->coefficient(Predictor::Height);
}

}  // namespace PointGridding
}  // namespace Fmi
