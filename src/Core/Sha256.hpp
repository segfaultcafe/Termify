#ifndef TERMIFY_CORE_SHA256_HPP
#define TERMIFY_CORE_SHA256_HPP

#include <filesystem>
#include <string>
#include <string_view>

namespace termify::core {

std::string Sha256(std::string_view value);
std::string Sha256File(const std::filesystem::path &path);

} // namespace termify::core

#endif // TERMIFY_CORE_SHA256_HPP
