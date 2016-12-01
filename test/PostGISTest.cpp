#include <regression/tframe.h>
#include "PostGIS.h"
#include "Host.h"
#include "OGR.h"
#include "Box.h"
#include <gdal/ogrsf_frmts.h>

using namespace std;

namespace Tests
{
// ----------------------------------------------------------------------

void read()
{
  Fmi::Host host("popper", "open_data_gis", "open_data_gis_admin", "b89+K/NpMn");

  auto conn = host.connect();
  auto geom = Fmi::PostGIS::read(NULL, conn, "esri.europe_country_wgs84");

  Fmi::Box box(0, 0, 100, 100, 100, 100);
  // for(int i=0; i<100; i++)
  // {
  auto str = Fmi::OGR::exportToSvg(*geom, box, 1);
  // std::cout << str.substr(0,40) << std::endl;
  // }

  TEST_PASSED();
}

// Test driver
class tests : public tframe::tests
{
  // Overridden message separator
  virtual const char* error_message_prefix() const { return "\n\t"; }
  // Main test suite
  void test() { TEST(read); }
};  // class tests

}  // namespace Tests

int main(void)
{
  OGRRegisterAll();

  cout << endl << "PostGIS tester" << endl << "==============" << endl;
  Tests::tests t;
  return t.run();
}
