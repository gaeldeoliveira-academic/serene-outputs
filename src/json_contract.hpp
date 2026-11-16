#pragma once
// Allocation-free lexical admission before the general JSON parser; request/metadata profile.
#include <algorithm>
#include <serene/outputs/model.hpp>
#include <string_view>
namespace serene::outputs::json_contract {
/** Allocation-free scanner for the restricted request/metadata JSON profile.
 * Borrows immutable input and advances a byte cursor while accumulating a
 * conservative parser charge. preflight() first validates UTF-8, then drives
 * this scanner; spool/model parsing performs decoded duplicate-key and finite
 * numeric checks afterward. No general JSON tree or source ownership lives here.
 * A failed scan latches its first Status; callers discard the incomplete counts.
 */
struct Scan {
    std::string_view input; ///< Borrowed UTF-8 JSON bytes, unchanged for the scan lifetime.
    std::size_t at{}; ///< Next byte offset to consume; never advances beyond input.size().
    std::uint64_t strings{}, ///< Decoded string-character units used for conservative UTF-8 storage charge.
     values{}, ///< Number of syntactic JSON values admitted so far.
     members{}, ///< Number of object members across the scanned tree.
     array_entries{}, ///< Number of array elements across the scanned tree.
     depth{}; ///< Maximum container nesting depth seen, limited to sixteen.
    Status status{}; ///< First scan failure, or success while lexical admission remains valid.
    /** Latch code as operation seven only if no prior error exists; defaults to invalid_contract. Mutates status and returns no value. */
    void bad(StatusCode code = StatusCode::invalid_contract) noexcept {
        // Preserve the first failure instead of replacing its diagnostic category.

        if (status)
            status = error(code, 7);
    }
    /** Skip the four JSON whitespace bytes at the current cursor; mutates at without allocating. */
    void space() noexcept {
        // Advance over space, tab, carriage return and line feed until a token or end.

        while (at < input.size() &&
               (input[at] == ' ' || input[at] == '\t' || input[at] == '\r' || input[at] == '\n'))
            at++;
    }
    /** After whitespace, consume expected ASCII delimiter c if present. Returns whether it matched and advances at only for whitespace/match. */
    bool take(char c) noexcept {
        // Skip JSON whitespace, compare the next byte, and advance once on a match.

        space();
        if (at < input.size() && input[at] == c) {
            at++;
            return true;
        }
        return false;
    }
    /** Return the hexadecimal value of ASCII c, accepting either case, or -1 for a non-hexadecimal byte. */
    static int hex(char c) noexcept {
        // Classify decimal digits and the two hexadecimal letter ranges; reject all other bytes.

        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        return -1;
    }
    /** Consume four hexadecimal bytes after an already-consumed Unicode escape prefix. Returns the 16-bit code unit, or zero after latching malformed input; callers inspect status to distinguish errors. */
    unsigned unit() noexcept {
        // Check the remaining extent, decode four digits, and stop at the first malformed digit.

        /* Decoded Unicode code-unit accumulator. */ unsigned v = 0;
        if (input.size() - at < 4) {
            bad();
            return 0;
        }
        for (/* Position among the four hexadecimal digits. */ unsigned k = 0; k < 4; k++) {
            /* Decoded nibble, with -1 indicating invalid hex. */ int d = hex(input[at++]);
            if (d < 0) {
                bad();
                return 0;
            }
            v = v * 16 + static_cast<unsigned>(d);
        }
        return v;
    }
    /** Consume one JSON string including quotes and update strings; latches malformed escapes, control bytes, escaped NUL or unpaired surrogates. Requires prior whole-input UTF-8 validation. */
    void string() noexcept {
        // Require an opening quote and scan encoded characters until its closing quote.
        // Validate escape spellings and surrogate pairs while rejecting control bytes/NUL.
        // Skip already-validated UTF-8 continuation bytes and accumulate conservative string units.

        if (!take('"')) {
            bad();
            return;
        }
        while (status && at < input.size()) {
            /* Current unsigned string byte, avoiding signed-char classification. */ auto c = static_cast<unsigned char>(input[at++]);
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
                /* Escape selector following the backslash. */ char e = input[at++];
                if (e == 'u') {
                    /* First Unicode escape code unit. */ auto x = unit();
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
                        /* Required low surrogate following a high surrogate. */ auto y = unit();
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
                /* UTF-8 continuation count; prior valid_utf8 checked their actual values. */ unsigned extra = c < 224 ? 1 : c < 240 ? 2 : 3;
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
    /** Consume restricted JSON number syntax at at. Limits encoded tokens to 128 bytes and rejects integer spelling -0; general parsing later checks representability/finiteness. */
    void number() noexcept {
        // Read optional sign and the integer part with no leading-zero extension.
        // Require digits after optional fraction/exponent markers.
        // Apply encoded-token length and integer negative-zero rules.

        /* Numeric-token start used for the byte-length/signed-zero checks. */ auto begin = at;
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
            /* Start of required fraction or exponent digits. */ auto b = at;
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
            /* Start of required fraction or exponent digits. */ auto b = at;
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
    /** Consume one value below parent_depth, updating counts/max depth and first failure. Container nesting is limited to 16 and value/member populations to 16384; does not allocate. */
    void value(std::uint64_t parent_depth) noexcept {
        // Stop on prior failure or missing input and charge this value.
        // For containers, enforce depth/member bounds and recurse through their syntax.
        // For scalars, consume string, literal or numeric syntax; enclosing/full-input checks reject suffixes.

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
        /* Next value token byte after whitespace. */ char c = input[at];
        if (c == '{' || c == '[') {
            /* Current container nesting depth below the caller. */ auto current = parent_depth + 1;
            depth = std::max(depth, current);
            if (current > 16) {
                bad(StatusCode::resource_limit);
                return;
            }
            at++;
            /* Object versus array branch selecting keys/closing delimiter. */ bool object = c == '{';
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
    /** Admit borrowed request/metadata text before general parsing under byte charge budget. Requires a complete UTF-8 JSON object no larger than 1 MiB; returns success, invalid_contract or resource_limit, all without allocation. This is narrower than retained-document JSON admission. */
inline Status preflight(std::string_view text, std::uint64_t budget = 64 * 1024 * 1024) noexcept {
        // Check byte size and UTF-8, then require an object root.
        // Scan its full lexical structure and reject unconsumed text.
        // Compute the bounded conservative general-parser charge and compare it with budget.

    if (text.size() > 1048576)
        return error(StatusCode::resource_limit, 7);
    if (!valid_utf8(text))
        return error(StatusCode::invalid_contract, 7);
    /* Borrowed lexical scan state for this object. */ Scan s{text};
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
    /* Conservative bounded parser-memory charge in bytes. */ auto charge = 5 * text.size() + 4 * s.strings + 128 * s.values + 128 * s.members +
                  16 * s.array_entries + 256 * s.depth;
    return charge <= budget ? Status{} : error(StatusCode::resource_limit, 7);
}
} // namespace serene::outputs::json_contract
