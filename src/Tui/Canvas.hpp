#ifndef TERMIFY_TUI_CANVAS_HPP
#define TERMIFY_TUI_CANVAS_HPP

#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace termify::tui {

struct Point {
  int column{0};
  int row{0};

  bool operator==(const Point &) const = default;
};

struct Rect {
  int column{0};
  int row{0};
  int width{0};
  int height{0};

  bool Contains(Point point) const;
  Point Center() const;
};

enum class Color {
  Default,
  Cyan,
  Muted,
  Green,
  Yellow,
  Red,
};

struct Style {
  Color foreground{Color::Default};
  bool bold{false};

  bool operator==(const Style &) const = default;
};

struct Span {
  std::string text;
  Style style;
};

class Canvas {
public:
  Canvas(int width, int height);

  int Width() const;
  int Height() const;
  void Clear();
  void Put(Point point, std::string glyph, Style style = {});
  int Text(Point point, std::string_view text, Style style = {},
           int maxWidth = -1);
  int Spans(Point point, const std::vector<Span> &spans, int maxWidth = -1);
  void HorizontalLine(Point point, int width, std::string glyph = "─",
                      Style style = {});
  void VerticalLine(Point point, int height, std::string glyph = "│",
                    Style style = {});
  void Present(std::ostream &output, Point origin,
               std::optional<Point> cursor = std::nullopt) const;

  static std::size_t TextWidth(std::string_view text);
  static std::string Clip(std::string_view text, std::size_t width);

private:
  struct Cell {
    std::string glyph{" "};
    Style style;
  };

  static std::vector<std::string> SplitGlyphs(std::string_view text);
  static std::string Ansi(Style style);

  int _width;
  int _height;
  std::vector<Cell> _cells;
};

} // namespace termify::tui

#endif // TERMIFY_TUI_CANVAS_HPP
