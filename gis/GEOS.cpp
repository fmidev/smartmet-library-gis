#include "GEOS.h"

#include <geos/geom/Geometry.h>
#include <geos/io/WKBWriter.h>
#include <macgyver/Exception.h>

#include <sstream>

// ----------------------------------------------------------------------
/*!
 * \brief Convert the geometry to WKB
 */
// ----------------------------------------------------------------------

std::string Fmi::GEOS::exportToWkb(const geos::geom::Geometry& theGeom)
{
  try
  {
    std::ostringstream out;
    geos::io::WKBWriter writer;
    writer.write(theGeom, out);
    return out.str();
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}
