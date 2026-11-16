// Source-text verification after the indexer's length-preserving folding.
// Both arguments must be in the same canonical byte representation.
// Positions and consumed lengths remain byte offsets in the original source.
#pragma once

#include <cstddef>
#include <cstring>

namespace source_match {

struct Result {
    bool matched = false;
    std::size_t bytes = 0;
};

// Normalization maps separator codepoints to ASCII spaces without changing
// the source byte length. Match runs using memcmp, allowing a run of spaces
// on either side to stand for one separator. This does not check the final
// word boundary: the caller must apply its exact-term/prefix policy there.
inline Result folded(const unsigned char *query, std::size_t query_bytes,
                     const unsigned char *source, std::size_t source_bytes)
{
    if (!query || !source || query_bytes == 0) return {};
    std::size_t q = 0, s = 0;
    while (q < query_bytes) {
        if (query[q] == ' ') {
            if (s == source_bytes || source[s] != ' ') return {};
            do { ++q; } while (q < query_bytes && query[q] == ' ');
            do { ++s; } while (s < source_bytes && source[s] == ' ');
            continue;
        }
        const std::size_t begin = q;
        while (q < query_bytes && query[q] != ' ') ++q;
        const std::size_t count = q - begin;
        if (count > source_bytes - s ||
            std::memcmp(query + begin, source + s, count) != 0)
            return {};
        s += count;
    }
    return {true, s};
}


// Comparison ordering is needed by TermSearch's binary search, not just by
// post-lookup verification. Inputs are already folded, never raw UTF-8.
// A 0 order means the query is a prefix; the caller decides whether the
// following source codepoint disqualifies an exact-term match.
struct OrderedResult {
    int order = 0;
    std::size_t bytes = 0;
};

inline OrderedResult compare_folded(const unsigned char *query, std::size_t query_bytes,
                                   const unsigned char *source, std::size_t source_bytes)
{
    if (!query || !source) return {1, 0};
    std::size_t q = 0, s = 0;
    while (q < query_bytes) {
        if (query[q] == ' ' && s < source_bytes && source[s] == ' ') {
            do { ++q; } while (q < query_bytes && query[q] == ' ');
            do { ++s; } while (s < source_bytes && source[s] == ' ');
            continue;
        }
        if (s == source_bytes) return {1, s};
        const std::size_t qb = q, sb = s;
        while (q < query_bytes && query[q] != ' ') ++q;
        while (s < source_bytes && source[s] != ' ') ++s;
        const std::size_t length = (q - qb < s - sb) ? q - qb : s - sb;
        const int order = length ? std::memcmp(query + qb, source + sb, length) : 0;
        if (order) return {order, sb + length};
        if (q - qb != s - sb) {
            if (q - qb < s - sb) {
                // A space or end of query sorts before a following word byte.
                return {q < query_bytes ? int(query[q]) - int(source[sb+length])
                                        : 0, sb + length};
            }
            return {int(query[qb+length]) -
                    (s < source_bytes ? int(source[s]) : 0), s};
        }
        // Both runs ended; the next iteration checks equivalent separators.
    }
    return {0, s};
}

// Given a tentative source offset for a phrase, find the beginning of its
// current word, or skip separators to the following word. Unlike the old
// backward byte walk, UTF-8 continuation bytes are never classified as
// characters. The caller applies the returned displacement to the GP *only
// after* verifying the source text.
template<class IsWord>
std::size_t align_start(const unsigned char *source, std::size_t source_bytes,
                        std::size_t preferred, bool utf8, IsWord is_word)
{
    if (!source || preferred >= source_bytes) return source_bytes;
    std::size_t at = preferred;
    if (utf8) {
        while (at > 0 && (source[at] & 0xc0) == 0x80) --at;
    }
    if (is_word(source + at)) {
        while (at > 0) {
            std::size_t prior = at - 1;
            if (utf8) {
                while (prior > 0 && (source[prior] & 0xc0) == 0x80) --prior;
            }
            if (!is_word(source + prior)) break;
            at = prior;
        }
    } else {
        at = preferred;
        while (at < source_bytes && source[at] && !is_word(source + at)) {
            ++at;
            if (utf8) {
                while (at < source_bytes && (source[at] & 0xc0) == 0x80) ++at;
            }
        }
    }
    return at;
}

} // namespace source_match
