# Circular AMOLED 1.75 UI Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** 为 Waveshare ESP32-S3-Touch-AMOLED-1.75/1.75C 做圆屏安全布局：表情置顶、中下多行字幕按圆弦换行，待机显示状态不显示时钟。

**Architecture:** 板级 `CustomLcdDisplay` 在父类 `SetupUI()` 之后重排几何；新增与 LVGL 解耦的圆弦换行 helper；`SetChatMessage` 预插入 `\n`；`SetStatus` 拦截 idle 时钟刷新。不改全局 `lcd_display.cc`。

**Tech Stack:** ESP-IDF、LVGL、C++17、现有 `SpiLcdDisplay` / `LvglDisplay`

**Design doc:** `docs/plans/2026-08-11-circular-amoled-1.75-ui-design.md`

---

### Task 1: 圆弦宽度与换行 helper

**Files:**
- Create: `main/boards/waveshare/esp32-s3-touch-amoled-1.75/circular_text_wrap.h`
- Create: `main/boards/waveshare/esp32-s3-touch-amoled-1.75/circular_text_wrap.cc`
- Note: `main/CMakeLists.txt` 已 `GLOB` 该目录下 `*.cc`，无需改 CMake

**Step 1: 写 header（几何常量 + API）**

```cpp
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
```

**Step 2: 实现 `ChordTextWidth`**

```cpp
#include "circular_text_wrap.h"
#include <cmath>

namespace circular_ui {

int ChordTextWidth(int y_center, int edge_pad) {
    const int dy = y_center - kCenterY;
    const int64_t r2 = int64_t(kSafeRadius) * kSafeRadius;
    const int64_t dy2 = int64_t(dy) * dy;
    if (dy2 >= r2) {
        return 0;
    }
    const int half = static_cast<int>(std::sqrt(static_cast<double>(r2 - dy2)));
    const int w = 2 * half - 2 * edge_pad;
    return w > 0 ? w : 0;
}

}  // namespace circular_ui
```

**Step 3: 实现 `WrapToCircle`（最小可用版）**

要点：
- 空/`nullptr` → 返回空串
- UTF-8：ASCII 字节 `<0x80` 单字节；否则按 UTF-8 首字节长度取码点
- 拉丁：累积到空白再决定是否换行；CJK 等非 ASCII 按码点逐个尝试
- 每换一行：`y += line_height`，重新 `ChordTextWidth(y)`
- 若某字符宽 `> max_w`：仍单独成行（避免死循环），允许极端字形略紧

伪代码骨架：

```cpp
std::string WrapToCircle(const char* text, int first_line_y_center, int line_height,
                         MeasureFn measure) {
    if (text == nullptr || *text == '\0') return {};
    std::string out;
    int y = first_line_y_center;
    int max_w = ChordTextWidth(y);
    size_t i = 0;
    size_t line_start = 0;
    int line_px = 0;
    const size_t n = std::strlen(text);
    auto emit_line = [&](size_t from, size_t to) {
        if (!out.empty()) out.push_back('\n');
        out.append(text + from, text + to);
    };
    while (i < n) {
        // 取下一个“单位”（ASCII 词或一个 UTF-8 码点），byte_len / unit_px
        // 若 line_px + unit_px > max_w 且 line 非空：emit_line(line_start, i); 换行重置
        // 否则累加
    }
    if (line_start < n) emit_line(line_start, n);
    return out;
}
```

实现时补全 UTF-8 步进与 ASCII 词边界（空格/`\n` 原样处理：`\n` 强制换行并推进 y）。

**Step 4: 本地快速校验（可选 host 小程序或临时断言）**

在实现文件底部可用 `#ifdef CIRCULAR_TEXT_WRAP_SELFTEST` 做自检，或用下面命令编译临时测试（不入库也可）：

```bash
# 期望：圆心附近弦宽接近 450-28=422；靠近边缘更窄
# ChordTextWidth(233) ≈ 422
# ChordTextWidth(50) 明显更小且 > 0
# ChordTextWidth(0) == 0
```

手算验收：`ChordTextWidth(233)` 应约为 `2*225 - 2*14 = 422`。

**Step 5: Commit**

```bash
git add main/boards/waveshare/esp32-s3-touch-amoled-1.75/circular_text_wrap.h \
        main/boards/waveshare/esp32-s3-touch-amoled-1.75/circular_text_wrap.cc
git commit -m "feat(1.75): add circular chord text wrap helper"
```

---

### Task 2: 板级启用多行字幕 + CustomLcdDisplay 布局

**Files:**
- Modify: `main/boards/waveshare/esp32-s3-touch-amoled-1.75/config.json`
- Modify: `main/boards/waveshare/esp32-s3-touch-amoled-1.75/esp32-s3-touch-amoled-1.75.cc`

