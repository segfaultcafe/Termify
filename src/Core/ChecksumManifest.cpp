#include "ChecksumManifest.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>

namespace termify::core {
namespace {

bool isHexDigest(std::string_view value) {
  return value.size() == 64 &&
         std::all_of(value.begin(), value.end(), [](unsigned char character) {
           return std::isxdigit(character) != 0;
         });
}

std::string lowercase(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char character) {
                   return static_cast<char>(std::tolower(character));
                 });
  return value;
}

} // namespace

std::string ChecksumForArtifact(std::string_view manifest,
                                std::string_view artifactName) {
  std::istringstream lines{std::string(manifest)};
  std::string line;
  std::string result;
  while (std::getline(lines, line)) {
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    if (line.size() < 67 || line[64] != ' ' ||
        (line[65] != ' ' && line[65] != '*'))
      throw std::runtime_error("The release checksum manifest is invalid");
    const auto digest = std::string_view(line).substr(0, 64);
    const auto filename = std::string_view(line).substr(66);
    if (!isHexDigest(digest) || filename.empty() ||
        filename.find_first_of("/\\") != std::string_view::npos)
      throw std::runtime_error("The release checksum manifest is invalid");
    if (filename == artifactName) {
      if (!result.empty())
        throw std::runtime_error(
            "The release checksum manifest contains a duplicate artifact");
      result = lowercase(std::string(digest));
    }
  }
  if (result.empty())
    throw std::runtime_error(
        "The release checksum manifest does not contain this update");
  return result;
}

} // namespace termify::core
