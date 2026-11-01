#ifndef _TERMIFY_CORE_DEFINES_HPP
#define _TERMIFY_CORE_DEFINES_HPP

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iomanip> // for std::quoted
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include <thread>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

// Unsigned aliases
using uint64 = uint64_t;
using uint32 = uint32_t;
using uint16 = uint16_t;
using uint8 = uint8_t;

// Signed aliases
using int64 = int64_t;
using int32 = int32_t;
using int16 = int16_t;
using int8 = int8_t;

// General aliases
using string = std::string;
using atomic_bool = std::atomic<bool>;
using atomic_uint32 = std::atomic<uint32>;
using condition_variable = std::condition_variable;

// Template aliases
template <typename ...Args>
using vector = std::vector<Args...>;

template <typename K, typename ...Args>
using umap = std::unordered_map<K, Args...>;


#endif // _TERMIFY_CORE_DEFINES_HPP

