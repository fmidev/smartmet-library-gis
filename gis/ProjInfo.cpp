#include "ProjInfo.h"

#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <fmt/format.h>

#include <string>
#include <vector>

namespace Fmi
{
namespace
{
// Created only once for better inverseProjStr() speed.

// And keep only the parameters relevant to it. Note that +init is not here,
// it is expanded to +datum and other options prior to this stage. Note that pm
// can be both a double and a city name

const std::set<std::string> g_str_keepers{
    "proj", "datum", "ellps", "towgs84", "o_proj", "pm", "axis"};

const std::set<std::string> g_num_keepers{
    "to_meter", "o_lon_p", "o_lat_p", "lon_0", "lon_wrap", "R", "a", "b", "k", "k_0", "pm", "f"};

const std::set<std::string> g_opt_keepers{"over", "no_defs", "wktext"};

const std::set<std::string> g_ints{"R", "a", "b"};  // one meter accuracy is enough for these
}  // namespace

// ----------------------------------------------------------------------
/*!
 * \brief Parse PROJ.4 settings
 *
 * Sample input: +to_meter=.0174532925199433 +proj=ob_tran +o_proj=eqc
 *               +o_lon_p=0 +o_lat_p=30 +R=6371220
 *               +wktext +over +towgs84=0,0,0 +no_defs
 */
// ----------------------------------------------------------------------

ProjInfo::ProjInfo(const std::string& theProj) : itsProjStr(theProj)
{
  // Split options by whitespace
  auto proj = boost::algorithm::trim_copy(theProj);
  std::vector<std::string> options;
  boost::algorithm::split(
      options, proj, boost::algorithm::is_any_of(" "), boost::token_compress_on);

  // Process the options one by one

  for (const auto& option : options)
  {
    if (option.empty()) continue;  // safety check

    if (option[0] != '+')
      throw std::runtime_error("Only PROJ options starting with '+' are allowed");

    std::vector<std::string> parts;
    boost::algorithm::split(parts, option, boost::algorithm::is_any_of("="));

    // Extract option name
    auto name = parts[0].substr(1, std::string::npos);

    if (parts.size() == 1)
      itsOptions.insert(name);
    else if (parts.size() == 2)
    {
      auto string_value = parts[1];

      // Store value as double or string

      try
      {
        std::size_t pos = 0;
        auto value = std::stod(string_value, &pos);
        if (pos == string_value.size())
          itsDoubles[name] = value;
        else
          itsStrings[name] = string_value;
      }
      catch (...)
      {
        itsStrings[name] = string_value;
      }
    }
    else
      throw std::runtime_error("PROJ option '" + option + "' contains too many '=' characters");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Return given PROJ.4 setting
 */
// ----------------------------------------------------------------------

boost::optional<double> ProjInfo::getDouble(const std::string& theName) const
{
  auto pos = itsDoubles.find(theName);
  if (pos == itsDoubles.end()) return {};
  return pos->second;
}

// ----------------------------------------------------------------------
/*!
 * \brief Return given PROJ.4 setting
 */
// ----------------------------------------------------------------------

boost::optional<std::string> ProjInfo::getString(const std::string& theName) const
{
  auto pos = itsStrings.find(theName);
  if (pos == itsStrings.end()) return {};
  return pos->second;
}

// ----------------------------------------------------------------------
/*!
 * \brief Return given PROJ.4 setting
 */
// ----------------------------------------------------------------------

bool ProjInfo::getBool(const std::string& theName) const
{
  auto pos = itsOptions.find(theName);
  return (pos != itsOptions.end());
}

//----------------------------------------------------------------------
/*!
 * \brief Dump the settings mostly for debugging purposes

 */
// ----------------------------------------------------------------------

void ProjInfo::dump(std::ostream& theOutput) const
{
  for (const auto& name_double : itsDoubles)
    theOutput << '+' << name_double.first << " = " << name_double.second << "\n";

  for (const auto& name_string : itsStrings)
    theOutput << '+' << name_string.first << " = \"" << name_string.second << "\"\n";

  for (const auto& name : itsOptions)
    theOutput << '+' << name << "\n";
}

// ----------------------------------------------------------------------
/*!
 * \brief Return inverse projection string to native geodetic coordinates
 */
// ----------------------------------------------------------------------

std::string ProjInfo::inverseProjStr() const
{
  std::string ret;
  ret.reserve(120);  // should cover most projections

  for (const auto& name_value : itsStrings)
  {
    if (g_str_keepers.find(name_value.first) == g_str_keepers.end()) continue;

    if (!ret.empty()) ret += ' ';

    if (name_value.first == "proj")
      ret += "+proj=longlat";
    else
    {
      ret += '+';
      ret += name_value.first;
      ret += '=';
      ret += name_value.second;
    }
  }

  for (const auto& name_value : itsDoubles)
  {
    if (g_num_keepers.find(name_value.first) == g_num_keepers.end()) continue;
    if (!ret.empty()) ret += ' ';
    ret += '+';
    ret += name_value.first;
    ret += '=';

    if (g_ints.find(name_value.first) == g_ints.end())
      ret += fmt::format("{}", name_value.second);
    else
      ret += fmt::format("{:.0f}", name_value.second);
  }

  for (const auto& name : itsOptions)
  {
    if (g_opt_keepers.find(name) == g_opt_keepers.end()) continue;
    if (!ret.empty()) ret += ' ';
    ret += '+';
    ret += name;
  }

  return ret;
}

}  // namespace Fmi