**Step 1: config.json 两个 build 都加多行**

在 `sdkconfig_append` 中增加：

```json
"CONFIG_USE_MULTILINE_CHAT_MESSAGE=y"
```

（`1.75` 与 `1.75c` 两段都加）

**Step 2: 扩展 `CustomLcdDisplay`**

`#include "circular_text_wrap.h"`、`assets/lang_config.h`、`<cstring>`、`<cctype>`。

覆盖：

```cpp
virtual void SetupUI() override;
virtual void SetChatMessage(const char* role, const char* content) override;
virtual void SetStatus(const char* status) override;
```

**Step 3: `SetupUI` 在父类之后重排**

```cpp
virtual void SetupUI() override {
    SpiLcdDisplay::SetupUI();
    DisplayLockGuard lock(this);

    // QSPI 对齐（保留原有）
    lv_display_add_event_cb(display_, rounder_event_cb, LV_EVENT_INVALIDATE_AREA, NULL);

    // 顶部状态：圆顶安全带
    if (top_bar_) {
        lv_obj_set_style_pad_left(top_bar_, LV_HOR_RES * 0.18, 0);
        lv_obj_set_style_pad_right(top_bar_, LV_HOR_RES * 0.18, 0);
        lv_obj_set_style_pad_top(top_bar_, 18, 0);
    }
    if (status_bar_) {
        lv_obj_set_style_pad_left(status_bar_, LV_HOR_RES * 0.18, 0);
        lv_obj_set_style_pad_right(status_bar_, LV_HOR_RES * 0.18, 0);
        lv_obj_set_style_pad_top(status_bar_, 18, 0);
    }
    if (status_label_) {
        lv_obj_set_width(status_label_, circular_ui::ChordTextWidth(40));
        lv_label_set_long_mode(status_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    }

    // 表情置顶（偏上）
    if (emoji_box_) {
        lv_obj_align(emoji_box_, LV_ALIGN_TOP_MID, 0, 56);
    }

    // 字幕区：中下，限高，可滚
    if (bottom_bar_) {
        const int bar_h = 220;
        lv_obj_set_size(bottom_bar_, circular_ui::kSafeDiameter, bar_h);
        lv_obj_align(bottom_bar_, LV_ALIGN_TOP_MID, 0, 190);
        lv_obj_set_style_bg_opa(bottom_bar_, LV_OPA_TRANSP, 0);
        lv_obj_set_style_pad_all(bottom_bar_, 0, 0);
        lv_obj_set_scrollbar_mode(bottom_bar_, LV_SCROLLBAR_MODE_AUTO);
        lv_obj_set_scroll_dir(bottom_bar_, LV_DIR_VER);
    }
    if (chat_message_label_) {
        // 标签宽度用最宽弦附近，实际换行由预插入 \n + 行弦宽保证
        lv_obj_set_width(chat_message_label_, circular_ui::ChordTextWidth(300));
        lv_label_set_long_mode(chat_message_label_, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(chat_message_label_, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(chat_message_label_, LV_ALIGN_TOP_MID, 0, 0);
    }
}
```

数值可按实机微调，但须满足：表情在上、字幕中下、内容在直径 450 内。

**Step 4: Commit**

```bash
git add main/boards/waveshare/esp32-s3-touch-amoled-1.75/config.json \
        main/boards/waveshare/esp32-s3-touch-amoled-1.75/esp32-s3-touch-amoled-1.75.cc
git commit -m "feat(1.75): reflow circular safe-area layout"
```

---

### Task 3: SetChatMessage 圆弦预换行

**Files:**
- Modify: `main/boards/waveshare/esp32-s3-touch-amoled-1.75/esp32-s3-touch-amoled-1.75.cc`

**Step 1: 实现 `SetChatMessage`**

```cpp
virtual void SetChatMessage(const char* role, const char* content) override {
    DisplayLockGuard lock(this);
    if (chat_message_label_ == nullptr) {
        return;
    }

    if (content == nullptr || content[0] == '\0') {
        lv_label_set_text(chat_message_label_, "");
        if (bottom_bar_) {
            lv_obj_add_flag(bottom_bar_, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }

    auto* theme = static_cast<LvglTheme*>(current_theme_);
    const lv_font_t* font = theme->text_font()->font();
    const int line_height = font->line_height > 0 ? font->line_height : 30;
    // 字幕区第一行中心 y（与 SetupUI 对齐）
    const int first_y = 190 + line_height / 2;

    auto measure = [font](const char* utf8, size_t byte_len) -> int {
        return lv_txt_get_width(utf8, byte_len, font, font->line_height, LV_TEXT_FLAG_NONE);
    };
    // 注意：确认当前工程 LVGL 版本的 lv_txt_get_width 签名；若不匹配则用
    // lv_text_get_width / lv_txt_get_size 等价 API。

    std::string wrapped = circular_ui::WrapToCircle(content, first_y, line_height, measure);
    lv_anim_delete(chat_message_label_, nullptr);
    lv_label_set_text(chat_message_label_, wrapped.c_str());
    if (bottom_bar_ != nullptr && !hide_subtitle_) {
        lv_obj_remove_flag(bottom_bar_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_scroll_to_y(bottom_bar_, LV_COORD_MAX, LV_ANIM_OFF);  // 看最新内容
    }
    (void)role;
}
```

