#include "Host.h"
#include <fmt/format.h>
#include <stdexcept>

namespace Fmi
{
// ----------------------------------------------------------------------
/*!
 * \brief The only available constructor
 */
// ----------------------------------------------------------------------

Host::Host(const std::string& theHostname,
           const std::string& theDatabase,
           const std::string& theUsername,
           const std::string& thePassword,
           int thePort)
    : itsHostname(theHostname),
      itsDatabase(theDatabase),
      itsUsername(theUsername),
      itsPassword(thePassword),
      itsPort(thePort)
{
}

// ----------------------------------------------------------------------
/*!
 * \brief Return the OGRDataSource descriptor
 */
// ----------------------------------------------------------------------

std::string Host::dataSource() const
{
  return fmt::format("PG:host='{}' port='{}' dbname='{}' user='{}' password='{}'",
                     itsHostname,
                     itsPort,
                     itsDatabase,
                     itsUsername,
                     itsPassword);
}

// ----------------------------------------------------------------------
/*!
 * \brief Connect to the database
 */
// ----------------------------------------------------------------------

OGRDataSourcePtr Host::connect() const
{
  OGRSFDriver* driver(OGRSFDriverRegistrar::GetRegistrar()->GetDriverByName("PostgreSQL"));
  if (driver == nullptr) throw std::runtime_error("PostgreSQL driver not installed!");

  auto src = dataSource();

  auto ptr = OGRDataSourcePtr(driver->Open(src.c_str()));

  if (!ptr) throw std::runtime_error("Failed to open database using source " + dataSource());

  ptr->ExecuteSQL("SET CLIENT_ENCODING TO 'UTF8'", nullptr, nullptr);
  return ptr;
}

}  // namespace Fmi
