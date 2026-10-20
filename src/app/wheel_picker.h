#pragma once

#include <M5Unified.h>

// iPhone-style scroll wheel for picking one of `count` values 0, step, 2*step, ...
// (wraps around). Drag to scroll, flick to spin with momentum, tap a row to move
// it to the centre; the wheel always settles on a whole value.
class WheelPicker {
 public:
  WheelPicker(int count, int x, int width, int step = 1)
      : count_(count), step_(step), x_(x), width_(width) {}

  // Values between steps are rounded to the nearest step.
  void setValue(int value);
  int value() const;

  // Where the selected row sits, as the distance of its centre from the top of the
  // wheel area (default: the middle). Higher up keeps it visible above the finger.
  void setSelectionOffset(int offset) { selectionOffset_ = offset; }

  int x() const { return x_; }
  int width() const { return width_; }

  // Feed the current touch state (call once per frame after M5.update()).
  // `top` / `height` describe the wheel area on screen.
  void handleTouch(const m5::touch_detail_t& touch, int top, int height);

  // Advance momentum / snapping. Returns true while the wheel is moving.
  bool animate();

  // Draw the wheel column into `canvas`, whose top-left corner is at screen
  // position (originX, originY); the wheel area spans `top` .. `top + height`.
  void draw(M5Canvas& canvas, int top, int height, int originX = 0, int originY = 0) const;

 private:
  static constexpr int kRowHeight = 40;

  int selectionY(int top, int height) const {
    return top + (selectionOffset_ >= 0 ? selectionOffset_ : height / 2);
  }

  int count_;
  int step_;
  int x_;
  int width_;
  int selectionOffset_ = -1;  // -1 = middle of the wheel area
  float position_ = 0;  // continuous value index; the centre row shows round(position_)
  float velocity_ = 0;  // rows per second
  bool dragging_ = false;
  bool snapping_ = false;
  float snapTarget_ = 0;
  uint32_t lastFrameMs_ = 0;
  uint32_t lastMoveMs_ = 0;
};
