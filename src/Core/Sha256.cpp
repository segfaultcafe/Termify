#include "Sha256.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace termify::core {
namespace {

constexpr std::array<std::uint32_t, 64> ROUND_CONSTANTS = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b,
    0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01,
    0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7,
    0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152,
    0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
    0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819,
    0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08,
    0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f,
    0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

class Sha256State {
public:
  void Update(const unsigned char *data, std::size_t size) {
    _totalBytes += size;
    while (size > 0) {
      const auto count = std::min(size, _block.size() - _blockBytes);
      std::copy_n(data, count, _block.data() + _blockBytes);
      data += count;
      size -= count;
      _blockBytes += count;
      if (_blockBytes == _block.size()) {
        Transform();
        _blockBytes = 0;
      }
    }
  }

  std::string Finish() {
    const std::uint64_t bits = _totalBytes * 8;
    _block[_blockBytes++] = 0x80;
    if (_blockBytes > 56) {
      std::fill(_block.begin() + static_cast<std::ptrdiff_t>(_blockBytes),
                _block.end(), 0);
      Transform();
      _blockBytes = 0;
    }
    std::fill(_block.begin() + static_cast<std::ptrdiff_t>(_blockBytes),
              _block.begin() + 56, 0);
    for (std::size_t index = 0; index < 8; ++index)
      _block[63 - index] = static_cast<unsigned char>(bits >> (index * 8));
    Transform();

    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (const auto value : _hash)
      output << std::setw(8) << value;
    return output.str();
  }

private:
  void Transform() {
    std::array<std::uint32_t, 64> words{};
    for (std::size_t index = 0; index < 16; ++index) {
      const auto offset = index * 4;
      words[index] = static_cast<std::uint32_t>(_block[offset]) << 24 |
                     static_cast<std::uint32_t>(_block[offset + 1]) << 16 |
                     static_cast<std::uint32_t>(_block[offset + 2]) << 8 |
                     static_cast<std::uint32_t>(_block[offset + 3]);
    }
    for (std::size_t index = 16; index < words.size(); ++index) {
      const auto first = std::rotr(words[index - 15], 7) ^
                         std::rotr(words[index - 15], 18) ^
                         (words[index - 15] >> 3);
      const auto second = std::rotr(words[index - 2], 17) ^
                          std::rotr(words[index - 2], 19) ^
                          (words[index - 2] >> 10);
      words[index] = words[index - 16] + first + words[index - 7] + second;
    }

    auto a = _hash[0];
    auto b = _hash[1];
    auto c = _hash[2];
    auto d = _hash[3];
    auto e = _hash[4];
    auto f = _hash[5];
    auto g = _hash[6];
    auto h = _hash[7];
    for (std::size_t index = 0; index < words.size(); ++index) {
      const auto sum1 = std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25);
      const auto choice = (e & f) ^ (~e & g);
      const auto temporary1 =
          h + sum1 + choice + ROUND_CONSTANTS[index] + words[index];
      const auto sum0 = std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22);
      const auto majority = (a & b) ^ (a & c) ^ (b & c);
      const auto temporary2 = sum0 + majority;
      h = g;
      g = f;
      f = e;
      e = d + temporary1;
      d = c;
      c = b;
      b = a;
      a = temporary1 + temporary2;
    }
    _hash[0] += a;
    _hash[1] += b;
    _hash[2] += c;
    _hash[3] += d;
    _hash[4] += e;
    _hash[5] += f;
    _hash[6] += g;
    _hash[7] += h;
  }

  std::array<std::uint32_t, 8> _hash = {
      0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
      0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  std::array<unsigned char, 64> _block{};
  std::size_t _blockBytes{0};
  std::uint64_t _totalBytes{0};
};

} // namespace

std::string Sha256(std::string_view value) {
  Sha256State state;
  state.Update(reinterpret_cast<const unsigned char *>(value.data()),
               value.size());
  return state.Finish();
}

std::string Sha256File(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input)
    throw std::runtime_error("Could not read the downloaded update");
  Sha256State state;
  std::array<unsigned char, 64 * 1024> buffer{};
  while (input) {
    input.read(reinterpret_cast<char *>(buffer.data()),
               static_cast<std::streamsize>(buffer.size()));
    state.Update(buffer.data(), static_cast<std::size_t>(input.gcount()));
  }
  if (!input.eof())
    throw std::runtime_error("Could not hash the downloaded update");
  return state.Finish();
}

} // namespace termify::core
