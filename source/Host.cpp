#include "Host.h"
#include <sstream>
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
  std::stringstream ss;
  ss << "PG:host='" << itsHostname << "' port='" << itsPort << "' dbname='" << itsDatabase
     << "' user='" << itsUsername << "' password='" << itsPassword << "'";
  return ss.str();
}

// ----------------------------------------------------------------------
/*!
 * \brief Connect to the database
 */
// ----------------------------------------------------------------------

OGRDataSourcePtr Host::connect() const
{
  OGRSFDriver* driver(OGRSFDriverRegistrar::GetRegistrar()->GetDriverByName("PostgreSQL"));
  if (!driver) throw std::runtime_error("PostgreSQL driver not installed!");

  auto src = dataSource();

  auto ptr = OGRDataSourcePtr(driver->Open(src.c_str()));

  if (!ptr) throw std::runtime_error("Failed to open database using source " + dataSource());

  ptr->ExecuteSQL("SET CLIENT_ENCODING TO 'UTF8'", 0, 0);
  return ptr;
}

}  // namespace Fmi
