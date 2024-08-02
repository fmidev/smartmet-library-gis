#pragma once
#include <optional>

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
  ProjInfo& operator=(const ProjInfo&) = default;
  explicit ProjInfo(const std::string& theProj);

  ProjInfo(ProjInfo&& other) = default;
  ProjInfo& operator=(ProjInfo&& other) = default;

  const std::string& projStr() const { return itsProjStr; }
  std::optional<double> getDouble(const std::string& theName) const;
  std::optional<std::string> getString(const std::string& theName) const;
  bool getBool(const std::string& theName) const;

  bool erase(const std::string& theName);

  std::string inverseProjStr() const;

  void dump(std::ostream& theOutput) const;

 private:
  std::string itsProjStr;
  std::map<std::string, double> itsDoubles;       // +R=radius etc
  std::map<std::string, std::string> itsStrings;  // +ellps=intl etc
  std::set<std::string> itsOptions;               // +over etc
};

}  // namespace Fmi
