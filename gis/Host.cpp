#include "Host.h"

#include <fmt/format.h>

#include <gdal_version.h>
#include <ogrsf_frmts.h>
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

GDALDataPtr Host::connect() const
{
#if GDAL_VERSION_MAJOR < 2
  auto* driver = OGRSFDriverRegistrar::GetRegistrar()->GetDriverByName("PostgreSQL");
#else
  auto* driver = GetGDALDriverManager()->GetDriverByName("PostgreSQL");
#endif
  if (driver == nullptr) throw std::runtime_error("PostgreSQL driver not installed!");

  auto src = dataSource();

#if GDAL_VERSION_MAJOR < 2
  auto ptr = GDALDataPtr(driver->Open(src.c_str()));
#else
  GDALOpenInfo info(src.c_str(), GA_ReadOnly);
  auto ptr = GDALDataPtr(driver->pfnOpen(&info));
#endif

  if (!ptr) throw std::runtime_error("Failed to open database using source " + dataSource());

  ptr->ExecuteSQL("SET CLIENT_ENCODING TO 'UTF8'", nullptr, nullptr);
  return ptr;
}

}  // namespace Fmi
