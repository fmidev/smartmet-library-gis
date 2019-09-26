// ======================================================================
/*!
 * \note The user is responsible for calling OGRRegisterAll() in the
 *       application init phase.
 */
// ======================================================================

#pragma once

#include "Types.h"
#include <boost/shared_ptr.hpp>
#include <string>

namespace Fmi
{
class Host
{
 public:
  Host() = delete;

  Host(const std::string& theHostname,
       const std::string& theDatabase,
       const std::string& theUsername,
       const std::string& thePassword,
       int thePort = 5432);

  GDALDataPtr connect() const;

 private:
  std::string dataSource() const;

  const std::string itsHostname;
  const std::string itsDatabase;
  const std::string itsUsername;
  const std::string itsPassword;
  const int itsPort;
};
}  // namespace Fmi
