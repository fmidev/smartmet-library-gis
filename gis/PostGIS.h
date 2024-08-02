// ======================================================================
/*!
 * \note The user is responsible for calling OGRRegisterAll() in the
 *       application init phase.
 */
// ======================================================================

#ifdef UNIX

#pragma once

#include "Host.h"
#include "SpatialReference.h"
#include "Types.h"

#include <optional>

#include <set>
#include <string>

class OGRSpatialReference;

namespace Fmi
{
namespace PostGIS
{
// read geometries and attribute fields
Features read(const Fmi::SpatialReference* theSR,
              const GDALDataPtr& theConnection,
              const std::string& theName,
              const std::set<std::string>& theFieldNames,
              const std::optional<std::string>& theWhereClause = std::optional<std::string>());

// name = "schema.table"
OGRGeometryPtr read(
    const Fmi::SpatialReference* theSR,
    const GDALDataPtr& theConnection,
    const std::string& theName,
    const std::optional<std::string>& theWhereClause = std::optional<std::string>());

}  // namespace PostGIS
}  // namespace Fmi

#endif
