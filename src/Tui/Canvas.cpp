#include "Canvas.hpp"

#include <algorithm>

namespace termify::tui {

namespace {
std::size_t glyphBytes(unsigned char lead) {
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
} // namespace

bool Rect::Contains(Point point) const {
  return point.column >= column && point.row >= row &&
         point.column < column + width && point.row < row + height;
}

Point Rect::Center() const {
  return {column + width / 2, row + height / 2};
}

Canvas::Canvas(int width, int height)
    : _width(std::max(1, width)), _height(std::max(1, height)),
      _cells(static_cast<std::size_t>(_width * _height)) {}

int Canvas::Width() const { return _width; }
int Canvas::Height() const { return _height; }

void Canvas::Clear() {
  std::fill(_cells.begin(), _cells.end(), Cell{});
}

void Canvas::Put(Point point, std::string glyph, Style style) {
  if (point.column < 0 || point.row < 0 || point.column >= _width ||
      point.row >= _height) {
    return;
  }
  _cells[static_cast<std::size_t>(point.row * _width + point.column)] =
      {std::move(glyph), style};
}

int Canvas::Text(Point point, std::string_view text, Style style,
                 int maxWidth) {
  const auto glyphs = SplitGlyphs(text);
  const int available = std::max(0, _width - point.column);
  const int limit = maxWidth < 0 ? available : std::min(maxWidth, available);
  int written = 0;
  for (const auto &glyph : glyphs) {
    if (written >= limit)
      break;
    Put({point.column + written, point.row}, glyph, style);
    ++written;
  }
  return written;
}

int Canvas::Spans(Point point, const std::vector<Span> &spans, int maxWidth) {
  const int available = maxWidth < 0 ? _width - point.column : maxWidth;
  int written = 0;
  for (const auto &span : spans) {
    if (written >= available)
      break;
    written += Text({point.column + written, point.row}, span.text, span.style,
                    available - written);
  }
  return written;
}

void Canvas::HorizontalLine(Point point, int width, std::string glyph,
                            Style style) {
  for (int offset = 0; offset < width; ++offset) {
    Put({point.column + offset, point.row}, glyph, style);
  }
}

void Canvas::VerticalLine(Point point, int height, std::string glyph,
                          Style style) {
  for (int offset = 0; offset < height; ++offset) {
    Put({point.column, point.row + offset}, glyph, style);
  }
}

void Canvas::Present(std::ostream &output, Point origin,
                     std::optional<Point> cursor) const {
  Style activeStyle;
  bool styleSet = false;
  for (int row = 0; row < _height; ++row) {
    output << "\033[" << origin.row + row << ';' << origin.column
           << "H\033[2K";
    for (int column = 0; column < _width; ++column) {
      const auto &cell =
          _cells[static_cast<std::size_t>(row * _width + column)];
      if (!styleSet || cell.style != activeStyle) {
        output << Ansi(cell.style);
        activeStyle = cell.style;
        styleSet = true;
      }
      output << cell.glyph;
    }
    output << "\033[0m";
    styleSet = false;
  }
  if (cursor) {
    output << "\033[?25h\033[" << origin.row + cursor->row << ';'
           << origin.column + cursor->column << 'H';
  } else {
    output << "\033[?25l";
  }
  output << std::flush;
}

std::size_t Canvas::TextWidth(std::string_view text) {
  return SplitGlyphs(text).size();
}

std::string Canvas::Clip(std::string_view text, std::size_t width) {
  const auto glyphs = SplitGlyphs(text);
  if (glyphs.size() <= width) {
    return std::string(text);
  }
  if (width <= 3) {
    return std::string(width, '.');
  }
  std::string result;
  for (std::size_t index = 0; index < width - 3; ++index) {
    result += glyphs[index];
  }
  return result + "...";
}

std::vector<std::string> Canvas::SplitGlyphs(std::string_view text) {
  std::vector<std::string> glyphs;
  std::size_t position = 0;
  while (position < text.size()) {
    const auto bytes = std::min(glyphBytes(
                                    static_cast<unsigned char>(text[position])),
                                text.size() - position);
    glyphs.emplace_back(text.substr(position, bytes));
    position += bytes;
  }
  return glyphs;
}

std::string Canvas::Ansi(Style style) {
  const char *color = "39";
  switch (style.foreground) {
  case Color::Cyan:
    color = "38;5;81";
    break;
  case Color::Muted:
    color = "38;5;244";
    break;
  case Color::Green:
    color = "38;5;84";
    break;
  case Color::Yellow:
    color = "38;5;220";
    break;
  case Color::Red:
    color = "38;5;203";
    break;
  case Color::Default:
    break;
  }
  return std::string("\033[0;") + (style.bold ? "1;" : "") + color + 'm';
}

} // namespace termify::tui
