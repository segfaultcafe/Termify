#include "Platform/Posix/PosixServices.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::cerr << "Check failed at line " << __LINE__ << ": " #condition      \
                << '\n';                                                       \
      return 1;                                                                \
    }                                                                          \
  } while (false)

template <typename Callback> bool throws(Callback callback) {
  try {
    callback();
    return false;
  } catch (const std::runtime_error &) {
    return true;
  }
}

namespace {

class TestFileSystem final : public termify::platform::posix::FileSystem {
public:
  explicit TestFileSystem(std::filesystem::path directory)
      : _directory(std::move(directory)) {}

  std::filesystem::path ExecutableDirectory() const override {
    return _directory;
  }

protected:
  std::vector<std::filesystem::path>
  SystemExecutableCandidates(const std::string &) const override {
    return {};
  }

private:
  std::filesystem::path _directory;
};

void writeFile(const std::filesystem::path &path, std::string_view value) {
  std::ofstream(path, std::ios::binary) << value;
}

std::string readFile(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

} // namespace

int main() {
  const auto root =
      std::filesystem::temp_directory_path() / "termify-posix-filesystem-tests";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);
  TestFileSystem files(root);

  const auto source = root / "source";
  const auto destination = root / "destination";
  writeFile(source, "replacement");
  writeFile(destination, "original");
  files.ReplaceFileAtomically(source, destination, true);
  CHECK(readFile(destination) == "replacement");
  const auto permissions = std::filesystem::status(destination).permissions();
  CHECK((permissions & std::filesystem::perms::owner_exec) !=
        std::filesystem::perms::none);
  CHECK((permissions & std::filesystem::perms::group_exec) ==
        std::filesystem::perms::none);

  const auto link = root / "source-link";
  std::filesystem::create_symlink(source, link);
  CHECK(throws([&] { files.ReplaceFileAtomically(link, destination, true); }));
  CHECK(readFile(destination) == "replacement");

  const auto privateDirectory = files.CreatePrivateDirectory(root, "private");
  CHECK(std::filesystem::is_directory(privateDirectory));
  const auto directoryPermissions =
      std::filesystem::status(privateDirectory).permissions();
  CHECK((directoryPermissions & std::filesystem::perms::owner_all) ==
        std::filesystem::perms::owner_all);
  CHECK((directoryPermissions & std::filesystem::perms::group_all) ==
        std::filesystem::perms::none);
  CHECK((directoryPermissions & std::filesystem::perms::others_all) ==
        std::filesystem::perms::none);

  const auto lockPath = root / "update.lock";
  writeFile(lockPath, "lock");
  const auto firstLock =
      files.TryLock(lockPath, termify::platform::FileLockMode::Exclusive);
  const auto secondLock =
      files.TryLock(lockPath, termify::platform::FileLockMode::Exclusive);
  CHECK(firstLock);
  CHECK(!secondLock);

  std::filesystem::remove_all(root);
  std::cout << "POSIX filesystem tests passed.\n";
  return 0;
}
