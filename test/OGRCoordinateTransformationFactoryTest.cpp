#include "OGRCoordinateTransformationFactory.h"
#include "TestDefs.h"
#include <regression/tframe.h>
#include <ogr_spatialref.h>

using namespace std;

namespace Tests
{
void create()
{
  using Fmi::OGRCoordinateTransformationFactory::Create;

  auto trans1 = Create("WGS84", "EPSG:2393");

  auto* ptr1 = trans1.get();  // save the address
  trans1.reset();             // delete the transformation returning it to the factory

  auto trans2 = Create("WGS84", "EPSG:2393");  // get the same transformation again

  if (ptr1 != trans2.get()) TEST_FAILED("Should get same transformation back after releasing it");

  TEST_PASSED();
}

// Test driver
class tests : public tframe::tests
{
  // Overridden message separator
  virtual const char* error_message_prefix() const { return "\n\t"; }
  // Main test suite
  void test() { TEST(create); }

};  // class tests

}  // namespace Tests

int main(void)
{
  cout << endl << "OGRCoordinateTransformationFactory tester" << endl << "==========" << endl;
  Tests::tests t;
  return t.run();
}
