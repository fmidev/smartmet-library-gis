#include <regression/tframe.h>
#include "LandCover.h"
#include <boost/lexical_cast.hpp>
using namespace std;

std::string tostr(double value)
{
  std::ostringstream out;
  out << std::fixed << std::setprecision(1) << value;
  return out.str();
}

namespace Tests
{
// ----------------------------------------------------------------------

void landtype()
{
  Fmi::LandCover cover("/smartmet/share/gis/rasters/globcover");

  Fmi::LandCover::Type value;

  // At sea
  value = cover.coverType(25, 60);
  if (value != Fmi::LandCover::Sea)
    TEST_FAILED("Expected type Sea at coordinate 25,60, not " +
                boost::lexical_cast<std::string>(value));

  // Kumpula
  value = cover.coverType(24.9642, 60.2089);
  if (value != Fmi::LandCover::Urban)
    TEST_FAILED("Expected type Urban at Kumpula, not " + boost::lexical_cast<std::string>(value));

  // Helsinki
  value = cover.coverType(24.93545, 60.16952);
  if (value != Fmi::LandCover::Urban)
    TEST_FAILED("Expected type Urban at Helsinki, not " + boost::lexical_cast<std::string>(value));

  // Ruka
  value = cover.coverType(29.1507, 66.1677);
  if (value != Fmi::LandCover::Urban)
    TEST_FAILED("Expected type Urban at Ruka, not " + boost::lexical_cast<std::string>(value));

  // Matterhorn
  value = cover.coverType(7.6583, 45.9764);
  if (value != Fmi::LandCover::MosaicVegetation)
    TEST_FAILED("Expected type MosaicVegetation at Matterhorn, not " +
                boost::lexical_cast<std::string>(value));

#if 0	
	// DATA DOES NOT REACH THE SOUTH POLE
	// South pole
	value = cover.coverType(0,-90);
	if(value != Fmi::LandCover::NoData)
	  TEST_FAILED("Expected type PermanentSnow at the South Pole, not "+boost::lexical_cast<std::string>(value));
#endif

  // North pole
  value = cover.coverType(0, 90);
  if (value != Fmi::LandCover::Sea)
    TEST_FAILED("Expected type Sea at the North Pole, not " +
                boost::lexical_cast<std::string>(value));

  // More special points

  value = cover.coverType(0, 0);
  if (value != Fmi::LandCover::Sea)
    TEST_FAILED("Expected type Sea at 0,0, not " + boost::lexical_cast<std::string>(value));

  value = cover.coverType(-180, 0);
  if (value != Fmi::LandCover::Sea)
    TEST_FAILED("Expected type Sea at -180,0, not " + boost::lexical_cast<std::string>(value));

  value = cover.coverType(180, 0);
  if (value != Fmi::LandCover::Sea)
    TEST_FAILED("Expected type Sea at 180,0, not " + boost::lexical_cast<std::string>(value));

  value = cover.coverType(180, 0);
  if (value != Fmi::LandCover::Sea)
    TEST_FAILED("Expected type Sea at 180,0, not " + boost::lexical_cast<std::string>(value));

  // Alanya had rounding issue with coordinates
  value = cover.coverType(31.99982, 36.543750000000003);
  if (value != Fmi::LandCover::MosaicForest)
    TEST_FAILED("Expected type MosaicForest at Alanya, not " +
                boost::lexical_cast<std::string>(value));

#if 0
	// Chukotskiy
	value = cover.coverType(179.999,67);
	if(value != Fmi::LandCover::Lakes)
	  TEST_FAILED("Expected type Lakes at 179.999,67, not "+boost::lexical_cast<std::string>(value));
#endif

  value = cover.coverType(-179.999, 67);
  if (value != Fmi::LandCover::Lakes)
    TEST_FAILED("Expected type Lakes at -179.999,67, not " +
                boost::lexical_cast<std::string>(value));

  value = cover.coverType(-180, 67);
  if (value != Fmi::LandCover::Lakes)
    TEST_FAILED("Expected type Lakes at -180,67, not " + boost::lexical_cast<std::string>(value));

  value = cover.coverType(180, 67);
  if (value != Fmi::LandCover::Lakes)
    TEST_FAILED("Expected type Lakes at 180,67, not " + boost::lexical_cast<std::string>(value));

  // Ladoga
  value = cover.coverType(31.6, 60.8);
  if (value != Fmi::LandCover::Lakes)
    TEST_FAILED("Expected type Lakes at Ladoga, not " + boost::lexical_cast<std::string>(value));

  TEST_PASSED();
}

// ----------------------------------------------------------------------

void isopenwater()
{
  Fmi::LandCover cover("/smartmet/share/gis/rasters/globcover");

  // At sea
  if (!cover.isOpenWater(cover.coverType(25, 60)))
    TEST_FAILED("Expected open water at coordinate 25,60");

  // Kumpula
  if (cover.isOpenWater(cover.coverType(24.9642, 60.2089))) TEST_FAILED("Expected land at Kumpula");

  // Ladoga
  if (!cover.isOpenWater(cover.coverType(31.6, 60.8)))
    TEST_FAILED("Expected open water at center of Ladoga");

  TEST_PASSED();
}

// Test driver
class tests : public tframe::tests
{
  // Overridden message separator
  virtual const char* error_message_prefix() const { return "\n\t"; }
  // Main test suite
  void test()
  {
    TEST(landtype);
    TEST(isopenwater);
  }

};  // class tests

}  // namespace Tests

int main(void)
{
  cout << endl << "LandCover tester" << endl << "================" << endl;
  Tests::tests t;
  return t.run();
}
