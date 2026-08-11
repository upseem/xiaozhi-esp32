#include "circular_text_wrap.h"

#include <cmath>
#include <cstring>

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

namespace {

size_t Utf8CodepointLen(const char* s, size_t remaining) {
    if (remaining == 0) {
        return 0;
    }
    const unsigned char b = static_cast<unsigned char>(s[0]);
    size_t len = 1;
    if (b < 0x80) {
        len = 1;
    } else if ((b & 0xE0) == 0xC0) {
        len = 2;
    } else if ((b & 0xF0) == 0xE0) {
        len = 3;
    } else if ((b & 0xF8) == 0xF0) {
        len = 4;
    } else {
        len = 1;  // invalid lead: advance one byte
    }
    if (len > remaining) {
        return remaining;
    }
    return len;
}

// Next wrap unit: ASCII word (or single whitespace), or one UTF-8 codepoint.
size_t NextUnitLen(const char* text, size_t i, size_t n) {
    const unsigned char b = static_cast<unsigned char>(text[i]);
    if (b < 0x80) {
        if (text[i] == ' ' || text[i] == '\t') {
            return 1;
        }
        size_t j = i;
        while (j < n) {
            const unsigned char c = static_cast<unsigned char>(text[j]);
            if (c >= 0x80 || text[j] == ' ' || text[j] == '\t' || text[j] == '\n') {
                break;
            }
            ++j;
        }
        return j - i;
    }
    return Utf8CodepointLen(text + i, n - i);
}

}  // namespace

std::string WrapToCircle(const char* text, int first_line_y_center, int line_height,
                         MeasureFn measure) {
    if (text == nullptr || *text == '\0') {
        return {};
    }

    std::string out;
    int y = first_line_y_center;
    int max_w = ChordTextWidth(y);
    size_t i = 0;
    size_t line_start = 0;
    int line_px = 0;
    const size_t n = std::strlen(text);

    auto emit_line = [&](size_t from, size_t to) {
        if (!out.empty()) {
            out.push_back('\n');
        }
        out.append(text + from, text + to);
    };

    auto advance_line = [&]() {
        y += line_height;
        max_w = ChordTextWidth(y);
        line_px = 0;
    };

    while (i < n) {
        if (text[i] == '\n') {
            emit_line(line_start, i);
            ++i;
            line_start = i;
            advance_line();
            continue;
        }

        const size_t unit_len = NextUnitLen(text, i, n);
        if (unit_len == 0) {
            break;
        }

        const int unit_px = measure(text + i, unit_len);

        if (line_px > 0 && line_px + unit_px > max_w) {
            emit_line(line_start, i);
            line_start = i;
            advance_line();
            // Retry same unit on the new line (do not advance i).
            continue;
        }

        // Oversized unit alone: still emit to avoid infinite loop.
        if (line_px == 0 && unit_px > max_w) {
            emit_line(line_start, i + unit_len);
            i += unit_len;
            line_start = i;
            advance_line();
            continue;
        }

        line_px += unit_px;
        i += unit_len;
    }

    if (line_start < n) {
        emit_line(line_start, n);
    }
    return out;
}

}  // namespace circular_ui
