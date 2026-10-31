#ifndef TERMIFY_TUI_INPUT_DECODER_HPP
#define TERMIFY_TUI_INPUT_DECODER_HPP

#include "Event.hpp"

#include <deque>
#include <optional>
#include <string>
#include <string_view>

namespace termify::tui {

class InputDecoder {
public:
  void Feed(std::string_view bytes);
  void FlushPending();
  std::optional<Event> Pop();
  bool HasPendingBytes() const;

private:
  void Decode(bool flush);
  void DecodeCsi(std::string_view parameters, char finalByte);
  void PushKey(Key key, std::string text = {}, Modifiers modifiers = {});

  std::string _buffer;
  std::deque<Event> _events;
};

} // namespace termify::tui

#endif // TERMIFY_TUI_INPUT_DECODER_HPP
