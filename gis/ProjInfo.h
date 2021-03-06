#pragma once
#include <boost/optional.hpp>

#include <map>
#include <set>
#include <string>

namespace Fmi
{
class ProjInfo
{
 public:
  ProjInfo() = default;
  ProjInfo(const ProjInfo&) = default;
  ProjInfo(const std::string& theProj);

  const std::string& projStr() const { return itsProjStr; }
  boost::optional<double> getDouble(const std::string& theName) const;
  boost::optional<std::string> getString(const std::string& theName) const;
  bool getBool(const std::string& theName) const;

  std::string inverseProjStr() const;

  void dump(std::ostream& theOutput) const;

 private:
  std::string itsProjStr;
  std::map<std::string, double> itsDoubles;       // +R=radius etc
  std::map<std::string, std::string> itsStrings;  // +ellps=intl etc
  std::set<std::string> itsOptions;               // +over etc
};

}  // namespace Fmi
