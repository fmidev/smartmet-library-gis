#include "GEOS.h"
#include <geos/io/WKTReader.h>
#include <regression/tframe.h>

using namespace std;

geos::geom::GeometryFactory factory;

namespace Tests
{
// ----------------------------------------------------------------------

void exportToSvg_wiki_examples()
{
  using namespace Fmi;
  using GEOS::exportToSvg;

  geos::io::WKTReader reader(&factory);

  // Ref: http://en.wikipedia.org/wiki/Well-known_text

  string point = "POINT (30 10)";
  string linestring = "LINESTRING (30 10, 10 30, 40 40)";
  string polygon1 = "POLYGON ((30 10, 40 40, 20 40, 10 20, 30 10))";
  string polygon2 = "POLYGON ((35 10, 45 45, 15 40, 10 20, 35 10),(20 30, 35 35, 30 20, 20 30))";
  string multipoint1 = "MULTIPOINT ((10 40), (40 30), (20 20), (30 10))";
  string multipoint2 = "MULTIPOINT (10 40, 40 30, 20 20, 30 10)";
  string multilinestring = "MULTILINESTRING ((10 10, 20 20, 10 40),(40 40, 30 30, 40 20, 30 10))";
  string multipolygon1 =
      "MULTIPOLYGON (((30 20, 45 40, 10 40, 30 20)),((15 5, 40 10, 10 20, 5 10, 15 5)))";
  string multipolygon2 =
      "MULTIPOLYGON (((40 40, 20 45, 45 30, 40 40)),((20 35, 10 30, 10 10, 30 5, 45 20, 20 35),(30 "
      "20, 20 15, 20 25, 30 20)))";

  {
    string result = exportToSvg(*reader.read(point));
    string ok = "M30 10";
    if (result != ok) TEST_FAILED("Expected: " + ok + "\n\tObtained: " + result);
  }

  {
    string result = exportToSvg(*reader.read(linestring));
    string ok = "M30 10 10 30 40 40";
    if (result != ok) TEST_FAILED("Expected: " + ok + "\n\tObtained: " + result);
  }

  {
    string result = exportToSvg(*reader.read(polygon1));
    string ok = "M30 10 40 40 20 40 10 20Z";
    if (result != ok) TEST_FAILED("Expected: " + ok + "\n\tObtained: " + result);
  }

  {
    string result = exportToSvg(*reader.read(polygon2));
    string ok = "M35 10 45 45 15 40 10 20ZM20 30 35 35 30 20Z";
    if (result != ok) TEST_FAILED("Expected: " + ok + "\n\tObtained: " + result);
  }

  {
    string result = exportToSvg(*reader.read(multipoint1));
    string ok = "M10 40M40 30M20 20M30 10";
    if (result != ok) TEST_FAILED("Expected: " + ok + "\n\tObtained: " + result);
  }

  {
    string result = exportToSvg(*reader.read(multipoint2));
    string ok = "M10 40M40 30M20 20M30 10";
    if (result != ok) TEST_FAILED("Expected: " + ok + "\n\tObtained: " + result);
  }

  {
    string result = exportToSvg(*reader.read(multilinestring));
    string ok = "M10 10 20 20 10 40M40 40 30 30 40 20 30 10";
    if (result != ok) TEST_FAILED("Expected: " + ok + "\n\tObtained: " + result);
  }

  {
    string result = exportToSvg(*reader.read(multipolygon1));
    string ok = "M30 20 45 40 10 40ZM15 5 40 10 10 20 5 10Z";
    if (result != ok) TEST_FAILED("Expected: " + ok + "\n\tObtained: " + result);
  }

  {
    string result = exportToSvg(*reader.read(multipolygon2));
    string ok = "M40 40 20 45 45 30ZM20 35 10 30 10 10 30 5 45 20ZM30 20 20 15 20 25Z";
    if (result != ok) TEST_FAILED("Expected: " + ok + "\n\tObtained: " + result);
  }

  TEST_PASSED();
}

// We prefer using 'Z' when linestrings are closed

void exportToSvg_closing_paths()
{
  using namespace Fmi;
  using GEOS::exportToSvg;

  geos::io::WKTReader reader(&factory);

  // Ref: http://en.wikipedia.org/wiki/Well-known_text

  string linestring = "LINESTRING (30 10, 10 30, 40 40, 30 10)";
  string multilinestring1 =
      "MULTILINESTRING ((10 10, 20 20, 10 40, 10 10),(40 40, 30 30, 40 20, 30 10))";
  string multilinestring2 =
      "MULTILINESTRING ((10 10, 20 20, 10 40),(40 40, 30 30, 40 20, 30 10, 40 40))";

  {
    string result = exportToSvg(*reader.read(linestring));
    string ok = "M30 10 10 30 40 40Z";
    if (result != ok) TEST_FAILED("Expected: " + ok + "\n\tObtained: " + result);
  }

  {
    string result = exportToSvg(*reader.read(multilinestring1));
    string ok = "M10 10 20 20 10 40ZM40 40 30 30 40 20 30 10";
    if (result != ok) TEST_FAILED("Expected: " + ok + "\n\tObtained: " + result);
  }

  {
    string result = exportToSvg(*reader.read(multilinestring2));
    string ok = "M10 10 20 20 10 40M40 40 30 30 40 20 30 10Z";
    if (result != ok) TEST_FAILED("Expected: " + ok + "\n\tObtained: " + result);
  }

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
    TEST(exportToSvg_wiki_examples);
    TEST(exportToSvg_closing_paths);
  }

};  // class tests

}  // namespace Tests

int main(void)
{
  cout << endl << "GEOS tester" << endl << "===========" << endl;
  Tests::tests t;
  return t.run();
}
