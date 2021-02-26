#include "CoordinateTransformation.h"

#include "Interrupt.h"
#include "OGR.h"
#include "OGRCoordinateTransformationFactory.h"
#include "ProjInfo.h"
#include "SpatialReference.h"
#include "Types.h"

#include <boost/functional/hash.hpp>

#include <gdal_version.h>
#include <iostream>
#include <limits>
#include <ogr_geometry.h>
#include <ogr_spatialref.h>
#include <stdexcept>

// So far we've had no data with axes swapped since we force axis orientation in all constructors.
// This might change when using external sources for spatial references, then we'll need to put this
// flag on.

#define CHECK_AXES 0

namespace Fmi
{
class CoordinateTransformation::Impl
{
 public:
  ~Impl() = default;
  Impl() = delete;

  Impl(const Impl& other)
      : m_source(other.m_source),
        m_target(other.m_target),
        m_transformation(OGRCoordinateTransformationFactory::Create(
            other.m_source.projInfo().projStr(), other.m_target.projInfo().projStr())),
        m_hash(other.m_hash)
  {
  }

  Impl(const SpatialReference& theSource, const SpatialReference& theTarget)
      : m_source(theSource),
        m_target(theTarget),
        m_transformation(OGRCoordinateTransformationFactory::Create(theSource.projInfo().projStr(),
                                                                    theTarget.projInfo().projStr()))
  {
    m_hash = theSource.hashValue();
    boost::hash_combine(m_hash, theTarget.hashValue());
  }

  Impl& operator=(const Impl&) = delete;

  const SpatialReference m_source;
  const SpatialReference m_target;
  OGRCoordinateTransformationFactory::Ptr m_transformation;
  std::size_t m_hash = 0;
};

CoordinateTransformation::CoordinateTransformation(const CoordinateTransformation& other)
    : impl(new Impl(*other.impl))
{
}

CoordinateTransformation::CoordinateTransformation(const SpatialReference& theSource,
                                                   const SpatialReference& theTarget)
    : impl(new Impl(theSource, theTarget))
{
}

bool CoordinateTransformation::transform(double& x, double& y) const
{
#if CHECK_AXES
  if (impl->m_swapInput)
    std::swap(x, y);
#endif

  bool ok = (impl->m_transformation->Transform(1, &x, &y) != 0);

  if (!ok)
  {
    x = std::numeric_limits<double>::quiet_NaN();
    y = x;
    return false;
  }

  // if (impl->m_swapOutput) std::swap(x, y);
  return true;
}

bool CoordinateTransformation::transform(std::vector<double>& x, std::vector<double>& y) const
{
  if (x.size() != y.size())
    throw std::runtime_error("X- and Y-coordinate vector sizes do not match");

  if (x.empty())
    throw std::runtime_error(
        "Cannot do coordinate transformation for empty X- and Y-coordinate vectors");

#if CHECK_AXES
  if (impl->m_swapInput)
    std::swap(x, y);
#endif

  int n = static_cast<int>(x.size());
  std::vector<int> flags(n, 0);

  bool ok = (impl->m_transformation->Transform(n, &x[0], &y[0], nullptr, &flags[0]) != 0);

#if CHECK_AXES
  if (impl->m_swapOutput)
    std::swap(x, y);
#endif

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

bool CoordinateTransformation::transform(OGRGeometry& geom) const
{
#if CHECK_AXES
  if (impl->m_swapInput)
    geom.swapXY();
#endif

  bool ok = (geom.transform(impl->m_transformation.get()) != OGRERR_NONE);

#if CHECK_AXES
  if (ok && impl->m_swapOutput)
    geom.swapXY();
#endif

  return ok;
}

const SpatialReference& CoordinateTransformation::getSourceCS() const
{
  return *impl->m_source;
}

const SpatialReference& CoordinateTransformation::getTargetCS() const
{
  return *impl->m_target;
}

const OGRCoordinateTransformation& CoordinateTransformation::operator*() const
{
  return *impl->m_transformation;
}

OGRCoordinateTransformation* CoordinateTransformation::get() const
{
  return impl->m_transformation.get();
}

bool isEmpty(const OGREnvelope& env)
{
  return (env.MinX == 0 && env.MinY == 0 && env.MaxX == 0 && env.MaxY == 0);
}

bool contains_longitudes(const OGREnvelope& env1, const OGREnvelope& env2)
{
  return env1.MinX <= env2.MinX && env1.MaxX >= env2.MaxX;
}

OGRGeometry* CoordinateTransformation::transformGeometry(const OGRGeometry& geom,
                                                         double theMaximumSegmentLength) const
{
  OGRGeometryPtr g(OGR::normalizeWindingOrder(geom));

  // If input is geographic apply geographic cuts
  if (impl->m_source.isGeographic())
  {
    auto target_envelope = interruptEnvelope(impl->m_target);

    OGREnvelope shape_envelope;
    geom.getEnvelope(&shape_envelope);

    Interrupt interrupt = interruptGeometry(impl->m_target);

    // Do quick vertical cuts
    if (!interrupt.cuts.empty())
    {
      for (const auto& box : interrupt.cuts)
      {
        g.reset(OGR::polycut(*g, box, theMaximumSegmentLength));
        if (!g || g->IsEmpty())
          return nullptr;
      }
    }

    // If the target envelope is not set, we must try clipping.
    // Otherwise if the geometry contains the target area, no clipping is needed.
    // We test only X-containment, since the target envelope may reach the North Pole,
    // but there is really no data beyond the 84th latitude.

    if (isEmpty(target_envelope) || !contains_longitudes(shape_envelope, target_envelope))
    {
      if (interrupt.cutGeometry)
        g.reset(g->Difference(interrupt.cutGeometry.get()));
      if (!g || g->IsEmpty())
        return nullptr;
    }

    if (interrupt.andGeometry)
      g.reset(g->Intersection(interrupt.andGeometry.get()));
    if (!g || g->IsEmpty())
      return nullptr;
  }

  // Here GDAL would also check if the geometry is geometric and circles the pole etc, we skip that
  // for now since almost all our data is in WGS84.

  if (!this->transform(*g))
  {
    // std::cerr << "Failed to transform geometry\n";
    // return nullptr;
  }

  return OGR::renormalizeWindingOrder(*g);
}

std::size_t CoordinateTransformation::hashValue() const
{
  return impl->m_hash;
}

}  // namespace Fmi
