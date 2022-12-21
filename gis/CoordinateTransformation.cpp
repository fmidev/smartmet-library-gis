#include "CoordinateTransformation.h"
#include "GeometryBuilder.h"
#include "Interrupt.h"
#include "OGR.h"
#include "OGRCoordinateTransformationFactory.h"
#include "ProjInfo.h"
#include "Shape.h"
#include "SpatialReference.h"
#include "Types.h"
#include <macgyver/Exception.h>
#include <macgyver/Hash.h>
#include <gdal_version.h>
#include <iostream>
#include <limits>
#include <ogr_geometry.h>
#include <ogr_spatialref.h>

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
    try
    {
      m_hash = theSource.hashValue();
      Fmi::hash_combine(m_hash, theTarget.hashValue());
    }
    catch (...)
    {
      throw Fmi::Exception::Trace(BCP, "Operation failed!");
    }
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

CoordinateTransformation::CoordinateTransformation(CoordinateTransformation&& other) noexcept
    : impl(std::move(other.impl))
{
}

CoordinateTransformation::CoordinateTransformation(const SpatialReference& theSource,
                                                   const SpatialReference& theTarget)
    : impl(new Impl(theSource, theTarget))
{
}

bool CoordinateTransformation::transform(double& x, double& y) const
{
  try
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
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

bool CoordinateTransformation::transform(std::vector<double>& x, std::vector<double>& y) const
{
  try
  {
    if (x.size() != y.size())
      throw Fmi::Exception::Trace(BCP, "X- and Y-coordinate vector sizes do not match");

    if (x.empty())
      throw Fmi::Exception::Trace(
          BCP, "Cannot do coordinate transformation for empty X- and Y-coordinate vectors");

#if CHECK_AXES
    if (impl->m_swapInput)
      std::swap(x, y);
#endif

    int n = static_cast<int>(x.size());
    std::vector<int> flags(n, 0);

    bool ok =
        (impl->m_transformation->Transform(n, x.data(), y.data(), nullptr, flags.data()) != 0);

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
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

bool CoordinateTransformation::transform(OGRGeometry& geom) const
{
  try
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
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

const SpatialReference& CoordinateTransformation::getSourceCS() const
{
  try
  {
    return impl->m_source;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

const SpatialReference& CoordinateTransformation::getTargetCS() const
{
  try
  {
    return impl->m_target;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

const OGRCoordinateTransformation& CoordinateTransformation::operator*() const
{
  try
  {
    return *impl->m_transformation;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

OGRCoordinateTransformation* CoordinateTransformation::get() const
{
  try
  {
    return impl->m_transformation.get();
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

bool isEmpty(const OGREnvelope& env)
{
  try
  {
    return (env.MinX == 0 && env.MinY == 0 && env.MaxX == 0 && env.MaxY == 0);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

bool contains_longitudes(const OGREnvelope& env1, const OGREnvelope& env2)
{
  try
  {
    return env1.MinX <= env2.MinX && env1.MaxX >= env2.MaxX;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

OGRGeometry* CoordinateTransformation::transformGeometry(const OGRGeometry& geom,
                                                         double theMaximumSegmentLength) const
{
  try
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
      for (const auto& box : interrupt.cuts)
      {
        g.reset(OGR::polycut(*g, box, theMaximumSegmentLength));
        if (!g || g->IsEmpty())  // NOLINT(cppcheck-nullPointerRedundantCheck)
          return nullptr;
      }

      // printf("***** CUTS ****\n");
      for (auto& shape : interrupt.shapeCuts)
      {
        // shape.print(std::cout);
        g.reset(OGR::polycut(*g, shape, theMaximumSegmentLength));
        if (!g || g->IsEmpty())  // NOLINT(cppcheck-nullPointerRedundantCheck)
          return nullptr;
      }

      if (!interrupt.shapeClips.empty())
      {
        // printf("***** CLIPS ****\n");
        for (auto& shape : interrupt.shapeClips)
        {
          // shape.print(std::cout);
          g.reset(OGR::polyclip(*g, shape, theMaximumSegmentLength));
          if (!g || g->IsEmpty())  // NOLINT(cppcheck-nullPointerRedundantCheck)
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

    // Here GDAL would also check if the geometry is geometric and circles the pole etc, we skip
    // that for now since almost all our data is in WGS84.

    if (!this->transform(*g))
    {
      // std::cerr << "Failed to transform geometry\n";
      // return nullptr;
    }

    return OGR::renormalizeWindingOrder(*g);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

std::size_t CoordinateTransformation::hashValue() const
{
  try
  {
    return impl->m_hash;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

}  // namespace Fmi
