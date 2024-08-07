#include "ProjInfo.h"

#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <fmt/format.h>
#include <macgyver/Exception.h>
#include <macgyver/StringConversion.h>
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
    "type", "proj", "datum", "ellps", "towgs84", "pm", "axis"};

// Note: lon_0 must be stripped in newer PROJ versions

const std::set<std::string> g_num_keepers{
    "to_meter", "o_lon_p", "o_lat_p", "lon_wrap", "R", "a", "b", "k", "k_0", "pm", "f"};

const std::set<std::string> g_opt_keepers{"over", "no_defs", "wktext"};

const std::set<std::string> g_ints{"R", "a", "b"};  // one meter accuracy is enough for these
}  // namespace

// ----------------------------------------------------------------------
/*!
 * \brief Parse a PROJ.4 setting to a double if possible
 */
// ----------------------------------------------------------------------

std::optional<double> parse_proj_number(const std::string& value)
{
  try
  {
    if (value.empty())
      throw Fmi::Exception::Trace(BCP, "Empty PROJ options not allowed");

    // Try parsing as a number
    auto opt_value = Fmi::stod_opt(value);
    if (opt_value)
      return opt_value;

    // Try removing possible s/n/w/e suffix

    auto suffix = value.back();
    auto prefix = value.substr(0, value.size() - 1);
    if (suffix == 'E' || suffix == 'e')
    {
      opt_value = Fmi::stod_opt(prefix);
      if (opt_value)
        return opt_value;
    }
    else if (suffix == 'W' || suffix == 'w')
    {
      opt_value = Fmi::stod_opt(prefix);
      if (opt_value)
        return -(*opt_value);
    }
    else if (suffix == 'N' || suffix == 'n')
    {
      opt_value = Fmi::stod_opt(prefix);
      if (opt_value)
        return *opt_value;
    }
    else if (suffix == 'S' || suffix == 's')
    {
      opt_value = Fmi::stod_opt(prefix);
      if (opt_value)
        return -(*opt_value);
    }
    return {};
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

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
  try
  {
    // Split options by whitespace
    auto proj = boost::algorithm::trim_copy(theProj);
    std::vector<std::string> options;
    boost::algorithm::split(
        options, proj, boost::algorithm::is_any_of(" "), boost::token_compress_on);

    // Process the options one by one

    for (const auto& option : options)
    {
      if (option.empty())
        continue;  // safety check

      if (option[0] != '+')
        throw Fmi::Exception::Trace(BCP, "Only PROJ options starting with '+' are allowed");

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

        auto opt_value = parse_proj_number(string_value);
        if (opt_value)
          itsDoubles[name] = *opt_value;
        else
          itsStrings[name] = string_value;
      }
      else
        throw Fmi::Exception::Trace(
            BCP, "PROJ option '" + option + "' contains too many '=' characters");
    }
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Failed to parse PROJ string").addParameter("PROJ", theProj);
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Return given PROJ.4 setting
 */
// ----------------------------------------------------------------------

std::optional<double> ProjInfo::getDouble(const std::string& theName) const
{
  try
  {
    auto pos = itsDoubles.find(theName);
    if (pos == itsDoubles.end())
      return {};
    return pos->second;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Failed to get PROJ double")
        .addParameter("Parameter", theName);
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Return given PROJ.4 setting
 */
// ----------------------------------------------------------------------

std::optional<std::string> ProjInfo::getString(const std::string& theName) const
{
  try
  {
    auto pos = itsStrings.find(theName);
    if (pos == itsStrings.end())
      return {};
    return pos->second;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Failed to get PROJ value").addParameter("Parameter", theName);
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Return given PROJ.4 setting
 */
// ----------------------------------------------------------------------

bool ProjInfo::getBool(const std::string& theName) const
{
  try
  {
    auto pos = itsOptions.find(theName);
    return (pos != itsOptions.end());
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Failed to get PROJ boolean value")
        .addParameter("Parameter", theName);
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Remove a setting if one exists
 */
// ----------------------------------------------------------------------

bool ProjInfo::erase(const std::string& theName)
{
  auto count = itsDoubles.erase(theName) + itsStrings.erase(theName) + itsOptions.erase(theName);

  if (count > 0)
  {
    std::string proj;
    for (const auto& name_string : itsStrings)
    {
      if (!proj.empty())
        proj += ' ';
      proj += fmt::format("+{}={}", name_string.first, name_string.second);
    }

    for (const auto& name_double : itsDoubles)
    {
      if (!proj.empty())
        proj += ' ';
      proj += fmt::format("+{}={}", name_double.first, name_double.second);
    }

    for (const auto& name : itsOptions)
    {
      if (!proj.empty())
        proj += ' ';
      proj += '+';
      proj += name;
    }

    itsProjStr = proj;
  }

  return (count > 0);
}

//----------------------------------------------------------------------
/*!
 * \brief Dump the settings mostly for debugging purposes

 */
// ----------------------------------------------------------------------

void ProjInfo::dump(std::ostream& theOutput) const
{
  try
  {
    for (const auto& name_double : itsDoubles)
      theOutput << '+' << name_double.first << " = " << name_double.second << "\n";

    for (const auto& name_string : itsStrings)
      theOutput << '+' << name_string.first << " = \"" << name_string.second << "\"\n";

    for (const auto& name : itsOptions)
      theOutput << '+' << name << "\n";
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Operation failed!");
  }
}

// ----------------------------------------------------------------------
/*!
 * \brief Return inverse projection string to native geodetic coordinates
 */
// ----------------------------------------------------------------------

std::string ProjInfo::inverseProjStr() const
{
  try
  {
    std::string ret;
    ret.reserve(120);  // should cover most projections

    for (const auto& name_value : itsStrings)
    {
      if (g_str_keepers.find(name_value.first) == g_str_keepers.end())
        continue;

      if (!ret.empty())
        ret += ' ';

      if (name_value.first == "proj")
      {
        if (name_value.second != "ob_tran")
          ret += "+proj=longlat";
        else
          ret += "+proj=ob_tran +o_proj=longlat";
      }
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
      if (g_num_keepers.find(name_value.first) == g_num_keepers.end())
        continue;
      if (!ret.empty())
        ret += ' ';
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
      if (g_opt_keepers.find(name) == g_opt_keepers.end())
        continue;
      if (!ret.empty())
        ret += ' ';
      ret += '+';
      ret += name;
    }

    return ret;
  }
  catch (...)
  {
    throw Fmi::Exception::Trace(BCP, "Creating inverse PROJ definition failed");
  }
}

}  // namespace Fmi
