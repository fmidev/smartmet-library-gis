#include "Box.h"
#include "CoordinateTransformation.h"
#include "OGR.h"
#include "Shape_circle.h"
#include "Shape_rect.h"
#include "Shape_sphere.h"
#include "SpatialReference.h"
#include "TestDefs.h"
#include <regression/tframe.h>
#include <memory>
#include <ogr_geometry.h>

using namespace std;

const int precision = 2;

namespace Tests
{
// ----------------------------------------------------------------------

std::string exportToWkt(const OGRGeometry &geom, int precision)
{
  if (precision >= 0)
    return Fmi::OGR::exportToWkt(geom, precision);
  return Fmi::OGR::exportToWkt(geom);
}

char *toUpperString(char *str)
{
  char *p = str;
  while (*p != '\0')
  {
    *p = toupper(*p);
    p++;
  }
  return str;
}

void splitString(const char *str, char separator, std::vector<std::string> &partList)
{
  char buf[10000];
  uint c = 0;
  char *p = const_cast<char *>(str);

  bool ind = false;
  while (*p != '\0' && *p != '\n' && c < 10000)
  {
    if (*p == '"')
      ind = !ind;

    if (*p == separator && !ind)
    {
      buf[c] = '\0';
      partList.emplace_back(std::string(buf));
      c = 0;
    }
    else
    {
      buf[c] = *p;
      c++;
    }
    p++;
  }
  if (c > 0)
  {
    buf[c] = '\0';
    partList.emplace_back(std::string(buf));
  }
}

// Test driver
class tests : public tframe::tests
{
 public:
  std::string filename;

