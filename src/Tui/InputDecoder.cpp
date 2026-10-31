#include "InputDecoder.hpp"

#include <algorithm>
#include <charconv>
#include <vector>

namespace termify::tui {

namespace {
std::size_t utf8Length(unsigned char lead) {
  if ((lead & 0x80) == 0)
    return 1;
  if ((lead & 0xE0) == 0xC0)
    return 2;
  if ((lead & 0xF0) == 0xE0)
    return 3;
  if ((lead & 0xF8) == 0xF0)
    return 4;
  return 1;
}

std::optional<int> parseNumber(std::string_view value) {
  int result = 0;
  const auto [end, error] =
      std::from_chars(value.data(), value.data() + value.size(), result);
  if (error != std::errc{} || end != value.data() + value.size()) {
    return std::nullopt;
  }
  return result;
}

std::vector<std::string_view> splitParameters(std::string_view value) {
  std::vector<std::string_view> result;
  std::size_t start = 0;
  while (start <= value.size()) {
    const auto separator = value.find(';', start);
    result.push_back(value.substr(start, separator - start));
    if (separator == std::string_view::npos)
      break;
    start = separator + 1;
  }
  return result;
}

Modifiers decodeModifiers(int encoded) {
  const int bits = std::max(0, encoded - 1);
  return {
      .shift = (bits & 1) != 0,
      .alt = (bits & 2) != 0,
      .control = (bits & 4) != 0,
  };
}
} // namespace

void InputDecoder::Feed(std::string_view bytes) {
  _buffer.append(bytes);
  Decode(false);
}

void InputDecoder::FlushPending() { Decode(true); }

std::optional<Event> InputDecoder::Pop() {
  if (_events.empty()) {
    return std::nullopt;
  }
  Event event = std::move(_events.front());
  _events.pop_front();
  return event;
}

bool InputDecoder::HasPendingBytes() const { return !_buffer.empty(); }

void InputDecoder::PushKey(Key key, std::string text, Modifiers modifiers) {
  _events.emplace_back(KeyEvent{key, std::move(text), modifiers});
}

void InputDecoder::Decode(bool flush) {
  while (!_buffer.empty()) {
    const auto first = static_cast<unsigned char>(_buffer.front());

    if (first == 0x1B) {
      if (_buffer.size() == 1) {
        if (flush) {
          PushKey(Key::Escape);
          _buffer.erase(0, 1);
        }
        return;
      }
      if (_buffer[1] == 'O') {
        if (_buffer.size() < 3) {
          if (flush) {
            PushKey(Key::Escape);
            _buffer.erase(0, 1);
            continue;
          }
          return;
        }
        switch (_buffer[2]) {
        case 'A':
          PushKey(Key::ArrowUp);
          break;
        case 'B':
          PushKey(Key::ArrowDown);
          break;
        case 'C':
          PushKey(Key::ArrowRight);
          break;
        case 'D':
          PushKey(Key::ArrowLeft);
          break;
        case 'H':
          PushKey(Key::Home);
          break;
        case 'F':
          PushKey(Key::End);
          break;
        default:
          PushKey(Key::Unknown, _buffer.substr(0, 3));
          break;
        }
        _buffer.erase(0, 3);
        continue;
      }
      if (_buffer[1] != '[') {
        const auto lead = static_cast<unsigned char>(_buffer[1]);
        const std::size_t characterBytes = utf8Length(lead);
        if (_buffer.size() < characterBytes + 1 && !flush)
          return;
        const std::size_t available =
            std::min(characterBytes, _buffer.size() - 1);
        if (lead >= 32 || characterBytes > 1) {
          PushKey(Key::Character, _buffer.substr(1, available), {.alt = true});
          _buffer.erase(0, available + 1);
        } else {
          PushKey(Key::Escape);
          _buffer.erase(0, 1);
        }
        continue;
      }

      std::size_t finalIndex = 2;
      while (finalIndex < _buffer.size()) {
        const unsigned char candidate =
            static_cast<unsigned char>(_buffer[finalIndex]);
        if (candidate >= 0x40 && candidate <= 0x7E)
          break;
        ++finalIndex;
      }
      if (finalIndex == _buffer.size()) {
        if (flush) {
          PushKey(Key::Escape);
          _buffer.erase(0, 1);
          continue;
        }
        return;
      }

      DecodeCsi(std::string_view(_buffer).substr(2, finalIndex - 2),
                _buffer[finalIndex]);
      _buffer.erase(0, finalIndex + 1);
      continue;
    }

    switch (first) {
    case '\r':
    case '\n':
      PushKey(Key::Enter);
      _buffer.erase(0, 1);
      continue;
    case '\t':
      PushKey(Key::Tab);
      _buffer.erase(0, 1);
      continue;
    case 0x03:
      PushKey(Key::Interrupt, {}, {.control = true});
      _buffer.erase(0, 1);
      continue;
    case 0x04:
      PushKey(Key::EndOfInput, {}, {.control = true});
      _buffer.erase(0, 1);
      continue;
    case 0x08:
    case 0x7F:
      PushKey(Key::Backspace);
      _buffer.erase(0, 1);
      continue;
    default:
      break;
    }

    const std::size_t characterBytes = utf8Length(first);
    if (_buffer.size() < characterBytes && !flush) {
      return;
    }
    const std::size_t available = std::min(characterBytes, _buffer.size());
    if (first >= 32 || characterBytes > 1) {
      PushKey(Key::Character, _buffer.substr(0, available));
    } else {
      PushKey(Key::Unknown, _buffer.substr(0, available));
    }
    _buffer.erase(0, available);
  }
}

void InputDecoder::DecodeCsi(std::string_view parameters, char finalByte) {
  if (finalByte == 'Z') {
    PushKey(Key::BackTab, {}, {.shift = true});
    return;
  }

  if (!parameters.empty() && parameters.front() == '<' &&
      (finalByte == 'M' || finalByte == 'm')) {
    const auto values = splitParameters(parameters.substr(1));
    if (values.size() != 3)
      return;
    const auto encoded = parseNumber(values[0]);
    const auto column = parseNumber(values[1]);
    const auto row = parseNumber(values[2]);
    if (!encoded || !column || !row)
      return;

    MouseEvent event;
    event.column = *column;
    event.row = *row;
    event.modifiers = {
        .shift = (*encoded & 4) != 0,
        .alt = (*encoded & 8) != 0,
        .control = (*encoded & 16) != 0,
    };
    const int button = *encoded & 0xC3;
    if (button == 64) {
      event.button = MouseButton::WheelUp;
      event.action = MouseAction::Scroll;
    } else if (button == 65) {
      event.button = MouseButton::WheelDown;
      event.action = MouseAction::Scroll;
    } else {
      event.button = button == 0   ? MouseButton::Left
                     : button == 1 ? MouseButton::Middle
                     : button == 2 ? MouseButton::Right
                                   : MouseButton::None;
      event.action = finalByte == 'm' ? MouseAction::Release
                     : (*encoded & 32) != 0 ? MouseAction::Move
                                            : MouseAction::Press;
    }
    _events.emplace_back(event);
    return;
  }

  const auto values = splitParameters(parameters);
  Modifiers modifiers;
  if (values.size() >= 2) {
    if (const auto encoded = parseNumber(values.back())) {
      modifiers = decodeModifiers(*encoded);
    }
  }

  switch (finalByte) {
  case 'A':
    PushKey(Key::ArrowUp, {}, modifiers);
    return;
  case 'B':
    PushKey(Key::ArrowDown, {}, modifiers);
    return;
  case 'C':
    PushKey(Key::ArrowRight, {}, modifiers);
    return;
  case 'D':
    PushKey(Key::ArrowLeft, {}, modifiers);
    return;
  case 'H':
    PushKey(Key::Home, {}, modifiers);
    return;
  case 'F':
    PushKey(Key::End, {}, modifiers);
    return;
  case '~':
    break;
  default:
    PushKey(Key::Unknown);
    return;
  }

  if (values.empty()) {
    PushKey(Key::Unknown);
    return;
  }
  const auto number = parseNumber(values.front());
  if (!number) {
    PushKey(Key::Unknown);
  } else if (*number == 1 || *number == 7) {
    PushKey(Key::Home, {}, modifiers);
  } else if (*number == 3) {
    PushKey(Key::Delete, {}, modifiers);
  } else if (*number == 4 || *number == 8) {
    PushKey(Key::End, {}, modifiers);
  } else if (*number == 5) {
    PushKey(Key::PageUp, {}, modifiers);
  } else if (*number == 6) {
    PushKey(Key::PageDown, {}, modifiers);
  } else {
    PushKey(Key::Unknown, {}, modifiers);
  }
}

} // namespace termify::tui
