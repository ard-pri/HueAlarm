#include "wheel_picker.h"

#include <math.h>

namespace {

constexpr float kFriction = 6.0f;       // momentum decay per second (exponential)
constexpr float kFlickVelocity = 6.0f;  // rows/s at release needed to spin on at all
constexpr float kMinVelocity = 2.0f;    // rows/s below which the wheel snaps
constexpr float kSnapRate = 18.0f;      // snapping speed (1/s)
constexpr uint32_t kFlickWindowMs = 60; // a release later than this after the last move is no flick

int wrap(int value, int count) { return ((value % count) + count) % count; }

}  // namespace

void WheelPicker::setValue(int value) {
  position_ = snapTarget_ = (float)wrap((value + step_ / 2) / step_, count_);
  velocity_ = 0;
  dragging_ = snapping_ = false;
}

int WheelPicker::value() const { return wrap((int)lroundf(position_), count_) * step_; }

void WheelPicker::handleTouch(const m5::touch_detail_t& touch, int top, int height) {
  bool inside = touch.base_x >= x_ && touch.base_x < x_ + width_ && touch.base_y >= top &&
                touch.base_y < top + height;
  if (!inside) return;

  uint32_t now = millis();
  if (touch.wasPressed()) {
    // Grabbing the wheel stops any momentum.
    dragging_ = true;
    snapping_ = false;
    velocity_ = 0;
    lastMoveMs_ = now;
    return;
  }

  if (dragging_ && touch.isPressed()) {
    int dy = touch.deltaY();
    if (dy != 0) {
      float rows = -(float)dy / kRowHeight;  // finger down -> smaller values come in from the top
      position_ += rows;
      float dt = max<uint32_t>(now - lastMoveMs_, 8) / 1000.0f;
      velocity_ = 0.5f * velocity_ + 0.5f * (rows / dt);
      lastMoveMs_ = now;
    }
    return;
  }

  if (dragging_ && touch.wasReleased()) {
    dragging_ = false;
    // Only a real flick spins on; a drag that slowed down or rested settles where it is.
    if (now - lastMoveMs_ > kFlickWindowMs || fabsf(velocity_) < kFlickVelocity) velocity_ = 0;
    if (touch.wasClicked()) {
      // Tap without dragging: bring the tapped row to the centre.
      int centerY = selectionY(top, height);
      int rows = (int)lroundf((float)(touch.y - centerY) / kRowHeight);
      velocity_ = 0;
      snapping_ = true;
      snapTarget_ = roundf(position_) + rows;
    }
  }
}

bool WheelPicker::animate() {
  uint32_t now = millis();
  float dt = lastFrameMs_ ? min<uint32_t>(now - lastFrameMs_, 100) / 1000.0f : 0;
  lastFrameMs_ = now;
  if (dragging_) return true;

  if (!snapping_) {
    if (fabsf(velocity_) > kMinVelocity) {
      position_ += velocity_ * dt;
      velocity_ *= expf(-kFriction * dt);
      return true;
    }
    velocity_ = 0;
    if (fabsf(position_ - roundf(position_)) < 0.001f) {
      position_ = roundf(position_);
      return false;  // at rest on a whole value
    }
    snapping_ = true;
    snapTarget_ = roundf(position_);  // settle on the nearest value, never one further
  }

  position_ += (snapTarget_ - position_) * min(1.0f, dt * kSnapRate);
  if (fabsf(snapTarget_ - position_) < 0.01f) {
    position_ = snapTarget_;
    snapping_ = false;
    // Keep the number small so float precision never suffers.
    position_ = (float)wrap((int)lroundf(position_), count_);
    snapTarget_ = position_;
    return false;
  }
  return true;
}

void WheelPicker::draw(M5Canvas& canvas, int top, int height, int originX, int originY) const {
  const int centerY = selectionY(top, height) - originY;
  // Rows fade with their distance from the selected row, up to the far edge.
  const int reach = max(selectionY(top, height) - top, top + height - selectionY(top, height));
  const int x = x_ - originX;

  canvas.setClipRect(x, top - originY, width_, height);
  canvas.setFont(&fonts::FreeSansBold18pt7b);
  canvas.setTextDatum(middle_center);

  int base = (int)floorf(position_);
  for (int k = -6; k <= 7; ++k) {
    int index = base + k;
    float dy = (index - position_) * kRowHeight;
    if (fabsf(dy) > reach + kRowHeight) continue;
    float distance = min(1.0f, fabsf(dy) / reach);
    // Rows shrink and fade towards the edges, like a drum seen from the front.
    canvas.setTextSize(1.0f - 0.35f * distance);
    uint8_t level = 255 - (uint8_t)(190 * distance);
    canvas.setTextColor(canvas.color565(level, level, level));
    char text[4];
    snprintf(text, sizeof(text), "%02d", wrap(index, count_) * step_);
    canvas.drawString(text, x + width_ / 2, centerY + (int)dy);
  }
  canvas.setTextSize(1);
  canvas.clearClipRect();
}