**Step 2: 编译前核对 LVGL API**

在工程内搜索：

```bash
rg "lv_txt_get_width|lv_text_get_width" -g '*.h' | head
```

按实际签名调整 `measure` lambda。

**Step 3: Commit**

```bash
git add main/boards/waveshare/esp32-s3-touch-amoled-1.75/esp32-s3-touch-amoled-1.75.cc
git commit -m "feat(1.75): wrap chat text to circular chords"
```

---

### Task 4: 待机不显示时钟

**Files:**
- Modify: `main/boards/waveshare/esp32-s3-touch-amoled-1.75/esp32-s3-touch-amoled-1.75.cc`

**Step 1: 覆盖 `SetStatus`**

`LvglDisplay::UpdateStatusBar` 在 idle 时会 `SetStatus("HH:MM")`。板级拦截：

```cpp
static bool LooksLikeClock(const char* s) {
    if (s == nullptr) return false;
    // H:MM or HH:MM
    int a = 0, b = 0, n = 0;
    if (std::sscanf(s, "%d:%d%n", &a, &b, &n) == 2 && s[n] == '\0') {
        return a >= 0 && a <= 23 && b >= 0 && b <= 59;
    }
    return false;
}

virtual void SetStatus(const char* status) override {
    if (LooksLikeClock(status)) {
        // 吞掉时钟刷新，避免覆盖「待机」；更新时间戳防止每 tick 重入
        last_status_update_time_ = std::chrono::system_clock::now();
        return;
    }
    LvglDisplay::SetStatus(status);
}
```

需要 `#include <chrono>`、`#include <cstdio>`。`last_status_update_time_` 在 `LvglDisplay` 为 `protected`，可直接写。

**Step 2: Commit**

```bash
git add main/boards/waveshare/esp32-s3-touch-amoled-1.75/esp32-s3-touch-amoled-1.75.cc
git commit -m "feat(1.75): keep standby status instead of clock"
```

---

### Task 5: README 与实机验收

**Files:**
- Modify: `main/boards/waveshare/esp32-s3-touch-amoled-1.75/README.md`

**Step 1: 补充 UI 说明（简短）**

说明：
- 安全圆直径 450
- 表情上、字幕中下、圆弦换行
- 待机显示状态、无时钟

**Step 2: 编译烧录**

```bash
# 选板：WAVESHARE ESP32-S3-Touch-AMOLED-1.75
idf.py set-target esp32s3
idf.py -D SDKCONFIG_DEFAULTS=...  # 或按仓库既有板级构建脚本
idf.py build flash monitor
```

若仓库有板级一键构建，按其文档使用 `config.json` 的 `esp32-s3-touch-amoled-1.75` build。

**Step 3: 验收清单（对照设计）**

1. 待机：表情在上，状态为待机/连接类文案，**无**大时钟 / 顶栏不被 `HH:MM` 替换掉业务状态过久后仍应保持非时钟（吞掉后保持原状态）。  
2. 说一句长中文：字幕中下多行，左右无半字出圆。  
3. 超长回复：区内可滚，表情不被顶出。  
4. 圆外黑边干净。

**Step 4: Commit**

```bash
git add main/boards/waveshare/esp32-s3-touch-amoled-1.75/README.md
git commit -m "docs(1.75): document circular UI layout"
```

---

## 风险与微调

| 风险 | 处理 |
|------|------|
| `lv_txt_get_width` 签名因 LVGL 版本不同 | Task 3 Step 2 先搜再写 |
| 表情 GIF 高度过大压住字幕 | 略减 emoji 尺寸或增大 `first_y` |
| 多行 config 未进已有 sdkconfig | 重新 `idf.py fullclean` 或确认 menuconfig 已开 `USE_MULTILINE_CHAT_MESSAGE` |
| 父类 `SetChatMessage` 与 override 锁顺序 | 板级完整接管，勿先调父类再改字（避免闪一下全宽字） |

## 完成定义

设计文档验收 1–5 条在 1.75 实机上通过；改动限于 `esp32-s3-touch-amoled-1.75` 目录 + 其 `config.json`。
