#include "CoordinateTransformation.h"
#include "SpatialReference.h"
#include <gdal_version.h>
#include <limits>
#include <ogr_spatialref.h>

namespace Fmi
{
bool is_axis_swapped(const OGRSpatialReference& sr)
{
#if GDAL_VERSION_MAJOR > 1
  return (sr.EPSGTreatsAsLatLong() || sr.EPSGTreatsAsNorthingEasting());
#else
  // GDAL1 does not seem to obey EPSGA flags at all
  return false;
#endif
}

CoordinateTransformation::CoordinateTransformation(const SpatialReference& theSource,
                                                   const SpatialReference& theTarget)
    : itsTransformation(OGRCreateCoordinateTransformation(theSource.get(), theTarget.get())),
      itsInputSwapFlag(theSource.IsAxisSwapped()),
      itsOutputSwapFlag(theTarget.IsAxisSwapped())
{
}

CoordinateTransformation::CoordinateTransformation(const OGRSpatialReference& theSource,
                                                   const OGRSpatialReference& theTarget)
    : itsTransformation(OGRCreateCoordinateTransformation(&theSource, &theTarget)),
      itsInputSwapFlag(is_axis_swapped(theSource)),
      itsOutputSwapFlag(is_axis_swapped(theTarget))
{
}

bool CoordinateTransformation::Transform(double& x, double& y) const
{
  if (itsInputSwapFlag) std::swap(x, y);

  bool ok = (itsTransformation->Transform(1, &x, &y) != 0);

  if (!ok)
  {
    x = std::numeric_limits<double>::quiet_NaN();
    y = x;
    return false;
  }

  if (itsOutputSwapFlag) std::swap(x, y);
  return true;
}

bool CoordinateTransformation::Transform(std::vector<double>& x, std::vector<double>& y) const
{
  if (x.size() != y.size())
    throw std::runtime_error("X- and Y-coordinate vector sizes do not match");

  if (x.empty())
    throw std::runtime_error(
        "Cannot do coordinate transformation for empty X- and Y-coordinate vectors");

  if (itsInputSwapFlag) std::swap(x, y);

  int n = static_cast<int>(x.size());
  std::vector<int> flags(n, 0);

  bool ok = (itsTransformation->Transform(n, &x[0], &y[0], nullptr, &flags[0]) != 0);

  if (itsOutputSwapFlag) std::swap(x, y);

  for (std::size_t i = 0; i < flags.size(); i++)
  {
    if (flags[i] == 0)
    {
      x[i] = std::numeric_limits<double>::quiet_NaN();
      y[i] = x[i];
    }
  }

  return ok;
}

const OGRSpatialReference& CoordinateTransformation::GetSourceCS() const
{
  return *itsTransformation->GetSourceCS();
}

const OGRSpatialReference& CoordinateTransformation::GetTargetCS() const
{
  return *itsTransformation->GetTargetCS();
}

const OGRCoordinateTransformation& CoordinateTransformation::operator*() const
{
  return *itsTransformation;
}

OGRCoordinateTransformation* CoordinateTransformation::get() const
{
  return itsTransformation.get();
}
}  // namespace Fmi