  void ogr_tests()
  {
    using namespace Fmi;

    cout << "---------------------------------------------------------\n";
    cout << "TEST FILE : " << filename << "\n";
    cout << "---------------------------------------------------------\n";
    FILE *file = fopen(filename.c_str(), "r");
    if (file == nullptr)
    {
      out << "*** FAILED ***\n";
      out << "\tFile     : " << filename << "\n";
      out << "\tReason   : "
          << "Cannot open the test file\n";
      fail++;
      return;
    }

    char st[10000];
    std::string testId;
    std::string functionality;
    std::string shapeStr;
    std::vector<std::string> sParams;
    std::string inWkt;
    std::string outWkt;

    uint flags = 0;
    uint line = 0;
    uint lineCnt = 0;
    int precision = -1;

    while (!feof(file))
    {
      if (fgets(st, 10000, file) != nullptr)
      {
        lineCnt++;
        char *p = strstr(st, "\n");
        if (p != nullptr)
          *p = '\0';

        // printf("%s\n",st);
        switch (st[0])
        {
          case 'T':
            line = lineCnt;
            testId = st + 2;
            sParams.clear();
            precision = -1;
            flags = flags | 1;
            break;

          case 'F':
            functionality = toUpperString(st + 2);
            flags = flags | 2;
            break;

          case 'S':
            shapeStr = st + 2;
            splitString(toUpperString(st + 2), ',', sParams);
            flags = flags | 4;
            break;

          case 'I':
            inWkt = st + 2;
            flags = flags | 8;
            break;

          case 'O':
            outWkt = st + 2;
            flags = flags | 16;
            break;
        }

        if (flags == 0x1F)
        {
          flags = 0;
          std::shared_ptr<Fmi::Shape> shape;

          uint sz = sParams.size();
          if (sz > 0)
          {
            if (sParams[0] == "RECT")
            {
              if (sz != 5 && sz != 6)
              {
                out << "Test " << testId << " : ";
                out << "*** FAILED ***\n";
                out << "\tFile     : " << filename << " (" << line << ")\n";
                out << "\tReason   : "
                    << "Invalid number of parameters for Shape_rect!\n";
                out << "\tParams   : " << sz << "\n";
                out << "\tShape    : " << shapeStr << "\n";
                fail++;
              }
              else
              {
                shape.reset(new Fmi::Shape_rect(atof(sParams[1].c_str()),
                                                atof(sParams[2].c_str()),
                                                atof(sParams[3].c_str()),
                                                atof(sParams[4].c_str())));
                if (sz == 6)
                  precision = atoi(sParams[5].c_str());
              }
            }
            else if (sParams[0] == "CIRCLE")
            {
              if (sz != 4 && sz != 5)
              {
                out << "Test " << testId << " : ";
                out << "*** FAILED ***\n";
                out << "\tFile     : " << filename << " (" << line << ")\n";
                out << "\tReason   : "
                    << "Invalid number of parameters for Shape_circle!\n";
                out << "\tShape    : " << shapeStr << "\n";
                fail++;
              }
              else
              {
                shape.reset(new Fmi::Shape_circle(
                    atof(sParams[1].c_str()), atof(sParams[2].c_str()), atof(sParams[3].c_str())));
                if (sz == 5)
                  precision = atoi(sParams[4].c_str());
              }
            }
            else if (sParams[0] == "SPHERE")
            {
              if (sz != 4 && sz != 5)
              {
                out << "Test " << testId << " : ";
                out << "*** FAILED ***\n";
                out << "\tFile     : " << filename << " (" << line << ")\n";
                out << "\tReason   : "
                    << "Invalid number of parameters for Shape_circle!\n";
                out << "\tShape    : " << shapeStr << "\n";
                fail++;
              }
              else
              {
                shape.reset(new Fmi::Shape_sphere(
                    atof(sParams[1].c_str()), atof(sParams[2].c_str()), atof(sParams[3].c_str())));
                if (sz == 5)
                  precision = atoi(sParams[4].c_str());
              }
            }
            else
            {
              out << "Test " << testId << " : ";
              out << "*** FAILED ***\n";
              out << "\tFile     : " << filename << " (" << line << ")\n";
              out << "\tReason   : "
                  << "Shape not defined!\n";
              fail++;
            }
          }

          if (shape)
          {
            OGRGeometry *input = nullptr;
            OGRGeometry *output = nullptr;

            try
            {
              auto err = OGRGeometryFactory::createFromWkt(inWkt.c_str(), nullptr, &input);
              if (err != OGRERR_NONE)
              {
                fail++;
                out << "Test " << testId << " : ";
                out << "*** FAILED ***\n";
                out << "\tFile     : " << filename << " (" << line << ")\n";
                out << "\tReason   : Failed to parse input\n";
                out << "\tInput    : " << inWkt << "\n";
              }

              if (!input->IsValid())
              {
                fail++;
                out << "Test " << testId << " : ";
                out << "*** FAILED ***\n";
                out << "\tFile     : " << filename << " (" << line << ")\n";
                out << "\tReason   : Input is not a valid geometry\n";
                out << "\tInput    : " << inWkt << "\n";

                OGRGeometry *valid = input->MakeValid();
                if (valid != nullptr)
                {
                  out << "\tCorrect  : " << OGR::exportToWkt(*valid, precision) << "\n";
                  OGRGeometryFactory::destroyGeometry(valid);
                }
                else
                  out << "\tCorrect   : (MakeValid failed)\n";

                OGRGeometryFactory::destroyGeometry(input);
                input = nullptr;
              }
            }
            catch (...)
            {
              fail++;
              out << "Test " << testId << " : ";
              out << "*** FAILED ***\n";
              out << "\tFile     : " << filename << " (" << line << ")\n";
              out << "\tReason   : "
                  << "Failed to parse input\n";
              out << "\tInput    : " << inWkt << "\n";
            }

            if (input)
            {
              if (functionality == "LINECUT")
              {
                output = OGR::linecut(*input, shape);
              }
              else if (functionality == "LINECLIP")
              {
                output = OGR::lineclip(*input, shape);
              }
              else if (functionality == "POLYCUT")
              {
                output = OGR::polycut(*input, shape);
              }
              else if (functionality == "POLYCLIP")
              {
                output = OGR::polyclip(*input, shape);
              }
              else
              {
                fail++;
                out << "Test " << testId << " : ";
                out << "*** FAILED ***\n";
                out << "\tFile     : " << filename << " (" << line << ")\n";
                out << "\tReason   : Unknown functionality (" << functionality << ")\n";
              }

              OGRGeometryFactory::destroyGeometry(input);

              string ret;
              if (output)
              {
                bool isvalid = output->IsValid();

                ret = exportToWkt(*output, precision);

                if (ret != outWkt || !isvalid)
                {
                  fail++;
                  out << "Test " << testId << " : ";
                  out << "*** FAILED ***\n";
                  out << "\tFile     : " << filename << " (" << line << ")\n";
                  out << "\tInput    : " << inWkt << "\n";
                  out << "\tExpected : " << outWkt << "\n";
                  out << "\tGot      : " << ret << "\n";
                  if (!isvalid)
                  {
                    out << "\tReason   : output is not a valid geometry\n";
                    auto *valid = output->MakeValid();
                    if (valid != nullptr)
                    {
                      out << "\tCorrect   : " << OGR::exportToWkt(*valid, precision) << "\n";
                      OGRGeometryFactory::destroyGeometry(valid);
                    }
                    else
                      out << "\tCorrect   : (MakeValid failed)\n";
                  }
                }
                else
                {
                  // out << "Test " << testId << " : Ok \n";
                  pass++;
                }

                OGRGeometryFactory::destroyGeometry(output);
              }
            }
          }
        }
      }
    }
    fclose(file);
  }

  // Overridden message separator
  virtual const char *error_message_prefix() const { return "\n\t"; }

  // Main test suite
  void test() { ogr_tests(); }
};  // namespace Tests

}  // namespace Tests

int main(int argc, char *argv[])
{
  if (argc == 1)
  {
    std::cout << "USAGE: ShapeTester <testFile1> [ <testFile2> ... <testFileN>]\n";
    return -1;
  }

  int totalRes = 0;

  cout << endl << "Shape tester" << endl << "============" << endl;
  for (int t = 1; t < argc; t++)
  {
    Tests::tests test;
    test.filename = argv[t];
    int res = test.run();
    if (res != 0)
      totalRes = res;
  }

  return totalRes;
}
