#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace circular_ui {

constexpr int kCanvas = 466;
constexpr int kSafeDiameter = 450;
constexpr int kSafeRadius = kSafeDiameter / 2;  // 225
constexpr int kCenterX = kCanvas / 2;           // 233
constexpr int kCenterY = kCanvas / 2;           // 233
constexpr int kEdgePad = 14;

// 行中心 y（画布坐标）处的最大文本像素宽；越界返回 0
int ChordTextWidth(int y_center, int edge_pad = kEdgePad);

// measure(utf8, byte_len) -> 像素宽。中文按码点切，ASCII 尽量按词。
using MeasureFn = std::function<int(const char* utf8, size_t byte_len)>;
std::string WrapToCircle(const char* text, int first_line_y_center, int line_height,
                         MeasureFn measure);

}  // namespace circular_ui
