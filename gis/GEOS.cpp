#include "GEOS.h"
#include <geos/geom/Geometry.h>
#include <geos/io/WKBWriter.h>
#include <sstream>
#include <stdexcept>

// ----------------------------------------------------------------------
/*!
 * \brief Convert the geometry to WKB
 */
// ----------------------------------------------------------------------

std::string Fmi::GEOS::exportToWkb(const geos::geom::Geometry& theGeom)
{
  std::ostringstream out;
  geos::io::WKBWriter writer;
  writer.write(theGeom, out);
  return out.str();
}
