#ifndef TERMIFY_CORE_UPDATE_CHECKER_HPP
#define TERMIFY_CORE_UPDATE_CHECKER_HPP

#include "../Platform/Services.hpp"
#include <compare>
#include <string>
#include <string_view>

namespace termify::core {

struct Version {
  unsigned long long major{0};
  unsigned long long minor{0};
  unsigned long long patch{0};

  static Version Parse(std::string_view value);
  std::string String() const;
  auto operator<=>(const Version &) const = default;
};

enum class UpdateState { UpToDate, Available, DevelopmentBuild };

struct UpdateCheckResult {
  UpdateState state{UpdateState::UpToDate};
  Version current;
  Version latest;
  std::string releaseUrl;
};

class UpdateChecker {
public:
  UpdateChecker(platform::ProcessRunner &processes,
                platform::FileSystem &files);

  UpdateCheckResult Check(std::string_view currentVersion) const;
  static UpdateCheckResult Evaluate(std::string_view currentVersion,
                                    std::string_view latestReleaseUrl);
  static std::string Format(const UpdateCheckResult &result);

private:
  std::string FetchLatestReleaseUrl() const;

  platform::ProcessRunner &_processes;
  platform::FileSystem &_files;
};

} // namespace termify::core

#endif // TERMIFY_CORE_UPDATE_CHECKER_HPP
