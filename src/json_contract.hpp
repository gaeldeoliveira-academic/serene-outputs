#pragma once
// Allocation-free lexical admission before the general JSON parser; request/metadata profile.
#include <algorithm>
#include <serene/outputs/model.hpp>
#include <string_view>
namespace serene::outputs::json_contract {
struct Scan {
    std::string_view input;
    std::size_t at{};
    std::uint64_t strings{}, values{}, members{}, array_entries{}, depth{};
    Status status{};
    void bad(StatusCode code = StatusCode::invalid_contract) noexcept {
        if (status)
            status = error(code, 7);
    }
    void space() noexcept {
        while (at < input.size() &&
               (input[at] == ' ' || input[at] == '\t' || input[at] == '\r' || input[at] == '\n'))
            at++;
    }
    bool take(char c) noexcept {
        space();
        if (at < input.size() && input[at] == c) {
            at++;
            return true;
        }
        return false;
    }
    static int hex(char c) noexcept {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        return -1;
    }
    unsigned unit() noexcept {
        unsigned v = 0;
        if (input.size() - at < 4) {
            bad();
            return 0;
        }
        for (unsigned k = 0; k < 4; k++) {
            int d = hex(input[at++]);
            if (d < 0) {
                bad();
                return 0;
            }
            v = v * 16 + static_cast<unsigned>(d);
        }
        return v;
    }
    void string() noexcept {
        if (!take('"')) {
            bad();
            return;
        }
        while (status && at < input.size()) {
            auto c = static_cast<unsigned char>(input[at++]);
            if (c == '"')
                return;
            if (c < 32) {
                bad();
                return;
            }
            if (c == '\\') {
                if (at == input.size()) {
                    bad();
                    return;
                }
                char e = input[at++];
                if (e == 'u') {
                    auto x = unit();
                    if (!status)
                        return;
                    if (x == 0) {
                        bad();
                        return;
                    }
                    if (x >= 0xd800 && x <= 0xdbff) {
                        if (input.size() - at < 2 || input[at] != '\\' || input[at + 1] != 'u') {
                            bad();
                            return;
                        }
                        at += 2;
                        auto y = unit();
                        if (y < 0xdc00 || y > 0xdfff) {
                            bad();
                            return;
                        }
                    } else if (x >= 0xdc00 && x <= 0xdfff) {
                        bad();
                        return;
                    }
                } else if (std::string_view("\"\\/bfnrt").find(e) == std::string_view::npos) {
                    bad();
                    return;
                }
            } else if (c >= 128) {
                unsigned extra = c < 224 ? 1 : c < 240 ? 2 : 3;
                if (input.size() - at < extra) {
                    bad();
                    return;
                }
                at += extra;
            }
            strings++;
        }
        bad();
    }
    void number() noexcept {
        auto begin = at;
        if (at < input.size() && input[at] == '-')
            at++;
        if (at == input.size()) {
            bad();
            return;
        }
        if (input[at] == '0')
            at++;
        else {
            if (input[at] < '1' || input[at] > '9') {
                bad();
                return;
            }
            while (at < input.size() && input[at] >= '0' && input[at] <= '9')
                at++;
        }
        if (at < input.size() && input[at] == '.') {
            at++;
            auto b = at;
            while (at < input.size() && input[at] >= '0' && input[at] <= '9')
                at++;
            if (at == b) {
                bad();
                return;
            }
        }
        if (at < input.size() && (input[at] == 'e' || input[at] == 'E')) {
            at++;
            if (at < input.size() && (input[at] == '+' || input[at] == '-'))
                at++;
            auto b = at;
            while (at < input.size() && input[at] >= '0' && input[at] <= '9')
                at++;
            if (at == b) {
                bad();
                return;
            }
        }
        if (at - begin > 128)
            bad(StatusCode::resource_limit);
        else if (input.substr(begin, at - begin) == "-0")
            bad();
    }
    void value(std::uint64_t parent_depth) noexcept {
        if (!status)
            return;
        space();
        if (at == input.size()) {
            bad();
            return;
        }
        if (++values > 16384) {
            bad(StatusCode::resource_limit);
            return;
        }
        char c = input[at];
        if (c == '{' || c == '[') {
            auto current = parent_depth + 1;
            depth = std::max(depth, current);
            if (current > 16) {
                bad(StatusCode::resource_limit);
                return;
            }
            at++;
            bool object = c == '{';
            if (take(object ? '}' : ']'))
                return;
            while (status) {
                if (object) {
                    if (++members > 16384) {
                        bad(StatusCode::resource_limit);
                        return;
                    }
                    string();
                    if (!status || !take(':')) {
                        bad();
                        return;
                    }
                } else
                    array_entries++;
                value(current);
                if (!status)
                    return;
                if (take(object ? '}' : ']'))
                    return;
                if (!take(',')) {
                    bad();
                    return;
                }
            }
        } else if (c == '"')
            string();
        else if (c == 't' && input.substr(at, 4) == "true")
            at += 4;
        else if (c == 'f' && input.substr(at, 5) == "false")
            at += 5;
        else if (c == 'n' && input.substr(at, 4) == "null")
            at += 4;
        else
            number();
    }
};
inline Status preflight(std::string_view text, std::uint64_t budget = 64 * 1024 * 1024) noexcept {
    if (text.size() > 1048576)
        return error(StatusCode::resource_limit, 7);
    if (!valid_utf8(text))
        return error(StatusCode::invalid_contract, 7);
    Scan s{text};
    s.space();
    if (s.at == text.size() || text[s.at] != '{')
        return error(StatusCode::invalid_contract, 7);
    s.value(0);
    s.space();
    if (!s.status)
        return s.status;
    if (s.at != text.size())
        return error(StatusCode::invalid_contract, 7);
    // All terms are protocol-bounded before this sum; no unchecked untrusted multiplication.
    auto charge = 5 * text.size() + 4 * s.strings + 128 * s.values + 128 * s.members +
                  16 * s.array_entries + 256 * s.depth;
    return charge <= budget ? Status{} : error(StatusCode::resource_limit, 7);
}
} // namespace serene::outputs::json_contract
