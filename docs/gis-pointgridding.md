# Terrain-aware gridding of point observations

`Fmi::PointGridding` interpolates station observations onto a regular grid. Without a
background field the only help it has is the terrain; with one, typically a recent forecast,
the observations correct the background (section 4). It is meant
for analyses made on the fly: a full 2.5 km grid over Europe (2000×1760 cells) from about
3000 stations takes well under a second.

The method was developed and validated with a Python prototype against European SYNOP
temperatures. The prototype also produced the reference data of `test/PointGriddingTest.cpp`.

## Contents

1. [Usage](#1-usage)
2. [The method](#2-the-method)
3. [Validation](#3-validation)
4. [Correcting a background](#4-correcting-a-background)
5. [Options](#5-options)
6. [Performance](#6-performance)
7. [Limitations](#7-limitations)

---

## 1. Usage

```cpp
#include <gis/PointGridding.h>

using namespace Fmi::PointGridding;

// Once per area: the grid and the terrain heights (m), rows from south to north.
// Use an equal-area projection, for Europe for example EPSG:3035.
const Grid grid{2000000, 1200000, 2500, 2000, 1760};  // x0, y0, dx, nx, ny
const Terrain terrain(grid, heights);

// Per analysis: stations in the same coordinate system, each with its own altitude
std::vector<Station> stations = ...;  // x, y, elevation, value

const Analysis analysis(terrain, stations, Options());
Field values = analysis.grid();             // the analysis on the terrain grid
double t = analysis.predict(x, y, height);  // or at any point
```

`Terrain` is immutable and can be shared by any number of threads. `Analysis` objects are
independent of each other. `Options::threads` sets the threads one analysis uses (0 = all
cores); the result does not depend on it.

Cell centres are at `x0 + (i + 0.5) * dx`. The station elevation should be the altitude of the
station itself: summit stations are often a kilometre above the cell mean height.

## 2. The method

1. **Gross error check.** Each station's residual from the terrain trend (step 2) is compared
   with the leave-one-out estimate of its neighbours at similar heights (the height-aware
   Barnes of step 3). Stations with too few such neighbours are compared horizontally at a
   larger scale. The rejection threshold scales with the local spread of the innovations, so
   that legitimately rough fields such as valley inversions or sparse deserts are not taken
   for errors. Up to three iterations.

2. **Terrain trend.** A local regression `value = a + b·h + c·tpi` is fitted at every cell of
   a coarse grid (10 km by default), where `h` is the height and `tpi` the topographic
   position index, the height relative to the terrain smoothed over 10 km (valleys negative,
   ridges positive). The fit is Gaussian-weighted, so it is computed from blurred station
   *moment fields* (sums of 1, z, h, tpi, h·z, h², ...) without any neighbour searches.
   A regional fit (600 km) acts as the prior of the local one (150 km), and the regional fit
   is pulled toward −6.5 K/km by ridge regression. Hence the lapse rate is fitted where
   the stations span enough height (the Alps, Scandinavia) and stays near the standard
   atmosphere elsewhere, for example over flat land, where a height regression would only
   pick up coast versus inland differences.

3. **Residual correction.** The station residuals from the trend are interpolated with a
   damped, height-aware Barnes: `w = exp(−d²/2σ²)·exp(−Δh²/2σh²)` with σh = 300 m, divided by
   `Σw + 0.25`. Two passes are made, σ = 60 km and then 25 km for what remains. The height term keeps valley and summit stations
   from correcting each other, which matters most in inversions. It is computed with a
   bilateral grid: stations are splatted into 250 m height layers, the layers are blurred
   horizontally and across, and each cell reads the result at its own height. The damping
   makes the correction fade to zero far from stations, so that over the sea and in sparse
   areas the result falls back to the terrain trend instead of spreading one station's
   anomaly.

4. **Composition.** The smooth fields are resampled to the terrain grid and combined with
   the full-resolution heights.

All Gaussian weighting uses four passes of an extended box filter (Gwosdek et al. 2011), the
approach of the fast Barnes interpolation of Zürcher (2023). The cost is linear in the number
of cells and stations and independent of the length scales. A Gaussian with any σ is exact in
variance; the shape differs from a true Gaussian by a few percent, which
`GaussianSumsApproximateExactWeights` checks.

## 3. Validation

10-fold cross-validation of 2 m temperature on 85 cases (00, 06, 12 and 18 UTC of 21 days in
every season from 2025-09 to 2026-10, chosen to include the strongest inversion mornings in the
Alps and Scandinavia), about 2800 European SYNOP stations each. RMSE (K) over all 237 000
predictions:

| Method | All | Above 800 m | Alps above 1500 m | Scandinavia |
|--------|-----|-------------|-------------------|-------------|
| Barnes, no terrain | 2.44 | 4.51 | 6.28 | 2.21 |
| Fixed lapse rate −6.5 K/km + Barnes | 1.98 | 3.10 | 4.27 | 2.07 |
| Local height regression + Barnes | 1.75 | 2.39 | 2.33 | 2.09 |
| Height + TPI regression + Barnes | 1.70 | 2.26 | 2.09 | 1.97 |
| Height + TPI + height-aware Barnes, one 40 km pass | 1.68 | 2.18 | 1.90 | 1.94 |
| The same with two passes (60 + 25 km), tuned (default) | **1.62** | **2.12** | **1.82** | **1.89** |

The ranking is the same in every season and at every hour. The defaults come from a
one-at-a-time sweep of the scales, ridges, damping and TPI smoothing followed by a check of
their combinations: two residual passes, damping 0.25 and three times stronger local ridges
improved every season; a third pass, a smaller local scale or a wider TPI did not. Plain Barnes leaves summits 4 K
too warm and a fixed lapse rate 2 K too cold on average; on inversion mornings the errors are
much larger. A quadratic height term was tried and made the results worse.

The gross error check was tested by adding errors of 4–15 K to 1 % of the stations: 94 % of
errors over 10 K and 88 % of 8–10 K errors were caught, with 0.08 % false alarms. The
remaining misses are mostly isolated stations which cannot be judged.

## 4. Correcting a background

```cpp
// The background and the model orography interpolated to the terrain grid
const Background background{forecast, modelOrography};
const Analysis analysis(terrain, stations, background, Options());
```

1. The background is moved from the model orography to the actual heights with
   `backgroundLapseRate` (−6.5 K/km; 0 for fields without a height dependency such as
   reduced pressure). A 25 km model sees the Alps as a plateau, so this alone fixes most of
   its mountain errors.
2. The differences of the observations from the moved background are gridded with the
   method of section 2. The height predictor becomes the height above the model orography,
   with prior 0 since the background has already been moved. Far from the stations the
   correction relaxes to zero, unlike the observations alone, whose level relaxes to their
   mean: over the seas the result is the background.
3. The result is blended with the analysis of the observations alone:
   `backgroundWeight · corrected + (1 − backgroundWeight) · observations only`.
   Where the background is missing (NaN), the observations alone are used. Near the edges of
   the background data its weight fades to zero over about `backgroundTaper` (50 km), so the
   two do not meet in a seam.

Validation with ECMWF IFS open data (0.25°) as the background, the same cross-validation as in
section 3 for the cases with a background available, RMSE (K):

| Method | All | 12 UTC | 18 UTC |
|--------|-----|--------|--------|
| Observations alone (section 3) | 1.62 | 1.43 | 1.49 |
| IFS +6 h forecast alone | 1.98 | | |
| IFS corrected with the observations | 1.49 | 1.28 | 1.35 |
| Blend 0.7 · corrected + 0.3 · observations alone (default) | **1.46** | | |

The forecast alone is worst in the Alps, which the 25 km model does not resolve, and the blend
removes that weakness. The lead time hardly matters once corrected: 1.50, 1.51, 1.52 and
1.54 K for 0, 6, 12 and 24 h forecasts, against 1.87–2.12 K uncorrected. Relaxing the
correction to zero instead of the mean innovation did not change any of the scores at the
stations; it only matters away from them. The blend weight was chosen on the same cases, so
its advantage is slightly optimistic.

## 5. Options

| Option | Default | Meaning |
|--------|---------|---------|
| `predictors` | height, TPI | Regression predictors. Empty for fields without a height dependency: the method is then a damped multi-scale Barnes interpolation. |
| `fixedLapseRate` | none | K/km applied instead of a fitted height term |
| `referenceHeight` | 500 m | Height at which the level `a` is defined |
| `priorLapseRate` | −6.5 K/km | Regional prior of the height coefficient |
| `regionalSigma`, `localSigma` | 600, 150 km | Scales of the regional and local fits |
| `regionalHeightRidge`, `localHeightRidge` | 1.8, 0.81 station·km² | How much height span is needed to move the coefficient from its prior |
| `regionalTPIRidge`, `localTPIRidge` | 0.2, 0.09 station·km² | The same for the TPI coefficient (prior 0) |
| `levelPrior` | 1 | Pseudo stations pulling the local level to the regional one |
| `residualSigmas` | 60, 25 km | Barnes passes, each correcting the residual of the previous |
| `bilateralSigmaH` | 300 m | Height scale of the residual passes, none for horizontal only |
| `layerSpacing`, `maxHeight` | 250 m, 4000 m | Bilateral grid layers |
| `damping` | 0.25 | Station equivalents in the Barnes denominator |
| `coarsen` | 4 | Resolution of the smooth fields relative to the terrain grid |
| `qc` and `qc*` | on | Gross error check, see section 2 |
| `backgroundLapseRate` | −6.5 K/km | Moves a background from the model orography to the actual heights |
| `backgroundWeight` | 0.7 | Weight of the corrected background, the rest is the analysis of the observations alone |
| `backgroundTaper` | 50 km | The background fades out over about this distance from the edges of its data |
| `threads` | 0 | Threads per analysis, 0 = all cores |

The TPI smoothing scale (10 km) is a `Terrain` constructor argument.

## 6. Performance

Full Europe, 2000×1760 cells at 2.5 km, 3000 stations, 8 threads: about 0.36 s for the fit
including the gross error check and 0.19 s for composing the full-resolution grid. The
smooth fields are computed on the 10 km grid; computing them at 2.5 km does not change the
cross-validation scores.

The `Terrain` constructor blurs the height field once, about 0.1 s for Europe. A background
doubles the cost when blended, since the observations are also analysed alone.

## 7. Limitations

- Terrain detail comes from the DEM only. Structures not seen by the stations, such as a
  cold pool in an unobserved valley, are modelled by the regression, not observed.
- Wind should be gridded as vector components, and precipitation needs its own treatment
  of dry and wet areas; neither is handled here.
- The algorithm assumes an equal-area grid; with geographic coordinates the Gaussian would
  be elongated toward the poles.
