#ifndef TERMIFY_CORE_CHECKSUM_MANIFEST_HPP
#define TERMIFY_CORE_CHECKSUM_MANIFEST_HPP

#include <string>
#include <string_view>

namespace termify::core {

std::string ChecksumForArtifact(std::string_view manifest,
                                std::string_view artifactName);

} // namespace termify::core

#endif // TERMIFY_CORE_CHECKSUM_MANIFEST_HPP
