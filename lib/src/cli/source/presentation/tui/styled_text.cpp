#include "tui/styled_text.h"

#include <charconv>
#include <string>
#include <utility>

namespace apogee::tui {

namespace {

void apply_code(int code, ansi::TextAttributes& attributes) {
    switch (code) {
        case 0:
            attributes = {};
            break;
        case 1:
            attributes.bold = true;
            break;
        case 2:
            attributes.dim = true;
            break;
        case 3:
            attributes.italic = true;
            break;
        case 4:
            attributes.underline = true;
            break;
        case 9:
            attributes.strike = true;
            break;
        case 22:
            attributes.bold = false;
            attributes.dim = false;
            break;
        case 23:
            attributes.italic = false;
            break;
        case 24:
            attributes.underline = false;
            break;
        case 29:
            attributes.strike = false;
            break;
        case 31:
            attributes.color = ansi::Color::Red;
            break;
        case 32:
            attributes.color = ansi::Color::Green;
            break;
        case 33:
            attributes.color = ansi::Color::Yellow;
            break;
        case 34:
            attributes.color = ansi::Color::Blue;
            break;
        case 35:
            attributes.color = ansi::Color::Magenta;
            break;
        case 36:
            attributes.color = ansi::Color::Cyan;
            break;
        case 37:
            attributes.color = ansi::Color::White;
            break;
        case 39:
            attributes.color = ansi::Color::Default;
            break;
        case 90:
            attributes.color = ansi::Color::BrightBlack;
            break;
        default:
            break;
    }
}

/// `parameters` -- `1;36`, or empty for a reset -- applied in order.
void apply_parameters(std::string_view parameters, ansi::TextAttributes& attributes) {
    if (parameters.empty()) {
        attributes = {};
        return;
    }
    while (!parameters.empty()) {
        const std::size_t semicolon = parameters.find(';');
        const std::string_view part = parameters.substr(0, semicolon);
        int code = 0;
        if (std::from_chars(part.data(), part.data() + part.size(), code).ec == std::errc{}) {
            apply_code(code, attributes);
        }
        if (semicolon == std::string_view::npos) {
            break;
        }
        parameters.remove_prefix(semicolon + 1);
    }
}

}  // namespace

markdown::Row spans_from_sgr(std::string_view text) {
    markdown::Row row;
    ansi::TextAttributes attributes;
    std::string link;
    std::string pending;
    const auto flush = [&]() {
        if (pending.empty()) {
            return;
        }
        if (!row.empty() && row.back().attributes == attributes && row.back().link == link) {
            row.back().text += pending;
        } else {
            row.push_back(markdown::Span{.text = pending, .attributes = attributes, .link = link});
        }
        pending.clear();
    };
    std::size_t i = 0;
    while (i < text.size()) {
        const char c = text[i];
        if (c == '\033' && i + 1 < text.size() && text[i + 1] == '[') {
            // A control sequence: parameters, then one final byte.
            std::size_t end = i + 2;
            while (end < text.size() && (text[end] < 0x40 || text[end] > 0x7e)) {
                ++end;
            }
            if (end < text.size() && text[end] == 'm') {
                flush();
                apply_parameters(text.substr(i + 2, end - i - 2), attributes);
            }
            i = end + 1;
            continue;
        }
        if (c == '\033' && i + 1 < text.size() && text[i + 1] == ']') {
            // An operating system command, to its string terminator: OSC 8
            // carries a link, which the spans keep.
            std::size_t end = text.find("\033\\", i + 2);
            const std::size_t bell = text.find('\a', i + 2);
            std::size_t terminator = 2;
            if (bell != std::string_view::npos && (end == std::string_view::npos || bell < end)) {
                end = bell;
                terminator = 1;
            }
            if (end == std::string_view::npos) {
                break;
            }
            const std::string_view body = text.substr(i + 2, end - i - 2);
            if (body.starts_with("8;")) {
                flush();
                const std::size_t semicolon = body.find(';', 2);
                link = semicolon == std::string_view::npos
                           ? std::string{}
                           : std::string{body.substr(semicolon + 1)};
            }
            i = end + terminator;
            continue;
        }
        if (c == '\033') {
            i += 2;  // a two-byte escape: nothing a line should draw
            continue;
        }
        if (static_cast<unsigned char>(c) < 0x20 && c != '\t') {
            ++i;  // a control character is never obeyed, and never drawn
            continue;
        }
        pending += c == '\t' ? ' ' : c;
        ++i;
    }
    flush();
    return row;
}

}  // namespace apogee::tui
