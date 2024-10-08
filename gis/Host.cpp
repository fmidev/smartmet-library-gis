#include "Host.h"
#include <fmt/format.h>
#include <macgyver/Exception.h>
#include <macgyver/StringConversion.h>
#include <gdal_version.h>
#include <ogrsf_frmts.h>

namespace Fmi
{
// ----------------------------------------------------------------------
/*!
 * \brief The only available constructor
 */
// ----------------------------------------------------------------------

Host::Host(std::string theHostname,
           std::string theDatabase,
           std::string theUsername,
           std::string thePassword,
           int thePort)
    : itsHostname(std::move(theHostname)),
      itsDatabase(std::move(theDatabase)),
      itsUsername(std::move(theUsername)),
      itsPassword(std::move(thePassword)),
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
  try
  {
    return fmt::format("PG:host='{}' port='{}' dbname='{}' user='{}' password='{}'",
                       itsHostname,
                       itsPort,
                       itsDatabase,
                       itsUsername,
                       itsPassword);
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Connect to the database
 */
// ----------------------------------------------------------------------

GDALDataPtr Host::connect() const
{
  try
  {
#if GDAL_VERSION_MAJOR < 2
    auto* driver = OGRSFDriverRegistrar::GetRegistrar()->GetDriverByName("PostgreSQL");
#else
    auto* driver = GetGDALDriverManager()->GetDriverByName("PostgreSQL");
#endif
    if (driver == nullptr)
      throw Fmi::Exception::Trace(BCP, "PostgreSQL driver not installed!");

    auto src = dataSource();

#if GDAL_VERSION_MAJOR < 2
    auto ptr = GDALDataPtr(driver->Open(src.c_str()));
#else
    GDALOpenInfo info(src.c_str(), GA_ReadOnly);
    auto ptr = GDALDataPtr(driver->Open(&info, true));
#endif

    if (!ptr)
      throw Fmi::Exception(BCP, "Failed to open connection to database")
          .addParameter("Host", itsHostname)
          .addParameter("Database", itsDatabase)
          .addParameter("User", itsUsername)
          .addParameter("Port", Fmi::to_string(itsPort));

    ptr->ExecuteSQL("SET CLIENT_ENCODING TO 'UTF8'", nullptr, nullptr);
    return ptr;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

}  // namespace Fmi
