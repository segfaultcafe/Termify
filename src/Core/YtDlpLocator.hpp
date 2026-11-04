#ifndef TERMIFY_CORE_YT_DLP_LOCATOR_HPP
#define TERMIFY_CORE_YT_DLP_LOCATOR_HPP

#include "../Platform/Services.hpp"
#include <filesystem>

namespace termify::core {

std::filesystem::path ManagedYtDlpPath(platform::FileSystem &files);

std::filesystem::path
FindYtDlpExecutable(platform::FileSystem &files,
                    const platform::UpdateEnvironment &environment);

} // namespace termify::core

#endif // TERMIFY_CORE_YT_DLP_LOCATOR_HPP
