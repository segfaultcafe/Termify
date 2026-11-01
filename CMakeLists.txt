cmake_minimum_required(VERSION 3.16)
project(termify VERSION 0.6.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)
find_package(SQLite3 REQUIRED)
if(TARGET SQLite3::SQLite3)
  set(TERMIFY_SQLITE_TARGET SQLite3::SQLite3)
else()
  set(TERMIFY_SQLITE_TARGET SQLite::SQLite3)
endif()

# FFmpeg modules
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
  libavformat libavcodec libavutil libswresample)

# ALSA
pkg_check_modules(ALSA REQUIRED alsa)

# Collect source files
file(GLOB_RECURSE SOURCES src/*.cpp)

# Define executable
add_executable(termify ${SOURCES})
target_compile_options(termify PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(termify PRIVATE TERMIFY_VERSION="${PROJECT_VERSION}")

# Include dirs
target_include_directories(termify PRIVATE
  ${FFMPEG_INCLUDE_DIRS}
  ${ALSA_INCLUDE_DIRS}
)

# Link libraries
target_link_libraries(termify
  PkgConfig::FFMPEG
  ${TERMIFY_SQLITE_TARGET}
  ${ALSA_LIBRARIES}
)

include(GNUInstallDirs)
install(TARGETS termify RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

include(CTest)
if(BUILD_TESTING)
  add_executable(song_queue_tests
    tests/SongQueueTests.cpp
    src/Core/SongQueue.cpp
  )
  target_include_directories(song_queue_tests PRIVATE src/Core)
  target_compile_options(song_queue_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME song_queue COMMAND song_queue_tests)

  add_executable(tui_foundation_tests
    tests/TuiFoundationTests.cpp
    src/Tui/Canvas.cpp
    src/Tui/InputDecoder.cpp
    src/Tui/Widget.cpp
    src/Tui/Widgets.cpp
  )
  target_include_directories(tui_foundation_tests PRIVATE src)
  target_compile_options(tui_foundation_tests PRIVATE
    -Wall -Wextra -Wpedantic)
  add_test(NAME tui_foundation COMMAND tui_foundation_tests)

  add_executable(local_store_tests tests/LocalStoreTests.cpp
    src/Core/LocalStore.cpp src/Core/SourceHash.cpp)
  target_include_directories(local_store_tests PRIVATE src)
  target_link_libraries(local_store_tests PRIVATE ${TERMIFY_SQLITE_TARGET})
  target_compile_options(local_store_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME local_store COMMAND local_store_tests)

  add_executable(navigation_tests tests/NavigationTests.cpp src/Tui/Navigation.cpp
    src/Tui/Canvas.cpp src/Tui/Widget.cpp src/Tui/Widgets.cpp)
  target_include_directories(navigation_tests PRIVATE src)
  target_compile_options(navigation_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME navigation COMMAND navigation_tests)

  add_executable(download_cache_tests tests/DownloadCacheTests.cpp
    src/Core/AudioDownloader.cpp src/Core/LocalStore.cpp
    src/Core/SourceHash.cpp)
  target_include_directories(download_cache_tests PRIVATE src)
  target_link_libraries(download_cache_tests PRIVATE ${TERMIFY_SQLITE_TARGET} PkgConfig::FFMPEG)
  target_compile_options(download_cache_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME download_cache COMMAND download_cache_tests)

  add_executable(mixer_order_tests tests/MixerOrderTests.cpp src/Core/Mixer.cpp
    src/Core/Cli.cpp src/Core/SongQueue.cpp src/Core/AudioDownloader.cpp
    src/Core/LocalStore.cpp src/Core/SourceHash.cpp src/Core/FFmpegDecoder.cpp
    src/Core/RingBuffer.cpp)
  target_include_directories(mixer_order_tests PRIVATE src)
  target_link_libraries(mixer_order_tests PRIVATE ${TERMIFY_SQLITE_TARGET}
    PkgConfig::FFMPEG ${ALSA_LIBRARIES})
  target_compile_options(mixer_order_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME mixer_order COMMAND mixer_order_tests)
endif()
