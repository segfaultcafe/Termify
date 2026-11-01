#ifndef TERMIFY_TUI_WIDGETS_HPP
#define TERMIFY_TUI_WIDGETS_HPP

#include "Widget.hpp"

#include <deque>

namespace termify::tui {

enum class Alignment { Left, Center, Right };

class Label : public Widget {
public:
  explicit Label(std::string id);

  void SetText(std::string text, Style style = {});
  void SetSpans(std::vector<Span> spans);
  void SetAlignment(Alignment alignment);
  void Render(Canvas &canvas) const override;

private:
  std::vector<Span> _spans;
  Alignment _alignment{Alignment::Left};
};

class Frame : public Widget {
public:
  explicit Frame(std::string id);

  void SetTitle(std::vector<Span> title);
  void SetStyle(Style style);
  void Render(Canvas &canvas) const override;

private:
  std::vector<Span> _title;
  Style _style{Color::Cyan};
};

class Rule : public Widget {
public:
  explicit Rule(std::string id);

  void SetLabel(std::string label);
  void SetStyle(Style style);
  void Render(Canvas &canvas) const override;

private:
  std::string _label;
  Style _style{Color::Muted};
};

class ProgressBar : public Widget {
public:
  explicit ProgressBar(std::string id);

  void SetProgress(double progress);
  void SetLabels(std::string leading, std::string trailing);
  void SetPointer(std::optional<Point> point) override;
  bool Handle(const Event &event) override;
  void Render(Canvas &canvas) const override;
  std::optional<double> TakeSeekFraction();
  int TakeSeekAdjustment();

private:
  double fractionAt(int column) const;
  double _progress{0.0};
  double _dragProgress{0.0};
  std::string _leading;
  std::string _trailing;
  std::optional<double> _seekFraction;
  int _seekAdjustment{0};
  bool _dragging{false};
  bool _hovered{false};
};

class TextInput : public Widget {
public:
  explicit TextInput(std::string id);

  void SetPrompt(std::vector<Span> prompt);
  bool Focusable() const override;
  bool Handle(const Event &event) override;
  void Render(Canvas &canvas) const override;
  std::optional<Point> Cursor() const override;
  std::optional<std::string> TakeSubmission();
  const std::string &Value() const;
  void Clear();

private:
  static std::size_t PreviousCharacter(std::string_view text,
                                       std::size_t cursor);
  static std::size_t NextCharacter(std::string_view text, std::size_t cursor);
  static std::size_t CharacterCount(std::string_view text, std::size_t end);
  static std::size_t ByteOffsetForCharacter(std::string_view text,
                                            std::size_t character);
  std::size_t VisibleStart() const;
  void PlaceCursorFromMouse(int terminalColumn);

  std::vector<Span> _prompt;
  std::string _value;
  std::size_t _cursor{0};
  std::deque<std::string> _submissions;
};

class Button : public Widget {
public:
  Button(std::string id, std::string label);
  bool Focusable() const override { return true; }
  void SetLabel(std::string label);
  void SetPointer(std::optional<Point> point) override;
  bool Handle(const Event &event) override;
  void Render(Canvas &canvas) const override;
  bool TakeActivation();

private:
  std::string _label;
  bool _activated{false};
  bool _hovered{false};
};

class Knob : public Widget {
public:
  explicit Knob(std::string id);
  bool Focusable() const override { return true; }
  void SetValue(int value);
  void SetPointer(std::optional<Point> point) override;
  bool Handle(const Event &event) override;
  void Render(Canvas &canvas) const override;
  int TakeAdjustment();

private:
  int _value{100};
  int _adjustment{0};
  bool _hovered{false};
};

} // namespace termify::tui

#endif // TERMIFY_TUI_WIDGETS_HPP
