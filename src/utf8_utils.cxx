/*
Copyright (c) 2020-21 Project re-Isearch and its contributors: See CONTRIBUTORS.
It is made available and licensed under the Apache 2.0 license: see LICENSE
*/
#include <cctype>
#include <string.h>

#include "common.hxx"
#include "string.hxx"
#include "ctype.hxx"
#include "utf8_utils.hxx"


typedef struct {
        unsigned char mask;    /* char data will be bitwise AND with this */
        unsigned char lead;    /* start bytes of current char in utf-8 encoded character */
        uint32_t beg; /* beginning of codepoint range */
        uint32_t end; /* end of codepoint range */
        int bits_stored; /* the number of bits from the codepoint that fits in char */
}utf_t; 
                
const utf_t utf[] = {
        /*             mask        lead        beg      end       bits */
        {0b00111111, 0b10000000, 0,       0,        6    },
        {0b01111111, 0b00000000, 0000,    0177,     7    },
        {0b00011111, 0b11000000, 0200,    03777,    5    },
        {0b00001111, 0b11100000, 04000,   0177777,  4    },
        {0b00000111, 0b11110000, 0200000, 04177777, 3    },
        {0,0,0,0,0}
};


static int utf8_len(const char ch)
{
#if 1
    if ((ch & 0x80) == 0x00) return 1; // ASCII
    if ((ch & 0xE0) == 0xC0) return 2;
    if ((ch & 0xF0) == 0xE0) return 3;
    if ((ch & 0xF8) == 0xF0) return 4;
    return 0; // continuation byte or invalid lead
#else
  int len = 0;
  for(utf_t **u = (utf_t **)&utf; *u; ++u) {
    if((ch & ~(*u)->mask) == (*u)->lead) break;
    ++len;
   }
  if(len > 4) len = -1; // Mailformed leading byte
  return len;
#endif
}



uint32_t to_ucs4(const unsigned char *chr, uint32_t *cp)
{
  uint32_t codep = 0;
  const int bytes = chr ? utf8_len(*chr) : 0;

 if (bytes > 0)
    {
      int shift = utf[0].bits_stored * (bytes - 1);
      codep = (*chr++ & utf[bytes].mask) << shift;

      for(int i = 1; i < bytes; ++i, ++chr) {
        shift -= utf[0].bits_stored;
        codep |= ((char)*chr & utf[0].mask) << shift;
      }
  }
  if (cp) *cp = codep;
  return codep;
}




#if 0
// ---------------------------------------------------------------------------
// _utf8_codepoint_len
//   Return the byte length of the UTF-8 sequence starting at *p,
//   or 1 for any invalid / continuation byte (safe forward-walk).
// ---------------------------------------------------------------------------
inline int _utf8_codepoint_len(const unsigned char *p)
{
    if      ((*p & 0x80) == 0x00) return 1; // 0xxxxxxx  ASCII
    else if ((*p & 0xe0) == 0xc0) return 2; // 110xxxxx
    else if ((*p & 0xf0) == 0xe0) return 3; // 1110xxxx
    else if ((*p & 0xf8) == 0xf0) return 4; // 11110xxx
    return 1; // continuation or invalid — advance one byte
}
#endif

// ---------------------------------------------------------------------------
// _utf8_lower_cp
//   Copy the codepoint at *src into dst[], lowercase it, and return the
//   number of bytes written (== bytes consumed from src).
//   dst must have room for at least 5 bytes (4 + NUL sentinel).
//   The result is NOT NUL-terminated beyond the sentinel.
//
//   Relies on _utf_StrToLower's LENGTH-STABILITY CONTRACT: the lowercased
//   sequence is always the same byte length as the original, so the returned
//   len is valid for both advancing src and decoding dst via _utf8_to_ucs32.
//   If _utf_StrToLower ever emits a shorter sequence (padded with _SP per
//   its contract), this function will return the original src length and
//   _utf8_to_ucs32 will decode the true lowercased codepoint correctly since
//   the _SP padding bytes are continuation-safe trailing filler.
// ---------------------------------------------------------------------------
#ifndef _SP
#define _SP ' '
#endif

static inline int _utf8_lower_cp(const unsigned char *src, unsigned char dst[5])
{
    int len = _utf8_codepoint_len(src);
    for (int i = 0; i < len; i++) dst[i] = src[i];
    dst[len] = '\0';                          // temporary sentinel for _utf_StrToLower
    _utf_StrToLower((UCHR *)dst, false, len); // bounded to this one codepoint
    return len;
}

// ---------------------------------------------------------------------------
// _utf8_to_ucs32
//   Decode one UTF-8 sequence (already length-checked by _utf8_codepoint_len)
//   into its UCS-32 codepoint value.  p must point at a valid lead byte.
//   Invalid / truncated sequences return the raw lead byte (still non-zero,
//   still unique, so comparisons remain meaningful).
// ---------------------------------------------------------------------------
#if 0
static inline uint32_t _utf8_to_ucs32(const UCHR *p)
{
    uint32_t cp;
    int len = _utf8_codepoint_len(p);
    switch (len)
    {
    case 1:
        cp = (uint32_t)(*p & 0x7f);
        break;
    case 2:
        cp = ((uint32_t)(*p     & 0x1f) <<  6)
           |  (uint32_t)(*(p+1) & 0x3f);
        break;
    case 3:
        cp = ((uint32_t)(*p     & 0x0f) << 12)
           | ((uint32_t)(*(p+1) & 0x3f) <<  6)
           |  (uint32_t)(*(p+2) & 0x3f);
        break;
    case 4:
        cp = ((uint32_t)(*p     & 0x07) << 18)
           | ((uint32_t)(*(p+1) & 0x3f) << 12)
           | ((uint32_t)(*(p+2) & 0x3f) <<  6)
           |  (uint32_t)(*(p+3) & 0x3f);
        break;
    default:
        cp = (uint32_t)*p; // fallback: raw byte
        break;
    }
    return cp;
}
#endif

// ---------------------------------------------------------------------------
// _utf_strncasecmp
//
//   Compare at most n UTF-8 codepoints of term p1 against phrase/buffer p2,
//   case-insensitively, with the same whitespace- and punctuation-collapsing
//   semantics as the ASCII _strncasecmp.
//
//   Parameters
//     p1       – term (the pattern), scanned for exactly n codepoints
//     p2       – buffer / phrase being searched
//     n        – number of codepoints in p1 to match
//     look     – if non-NULL, set to true when p2 still sits on a term char
//                after the match (i.e. the match landed mid-term)
//     p2_bytes – if non-NULL and diff==0, receives the byte length consumed
//                from p2  (== p2_end - p2_start - 1, matching original)
//
//   Returns 0 on a case-insensitive match, non-zero otherwise.  On mismatch
//   the value is the signed UCS-32 codepoint difference (lo1 - lo2) at the
//   first diverging position, giving true Unicode ordering.  When p2 is
//   exhausted before n codepoints the full UCS-32 value of the next p1
//   codepoint is returned as a positive integer.
// ---------------------------------------------------------------------------
// Term, Buffer, Codepoint-count
INT _utf_strncasecmp(const UCHR *p1, const UCHR *p2, const INT n,
// Whether more term chars follow in p2 after the match, byte-length of p2 match
                                   bool *look, size_t *p2_bytes)
{
    const UCHR *p2_Start = p2;
    int         diff = 0;
    int         q    = 0;     // true when both sides are whitespace
    INT         x    = 0;     // codepoints consumed from p1

    while (*p1 && *p2)
    {
        // ---- classify the current positions --------------------------------
        // For whitespace / punctuation checks we only care about the lead byte;
        // multi-byte sequences are never whitespace or ASCII punctuation.
        int p1_ascii = ((*p1 & 0x80) == 0); // single-byte (ASCII) codepoint
        int p2_ascii = ((*p2 & 0x80) == 0);

        q = 0;

        if (p1_ascii && p2_ascii)
        {
            q = (IsTermWhite(*p1) && IsTermWhite(*p2));

            if (!q)
            {
                // Both punctuation collapsing: '.' or ','
                int p1_dot = (*p1 == '.' || *p1 == ',');
                int p2_dot = (*p2 == '.' || *p2 == ',');
                if (p1_dot && p2_dot)
                {
                    p1++; p2++; x++;
                    if (x >= n) break;
                    continue;
                }

                // Both non-term (but not whitespace and not dot): skip together
                if (!IsTermChar(*p1) && !IsTermChar(*p2))
                {
                    p1++; p2++; x++;
                    if (x >= n) break;
                    continue;
                }
            }
        }

        if (q)
        {
            // Collapse any run of whitespace on each side independently
            do { p2++; } while (IsTermWhite(*p2));
            do { p1++; x++; } while (x < n && IsTermWhite(*p1));
        }
        else
        {
            // General case: read one full codepoint from each side,
            // lowercase both, compare byte-by-byte.
            UCHR lo1[5], lo2[5];            int  len1 = _utf8_lower_cp(p1, lo1);
            int  len2 = _utf8_lower_cp(p2, lo2);

            // Compare as UCS-32 codepoints so the return value has real
            // Unicode ordering meaning, not an accident of UTF-8 byte layout.
            uint32_t ucs1 = _utf8_to_ucs32(lo1);
            uint32_t ucs2 = _utf8_to_ucs32(lo2);
            diff = (ucs1 > ucs2) ? (int)(ucs1 - ucs2) : -(int)(ucs2 - ucs1);

            if (diff != 0)
                break;

            p1 += len1;
            p2 += len2;
            x++;
        }

        if (x >= n)
            break;
    }

    // Did p1 run out before n codepoints but p2 still has content?
    if (diff == 0 && x < n && *p2 == '\0')
        diff = (int)_utf8_to_ucs32(p1); // p1 has more; full codepoint as mismatch signal

    if (look)     *look     = (bool)IsTermChar(*p2);
    if (diff == 0 && p2_bytes) *p2_bytes = (size_t)(p2 - p2_Start - 1);

    return (INT)diff;
}


// _ib_IsUTF8TermChr
//
// Returns non-zero if the codepoint at Buffer is a term character, applying
// the same dot-in-word logic as the 8-bit IsTermChar macro but correctly
// handling UTF-8 multi-byte sequences.
//
// Design notes:
//   - The CURRENT codepoint must be fully decoded to UCS-32 before testing
//     IsTermChar / _ib_isalnum, because those predicates operate on codepoint
//     values, not raw bytes.
//
//   - The DOT-IN-WORD lookahead (IsDotInWord / IsAfterDotChar) is intentionally
//     kept as a raw byte test.  Every character in every _ib_isdot signature is
//     ASCII (0x00-0x7f): '.', '_', '&', '@', '/', '-', ';', ':', '+'.
//     Similarly IsAfterDotChar calls _ib_isalpha which is ASCII-range.
//     A raw byte test is therefore correct and avoids a redundant decode.
//     If a future DOT_WORDS_SIGNATURE ever includes a non-ASCII separator,
//     those peek bytes must be decoded with _utf8_to_ucs32() instead.
//
//   - After the current codepoint we peek at the LEAD BYTE of the next
//     sequence (tcp[0]) and the byte after that (tcp[1]).  For ASCII
//     characters these are the complete codepoints.  For multi-byte
//     sequences tcp[0] will be a lead byte >= 0xc0, which is never a dot
//     or alpha in the ASCII sense, so IsDotInWord/IsAfterDotChar correctly
//     return false — no special handling needed.
//
//   - The two-level dot lookahead mirrors the original:
//       term  dot  alpha          → term char  (e.g. "3.14", "U.S.A")
//       term  dot  dot   alpha    → term char  (e.g. "e.g.")  [debatable but preserved]
//
int _ib_IsUTF8TermChr(const unsigned char *Buffer)
{
    if (!Buffer || !*Buffer)
        return 0;

    // Decode the current codepoint
    uint32_t cp;
    int bytes = to_ucs4(Buffer, &cp);
    if (!bytes)
        return 0;

    // Is the current codepoint itself a term character?
    if (IsTermChar(cp))
        return bytes;  // return byte length so caller can advance correctly

    // Not a plain term char — check dot-in-word:
    // current byte must be an ASCII dot-class character preceded by context
    // the CALLER knows about.  Here we check only the forward lookahead:
    // if the current position is a dot and what follows qualifies, we are
    // mid-term (caller has already confirmed a preceding term char).
    //
    // Lookahead bytes: since all dot/alpha chars are ASCII, tcp[0] and tcp[1]
    // are safe to read as raw bytes.
    const unsigned char *tcp = Buffer + bytes;  // next codepoint start

    if (IsDotInWord(Buffer[0]))
    {
        // Case 1: dot followed immediately by an alpha  → mid-term
        if (IsAfterDotChar(tcp[0]))
            return bytes;

        // Case 2: dot dot alpha  → mid-term (e.g. trailing "e.g.")
        if (IsDotInWord(tcp[0]) && IsAfterDotChar(tcp[1]))
            return bytes;
    }

    return 0;
}


// Convert a UTF-8 string (or buffer slice) to lower case in-place.
// If clean is true, control characters, combining diacritics, modifier
// letters, and non-Latin/Greek/Cyrillic script characters are replaced
// with a space (_SP) so the result is safe for plain-text indexing.
//
// length — byte length of the region to process.  Pass 0 to use the
//   NUL-terminated length (original behaviour).  When processing a
//   buffer slice that is not NUL-terminated, pass the exact byte count.
//   Always a byte count, never a codepoint count — callers with
//   byte-addressed buffers should not need to pre-scan to count codepoints.
//   Use your UTF-8 character-length function when codepoint alignment is
//   needed independently.
//
// LENGTH-STABILITY CONTRACT
//   Every uppercase→lowercase mapping in this function must produce a UTF-8
//   sequence of exactly the same byte length as the original.  This is
//   required because callers (indexers, _utf_strncasecmp) rely on byte
//   offsets into the original string remaining valid after lowercasing.
//   All current mappings satisfy this: they only mutate bytes within an
//   already-decoded sequence of fixed length (e.g. 0xce→0xcf is still a
//   2-byte lead, 0xe1 0x82→0xe1 0x83 is still a 3-byte sequence).
//
//   If a future script requires a mapping that changes sequence length
//   (e.g. a 3-byte uppercase to a 2-byte lowercase), the shorter result
//   MUST be padded with trailing _SP bytes to fill the original byte
//   footprint, preserving the byte offset of every subsequent codepoint.
//   Use _UTF8_LOWER_PAD() for that case:
//
//     _UTF8_LOWER_PAD(seq_start, new_len, old_len)
//       Fills bytes [new_len .. old_len-1] at seq_start with _SP.
//       seq_start must point to the lead byte of the original sequence.
#define _UTF8_LOWER_PAD(base, new_len, old_len) \
    do { for (int _i = (new_len); _i < (old_len); _i++) (base)[_i] = _SP; } while (0)


unsigned char *_utf_StrToLower(unsigned char *pString, const bool clean, unsigned length)
{
    if (pString && *pString) {
        unsigned char       *p   = pString;
        const unsigned char *end = length ? pString + length : NULL;
        unsigned char *pExtChar = 0;
        while (*p && (!end || p < end)) {
            if (*p < 0x20) {
                if (clean) *p = _SP; // Zap control chars
            } else if ((*p >= 0x41) && (*p <= 0x5a)) // US ASCII
                (*p) += 0x20;
            else if (*p > 0xc0) {
                pExtChar = p;
                p++;
                switch (*pExtChar) {
                case 0xc3: // Latin 1
                    if ((*p >= 0x80)
                        && (*p <= 0x9e)
                        && (*p != 0x97))
                        (*p) += 0x20; // US ASCII shift
                    break;
                case 0xc4: // Latin Extended
                    if ((*p >= 0x80)
                        && (*p <= 0xb7)
                        && (!(*p % 2))) // Even
                        (*p)++; // Next char is lwr
                    else if ((*p >= 0xb9)
                        && (*p <= 0xbe)
                        && (*p % 2)) // Odd
                        (*p)++; // Next char is lwr
                    else if (*p == 0xbf) {
                        *pExtChar = 0xc5;
                        (*p) = 0x80;
                    }
                    break;
                case 0xc5: // Latin Extended
                    if ((*p >= 0x80)
                        && (*p <= 0x88)
                        && (*p % 2)) // Odd
                        (*p)++; // Next char is lwr
                    else if ((*p >= 0x8a)
                        && (*p <= 0xb7)
                        && (!(*p % 2))) // Even
                        (*p)++; // Next char is lwr
                    else if ((*p >= 0xb9)
                        && (*p <= 0xbe)
                        && (*p % 2)) // Odd
                        (*p)++; // Next char is lwr
                    break;
                case 0xc6: // Latin Extended
                    switch (*p) {
                    case 0x82: case 0x84: case 0x87: case 0x8b: case 0x91:
                    case 0x98: case 0xa0: case 0xa2: case 0xa4: case 0xa7:
                    case 0xac: case 0xaf: case 0xb3: case 0xb5: case 0xb8:
                    case 0xbc:
                        (*p)++; // Next char is lwr
                        break;
                    default:
                        break;
                    }
                    break;
                case 0xc7: // Latin Extended
                    if (*p == 0x84)
                        (*p) = 0x86;
                    else if (*p == 0x85)
                        (*p)++; // Next char is lwr
                    else if (*p == 0x87)
                        (*p) = 0x89;
                    else if (*p == 0x88)
                        (*p)++; // Next char is lwr
                    else if (*p == 0x8a)
                        (*p) = 0x8c;
                    else if (*p == 0x8b)
                        (*p)++; // Next char is lwr
                    else if ((*p >= 0x8d)
                        && (*p <= 0x9c)
                        && (*p % 2)) // Odd
                        (*p)++; // Next char is lwr
                    else if ((*p >= 0x9e)
                        && (*p <= 0xaf)
                        && (!(*p % 2))) // Even
                        (*p)++; // Next char is lwr
                    else if (*p == 0xb1)
                        (*p) = 0xb3;
                    else if (*p == 0xb2)
                        (*p)++; // Next char is lwr
                    else if (*p == 0xb4)
                        (*p)++; // Next char is lwr
                    else if (*p == 0xb8)
                        (*p)++; // Next char is lwr
                    else if (*p == 0xba)
                        (*p)++; // Next char is lwr
                    else if (*p == 0xbc)
                        (*p)++; // Next char is lwr
                    else if (*p == 0xbe)
                        (*p)++; // Next char is lwr
                    break;
                case 0xc8: // Latin Extended
                    if ((*p >= 0x80)
                        && (*p <= 0x9f)
                        && (!(*p % 2))) // Even
                        (*p)++; // Next char is lwr
                    else if ((*p >= 0xa2)
                        && (*p <= 0xb3)
                        && (!(*p % 2))) // Even
                        (*p)++; // Next char is lwr
                    else if (*p == 0xbb)
                        (*p)++; // Next char is lwr
                    break;

                // Latin modifier letters (U+02C2..U+02FF range subset)
                case 0xcb:
                    if (clean && (
                        (*p >= 0x82 && *p <= 0x85) ||
                        (*p >= 0x92 && *p <= 0x9f) ||
                        (*p >= 0xa5 && *p <= 0xab) ||
                        (*p >= 0xaf && *p <= 0xbf)))
                        *pExtChar = *p = _SP;
                    break;

                // Combining Diacritical Marks (U+0300..U+036F)
                case 0xcc:
                    if (clean && (*p >= 0x80 && *p <= 0xbf))
                        *pExtChar = *p = _SP;
                    break;

                case 0xcd: // Greek & Coptic
                    switch (*p) {
                    case 0xb0:
                    case 0xb2:
                    case 0xb6:
                        (*p)++; // Next char is lwr
                        break;
                    default:
                        if (*p == 0xbf) {
                            *pExtChar = 0xcf;
                            (*p) = 0xb3;
                        }
                        break;
                    }
                    break;
                case 0xce: // Greek & Coptic
                    if (*p == 0x86)
                        (*p) = 0xac;
                    else if (*p == 0x88)
                        (*p) = 0xad;
                    else if (*p == 0x89)
                        (*p) = 0xae;
                    else if (*p == 0x8a)
                        (*p) = 0xaf;
                    else if (*p == 0x8c) {
                        *pExtChar = 0xcf;
                        (*p) = 0x8c;
                    }
                    else if (*p == 0x8e) {
                        *pExtChar = 0xcf;
                        (*p) = 0x8d;
                    }
                    else if (*p == 0x8f) {
                        *pExtChar = 0xcf;
                        (*p) = 0x8e;
                    }
                    else if ((*p >= 0x91)
                        && (*p <= 0x9f))
                        (*p) += 0x20; // US ASCII shift
                    else if ((*p >= 0xa0)
                        && (*p <= 0xab)
                        && (*p != 0xa2)) {
                        *pExtChar = 0xcf;
                        (*p) -= 0x20;
                    }
                    else if (clean) {
                        switch (*p) {
                        // Zap Greek diacritics (Tonos, Dialytika, Perispomeni)
                        case 0x84: // Greek Tonos
                        case 0x85: // Greek Dialytika Tonos
                        case 0x87: // Greek Perispomeni (middle dot)
                            *pExtChar = *p = _SP;
                            break;
                        }
                    }
                    break;
                case 0xcf: // Greek & Coptic
                    if (*p == 0x8f)
                        (*p) = 0xb4;
                    else if (*p == 0x91)
                        (*p)++; // Next char is lwr
                    else if ((*p >= 0x98)
                        && (*p <= 0xaf)
                        && (!(*p % 2))) // Even
                        (*p)++; // Next char is lwr
                    else if (*p == 0xb4)
                        (*p) = 0x91;
                    else if (*p == 0xb7)
                        (*p)++; // Next char is lwr
                    else if (*p == 0xb9)
                        (*p) = 0xb2;
                    else if (*p == 0xbb)
                        (*p)++; // Next char is lwr
                    else if (*p == 0xbd) {
                        *pExtChar = 0xcd;
                        (*p) = 0xbb;
                    }
                    else if (*p == 0xbe) {
                        *pExtChar = 0xcd;
                        (*p) = 0xbc;
                    }
                    else if (*p == 0xbf) {
                        *pExtChar = 0xcd;
                        (*p) = 0xbd;
                    }
                    // Greek Reversed Lunate Epsilon Symbol
                    else if (clean && *p == 0xb6)
                        *pExtChar = (*p) = _SP;
                    break;
                case 0xd0: // Cyrillic
                    if ((*p >= 0x80)
                        && (*p <= 0x8f)) {
                        *pExtChar = 0xd1;
                        (*p) += 0x10;
                    }
                    else if ((*p >= 0x90)
                        && (*p <= 0x9f))
                        (*p) += 0x20; // US ASCII shift
                    else if ((*p >= 0xa0)
                        && (*p <= 0xaf)) {
                        *pExtChar = 0xd1;
                        (*p) -= 0x20;
                    }
                    break;
                case 0xd1: // Cyrillic supplement
                    if ((*p >= 0xa0)
                        && (*p <= 0xbf)
                        && (!(*p % 2))) // Even
                        (*p)++; // Next char is lwr
                    break;
                case 0xd2: // Cyrillic supplement
                    if (*p == 0x80)
                        (*p)++; // Next char is lwr
                    else if ((*p >= 0x8a)
                        && (*p <= 0xbf)
                        && (!(*p % 2))) // Even
                        (*p)++; // Next char is lwr
                    break;
                case 0xd3: // Cyrillic supplement
                    if ((*p >= 0x81)
                        && (*p <= 0x8e)
                        && (*p % 2)) // Odd
                        (*p)++; // Next char is lwr
                    else if ((*p >= 0x90)
                        && (*p <= 0xbf)
                        && (!(*p % 2))) // Even
                        (*p)++; // Next char is lwr
                    break;
                case 0xd4: // Cyrillic supplement & Armenian
                    if ((*p >= 0x80)
                        && (*p <= 0xaf)
                        && (!(*p % 2))) // Even
                        (*p)++; // Next char is lwr
                    else if ((*p >= 0xb1)
                        && (*p <= 0xbf)) {
                        *pExtChar = 0xd5;
                        (*p) -= 0x10;
                    }
                    break;
                case 0xd5: // Armenian
                    if ((*p >= 0x80)
                        && (*p <= 0x96)
                        && (!(*p % 2))) // Even
                        (*p)++; // Next char is lwr
                    break;

                // More symbol / non-alpha blocks (clean only)
                case 0xdc: // Syriac Supplement / misc
                    if (clean && (*p >= 0x80 && *p <= 0x8f))
                        *pExtChar = *p = _SP;
                    break;
                case 0xdd: // Thaana / misc
                    if (clean && (*p >= 0x80 && *p <= 0x8a))
                        *pExtChar = *p = _SP;
                    break;
                case 0xdf: // NKo block
                    // Zap combining tones through NKo exclamation mark,
                    // but keep high/low tone apostrophes (U+07F4, U+07F5)
                    if (clean && (*p >= 0xab && *p <= 0xb9) &&
                        *p != 0xb4 && *p != 0xb5)
                        *pExtChar = *p = _SP;
                    break;

                // 3-byte sequences for Indic & other non-Latin scripts.
                // In clean mode the entire script block is zapped to space;
                // the pointer already sits on byte 2 of 3 after the outer p++,
                // so we advance once more to consume byte 3 before zapping.
                case 0xe0:
                    pExtChar = p;
                    p++;
                    if (clean) {
                        switch (*pExtChar) {
                        case 0xa0: // Samaritan
                        case 0xa1: // Mandaic
                        case 0xa3: // Syriac Supplement
                        case 0xa4: // Devanagari Extended
                        case 0xa5: // Devanagari
                        case 0xa6: // Bengali
                        case 0xa7: // Gurmukhi
                        case 0xa8: // Gujarati
                        case 0xa9: // Oriya
                        case 0xaa: // Tamil
                        case 0xab: // Telugu
                        case 0xac: // Kannada
                        case 0xad: // Malayalam
                        case 0xae: // Sinhala
                        case 0xaf: // Thai
                        case 0xb0: // Lao
                        case 0xb1: // Tibetan
                        case 0xb2: // Myanmar
                        case 0xb3: // Georgian (old range)
                        case 0xb4: // Hangul Jamo
                        case 0xb5: // Ethiopic
                        case 0xb6: // Ethiopic Supplement
                        case 0xb7: // Cherokee
                        case 0xb8: // Unified Canadian Aboriginal Syllabics
                        case 0xb9: // Ogham
                        case 0xba: // Runic
                        case 0xbb: // Tagalog / Hanunoo / Buhid / Tagbanwa
                        case 0xbc: // Khmer
                        case 0xbd: // Mongolian
                        case 0xbe: // Unified Canadian Aboriginal Syllabics Ext
                        case 0xbf: // Limbu / Tai Le
                            // Zap all three bytes: pExtChar-1 (0xe0), pExtChar, p
                            *(pExtChar - 1) = *pExtChar = *p = _SP;
                            break;
                        default:
                            break;
                        }
                    }
                    break;

                case 0xe1: // Three byte code — Latin/Greek extended, Georgian, etc.
                    pExtChar = p;
                    p++;
                    switch (*pExtChar) {
                    case 0x82: // Georgian
                        if ((*p >= 0xa0)
                            && (*p <= 0xbf)) {
                            *pExtChar = 0x83;
                            (*p) -= 0x10;
                        }
                        break;
                    case 0x83: // Georgian
                        if ((*p >= 0x80)
                            && ((*p <= 0x85)
                                || (*p == 0x87))
                            || (*p == 0x8d))
                            (*p) += 0x30;
                        break;
                    case 0xb8: // Latin Extended Additional
                        if ((*p >= 0x80)
                            && (*p <= 0xbf)
                            && (!(*p % 2))) // Even
                            (*p)++; // Next char is lwr
                        break;
                    case 0xb9: // Latin Extended Additional
                        if ((*p >= 0x80)
                            && (*p <= 0xbf)
                            && (!(*p % 2))) // Even
                            (*p)++; // Next char is lwr
                        break;
                    case 0xba: // Latin Extended Additional
                        if ((*p >= 0x80)
                            && (*p <= 0x94)
                            && (!(*p % 2))) // Even
                            (*p)++; // Next char is lwr
                        else if ((*p >= 0x9e)
                            && (*p <= 0xbf)
                            && (!(*p % 2))) // Even
                            (*p)++; // Next char is lwr
                        break;
                    case 0xbb: // Latin Extended Additional
                        if ((*p >= 0x80)
                            && (*p <= 0xbf)
                            && (!(*p % 2))) // Even
                            (*p)++; // Next char is lwr
                        break;
                    case 0xbc: // Greek Extended
                        if ((*p >= 0x88)
                            && (*p <= 0x8f))
                            (*p) -= 0x08;
                        else if ((*p >= 0x98)
                            && (*p <= 0x9f))
                            (*p) -= 0x08;
                        else if ((*p >= 0xa8)
                            && (*p <= 0xaf))
                            (*p) -= 0x08;
                        else if ((*p >= 0xb8)  // FIX: was (*p <= 0x8f) — impossible range
                            && (*p <= 0xbf))
                            (*p) -= 0x08;
                        break;
                    case 0xbd: // Greek Extended
                        if ((*p >= 0x88)
                            && (*p <= 0x8d))
                            (*p) -= 0x08;
                        else if ((*p >= 0x98)
                            && (*p <= 0x9f))
                            (*p) -= 0x08;
                        else if ((*p >= 0xa8)
                            && (*p <= 0xaf))
                            (*p) -= 0x08;
                        else if ((*p >= 0xb8)  // FIX: was (*p <= 0x8f) — impossible range
                            && (*p <= 0xbf))
                            (*p) -= 0x08;
                        break;
                    case 0xbe: // Greek Extended
                        if ((*p >= 0x88)
                            && (*p <= 0x8f))
                            (*p) -= 0x08;
                        else if ((*p >= 0x98)
                            && (*p <= 0x9f))
                            (*p) -= 0x08;
                        else if ((*p >= 0xa8)
                            && (*p <= 0xaf))
                            (*p) -= 0x08;
                        else if ((*p >= 0xb8)
                            && (*p <= 0xb9))
                            (*p) -= 0x08;
                        break;
                    case 0xbf: // Greek Extended
                        if ((*p >= 0x88)
                            && (*p <= 0x8c))
                            (*p) -= 0x08;
                        else if ((*p >= 0x98)
                            && (*p <= 0x9b))
                            (*p) -= 0x08;
                        else if ((*p >= 0xa8)
                            && (*p <= 0xac))
                            (*p) -= 0x08;
                        break;
                    default:
                        break;
                    }
                    break;

                case 0xf0: // Four byte code
                    pExtChar = p;
                    p++;
                    switch (*pExtChar) {
                    case 0x90:
                        pExtChar = p;
                        p++;
                        switch (*pExtChar) {
                        case 0x92: // Osage uppercase
                            if ((*p >= 0xb0)
                                && (*p <= 0xbf)) {
                                *pExtChar = 0x93;
                                (*p) -= 0x18;
                            }
                            break;
                        case 0x93: // Osage lowercase range
                            if ((*p >= 0x80)
                                && (*p <= 0x93))
                                (*p) += 0x18;
                            break;
                        default:
                            break;
                        }
                        break;
                    case 0x9e: // FIX: was stray case outside this switch
                        pExtChar = p;
                        p++;
                        switch (*pExtChar) {
                        case 0xa4: // Adlam uppercase
                            if ((*p >= 0x80)
                                && (*p <= 0xa1))
                                (*p) += 0x22;
                            break;
                        default:
                            break;
                        }
                        break;
                    default:
                        break;
                    }
                    break;

                default:
                    break;
                }
                pExtChar = 0;
            }
            p++;
        }
    }
    return pString;
}


// Convert a UTF-8 string (or buffer slice) to upper case in-place.
//
// length — byte length of the region to process.  Pass 0 to use the
//   NUL-terminated length.  Always a byte count, never a codepoint count.
//
// LENGTH-STABILITY CONTRACT (identical to _utf_StrToLower)
//   Every lowercase→uppercase mapping must produce a UTF-8 sequence of
//   exactly the same byte length as the original.  All current mappings
//   satisfy this.  Use _UTF8_LOWER_PAD() if a future mapping ever
//   produces a shorter sequence.
//
// Note: there is no 'clean' parameter.  Cleaning (diacritic removal,
//   script zapping) is a normalisation concept tied to indexing, which
//   always works on lowercased data.  Uppercasing is a display/output
//   operation; stripping characters there would be destructive and wrong.
unsigned char *_utf_StrToUpper(unsigned char *pString, unsigned length)
{
    if (pString && *pString) {
        unsigned char       *p   = pString;
        const unsigned char *end = length ? pString + length : NULL;
        unsigned char       *pExtChar = 0;

        while (*p && (!end || p < end)) {
            if ((*p >= 0x61) && (*p <= 0x7a)) // US ASCII a-z
                (*p) -= 0x20;
            else if (*p > 0xc0) {
                pExtChar = p;
                p++;
                switch (*pExtChar) {
                case 0xc3: // Latin 1 — lower is 0xa0..0xbe (+0x20 from upper)
                    if ((*p >= 0xa0)
                        && (*p <= 0xbe)
                        && (*p != 0xb7))    // 0xb7 is × (multiply), not a letter
                        (*p) -= 0x20;
                    break;

                case 0xc4: // Latin Extended
                    // Lower was even→even+1; upper is odd→odd-1
                    if ((*p >= 0x81)
                        && (*p <= 0xb8)
                        && (*p % 2))        // Odd → prev char is upr
                        (*p)--;
                    else if ((*p >= 0xba)
                        && (*p <= 0xbf)
                        && (!(*p % 2)))     // Even
                        (*p)--;
                    break;

                case 0xc5: // Latin Extended
                    if (*p == 0x80) {       // ĸ → 0xc4 0xbf (Ŀ) cross-block
                        *pExtChar = 0xc4;
                        (*p) = 0xbf;
                    }
                    else if ((*p >= 0x81)
                        && (*p <= 0x89)
                        && (!(*p % 2)))     // Even
                        (*p)--;
                    else if ((*p >= 0x8b)
                        && (*p <= 0xb8)
                        && (*p % 2))        // Odd
                        (*p)--;
                    else if ((*p >= 0xba)
                        && (*p <= 0xbf)
                        && (!(*p % 2)))     // Even
                        (*p)--;
                    break;

                case 0xc6: // Latin Extended — reverse the (*p)++ list
                    switch (*p) {
                    case 0x83: case 0x85: case 0x88: case 0x8c: case 0x92:
                    case 0x99: case 0xa1: case 0xa3: case 0xa5: case 0xa8:
                    case 0xad: case 0xb0: case 0xb4: case 0xb6: case 0xb9:
                    case 0xbd:
                        (*p)--;             // Prev char is upr
                        break;
                    default:
                        break;
                    }
                    break;

                case 0xc7: // Latin Extended
                    // Titlecase triples: upr=0x84, title=0x85, lwr=0x86
                    //                   upr=0x87, title=0x88, lwr=0x89
                    //                   upr=0x8a, title=0x8b, lwr=0x8c
                    if (*p == 0x86)
                        (*p) = 0x84;        // lwr→upr (skip title)
                    else if (*p == 0x85)
                        (*p)--;             // title→upr
                    else if (*p == 0x89)
                        (*p) = 0x87;
                    else if (*p == 0x88)
                        (*p)--;
                    else if (*p == 0x8c)
                        (*p) = 0x8a;
                    else if (*p == 0x8b)
                        (*p)--;
                    else if ((*p >= 0x8e)
                        && (*p <= 0x9d)
                        && (!(*p % 2)))     // Even → prev is upr
                        (*p)--;
                    else if ((*p >= 0x9f)
                        && (*p <= 0xb0)
                        && (*p % 2))        // Odd
                        (*p)--;
                    else if (*p == 0xb3)
                        (*p) = 0xb1;
                    else if (*p == 0xb3)    // DZ → Dz handled above; DZ is 0xb1
                        (*p)--;
                    else if (*p == 0xb5)
                        (*p)--;
                    else if (*p == 0xb9)
                        (*p)--;
                    else if (*p == 0xbb)
                        (*p)--;
                    else if (*p == 0xbd)
                        (*p)--;
                    else if (*p == 0xbf)
                        (*p)--;
                    break;

                case 0xc8: // Latin Extended
                    if ((*p >= 0x81)
                        && (*p <= 0xa0)
                        && (*p % 2))        // Odd
                        (*p)--;
                    else if ((*p >= 0xa3)
                        && (*p <= 0xb4)
                        && (*p % 2))        // Odd
                        (*p)--;
                    else if (*p == 0xbc)
                        (*p)--;
                    break;

                case 0xcd: // Greek & Coptic
                    switch (*p) {
                    case 0xb1:              // lwr of 0xb0
                    case 0xb3:              // lwr of 0xb2
                    case 0xb7:              // lwr of 0xb6
                        (*p)--;
                        break;
                    default:
                        break;
                    }
                    // 0xcf 0xb3 (ϳ) → 0xcd 0xbf: handled in 0xcf case below
                    break;

                case 0xce: // Greek & Coptic
                    if (*p == 0xac)
                        (*p) = 0x86;
                    else if (*p == 0xad)
                        (*p) = 0x88;
                    else if (*p == 0xae)
                        (*p) = 0x89;
                    else if (*p == 0xaf)
                        (*p) = 0x8a;
                    else if ((*p >= 0xb1)
                        && (*p <= 0xbf))    // α..ο → Α..Ο
                        (*p) -= 0x20;
                    break;

                case 0xcf: // Greek & Coptic
                    if (*p == 0x8c) {       // ό → 0xce 0x8c
                        *pExtChar = 0xce;
                        (*p) = 0x8c;
                    }
                    else if (*p == 0x8d) {  // ύ → 0xce 0x8e
                        *pExtChar = 0xce;
                        (*p) = 0x8e;
                    }
                    else if (*p == 0x8e) {  // ώ → 0xce 0x8f
                        *pExtChar = 0xce;
                        (*p) = 0x8f;
                    }
                    else if ((*p >= 0x80)   // π..ϟ range back to 0xce 0xa0..0xab
                        && (*p <= 0x8b)
                        && (*p != 0x82)) {
                        *pExtChar = 0xce;
                        (*p) += 0x20;
                    }
                    else if (*p == 0x91)    // ϑ → Θ (0xcf 0x91 lwr of 0xcf 0x90? no:
                        (*p)--;             //   lwr incremented 0x91, so upr is 0x90)
                    else if (*p == 0xb4)    // ϴ (theta) — lwr set to 0x91, upr was 0x8f
                        (*p) = 0x8f;        //   reverse: 0xb4→0x8f
                    else if ((*p >= 0x99)
                        && (*p <= 0xb0)
                        && (*p % 2))        // Odd
                        (*p)--;
                    else if (*p == 0xb2)    // ϲ → Ϲ
                        (*p) = 0xb9;        //   reverse of 0xb9→0xb2
                    else if (*p == 0xb3) {  // ϳ → Ϳ (0xcd 0xbf)
                        *pExtChar = 0xcd;
                        (*p) = 0xbf;
                    }
                    else if (*p == 0xbc) {  // ϼ → 0xcd 0xbb
                        *pExtChar = 0xcd;
                        (*p) = 0xbb;
                    }
                    else if (*p == 0xbd) {  // → 0xcd 0xbc
                        *pExtChar = 0xcd;
                        (*p) = 0xbc;
                    }
                    else if (*p == 0xbe) {  // → 0xcd 0xbd
                        *pExtChar = 0xcd;
                        (*p) = 0xbd;
                    }
                    else if (*p == 0xbc)
                        (*p)--;
                    else if (*p == 0xbe)
                        (*p)--;
                    break;

                case 0xd0: // Cyrillic
                    // Lower: 0xd1 0x90..0x9f → 0xd0 0x80..0x8f  (reverse: +0x10 on p)
                    //        0xd0 0xb0..0xbf → 0xd0 0x90..0x9f  (reverse: -0x20)
                    //        0xd1 0x80..0x8f → 0xd0 0xa0..0xaf  (reverse: handled in 0xd1)
                    if ((*p >= 0xb0)
                        && (*p <= 0xbf))
                        (*p) -= 0x20;
                    break;

                case 0xd1: // Cyrillic
                    if ((*p >= 0x80)
                        && (*p <= 0x8f)) {  // 0xd1 0x80..0x8f → 0xd0 0x80..0x8f upr
                        *pExtChar = 0xd0;
                        (*p) -= 0x10;
                    }
                    else if ((*p >= 0x90)
                        && (*p <= 0x9f)) {  // 0xd1 0x90..0x9f → 0xd0 0xa0..0xaf upr
                        *pExtChar = 0xd0;
                        (*p) += 0x20;
                    }
                    else if ((*p >= 0xa1)
                        && (*p <= 0xbf)
                        && (*p % 2))        // Odd → prev is upr
                        (*p)--;
                    break;

                case 0xd2: // Cyrillic supplement
                    if (*p == 0x81)
                        (*p)--;
                    else if ((*p >= 0x8b)
                        && (*p <= 0xbf)
                        && (*p % 2))        // Odd
                        (*p)--;
                    break;

                case 0xd3: // Cyrillic supplement
                    if ((*p >= 0x82)
                        && (*p <= 0x8f)
                        && (!(*p % 2)))     // Even
                        (*p)--;
                    else if ((*p >= 0x91)
                        && (*p <= 0xbf)
                        && (*p % 2))        // Odd
                        (*p)--;
                    break;

                case 0xd4: // Cyrillic supplement & Armenian
                    if ((*p >= 0x81)
                        && (*p <= 0xb0)
                        && (*p % 2))        // Odd
                        (*p)--;
                    break;

                case 0xd5: // Armenian
                    // Lower: 0xd4 0xb1..0xbf → 0xd5 0xa1..0xaf (+0x10 on d4 block)
                    // Also:  0xd5 0x81..0x97 odd → prev (even) is upr
                    if ((*p >= 0xa1)
                        && (*p <= 0xaf)) {  // lwr → 0xd4 0xb1..0xbf
                        *pExtChar = 0xd4;
                        (*p) += 0x10;
                    }
                    else if ((*p >= 0x81)
                        && (*p <= 0x97)
                        && (*p % 2))        // Odd
                        (*p)--;
                    break;

                case 0xe1: // Three byte code
                    pExtChar = p;
                    p++;
                    switch (*pExtChar) {
                    case 0x82: // Georgian — lwr was 0x83; upr is 0x82
                        // Lower moved 0xe1 0x82 0xa0..0xbf → 0xe1 0x83 0x90..0xaf
                        // Reverse: 0xe1 0x83 0x90..0xaf → 0xe1 0x82 0xa0..0xbf
                        // (handled in case 0x83 below)
                        break;
                    case 0x83: // Georgian lowercase → uppercase
                        if ((*p >= 0x90)
                            && (*p <= 0xaf)) {
                            *pExtChar = 0x82;
                            (*p) += 0x10;
                        }
                        // Also Georgian Mkhedruli 0x80..0x85, 0x87, 0x8d
                        // Lower added 0x30; upper subtracts 0x30
                        else if (((*p >= 0xb0)
                            && (*p <= 0xb5))
                            || (*p == 0xb7)
                            || (*p == 0xbd))
                            (*p) -= 0x30;
                        break;
                    case 0xb8: // Latin Extended Additional — odd → upr (even)
                        if ((*p >= 0x81)
                            && (*p <= 0xbf)
                            && (*p % 2))
                            (*p)--;
                        break;
                    case 0xb9: // Latin Extended Additional
                        if ((*p >= 0x81)
                            && (*p <= 0xbf)
                            && (*p % 2))
                            (*p)--;
                        break;
                    case 0xba: // Latin Extended Additional
                        if ((*p >= 0x81)
                            && (*p <= 0x95)
                            && (*p % 2))
                            (*p)--;
                        else if ((*p >= 0x9f)
                            && (*p <= 0xbf)
                            && (*p % 2))
                            (*p)--;
                        break;
                    case 0xbb: // Latin Extended Additional
                        if ((*p >= 0x81)
                            && (*p <= 0xbf)
                            && (*p % 2))
                            (*p)--;
                        break;
                    case 0xbc: // Greek Extended — lwr -= 0x08, so upr += 0x08
                        if ((*p >= 0x80)
                            && (*p <= 0x87))
                            (*p) += 0x08;
                        else if ((*p >= 0x90)
                            && (*p <= 0x97))
                            (*p) += 0x08;
                        else if ((*p >= 0xa0)
                            && (*p <= 0xa7))
                            (*p) += 0x08;
                        else if ((*p >= 0xb0)
                            && (*p <= 0xb7))
                            (*p) += 0x08;
                        break;
                    case 0xbd: // Greek Extended
                        if ((*p >= 0x80)
                            && (*p <= 0x85))
                            (*p) += 0x08;
                        else if ((*p >= 0x90)
                            && (*p <= 0x97))
                            (*p) += 0x08;
                        else if ((*p >= 0xa0)
                            && (*p <= 0xa7))
                            (*p) += 0x08;
                        else if ((*p >= 0xb0)
                            && (*p <= 0xb7))
                            (*p) += 0x08;
                        break;
                    case 0xbe: // Greek Extended
                        if ((*p >= 0x80)
                            && (*p <= 0x87))
                            (*p) += 0x08;
                        else if ((*p >= 0x90)
                            && (*p <= 0x97))
                            (*p) += 0x08;
                        else if ((*p >= 0xa0)
                            && (*p <= 0xa7))
                            (*p) += 0x08;
                        else if ((*p >= 0xb0)
                            && (*p <= 0xb1))
                            (*p) += 0x08;
                        break;
                    case 0xbf: // Greek Extended
                        if ((*p >= 0x80)
                            && (*p <= 0x84))
                            (*p) += 0x08;
                        else if ((*p >= 0x90)
                            && (*p <= 0x93))
                            (*p) += 0x08;
                        else if ((*p >= 0xa0)
                            && (*p <= 0xa4))
                            (*p) += 0x08;
                        break;
                    default:
                        break;
                    }
                    break;

                case 0xf0: // Four byte code
                    pExtChar = p;
                    p++;
                    switch (*pExtChar) {
                    case 0x90:
                        pExtChar = p;
                        p++;
                        switch (*pExtChar) {
                        case 0x92: // Osage — lwr was 0x93 low; upr is 0x92 high
                            // Lower: 0x90 0x93 0x98..0xab → 0x90 0x92 0xb0..0xbf
                            // (handled in 0x93 case below)
                            break;
                        case 0x93: // Osage lowercase → uppercase
                            if ((*p >= 0x98)
                                && (*p <= 0xab)) {
                                *pExtChar = 0x92;
                                (*p) += 0x18;
                            }
                            break;
                        default:
                            break;
                        }
                        break;
                    case 0x9e:
                        pExtChar = p;
                        p++;
                        switch (*pExtChar) {
                        case 0xa4: // Adlam — lwr added 0x22; upr subtracts 0x22
                            if ((*p >= 0xa2)
                                && (*p <= 0xc3))
                                (*p) -= 0x22;
                            break;
                        default:
                            break;
                        }
                        break;
                    default:
                        break;
                    }
                    break;

                default:
                    break;
                }
                pExtChar = 0;
            }
            p++;
        }
    }
    return pString;
}



// Convert a UTF-8 buffer slice to lower case in-place. Buffer-length
// variant for the word parser: pString is NOT NUL-terminated and MUST
// NOT be read past pString[length-1]. There is no *p/*pString sentinel
// check anywhere in this function — length is the only end-of-data
// signal. Callers that still have a NUL-terminated C string should keep
// using _utf_StrToLower(); this variant exists specifically for slices
// carved out of a larger buffer (e.g. word-parser spans) where a NUL
// byte may not exist at all, or where a stray 0x00 inside the slice
// must NOT be treated as a terminator.
//
// EMBEDDED NUL BYTES ARE DATA, NOT A TERMINATOR
//   Unlike the original, this variant may be handed a buffer that
//   already has 0x00 bytes inside [pString, pString+length) — e.g. once
//   ParseWords' word-separator zapping is merged in to save a pass,
//   IsWordSep()/!IsTermChr() positions are zapped to '\0' rather than
//   left as their original byte. 0x00 is never treated as a stop
//   condition anywhere in the loop or in any of the byte-value
//   comparisons below (0x00 simply fails every UTF-8 lead/continuation
//   range test, the same way any other non-matching byte would).
//
// length — byte length of the region to process. Mandatory, always a
//   byte count, never a codepoint count. Zero-length is a valid no-op.
//   This is an exact length, not an upper bound on some larger
//   NUL-terminated string: contrast with _utf_StrToLower()'s optional
//   length, which the caller guarantees is <= strlen(pString).
//
// ZapChr — the fill byte written in "clean" mode wherever the original
//   hardcoded space (_SP). Pass _SP for the historic behaviour (safe
//   for plain-text indexing), or '\0' once this routine is merged with
//   ParseWords' tokenization pass, so cleaning and word-separator
//   zapping agree on one sentinel value instead of needing a second
//   pass to convert one into the other.
//
// OUT-OF-BOUNDS SAFETY CONTRACT
//   Every lookahead byte (2nd/3rd/4th byte of a multi-byte sequence) is
//   guarded by a "p < end" check before it is ever dereferenced. If a
//   multi-byte sequence is truncated by the end of the slice (i.e. the
//   lead byte is the last byte in [pString, pString+length)), that
//   trailing partial sequence is left untouched rather than read past
//   the boundary. This differs from _utf_StrToLower(), which relies on
//   the NUL terminator to make the equivalent unguarded reads safe.
//
// LENGTH-STABILITY CONTRACT
//   Every uppercase→lowercase mapping in this function must produce a UTF-8
//   sequence of exactly the same byte length as the original.  This is
//   required because callers (indexers, _utf_strncasecmp) rely on byte
//   offsets into the original string remaining valid after lowercasing.
//   All current mappings satisfy this: they only mutate bytes within an
//   already-decoded sequence of fixed length (e.g. 0xce→0xcf is still a
//   2-byte lead, 0xe1 0x82→0xe1 0x83 is still a 3-byte sequence).
//
//   If a future script requires a mapping that changes sequence length
//   (e.g. a 3-byte uppercase to a 2-byte lowercase), the shorter result
//   MUST be padded with trailing ZapChr bytes to fill the original byte
//   footprint, preserving the byte offset of every subsequent codepoint.
//   Use _UTF8_LOWER_PAD() for that case:
//
//     _UTF8_LOWER_PAD(seq_start, new_len, old_len, fill)
//       Fills bytes [new_len .. old_len-1] at seq_start with fill (pass
//       ZapChr from the caller, not a hardcoded _SP).
//       seq_start must point to the lead byte of the original sequence.
// #define _UTF8_LOWER_PAD(base, new_len, old_len, fill) \
//    do { for (int _i = (new_len); _i < (old_len); _i++) (base)[_i] = (fill); } while (0)

// _IB_UTF8_SILENT_ZAP
//
// A SECOND, fixed sentinel distinct from the caller-supplied ZapChr.
// ZapChr marks genuine word-breaking separators (space, punctuation,
// symbols) -- content the tokenizer should treat as ending a word.
// _IB_UTF8_SILENT_ZAP marks content that is zapped for the same reason
// (it's not a letter/digit) but must NOT break a word: combining marks
// (Unicode category Mn/Me -- accents, niqqud, tashkeel, and anything
// else that's logically attached to the letter it modifies rather than
// a character in its own right) and format characters (Cf -- invisible
// joiners/marks). "שלום" with niqqud, or "café" typed as e + combining
// acute, must fold to one continuous term with the mark silently
// removed, not fracture into fragments at every stripped mark.
//
// Fixed rather than caller-configurable because, unlike ZapChr (which
// legitimately differs by context -- ' ' for plain-text indexing, '\0'
// once merged with tokenization), this sentinel's meaning never changes:
// it always means "skip this byte, do not end the word". Chosen as 0x01
// (SOH) specifically because it can never collide with either value
// ZapChr is actually used with in this codebase (' ' or '\0'); if a
// caller ever needs ZapChr == 0x01 for some reason, this constant needs
// to move to a value that isn't.
//
// _ib_IsUTF8TermChrFast and the ParseWordsUTF8 tokenizer both need to
// know about this constant -- see their own comments for how each uses
// it. Keep this definition in sync if it's hoisted into a shared header.
#define _IB_UTF8_SILENT_ZAP ((unsigned char)0x01)

#if 0
unsigned char *_utf_StrToLowerBuf(unsigned char *pString, unsigned length, const bool clean, unsigned char ZapChr)
{
    if (pString && length) {
        unsigned char       *p   = pString;
        const unsigned char *end = pString + length;
        unsigned char *pExtChar = 0;
        while (p < end) {
            if (*p < 0x20) {
                if (clean) *p = ZapChr; // Zap control chars
            } else if ((*p >= 0x41) && (*p <= 0x5a)) // US ASCII
                (*p) += 0x20;
            else if (*p > 0xc0) {
                pExtChar = p;
                p++;
                if (p < end) { // else: lead byte truncated at slice end — leave untouched
                switch (*pExtChar) {
                case 0xc2: // Latin-1 Supplement controls & punctuation — this
                           // block (U+0080-00BF: C1 controls, NBSP, ¡ ¢ £ ¤ ¥
                           // ¦ § ¨ © ª « ¬ SHY ® ¯ ° ± ² ³ ´ µ ¶ · ¸ ¹ º » ¼ ½
                           // ¾ ¿) contains ZERO letters. Previously fell to
                           // default: break and passed through unzapped even
                           // with clean=true — breaks any "survived clean ⇒
                           // term char" shortcut downstream.
                    if (clean)
                        *pExtChar = *p = ZapChr;
                    break;
                case 0xc3: // Latin 1
                    if ((*p >= 0x80)
                        && (*p <= 0x9e)
                        && (*p != 0x97))
                        (*p) += 0x20; // US ASCII shift
                    else if (clean && ((*p == 0x97) || (*p == 0xb7)))
                        // × U+00D7 MULTIPLICATION SIGN and ÷ U+00F7 DIVISION
                        // SIGN are symbols, not letters, hiding inside this
                        // otherwise-letters block. Previously fell through
                        // untouched even with clean=true.
                        *pExtChar = *p = ZapChr;
                    break;
                case 0xc4: // Latin Extended
                    if ((*p >= 0x80)
                        && (*p <= 0xb7)
                        && (!(*p % 2))) // Even
                        (*p)++; // Next char is lwr
                    else if ((*p >= 0xb9)
                        && (*p <= 0xbe)
                        && (*p % 2)) // Odd
                        (*p)++; // Next char is lwr
                    else if (*p == 0xbf) {
                        *pExtChar = 0xc5;
                        (*p) = 0x80;
                    }
                    break;
                case 0xc5: // Latin Extended
                    if ((*p >= 0x80)
                        && (*p <= 0x88)
                        && (*p % 2)) // Odd
                        (*p)++; // Next char is lwr
                    else if ((*p >= 0x8a)
                        && (*p <= 0xb7)
                        && (!(*p % 2))) // Even
                        (*p)++; // Next char is lwr
                    else if ((*p >= 0xb9)
                        && (*p <= 0xbe)
                        && (*p % 2)) // Odd
                        (*p)++; // Next char is lwr
                    break;
                case 0xc6: // Latin Extended
                    switch (*p) {
                    case 0x82: case 0x84: case 0x87: case 0x8b: case 0x91:
                    case 0x98: case 0xa0: case 0xa2: case 0xa4: case 0xa7:
                    case 0xac: case 0xaf: case 0xb3: case 0xb5: case 0xb8:
                    case 0xbc:
                        (*p)++; // Next char is lwr
                        break;
                    default:
                        break;
                    }
                    break;
                case 0xc7: // Latin Extended
                    if (*p == 0x84)
                        (*p) = 0x86;
                    else if (*p == 0x85)
                        (*p)++; // Next char is lwr
                    else if (*p == 0x87)
                        (*p) = 0x89;
                    else if (*p == 0x88)
                        (*p)++; // Next char is lwr
                    else if (*p == 0x8a)
                        (*p) = 0x8c;
                    else if (*p == 0x8b)
                        (*p)++; // Next char is lwr
                    else if ((*p >= 0x8d)
                        && (*p <= 0x9c)
                        && (*p % 2)) // Odd
                        (*p)++; // Next char is lwr
                    else if ((*p >= 0x9e)
                        && (*p <= 0xaf)
                        && (!(*p % 2))) // Even
                        (*p)++; // Next char is lwr
                    else if (*p == 0xb1)
                        (*p) = 0xb3;
                    else if (*p == 0xb2)
                        (*p)++; // Next char is lwr
                    else if (*p == 0xb4)
                        (*p)++; // Next char is lwr
                    else if (*p == 0xb8)
                        (*p)++; // Next char is lwr
                    else if (*p == 0xba)
                        (*p)++; // Next char is lwr
                    else if (*p == 0xbc)
                        (*p)++; // Next char is lwr
                    else if (*p == 0xbe)
                        (*p)++; // Next char is lwr
                    break;
                case 0xc8: // Latin Extended
                    if ((*p >= 0x80)
                        && (*p <= 0x9f)
                        && (!(*p % 2))) // Even
                        (*p)++; // Next char is lwr
                    else if ((*p >= 0xa2)
                        && (*p <= 0xb3)
                        && (!(*p % 2))) // Even
                        (*p)++; // Next char is lwr
                    else if (*p == 0xbb)
                        (*p)++; // Next char is lwr
                    break;

                // Latin modifier letters (U+02C2..U+02FF range subset)
                case 0xcb:
                    if (clean && (
                        (*p >= 0x82 && *p <= 0x85) ||
                        (*p >= 0x92 && *p <= 0x9f) ||
                        (*p >= 0xa5 && *p <= 0xab) ||
                        (*p >= 0xaf && *p <= 0xbf)))
                        *pExtChar = *p = ZapChr;
                    break;

                // Combining Diacritical Marks (U+0300..U+036F)
                case 0xcc: // U+0300-033F: Combining Diacritical Marks (all Mn) --
                           // silent, must not break a word (e.g. NFD "café"
                           // as e + combining acute must stay one term).
                    if (clean && (*p >= 0x80 && *p <= 0xbf))
                        *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                    break;

                case 0xcd:
                    if ((*p >= 0x80)
                        && (*p <= 0xaf)) {
                        // U+0340-036F: tail of the Combining Diacritical
                        // Marks block (same block as case 0xcc, split
                        // across the 0xCC/0xCD lead-byte boundary).
                        // Includes COMBINING GREEK PERISPOMENI (U+0342),
                        // COMBINING GREEK KORONIS (U+0343), COMBINING
                        // GREEK DIALYTIKA TONOS (U+0344), and COMBINING
                        // GREEK YPOGEGRAMMENI (U+0345) — the marks used
                        // for decomposed/NFD polytonic Greek. Silent --
                        // same rule as case 0xcc -- must not break a word.
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    }
                    switch (*p) {
                    case 0xb0: // HETA
                    case 0xb2: // ARCHAIC SAMPI
                    case 0xb6: // PAMPHYLIAN DIGAMMA
                        (*p)++; // Next char is lwr
                        break;
                    case 0xb4: // GREEK NUMERAL SIGN (Lm) — not a full letter
                    case 0xb5: // GREEK LOWER NUMERAL SIGN (Sk)
                    case 0xba: // GREEK YPOGEGRAMMENI, spacing form (Lm)
                    case 0xbe: // GREEK QUESTION MARK (Po) — looks like ';'
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    default:
                        if (*p == 0xbf) { // GREEK CAPITAL LETTER YOT
                            *pExtChar = 0xcf;
                            (*p) = 0xb3; // -> U+03F3 GREEK LETTER YOT
                        }
                        break;
                    }
                    break;
                case 0xce: // Greek & Coptic
                    if (*p == 0x86)
                        (*p) = 0xac;
                    else if (*p == 0x88)
                        (*p) = 0xad;
                    else if (*p == 0x89)
                        (*p) = 0xae;
                    else if (*p == 0x8a)
                        (*p) = 0xaf;
                    else if (*p == 0x8c) {
                        *pExtChar = 0xcf;
                        (*p) = 0x8c;
                    }
                    else if (*p == 0x8e) {
                        *pExtChar = 0xcf;
                        (*p) = 0x8d;
                    }
                    else if (*p == 0x8f) {
                        *pExtChar = 0xcf;
                        (*p) = 0x8e;
                    }
                    else if ((*p >= 0x91)
                        && (*p <= 0x9f))
                        (*p) += 0x20; // US ASCII shift
                    else if ((*p >= 0xa0)
                        && (*p <= 0xab)
                        && (*p != 0xa2)) {
                        *pExtChar = 0xcf;
                        (*p) -= 0x20;
                    }
                    else if (clean) {
                        switch (*p) {
                        // Zap Greek punctuation/diacritics that are not letters
                        case 0x84: // Greek Tonos
                        case 0x85: // Greek Dialytika Tonos
                        case 0x87: // Greek Ano Teleia (U+0387) — punctuation,
                                   // functions like a semicolon. NOT the
                                   // same as COMBINING GREEK PERISPOMENI
                                   // (U+0342, a real polytonic accent) —
                                   // that lives in case 0xcd's combining-
                                   // marks range and is zapped there.
                            *pExtChar = *p = ZapChr;
                            break;
                        }
                    }
                    break;
                case 0xcf: // Greek & Coptic
                    if (*p == 0x8f)
                        (*p) = 0x97; // FIX: Ϗ U+03CF -> ϗ U+03D7 (was 0xb4,
                                     // which corrupted Kai Symbol into
                                     // Capital Theta Symbol). This ligature
                                     // for "και" appears in papyri and
                                     // inscriptions.
                    // FIX: removed erroneous "else if (*p == 0x91) (*p)++;"
                    // here. U+03D1 (ϑ GREEK THETA SYMBOL) is ALREADY
                    // lowercase (its simple lowercase mapping is itself) —
                    // it must pass through unchanged. The removed line
                    // mutated it into U+03D2 (ϒ GREEK UPSILON WITH HOOK
                    // SYMBOL), an unrelated, unattested letter used in
                    // classical textual-criticism apparatus.
                    else if ((*p >= 0x98)
                        && (*p <= 0xaf)
                        && (!(*p % 2))) // Even
                        (*p)++; // Next char is lwr
                    else if (*p == 0xb4) {
                        // FIX: Ϝ U+03F4 GREEK CAPITAL THETA SYMBOL's real
                        // simple lowercase mapping is U+03B8 (regular θ),
                        // NOT U+03D1 (the theta SYMBOL, a different, caseless
                        // glyph variant) — verified against UCD, not just
                        // the visual "these look related" assumption. That
                        // crosses back into case 0xce's block.
                        *pExtChar = 0xce;
                        (*p) = 0xb8;
                    }
                    else if (*p == 0xb7)
                        (*p)++; // Next char is lwr
                    else if (*p == 0xb9)
                        (*p) = 0xb2;
                    else if (*p == 0xba) // FIX: was erroneously keyed on 0xbb,
                        (*p)++;          // which corrupted the ALREADY-lower
                                         // Ϻ/ϻ SAN pair's small form into
                                         // Ϲ GREEK RHO WITH STROKE SYMBOL.
                                         // Ϻ U+03FA (capital SAN, this byte)
                                         // -> ϻ U+03FB (small SAN).
                    else if (*p == 0xbd) {
                        *pExtChar = 0xcd;
                        (*p) = 0xbb;
                    }
                    else if (*p == 0xbe) {
                        *pExtChar = 0xcd;
                        (*p) = 0xbc;
                    }
                    else if (*p == 0xbf) {
                        *pExtChar = 0xcd;
                        (*p) = 0xbd;
                    }
                    // Greek Reversed Lunate Epsilon Symbol
                    else if (clean && *p == 0xb6)
                        *pExtChar = (*p) = ZapChr;
                    break;
                case 0xd0:
                    switch (*p) {
                    case 0x80: // CYRILLIC CAPITAL LETTER IE WITH GRAVE (crosses to lead 0xd1)
                        *pExtChar = 0xd1;
                        (*p) = 0x90;
                        break;
                    case 0x81: // CYRILLIC CAPITAL LETTER IO (crosses to lead 0xd1)
                        *pExtChar = 0xd1;
                        (*p) = 0x91;
                        break;
                    case 0x82: // CYRILLIC CAPITAL LETTER DJE (crosses to lead 0xd1)
                        *pExtChar = 0xd1;
                        (*p) = 0x92;
                        break;
                    case 0x83: // CYRILLIC CAPITAL LETTER GJE (crosses to lead 0xd1)
                        *pExtChar = 0xd1;
                        (*p) = 0x93;
                        break;
                    case 0x84: // CYRILLIC CAPITAL LETTER UKRAINIAN IE (crosses to lead 0xd1)
                        *pExtChar = 0xd1;
                        (*p) = 0x94;
                        break;
                    case 0x85: // CYRILLIC CAPITAL LETTER DZE (crosses to lead 0xd1)
                        *pExtChar = 0xd1;
                        (*p) = 0x95;
                        break;
                    case 0x86: // CYRILLIC CAPITAL LETTER BYELORUSSIAN-UKRAINIAN I (crosses to lead 0xd1)
                        *pExtChar = 0xd1;
                        (*p) = 0x96;
                        break;
                    case 0x87: // CYRILLIC CAPITAL LETTER YI (crosses to lead 0xd1)
                        *pExtChar = 0xd1;
                        (*p) = 0x97;
                        break;
                    case 0x88: // CYRILLIC CAPITAL LETTER JE (crosses to lead 0xd1)
                        *pExtChar = 0xd1;
                        (*p) = 0x98;
                        break;
                    case 0x89: // CYRILLIC CAPITAL LETTER LJE (crosses to lead 0xd1)
                        *pExtChar = 0xd1;
                        (*p) = 0x99;
                        break;
                    case 0x8a: // CYRILLIC CAPITAL LETTER NJE (crosses to lead 0xd1)
                        *pExtChar = 0xd1;
                        (*p) = 0x9a;
                        break;
                    case 0x8b: // CYRILLIC CAPITAL LETTER TSHE (crosses to lead 0xd1)
                        *pExtChar = 0xd1;
                        (*p) = 0x9b;
                        break;
                    case 0x8c: // CYRILLIC CAPITAL LETTER KJE (crosses to lead 0xd1)
                        *pExtChar = 0xd1;
                        (*p) = 0x9c;
                        break;
                    case 0x8d: // CYRILLIC CAPITAL LETTER I WITH GRAVE (crosses to lead 0xd1)
                        *pExtChar = 0xd1;
                        (*p) = 0x9d;
                        break;
                    case 0x8e: // CYRILLIC CAPITAL LETTER SHORT U (crosses to lead 0xd1)
                        *pExtChar = 0xd1;
                        (*p) = 0x9e;
                        break;
                    case 0x8f: // CYRILLIC CAPITAL LETTER DZHE (crosses to lead 0xd1)
                        *pExtChar = 0xd1;
                        (*p) = 0x9f;
                        break;
                    case 0x90: // CYRILLIC CAPITAL LETTER A
                        (*p) = 0xb0;
                        break;
                    case 0x91: // CYRILLIC CAPITAL LETTER BE
                        (*p) = 0xb1;
                        break;
                    case 0x92: // CYRILLIC CAPITAL LETTER VE
                        (*p) = 0xb2;
                        break;
                    case 0x93: // CYRILLIC CAPITAL LETTER GHE
                        (*p) = 0xb3;
                        break;
                    case 0x94: // CYRILLIC CAPITAL LETTER DE
                        (*p) = 0xb4;
                        break;
                    case 0x95: // CYRILLIC CAPITAL LETTER IE
                        (*p) = 0xb5;
                        break;
                    case 0x96: // CYRILLIC CAPITAL LETTER ZHE
                        (*p) = 0xb6;
                        break;
                    case 0x97: // CYRILLIC CAPITAL LETTER ZE
                        (*p) = 0xb7;
                        break;
                    case 0x98: // CYRILLIC CAPITAL LETTER I
                        (*p) = 0xb8;
                        break;
                    case 0x99: // CYRILLIC CAPITAL LETTER SHORT I
                        (*p) = 0xb9;
                        break;
                    case 0x9a: // CYRILLIC CAPITAL LETTER KA
                        (*p) = 0xba;
                        break;
                    case 0x9b: // CYRILLIC CAPITAL LETTER EL
                        (*p) = 0xbb;
                        break;
                    case 0x9c: // CYRILLIC CAPITAL LETTER EM
                        (*p) = 0xbc;
                        break;
                    case 0x9d: // CYRILLIC CAPITAL LETTER EN
                        (*p) = 0xbd;
                        break;
                    case 0x9e: // CYRILLIC CAPITAL LETTER O
                        (*p) = 0xbe;
                        break;
                    case 0x9f: // CYRILLIC CAPITAL LETTER PE
                        (*p) = 0xbf;
                        break;
                    case 0xa0: // CYRILLIC CAPITAL LETTER ER (crosses to lead 0xd1)
                        *pExtChar = 0xd1;
                        (*p) = 0x80;
                        break;
                    case 0xa1: // CYRILLIC CAPITAL LETTER ES (crosses to lead 0xd1)
                        *pExtChar = 0xd1;
                        (*p) = 0x81;
                        break;
                    case 0xa2: // CYRILLIC CAPITAL LETTER TE (crosses to lead 0xd1)
                        *pExtChar = 0xd1;
                        (*p) = 0x82;
                        break;
                    case 0xa3: // CYRILLIC CAPITAL LETTER U (crosses to lead 0xd1)
                        *pExtChar = 0xd1;
                        (*p) = 0x83;
                        break;
                    case 0xa4: // CYRILLIC CAPITAL LETTER EF (crosses to lead 0xd1)
                        *pExtChar = 0xd1;
                        (*p) = 0x84;
                        break;
                    case 0xa5: // CYRILLIC CAPITAL LETTER HA (crosses to lead 0xd1)
                        *pExtChar = 0xd1;
                        (*p) = 0x85;
                        break;
                    case 0xa6: // CYRILLIC CAPITAL LETTER TSE (crosses to lead 0xd1)
                        *pExtChar = 0xd1;
                        (*p) = 0x86;
                        break;
                    case 0xa7: // CYRILLIC CAPITAL LETTER CHE (crosses to lead 0xd1)
                        *pExtChar = 0xd1;
                        (*p) = 0x87;
                        break;
                    case 0xa8: // CYRILLIC CAPITAL LETTER SHA (crosses to lead 0xd1)
                        *pExtChar = 0xd1;
                        (*p) = 0x88;
                        break;
                    case 0xa9: // CYRILLIC CAPITAL LETTER SHCHA (crosses to lead 0xd1)
                        *pExtChar = 0xd1;
                        (*p) = 0x89;
                        break;
                    case 0xaa: // CYRILLIC CAPITAL LETTER HARD SIGN (crosses to lead 0xd1)
                        *pExtChar = 0xd1;
                        (*p) = 0x8a;
                        break;
                    case 0xab: // CYRILLIC CAPITAL LETTER YERU (crosses to lead 0xd1)
                        *pExtChar = 0xd1;
                        (*p) = 0x8b;
                        break;
                    case 0xac: // CYRILLIC CAPITAL LETTER SOFT SIGN (crosses to lead 0xd1)
                        *pExtChar = 0xd1;
                        (*p) = 0x8c;
                        break;
                    case 0xad: // CYRILLIC CAPITAL LETTER E (crosses to lead 0xd1)
                        *pExtChar = 0xd1;
                        (*p) = 0x8d;
                        break;
                    case 0xae: // CYRILLIC CAPITAL LETTER YU (crosses to lead 0xd1)
                        *pExtChar = 0xd1;
                        (*p) = 0x8e;
                        break;
                    case 0xaf: // CYRILLIC CAPITAL LETTER YA (crosses to lead 0xd1)
                        *pExtChar = 0xd1;
                        (*p) = 0x8f;
                        break;
                    default:
                        break;
                    }
                    break;

                case 0xd1:
                    switch (*p) {
                    case 0xa0: // CYRILLIC CAPITAL LETTER OMEGA
                        (*p) = 0xa1;
                        break;
                    case 0xa2: // CYRILLIC CAPITAL LETTER YAT
                        (*p) = 0xa3;
                        break;
                    case 0xa4: // CYRILLIC CAPITAL LETTER IOTIFIED E
                        (*p) = 0xa5;
                        break;
                    case 0xa6: // CYRILLIC CAPITAL LETTER LITTLE YUS
                        (*p) = 0xa7;
                        break;
                    case 0xa8: // CYRILLIC CAPITAL LETTER IOTIFIED LITTLE YUS
                        (*p) = 0xa9;
                        break;
                    case 0xaa: // CYRILLIC CAPITAL LETTER BIG YUS
                        (*p) = 0xab;
                        break;
                    case 0xac: // CYRILLIC CAPITAL LETTER IOTIFIED BIG YUS
                        (*p) = 0xad;
                        break;
                    case 0xae: // CYRILLIC CAPITAL LETTER KSI
                        (*p) = 0xaf;
                        break;
                    case 0xb0: // CYRILLIC CAPITAL LETTER PSI
                        (*p) = 0xb1;
                        break;
                    case 0xb2: // CYRILLIC CAPITAL LETTER FITA
                        (*p) = 0xb3;
                        break;
                    case 0xb4: // CYRILLIC CAPITAL LETTER IZHITSA
                        (*p) = 0xb5;
                        break;
                    case 0xb6: // CYRILLIC CAPITAL LETTER IZHITSA WITH DOUBLE GRAVE ACCENT
                        (*p) = 0xb7;
                        break;
                    case 0xb8: // CYRILLIC CAPITAL LETTER UK
                        (*p) = 0xb9;
                        break;
                    case 0xba: // CYRILLIC CAPITAL LETTER ROUND OMEGA
                        (*p) = 0xbb;
                        break;
                    case 0xbc: // CYRILLIC CAPITAL LETTER OMEGA WITH TITLO
                        (*p) = 0xbd;
                        break;
                    case 0xbe: // CYRILLIC CAPITAL LETTER OT
                        (*p) = 0xbf;
                        break;
                    default:
                        break;
                    }
                    break;

                case 0xd2:
                    switch (*p) {
                    case 0x80: // CYRILLIC CAPITAL LETTER KOPPA
                        (*p) = 0x81;
                        break;
                    case 0x82: // CYRILLIC THOUSANDS SIGN (So) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0x83: // COMBINING CYRILLIC TITLO (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x84: // COMBINING CYRILLIC PALATALIZATION (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x85: // COMBINING CYRILLIC DASIA PNEUMATA (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x86: // COMBINING CYRILLIC PSILI PNEUMATA (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x87: // COMBINING CYRILLIC POKRYTIE (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x88: // COMBINING CYRILLIC HUNDRED THOUSANDS SIGN (Me) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x89: // COMBINING CYRILLIC MILLIONS SIGN (Me) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x8a: // CYRILLIC CAPITAL LETTER SHORT I WITH TAIL
                        (*p) = 0x8b;
                        break;
                    case 0x8c: // CYRILLIC CAPITAL LETTER SEMISOFT SIGN
                        (*p) = 0x8d;
                        break;
                    case 0x8e: // CYRILLIC CAPITAL LETTER ER WITH TICK
                        (*p) = 0x8f;
                        break;
                    case 0x90: // CYRILLIC CAPITAL LETTER GHE WITH UPTURN
                        (*p) = 0x91;
                        break;
                    case 0x92: // CYRILLIC CAPITAL LETTER GHE WITH STROKE
                        (*p) = 0x93;
                        break;
                    case 0x94: // CYRILLIC CAPITAL LETTER GHE WITH MIDDLE HOOK
                        (*p) = 0x95;
                        break;
                    case 0x96: // CYRILLIC CAPITAL LETTER ZHE WITH DESCENDER
                        (*p) = 0x97;
                        break;
                    case 0x98: // CYRILLIC CAPITAL LETTER ZE WITH DESCENDER
                        (*p) = 0x99;
                        break;
                    case 0x9a: // CYRILLIC CAPITAL LETTER KA WITH DESCENDER
                        (*p) = 0x9b;
                        break;
                    case 0x9c: // CYRILLIC CAPITAL LETTER KA WITH VERTICAL STROKE
                        (*p) = 0x9d;
                        break;
                    case 0x9e: // CYRILLIC CAPITAL LETTER KA WITH STROKE
                        (*p) = 0x9f;
                        break;
                    case 0xa0: // CYRILLIC CAPITAL LETTER BASHKIR KA
                        (*p) = 0xa1;
                        break;
                    case 0xa2: // CYRILLIC CAPITAL LETTER EN WITH DESCENDER
                        (*p) = 0xa3;
                        break;
                    case 0xa4: // CYRILLIC CAPITAL LIGATURE EN GHE
                        (*p) = 0xa5;
                        break;
                    case 0xa6: // CYRILLIC CAPITAL LETTER PE WITH MIDDLE HOOK
                        (*p) = 0xa7;
                        break;
                    case 0xa8: // CYRILLIC CAPITAL LETTER ABKHASIAN HA
                        (*p) = 0xa9;
                        break;
                    case 0xaa: // CYRILLIC CAPITAL LETTER ES WITH DESCENDER
                        (*p) = 0xab;
                        break;
                    case 0xac: // CYRILLIC CAPITAL LETTER TE WITH DESCENDER
                        (*p) = 0xad;
                        break;
                    case 0xae: // CYRILLIC CAPITAL LETTER STRAIGHT U
                        (*p) = 0xaf;
                        break;
                    case 0xb0: // CYRILLIC CAPITAL LETTER STRAIGHT U WITH STROKE
                        (*p) = 0xb1;
                        break;
                    case 0xb2: // CYRILLIC CAPITAL LETTER HA WITH DESCENDER
                        (*p) = 0xb3;
                        break;
                    case 0xb4: // CYRILLIC CAPITAL LIGATURE TE TSE
                        (*p) = 0xb5;
                        break;
                    case 0xb6: // CYRILLIC CAPITAL LETTER CHE WITH DESCENDER
                        (*p) = 0xb7;
                        break;
                    case 0xb8: // CYRILLIC CAPITAL LETTER CHE WITH VERTICAL STROKE
                        (*p) = 0xb9;
                        break;
                    case 0xba: // CYRILLIC CAPITAL LETTER SHHA
                        (*p) = 0xbb;
                        break;
                    case 0xbc: // CYRILLIC CAPITAL LETTER ABKHASIAN CHE
                        (*p) = 0xbd;
                        break;
                    case 0xbe: // CYRILLIC CAPITAL LETTER ABKHASIAN CHE WITH DESCENDER
                        (*p) = 0xbf;
                        break;
                    default:
                        break;
                    }
                    break;

                case 0xd3:
                    switch (*p) {
                    case 0x80: // CYRILLIC LETTER PALOCHKA
                        (*p) = 0x8f;
                        break;
                    case 0x81: // CYRILLIC CAPITAL LETTER ZHE WITH BREVE
                        (*p) = 0x82;
                        break;
                    case 0x83: // CYRILLIC CAPITAL LETTER KA WITH HOOK
                        (*p) = 0x84;
                        break;
                    case 0x85: // CYRILLIC CAPITAL LETTER EL WITH TAIL
                        (*p) = 0x86;
                        break;
                    case 0x87: // CYRILLIC CAPITAL LETTER EN WITH HOOK
                        (*p) = 0x88;
                        break;
                    case 0x89: // CYRILLIC CAPITAL LETTER EN WITH TAIL
                        (*p) = 0x8a;
                        break;
                    case 0x8b: // CYRILLIC CAPITAL LETTER KHAKASSIAN CHE
                        (*p) = 0x8c;
                        break;
                    case 0x8d: // CYRILLIC CAPITAL LETTER EM WITH TAIL
                        (*p) = 0x8e;
                        break;
                    case 0x90: // CYRILLIC CAPITAL LETTER A WITH BREVE
                        (*p) = 0x91;
                        break;
                    case 0x92: // CYRILLIC CAPITAL LETTER A WITH DIAERESIS
                        (*p) = 0x93;
                        break;
                    case 0x94: // CYRILLIC CAPITAL LIGATURE A IE
                        (*p) = 0x95;
                        break;
                    case 0x96: // CYRILLIC CAPITAL LETTER IE WITH BREVE
                        (*p) = 0x97;
                        break;
                    case 0x98: // CYRILLIC CAPITAL LETTER SCHWA
                        (*p) = 0x99;
                        break;
                    case 0x9a: // CYRILLIC CAPITAL LETTER SCHWA WITH DIAERESIS
                        (*p) = 0x9b;
                        break;
                    case 0x9c: // CYRILLIC CAPITAL LETTER ZHE WITH DIAERESIS
                        (*p) = 0x9d;
                        break;
                    case 0x9e: // CYRILLIC CAPITAL LETTER ZE WITH DIAERESIS
                        (*p) = 0x9f;
                        break;
                    case 0xa0: // CYRILLIC CAPITAL LETTER ABKHASIAN DZE
                        (*p) = 0xa1;
                        break;
                    case 0xa2: // CYRILLIC CAPITAL LETTER I WITH MACRON
                        (*p) = 0xa3;
                        break;
                    case 0xa4: // CYRILLIC CAPITAL LETTER I WITH DIAERESIS
                        (*p) = 0xa5;
                        break;
                    case 0xa6: // CYRILLIC CAPITAL LETTER O WITH DIAERESIS
                        (*p) = 0xa7;
                        break;
                    case 0xa8: // CYRILLIC CAPITAL LETTER BARRED O
                        (*p) = 0xa9;
                        break;
                    case 0xaa: // CYRILLIC CAPITAL LETTER BARRED O WITH DIAERESIS
                        (*p) = 0xab;
                        break;
                    case 0xac: // CYRILLIC CAPITAL LETTER E WITH DIAERESIS
                        (*p) = 0xad;
                        break;
                    case 0xae: // CYRILLIC CAPITAL LETTER U WITH MACRON
                        (*p) = 0xaf;
                        break;
                    case 0xb0: // CYRILLIC CAPITAL LETTER U WITH DIAERESIS
                        (*p) = 0xb1;
                        break;
                    case 0xb2: // CYRILLIC CAPITAL LETTER U WITH DOUBLE ACUTE
                        (*p) = 0xb3;
                        break;
                    case 0xb4: // CYRILLIC CAPITAL LETTER CHE WITH DIAERESIS
                        (*p) = 0xb5;
                        break;
                    case 0xb6: // CYRILLIC CAPITAL LETTER GHE WITH DESCENDER
                        (*p) = 0xb7;
                        break;
                    case 0xb8: // CYRILLIC CAPITAL LETTER YERU WITH DIAERESIS
                        (*p) = 0xb9;
                        break;
                    case 0xba: // CYRILLIC CAPITAL LETTER GHE WITH STROKE AND HOOK
                        (*p) = 0xbb;
                        break;
                    case 0xbc: // CYRILLIC CAPITAL LETTER HA WITH HOOK
                        (*p) = 0xbd;
                        break;
                    case 0xbe: // CYRILLIC CAPITAL LETTER HA WITH STROKE
                        (*p) = 0xbf;
                        break;
                    default:
                        break;
                    }
                    break;

                case 0xd4:
                    switch (*p) {
                    case 0x80: // CYRILLIC CAPITAL LETTER KOMI DE
                        (*p) = 0x81;
                        break;
                    case 0x82: // CYRILLIC CAPITAL LETTER KOMI DJE
                        (*p) = 0x83;
                        break;
                    case 0x84: // CYRILLIC CAPITAL LETTER KOMI ZJE
                        (*p) = 0x85;
                        break;
                    case 0x86: // CYRILLIC CAPITAL LETTER KOMI DZJE
                        (*p) = 0x87;
                        break;
                    case 0x88: // CYRILLIC CAPITAL LETTER KOMI LJE
                        (*p) = 0x89;
                        break;
                    case 0x8a: // CYRILLIC CAPITAL LETTER KOMI NJE
                        (*p) = 0x8b;
                        break;
                    case 0x8c: // CYRILLIC CAPITAL LETTER KOMI SJE
                        (*p) = 0x8d;
                        break;
                    case 0x8e: // CYRILLIC CAPITAL LETTER KOMI TJE
                        (*p) = 0x8f;
                        break;
                    case 0x90: // CYRILLIC CAPITAL LETTER REVERSED ZE
                        (*p) = 0x91;
                        break;
                    case 0x92: // CYRILLIC CAPITAL LETTER EL WITH HOOK
                        (*p) = 0x93;
                        break;
                    case 0x94: // CYRILLIC CAPITAL LETTER LHA
                        (*p) = 0x95;
                        break;
                    case 0x96: // CYRILLIC CAPITAL LETTER RHA
                        (*p) = 0x97;
                        break;
                    case 0x98: // CYRILLIC CAPITAL LETTER YAE
                        (*p) = 0x99;
                        break;
                    case 0x9a: // CYRILLIC CAPITAL LETTER QA
                        (*p) = 0x9b;
                        break;
                    case 0x9c: // CYRILLIC CAPITAL LETTER WE
                        (*p) = 0x9d;
                        break;
                    case 0x9e: // CYRILLIC CAPITAL LETTER ALEUT KA
                        (*p) = 0x9f;
                        break;
                    case 0xa0: // CYRILLIC CAPITAL LETTER EL WITH MIDDLE HOOK
                        (*p) = 0xa1;
                        break;
                    case 0xa2: // CYRILLIC CAPITAL LETTER EN WITH MIDDLE HOOK
                        (*p) = 0xa3;
                        break;
                    case 0xa4: // CYRILLIC CAPITAL LETTER PE WITH DESCENDER
                        (*p) = 0xa5;
                        break;
                    case 0xa6: // CYRILLIC CAPITAL LETTER SHHA WITH DESCENDER
                        (*p) = 0xa7;
                        break;
                    case 0xa8: // CYRILLIC CAPITAL LETTER EN WITH LEFT HOOK
                        (*p) = 0xa9;
                        break;
                    case 0xaa: // CYRILLIC CAPITAL LETTER DZZHE
                        (*p) = 0xab;
                        break;
                    case 0xac: // CYRILLIC CAPITAL LETTER DCHE
                        (*p) = 0xad;
                        break;
                    case 0xae: // CYRILLIC CAPITAL LETTER EL WITH DESCENDER
                        (*p) = 0xaf;
                        break;
                    case 0xb1: // ARMENIAN CAPITAL LETTER AYB (crosses to lead 0xd5)
                        *pExtChar = 0xd5;
                        (*p) = 0xa1;
                        break;
                    case 0xb2: // ARMENIAN CAPITAL LETTER BEN (crosses to lead 0xd5)
                        *pExtChar = 0xd5;
                        (*p) = 0xa2;
                        break;
                    case 0xb3: // ARMENIAN CAPITAL LETTER GIM (crosses to lead 0xd5)
                        *pExtChar = 0xd5;
                        (*p) = 0xa3;
                        break;
                    case 0xb4: // ARMENIAN CAPITAL LETTER DA (crosses to lead 0xd5)
                        *pExtChar = 0xd5;
                        (*p) = 0xa4;
                        break;
                    case 0xb5: // ARMENIAN CAPITAL LETTER ECH (crosses to lead 0xd5)
                        *pExtChar = 0xd5;
                        (*p) = 0xa5;
                        break;
                    case 0xb6: // ARMENIAN CAPITAL LETTER ZA (crosses to lead 0xd5)
                        *pExtChar = 0xd5;
                        (*p) = 0xa6;
                        break;
                    case 0xb7: // ARMENIAN CAPITAL LETTER EH (crosses to lead 0xd5)
                        *pExtChar = 0xd5;
                        (*p) = 0xa7;
                        break;
                    case 0xb8: // ARMENIAN CAPITAL LETTER ET (crosses to lead 0xd5)
                        *pExtChar = 0xd5;
                        (*p) = 0xa8;
                        break;
                    case 0xb9: // ARMENIAN CAPITAL LETTER TO (crosses to lead 0xd5)
                        *pExtChar = 0xd5;
                        (*p) = 0xa9;
                        break;
                    case 0xba: // ARMENIAN CAPITAL LETTER ZHE (crosses to lead 0xd5)
                        *pExtChar = 0xd5;
                        (*p) = 0xaa;
                        break;
                    case 0xbb: // ARMENIAN CAPITAL LETTER INI (crosses to lead 0xd5)
                        *pExtChar = 0xd5;
                        (*p) = 0xab;
                        break;
                    case 0xbc: // ARMENIAN CAPITAL LETTER LIWN (crosses to lead 0xd5)
                        *pExtChar = 0xd5;
                        (*p) = 0xac;
                        break;
                    case 0xbd: // ARMENIAN CAPITAL LETTER XEH (crosses to lead 0xd5)
                        *pExtChar = 0xd5;
                        (*p) = 0xad;
                        break;
                    case 0xbe: // ARMENIAN CAPITAL LETTER CA (crosses to lead 0xd5)
                        *pExtChar = 0xd5;
                        (*p) = 0xae;
                        break;
                    case 0xbf: // ARMENIAN CAPITAL LETTER KEN (crosses to lead 0xd5)
                        *pExtChar = 0xd5;
                        (*p) = 0xaf;
                        break;
                    default:
                        break;
                    }
                    break;

                case 0xd5:
                    switch (*p) {
                    case 0x80: // ARMENIAN CAPITAL LETTER HO
                        (*p) = 0xb0;
                        break;
                    case 0x81: // ARMENIAN CAPITAL LETTER JA
                        (*p) = 0xb1;
                        break;
                    case 0x82: // ARMENIAN CAPITAL LETTER GHAD
                        (*p) = 0xb2;
                        break;
                    case 0x83: // ARMENIAN CAPITAL LETTER CHEH
                        (*p) = 0xb3;
                        break;
                    case 0x84: // ARMENIAN CAPITAL LETTER MEN
                        (*p) = 0xb4;
                        break;
                    case 0x85: // ARMENIAN CAPITAL LETTER YI
                        (*p) = 0xb5;
                        break;
                    case 0x86: // ARMENIAN CAPITAL LETTER NOW
                        (*p) = 0xb6;
                        break;
                    case 0x87: // ARMENIAN CAPITAL LETTER SHA
                        (*p) = 0xb7;
                        break;
                    case 0x88: // ARMENIAN CAPITAL LETTER VO
                        (*p) = 0xb8;
                        break;
                    case 0x89: // ARMENIAN CAPITAL LETTER CHA
                        (*p) = 0xb9;
                        break;
                    case 0x8a: // ARMENIAN CAPITAL LETTER PEH
                        (*p) = 0xba;
                        break;
                    case 0x8b: // ARMENIAN CAPITAL LETTER JHEH
                        (*p) = 0xbb;
                        break;
                    case 0x8c: // ARMENIAN CAPITAL LETTER RA
                        (*p) = 0xbc;
                        break;
                    case 0x8d: // ARMENIAN CAPITAL LETTER SEH
                        (*p) = 0xbd;
                        break;
                    case 0x8e: // ARMENIAN CAPITAL LETTER VEW
                        (*p) = 0xbe;
                        break;
                    case 0x8f: // ARMENIAN CAPITAL LETTER TIWN
                        (*p) = 0xbf;
                        break;
                    case 0x90: // ARMENIAN CAPITAL LETTER REH (crosses to lead 0xd6)
                        *pExtChar = 0xd6;
                        (*p) = 0x80;
                        break;
                    case 0x91: // ARMENIAN CAPITAL LETTER CO (crosses to lead 0xd6)
                        *pExtChar = 0xd6;
                        (*p) = 0x81;
                        break;
                    case 0x92: // ARMENIAN CAPITAL LETTER YIWN (crosses to lead 0xd6)
                        *pExtChar = 0xd6;
                        (*p) = 0x82;
                        break;
                    case 0x93: // ARMENIAN CAPITAL LETTER PIWR (crosses to lead 0xd6)
                        *pExtChar = 0xd6;
                        (*p) = 0x83;
                        break;
                    case 0x94: // ARMENIAN CAPITAL LETTER KEH (crosses to lead 0xd6)
                        *pExtChar = 0xd6;
                        (*p) = 0x84;
                        break;
                    case 0x95: // ARMENIAN CAPITAL LETTER OH (crosses to lead 0xd6)
                        *pExtChar = 0xd6;
                        (*p) = 0x85;
                        break;
                    case 0x96: // ARMENIAN CAPITAL LETTER FEH (crosses to lead 0xd6)
                        *pExtChar = 0xd6;
                        (*p) = 0x86;
                        break;
                    case 0x99: // ARMENIAN MODIFIER LETTER LEFT HALF RING (Lm) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0x9a: // ARMENIAN APOSTROPHE (Po) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0x9b: // ARMENIAN EMPHASIS MARK (Po) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0x9c: // ARMENIAN EXCLAMATION MARK (Po) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0x9d: // ARMENIAN COMMA (Po) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0x9e: // ARMENIAN QUESTION MARK (Po) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0x9f: // ARMENIAN ABBREVIATION MARK (Po) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    default:
                        break;
                    }
                    break;

                case 0xd6:
                    switch (*p) {
                    case 0x89: // ARMENIAN FULL STOP (Po) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0x8a: // ARMENIAN HYPHEN (Pd) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0x8d: // RIGHT-FACING ARMENIAN ETERNITY SIGN (So) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0x8e: // LEFT-FACING ARMENIAN ETERNITY SIGN (So) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0x8f: // ARMENIAN DRAM SIGN (Sc) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0x91: // HEBREW ACCENT ETNAHTA (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x92: // HEBREW ACCENT SEGOL (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x93: // HEBREW ACCENT SHALSHELET (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x94: // HEBREW ACCENT ZAQEF QATAN (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x95: // HEBREW ACCENT ZAQEF GADOL (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x96: // HEBREW ACCENT TIPEHA (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x97: // HEBREW ACCENT REVIA (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x98: // HEBREW ACCENT ZARQA (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x99: // HEBREW ACCENT PASHTA (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x9a: // HEBREW ACCENT YETIV (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x9b: // HEBREW ACCENT TEVIR (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x9c: // HEBREW ACCENT GERESH (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x9d: // HEBREW ACCENT GERESH MUQDAM (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x9e: // HEBREW ACCENT GERSHAYIM (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x9f: // HEBREW ACCENT QARNEY PARA (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xa0: // HEBREW ACCENT TELISHA GEDOLA (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xa1: // HEBREW ACCENT PAZER (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xa2: // HEBREW ACCENT ATNAH HAFUKH (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xa3: // HEBREW ACCENT MUNAH (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xa4: // HEBREW ACCENT MAHAPAKH (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xa5: // HEBREW ACCENT MERKHA (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xa6: // HEBREW ACCENT MERKHA KEFULA (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xa7: // HEBREW ACCENT DARGA (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xa8: // HEBREW ACCENT QADMA (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xa9: // HEBREW ACCENT TELISHA QETANA (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xaa: // HEBREW ACCENT YERAH BEN YOMO (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xab: // HEBREW ACCENT OLE (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xac: // HEBREW ACCENT ILUY (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xad: // HEBREW ACCENT DEHI (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xae: // HEBREW ACCENT ZINOR (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xaf: // HEBREW MARK MASORA CIRCLE (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xb0: // HEBREW POINT SHEVA (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xb1: // HEBREW POINT HATAF SEGOL (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xb2: // HEBREW POINT HATAF PATAH (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xb3: // HEBREW POINT HATAF QAMATS (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xb4: // HEBREW POINT HIRIQ (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xb5: // HEBREW POINT TSERE (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xb6: // HEBREW POINT SEGOL (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xb7: // HEBREW POINT PATAH (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xb8: // HEBREW POINT QAMATS (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xb9: // HEBREW POINT HOLAM (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xba: // HEBREW POINT HOLAM HASER FOR VAV (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xbb: // HEBREW POINT QUBUTS (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xbc: // HEBREW POINT DAGESH OR MAPIQ (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xbd: // HEBREW POINT METEG (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xbe: // HEBREW PUNCTUATION MAQAF (Pd) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0xbf: // HEBREW POINT RAFE (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    default:
                        break;
                    }
                    break;

                case 0xd7:
                    switch (*p) {
                    case 0x80: // HEBREW PUNCTUATION PASEQ (Po) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0x81: // HEBREW POINT SHIN DOT (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x82: // HEBREW POINT SIN DOT (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x83: // HEBREW PUNCTUATION SOF PASUQ (Po) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0x84: // HEBREW MARK UPPER DOT (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x85: // HEBREW MARK LOWER DOT (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x86: // HEBREW PUNCTUATION NUN HAFUKHA (Po) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0x87: // HEBREW POINT QAMATS QATAN (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xb3: // HEBREW PUNCTUATION GERESH (Po) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0xb4: // HEBREW PUNCTUATION GERSHAYIM (Po) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    default:
                        break;
                    }
                    break;

                case 0xd8:
                    switch (*p) {
                    case 0x80: // ARABIC NUMBER SIGN (Cf) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x81: // ARABIC SIGN SANAH (Cf) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x82: // ARABIC FOOTNOTE MARKER (Cf) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x83: // ARABIC SIGN SAFHA (Cf) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x84: // ARABIC SIGN SAMVAT (Cf) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x85: // ARABIC NUMBER MARK ABOVE (Cf) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x86: // ARABIC-INDIC CUBE ROOT (Sm) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0x87: // ARABIC-INDIC FOURTH ROOT (Sm) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0x88: // ARABIC RAY (Sm) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0x89: // ARABIC-INDIC PER MILLE SIGN (Po) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0x8a: // ARABIC-INDIC PER TEN THOUSAND SIGN (Po) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0x8b: // AFGHANI SIGN (Sc) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0x8c: // ARABIC COMMA (Po) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0x8d: // ARABIC DATE SEPARATOR (Po) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0x8e: // ARABIC POETIC VERSE SIGN (So) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0x8f: // ARABIC SIGN MISRA (So) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0x90: // ARABIC SIGN SALLALLAHOU ALAYHE WASSALLAM (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x91: // ARABIC SIGN ALAYHE ASSALLAM (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x92: // ARABIC SIGN RAHMATULLAH ALAYHE (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x93: // ARABIC SIGN RADI ALLAHOU ANHU (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x94: // ARABIC SIGN TAKHALLUS (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x95: // ARABIC SMALL HIGH TAH (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x96: // ARABIC SMALL HIGH LIGATURE ALEF WITH LAM WITH YEH (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x97: // ARABIC SMALL HIGH ZAIN (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x98: // ARABIC SMALL FATHA (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x99: // ARABIC SMALL DAMMA (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x9a: // ARABIC SMALL KASRA (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x9b: // ARABIC SEMICOLON (Po) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0x9c: // ARABIC LETTER MARK (Cf) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x9d: // ARABIC END OF TEXT MARK (Po) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0x9e: // ARABIC TRIPLE DOT PUNCTUATION MARK (Po) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0x9f: // ARABIC QUESTION MARK (Po) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    default:
                        break;
                    }
                    break;

                case 0xd9:
                    switch (*p) {
                    case 0x80: // ARABIC TATWEEL (Lm) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0x8b: // ARABIC FATHATAN (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x8c: // ARABIC DAMMATAN (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x8d: // ARABIC KASRATAN (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x8e: // ARABIC FATHA (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x8f: // ARABIC DAMMA (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x90: // ARABIC KASRA (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x91: // ARABIC SHADDA (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x92: // ARABIC SUKUN (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x93: // ARABIC MADDAH ABOVE (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x94: // ARABIC HAMZA ABOVE (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x95: // ARABIC HAMZA BELOW (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x96: // ARABIC SUBSCRIPT ALEF (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x97: // ARABIC INVERTED DAMMA (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x98: // ARABIC MARK NOON GHUNNA (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x99: // ARABIC ZWARAKAY (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x9a: // ARABIC VOWEL SIGN SMALL V ABOVE (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x9b: // ARABIC VOWEL SIGN INVERTED SMALL V ABOVE (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x9c: // ARABIC VOWEL SIGN DOT BELOW (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x9d: // ARABIC REVERSED DAMMA (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x9e: // ARABIC FATHA WITH TWO DOTS (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x9f: // ARABIC WAVY HAMZA BELOW (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xaa: // ARABIC PERCENT SIGN (Po) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0xab: // ARABIC DECIMAL SEPARATOR (Po) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0xac: // ARABIC THOUSANDS SEPARATOR (Po) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0xad: // ARABIC FIVE POINTED STAR (Po) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0xb0: // ARABIC LETTER SUPERSCRIPT ALEF (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    default:
                        break;
                    }
                    break;

                case 0xda:
                    switch (*p) {
                    default:
                        break;
                    }
                    break;

                case 0xdb:
                    switch (*p) {
                    case 0x94: // ARABIC FULL STOP (Po) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0x96: // ARABIC SMALL HIGH LIGATURE SAD WITH LAM WITH ALEF MAKSURA (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x97: // ARABIC SMALL HIGH LIGATURE QAF WITH LAM WITH ALEF MAKSURA (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x98: // ARABIC SMALL HIGH MEEM INITIAL FORM (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x99: // ARABIC SMALL HIGH LAM ALEF (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x9a: // ARABIC SMALL HIGH JEEM (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x9b: // ARABIC SMALL HIGH THREE DOTS (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x9c: // ARABIC SMALL HIGH SEEN (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x9d: // ARABIC END OF AYAH (Cf) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x9e: // ARABIC START OF RUB EL HIZB (So) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0x9f: // ARABIC SMALL HIGH ROUNDED ZERO (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xa0: // ARABIC SMALL HIGH UPRIGHT RECTANGULAR ZERO (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xa1: // ARABIC SMALL HIGH DOTLESS HEAD OF KHAH (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xa2: // ARABIC SMALL HIGH MEEM ISOLATED FORM (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xa3: // ARABIC SMALL LOW SEEN (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xa4: // ARABIC SMALL HIGH MADDA (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xa5: // ARABIC SMALL WAW (Lm) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0xa6: // ARABIC SMALL YEH (Lm) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0xa7: // ARABIC SMALL HIGH YEH (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xa8: // ARABIC SMALL HIGH NOON (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xa9: // ARABIC PLACE OF SAJDAH (So) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0xaa: // ARABIC EMPTY CENTRE LOW STOP (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xab: // ARABIC EMPTY CENTRE HIGH STOP (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xac: // ARABIC ROUNDED HIGH STOP WITH FILLED CENTRE (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xad: // ARABIC SMALL LOW MEEM (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xbd: // ARABIC SIGN SINDHI AMPERSAND (So) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0xbe: // ARABIC SIGN SINDHI POSTPOSITION MEN (So) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    default:
                        break;
                    }
                    break;

                case 0xdc:
                    switch (*p) {
                    case 0x80: // SYRIAC END OF PARAGRAPH (Po)
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0x81: // SYRIAC SUPRALINEAR FULL STOP (Po)
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0x82: // SYRIAC SUBLINEAR FULL STOP (Po)
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0x83: // SYRIAC SUPRALINEAR COLON (Po)
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0x84: // SYRIAC SUBLINEAR COLON (Po)
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0x85: // SYRIAC HORIZONTAL COLON (Po)
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0x86: // SYRIAC COLON SKEWED LEFT (Po)
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0x87: // SYRIAC COLON SKEWED RIGHT (Po)
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0x88: // SYRIAC SUPRALINEAR COLON SKEWED LEFT (Po)
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0x89: // SYRIAC SUBLINEAR COLON SKEWED RIGHT (Po)
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0x8a: // SYRIAC CONTRACTION (Po)
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0x8b: // SYRIAC HARKLEAN OBELUS (Po)
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0x8c: // SYRIAC HARKLEAN METOBELUS (Po)
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0x8d: // SYRIAC HARKLEAN ASTERISCUS (Po)
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0x8f: // SYRIAC ABBREVIATION MARK (Cf)
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x91: // SYRIAC LETTER SUPERSCRIPT ALAPH (Mn)
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xb0: // SYRIAC PTHAHA ABOVE (Mn)
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xb1: // SYRIAC PTHAHA BELOW (Mn)
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xb2: // SYRIAC PTHAHA DOTTED (Mn)
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xb3: // SYRIAC ZQAPHA ABOVE (Mn)
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xb4: // SYRIAC ZQAPHA BELOW (Mn)
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xb5: // SYRIAC ZQAPHA DOTTED (Mn)
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xb6: // SYRIAC RBASA ABOVE (Mn)
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xb7: // SYRIAC RBASA BELOW (Mn)
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xb8: // SYRIAC DOTTED ZLAMA HORIZONTAL (Mn)
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xb9: // SYRIAC DOTTED ZLAMA ANGULAR (Mn)
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xba: // SYRIAC HBASA ABOVE (Mn)
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xbb: // SYRIAC HBASA BELOW (Mn)
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xbc: // SYRIAC HBASA-ESASA DOTTED (Mn)
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xbd: // SYRIAC ESASA ABOVE (Mn)
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xbe: // SYRIAC ESASA BELOW (Mn)
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xbf: // SYRIAC RWAHA (Mn)
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    default:
                        break;
                    }
                    break;

                case 0xdd:
                    switch (*p) {
                    case 0x80: // SYRIAC FEMININE DOT (Mn)
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x81: // SYRIAC QUSHSHAYA (Mn)
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x82: // SYRIAC RUKKAKHA (Mn)
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x83: // SYRIAC TWO VERTICAL DOTS ABOVE (Mn)
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x84: // SYRIAC TWO VERTICAL DOTS BELOW (Mn)
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x85: // SYRIAC THREE DOTS ABOVE (Mn)
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x86: // SYRIAC THREE DOTS BELOW (Mn)
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x87: // SYRIAC OBLIQUE LINE ABOVE (Mn)
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x88: // SYRIAC OBLIQUE LINE BELOW (Mn)
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x89: // SYRIAC MUSIC (Mn)
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x8a: // SYRIAC BARREKH (Mn)
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    default:
                        break;
                    }
                    break;

                case 0xdf: // NKo block
                    // Zap combining tones through NKo exclamation mark,
                    // but keep high/low tone apostrophes (U+07F4, U+07F5)
                    if (clean && (*p >= 0xab && *p <= 0xb9) &&
                        *p != 0xb4 && *p != 0xb5)
                        *pExtChar = *p = ZapChr;
                    break;

                // 3-byte sequences for Indic & other non-Latin scripts.
                // In clean mode the entire script block is zapped to space;
                // the pointer already sits on byte 2 of 3 after the outer p++,
                // so we advance once more to consume byte 3 before zapping.
                case 0xe0:
                    pExtChar = p;
                    p++;
                    if (p < end) { // else: sequence truncated at slice end — leave untouched
                    if (clean) {
                        switch (*pExtChar) {
                        case 0xa0: // Samaritan
                        case 0xa1: // Mandaic
                        case 0xa3: // Syriac Supplement
                        case 0xa4: // Devanagari Extended
                        case 0xa5: // Devanagari
                        case 0xa6: // Bengali
                        case 0xa7: // Gurmukhi
                        case 0xa8: // Gujarati
                        case 0xa9: // Oriya
                        case 0xaa: // Tamil
                        case 0xab: // Telugu
                        case 0xac: // Kannada
                        case 0xad: // Malayalam
                        case 0xae: // Sinhala
                        case 0xaf: // Thai
                        case 0xb0: // Lao
                        case 0xb1: // Tibetan
                        case 0xb2: // Myanmar
                        case 0xb3: // Georgian (old range)
                        case 0xb4: // Hangul Jamo
                        case 0xb5: // Ethiopic
                        case 0xb6: // Ethiopic Supplement
                        case 0xb7: // Cherokee
                        case 0xb8: // Unified Canadian Aboriginal Syllabics
                        case 0xb9: // Ogham
                        case 0xba: // Runic
                        case 0xbb: // Tagalog / Hanunoo / Buhid / Tagbanwa
                        case 0xbc: // Khmer
                        case 0xbd: // Mongolian
                        case 0xbe: // Unified Canadian Aboriginal Syllabics Ext
                        case 0xbf: // Limbu / Tai Le
                            // Zap all three bytes: pExtChar-1 (0xe0), pExtChar, p
                            *(pExtChar - 1) = *pExtChar = *p = ZapChr;
                            break;
                        default:
                            break;
                        }
                    }
                    }
                    break;

                case 0xe1: // Three byte code — Latin/Greek extended, Georgian, etc.
                    pExtChar = p;
                    p++;
                    if (p < end) { // else: sequence truncated at slice end — leave untouched
                    switch (*pExtChar) {
                    case 0x82: // Georgian
                        if ((*p >= 0xa0)
                            && (*p <= 0xbf)) {
                            *pExtChar = 0x83;
                            (*p) -= 0x10;
                        }
                        break;
                    case 0x83: // Georgian
                        if ((*p >= 0x80)
                            && ((*p <= 0x85)
                                || (*p == 0x87))
                            || (*p == 0x8d))
                            (*p) += 0x30;
                        break;
                    case 0xb8: // Latin Extended Additional
                        if ((*p >= 0x80)
                            && (*p <= 0xbf)
                            && (!(*p % 2))) // Even
                            (*p)++; // Next char is lwr
                        break;
                    case 0xb9: // Latin Extended Additional
                        if ((*p >= 0x80)
                            && (*p <= 0xbf)
                            && (!(*p % 2))) // Even
                            (*p)++; // Next char is lwr
                        break;
                    case 0xba: // Latin Extended Additional
                        if ((*p >= 0x80)
                            && (*p <= 0x94)
                            && (!(*p % 2))) // Even
                            (*p)++; // Next char is lwr
                        else if ((*p >= 0x9e)
                            && (*p <= 0xbf)
                            && (!(*p % 2))) // Even
                            (*p)++; // Next char is lwr
                        break;
                    case 0xbb: // Latin Extended Additional
                        if ((*p >= 0x80)
                            && (*p <= 0xbf)
                            && (!(*p % 2))) // Even
                            (*p)++; // Next char is lwr
                        break;
                    case 0xbc: // Greek Extended: ALPHA/EPSILON/ETA/IOTA
                        switch (*p) {
                        case 0x88: // GREEK CAPITAL LETTER ALPHA WITH PSILI
                            (*p) = 0x80;
                            break;
                        case 0x89: // GREEK CAPITAL LETTER ALPHA WITH DASIA
                            (*p) = 0x81;
                            break;
                        case 0x8a: // GREEK CAPITAL LETTER ALPHA WITH PSILI AND VARIA
                            (*p) = 0x82;
                            break;
                        case 0x8b: // GREEK CAPITAL LETTER ALPHA WITH DASIA AND VARIA
                            (*p) = 0x83;
                            break;
                        case 0x8c: // GREEK CAPITAL LETTER ALPHA WITH PSILI AND OXIA
                            (*p) = 0x84;
                            break;
                        case 0x8d: // GREEK CAPITAL LETTER ALPHA WITH DASIA AND OXIA
                            (*p) = 0x85;
                            break;
                        case 0x8e: // GREEK CAPITAL LETTER ALPHA WITH PSILI AND PERISPOMENI
                            (*p) = 0x86;
                            break;
                        case 0x8f: // GREEK CAPITAL LETTER ALPHA WITH DASIA AND PERISPOMENI
                            (*p) = 0x87;
                            break;
                        case 0x98: // GREEK CAPITAL LETTER EPSILON WITH PSILI
                            (*p) = 0x90;
                            break;
                        case 0x99: // GREEK CAPITAL LETTER EPSILON WITH DASIA
                            (*p) = 0x91;
                            break;
                        case 0x9a: // GREEK CAPITAL LETTER EPSILON WITH PSILI AND VARIA
                            (*p) = 0x92;
                            break;
                        case 0x9b: // GREEK CAPITAL LETTER EPSILON WITH DASIA AND VARIA
                            (*p) = 0x93;
                            break;
                        case 0x9c: // GREEK CAPITAL LETTER EPSILON WITH PSILI AND OXIA
                            (*p) = 0x94;
                            break;
                        case 0x9d: // GREEK CAPITAL LETTER EPSILON WITH DASIA AND OXIA
                            (*p) = 0x95;
                            break;
                        case 0xa8: // GREEK CAPITAL LETTER ETA WITH PSILI
                            (*p) = 0xa0;
                            break;
                        case 0xa9: // GREEK CAPITAL LETTER ETA WITH DASIA
                            (*p) = 0xa1;
                            break;
                        case 0xaa: // GREEK CAPITAL LETTER ETA WITH PSILI AND VARIA
                            (*p) = 0xa2;
                            break;
                        case 0xab: // GREEK CAPITAL LETTER ETA WITH DASIA AND VARIA
                            (*p) = 0xa3;
                            break;
                        case 0xac: // GREEK CAPITAL LETTER ETA WITH PSILI AND OXIA
                            (*p) = 0xa4;
                            break;
                        case 0xad: // GREEK CAPITAL LETTER ETA WITH DASIA AND OXIA
                            (*p) = 0xa5;
                            break;
                        case 0xae: // GREEK CAPITAL LETTER ETA WITH PSILI AND PERISPOMENI
                            (*p) = 0xa6;
                            break;
                        case 0xaf: // GREEK CAPITAL LETTER ETA WITH DASIA AND PERISPOMENI
                            (*p) = 0xa7;
                            break;
                        case 0xb8: // GREEK CAPITAL LETTER IOTA WITH PSILI
                            (*p) = 0xb0;
                            break;
                        case 0xb9: // GREEK CAPITAL LETTER IOTA WITH DASIA
                            (*p) = 0xb1;
                            break;
                        case 0xba: // GREEK CAPITAL LETTER IOTA WITH PSILI AND VARIA
                            (*p) = 0xb2;
                            break;
                        case 0xbb: // GREEK CAPITAL LETTER IOTA WITH DASIA AND VARIA
                            (*p) = 0xb3;
                            break;
                        case 0xbc: // GREEK CAPITAL LETTER IOTA WITH PSILI AND OXIA
                            (*p) = 0xb4;
                            break;
                        case 0xbd: // GREEK CAPITAL LETTER IOTA WITH DASIA AND OXIA
                            (*p) = 0xb5;
                            break;
                        case 0xbe: // GREEK CAPITAL LETTER IOTA WITH PSILI AND PERISPOMENI
                            (*p) = 0xb6;
                            break;
                        case 0xbf: // GREEK CAPITAL LETTER IOTA WITH DASIA AND PERISPOMENI
                            (*p) = 0xb7;
                            break;
                        default:
                            break;
                        }
                        break;

                    case 0xbd: // Greek Extended: OMICRON/UPSILON/OMEGA
                        switch (*p) {
                        case 0x88: // GREEK CAPITAL LETTER OMICRON WITH PSILI
                            (*p) = 0x80;
                            break;
                        case 0x89: // GREEK CAPITAL LETTER OMICRON WITH DASIA
                            (*p) = 0x81;
                            break;
                        case 0x8a: // GREEK CAPITAL LETTER OMICRON WITH PSILI AND VARIA
                            (*p) = 0x82;
                            break;
                        case 0x8b: // GREEK CAPITAL LETTER OMICRON WITH DASIA AND VARIA
                            (*p) = 0x83;
                            break;
                        case 0x8c: // GREEK CAPITAL LETTER OMICRON WITH PSILI AND OXIA
                            (*p) = 0x84;
                            break;
                        case 0x8d: // GREEK CAPITAL LETTER OMICRON WITH DASIA AND OXIA
                            (*p) = 0x85;
                            break;
                        case 0x99: // GREEK CAPITAL LETTER UPSILON WITH DASIA
                            (*p) = 0x91;
                            break;
                        case 0x9b: // GREEK CAPITAL LETTER UPSILON WITH DASIA AND VARIA
                            (*p) = 0x93;
                            break;
                        case 0x9d: // GREEK CAPITAL LETTER UPSILON WITH DASIA AND OXIA
                            (*p) = 0x95;
                            break;
                        case 0x9f: // GREEK CAPITAL LETTER UPSILON WITH DASIA AND PERISPOMENI
                            (*p) = 0x97;
                            break;
                        case 0xa8: // GREEK CAPITAL LETTER OMEGA WITH PSILI
                            (*p) = 0xa0;
                            break;
                        case 0xa9: // GREEK CAPITAL LETTER OMEGA WITH DASIA
                            (*p) = 0xa1;
                            break;
                        case 0xaa: // GREEK CAPITAL LETTER OMEGA WITH PSILI AND VARIA
                            (*p) = 0xa2;
                            break;
                        case 0xab: // GREEK CAPITAL LETTER OMEGA WITH DASIA AND VARIA
                            (*p) = 0xa3;
                            break;
                        case 0xac: // GREEK CAPITAL LETTER OMEGA WITH PSILI AND OXIA
                            (*p) = 0xa4;
                            break;
                        case 0xad: // GREEK CAPITAL LETTER OMEGA WITH DASIA AND OXIA
                            (*p) = 0xa5;
                            break;
                        case 0xae: // GREEK CAPITAL LETTER OMEGA WITH PSILI AND PERISPOMENI
                            (*p) = 0xa6;
                            break;
                        case 0xaf: // GREEK CAPITAL LETTER OMEGA WITH DASIA AND PERISPOMENI
                            (*p) = 0xa7;
                            break;
                        default:
                            break;
                        }
                        break;

                    case 0xbe: // Greek Extended: ALPHA+YPOGEGRAMMENI/ETA+YPOGEGRAMMENI/OMEGA+YPOGEGRAMMENI/ALPHA-MACRON-VRACHY/misplaced-VARIA-OXIA
                        switch (*p) {
                        case 0x88: // GREEK CAPITAL LETTER ALPHA WITH PSILI AND PROSGEGRAMMENI
                            (*p) = 0x80;
                            break;
                        case 0x89: // GREEK CAPITAL LETTER ALPHA WITH DASIA AND PROSGEGRAMMENI
                            (*p) = 0x81;
                            break;
                        case 0x8a: // GREEK CAPITAL LETTER ALPHA WITH PSILI AND VARIA AND PROSGEGRAMMENI
                            (*p) = 0x82;
                            break;
                        case 0x8b: // GREEK CAPITAL LETTER ALPHA WITH DASIA AND VARIA AND PROSGEGRAMMENI
                            (*p) = 0x83;
                            break;
                        case 0x8c: // GREEK CAPITAL LETTER ALPHA WITH PSILI AND OXIA AND PROSGEGRAMMENI
                            (*p) = 0x84;
                            break;
                        case 0x8d: // GREEK CAPITAL LETTER ALPHA WITH DASIA AND OXIA AND PROSGEGRAMMENI
                            (*p) = 0x85;
                            break;
                        case 0x8e: // GREEK CAPITAL LETTER ALPHA WITH PSILI AND PERISPOMENI AND PROSGEGRAMMENI
                            (*p) = 0x86;
                            break;
                        case 0x8f: // GREEK CAPITAL LETTER ALPHA WITH DASIA AND PERISPOMENI AND PROSGEGRAMMENI
                            (*p) = 0x87;
                            break;
                        case 0x98: // GREEK CAPITAL LETTER ETA WITH PSILI AND PROSGEGRAMMENI
                            (*p) = 0x90;
                            break;
                        case 0x99: // GREEK CAPITAL LETTER ETA WITH DASIA AND PROSGEGRAMMENI
                            (*p) = 0x91;
                            break;
                        case 0x9a: // GREEK CAPITAL LETTER ETA WITH PSILI AND VARIA AND PROSGEGRAMMENI
                            (*p) = 0x92;
                            break;
                        case 0x9b: // GREEK CAPITAL LETTER ETA WITH DASIA AND VARIA AND PROSGEGRAMMENI
                            (*p) = 0x93;
                            break;
                        case 0x9c: // GREEK CAPITAL LETTER ETA WITH PSILI AND OXIA AND PROSGEGRAMMENI
                            (*p) = 0x94;
                            break;
                        case 0x9d: // GREEK CAPITAL LETTER ETA WITH DASIA AND OXIA AND PROSGEGRAMMENI
                            (*p) = 0x95;
                            break;
                        case 0x9e: // GREEK CAPITAL LETTER ETA WITH PSILI AND PERISPOMENI AND PROSGEGRAMMENI
                            (*p) = 0x96;
                            break;
                        case 0x9f: // GREEK CAPITAL LETTER ETA WITH DASIA AND PERISPOMENI AND PROSGEGRAMMENI
                            (*p) = 0x97;
                            break;
                        case 0xa8: // GREEK CAPITAL LETTER OMEGA WITH PSILI AND PROSGEGRAMMENI
                            (*p) = 0xa0;
                            break;
                        case 0xa9: // GREEK CAPITAL LETTER OMEGA WITH DASIA AND PROSGEGRAMMENI
                            (*p) = 0xa1;
                            break;
                        case 0xaa: // GREEK CAPITAL LETTER OMEGA WITH PSILI AND VARIA AND PROSGEGRAMMENI
                            (*p) = 0xa2;
                            break;
                        case 0xab: // GREEK CAPITAL LETTER OMEGA WITH DASIA AND VARIA AND PROSGEGRAMMENI
                            (*p) = 0xa3;
                            break;
                        case 0xac: // GREEK CAPITAL LETTER OMEGA WITH PSILI AND OXIA AND PROSGEGRAMMENI
                            (*p) = 0xa4;
                            break;
                        case 0xad: // GREEK CAPITAL LETTER OMEGA WITH DASIA AND OXIA AND PROSGEGRAMMENI
                            (*p) = 0xa5;
                            break;
                        case 0xae: // GREEK CAPITAL LETTER OMEGA WITH PSILI AND PERISPOMENI AND PROSGEGRAMMENI
                            (*p) = 0xa6;
                            break;
                        case 0xaf: // GREEK CAPITAL LETTER OMEGA WITH DASIA AND PERISPOMENI AND PROSGEGRAMMENI
                            (*p) = 0xa7;
                            break;
                        case 0xb8: // GREEK CAPITAL LETTER ALPHA WITH VRACHY
                            (*p) = 0xb0;
                            break;
                        case 0xb9: // GREEK CAPITAL LETTER ALPHA WITH MACRON
                            (*p) = 0xb1;
                            break;
                        case 0xba: // GREEK CAPITAL LETTER ALPHA WITH VARIA (crosses to byte2 0xbd)
                            *pExtChar = 0xbd;
                            (*p) = 0xb0;
                            break;
                        case 0xbb: // GREEK CAPITAL LETTER ALPHA WITH OXIA (crosses to byte2 0xbd)
                            *pExtChar = 0xbd;
                            (*p) = 0xb1;
                            break;
                        case 0xbc: // GREEK CAPITAL LETTER ALPHA WITH PROSGEGRAMMENI
                            (*p) = 0xb3;
                            break;
                        case 0xbd: // GREEK KORONIS (Sk) -- not a letter
                            if (clean)
                                *(pExtChar - 1) = *pExtChar = *p = ZapChr;
                            break;
                        case 0xbf: // GREEK PSILI (Sk) -- not a letter
                            if (clean)
                                *(pExtChar - 1) = *pExtChar = *p = ZapChr;
                            break;
                        default:
                            break;
                        }
                        break;

                    case 0xbf: // Greek Extended: ETA+YPOGEGRAMMENI/IOTA-MACRON-VRACHY/UPSILON-MACRON-VRACHY/RHO+DASIA/OMEGA+YPOGEGRAMMENI/misplaced-VARIA-OXIA
                        switch (*p) {
                        case 0x80: // GREEK PERISPOMENI (Sk) -- not a letter
                            if (clean)
                                *(pExtChar - 1) = *pExtChar = *p = ZapChr;
                            break;
                        case 0x81: // GREEK DIALYTIKA AND PERISPOMENI (Sk) -- not a letter
                            if (clean)
                                *(pExtChar - 1) = *pExtChar = *p = ZapChr;
                            break;
                        case 0x88: // GREEK CAPITAL LETTER EPSILON WITH VARIA (crosses to byte2 0xbd)
                            *pExtChar = 0xbd;
                            (*p) = 0xb2;
                            break;
                        case 0x89: // GREEK CAPITAL LETTER EPSILON WITH OXIA (crosses to byte2 0xbd)
                            *pExtChar = 0xbd;
                            (*p) = 0xb3;
                            break;
                        case 0x8a: // GREEK CAPITAL LETTER ETA WITH VARIA (crosses to byte2 0xbd)
                            *pExtChar = 0xbd;
                            (*p) = 0xb4;
                            break;
                        case 0x8b: // GREEK CAPITAL LETTER ETA WITH OXIA (crosses to byte2 0xbd)
                            *pExtChar = 0xbd;
                            (*p) = 0xb5;
                            break;
                        case 0x8c: // GREEK CAPITAL LETTER ETA WITH PROSGEGRAMMENI
                            (*p) = 0x83;
                            break;
                        case 0x8d: // GREEK PSILI AND VARIA (Sk) -- not a letter
                            if (clean)
                                *(pExtChar - 1) = *pExtChar = *p = ZapChr;
                            break;
                        case 0x8e: // GREEK PSILI AND OXIA (Sk) -- not a letter
                            if (clean)
                                *(pExtChar - 1) = *pExtChar = *p = ZapChr;
                            break;
                        case 0x8f: // GREEK PSILI AND PERISPOMENI (Sk) -- not a letter
                            if (clean)
                                *(pExtChar - 1) = *pExtChar = *p = ZapChr;
                            break;
                        case 0x98: // GREEK CAPITAL LETTER IOTA WITH VRACHY
                            (*p) = 0x90;
                            break;
                        case 0x99: // GREEK CAPITAL LETTER IOTA WITH MACRON
                            (*p) = 0x91;
                            break;
                        case 0x9a: // GREEK CAPITAL LETTER IOTA WITH VARIA (crosses to byte2 0xbd)
                            *pExtChar = 0xbd;
                            (*p) = 0xb6;
                            break;
                        case 0x9b: // GREEK CAPITAL LETTER IOTA WITH OXIA (crosses to byte2 0xbd)
                            *pExtChar = 0xbd;
                            (*p) = 0xb7;
                            break;
                        case 0x9d: // GREEK DASIA AND VARIA (Sk) -- not a letter
                            if (clean)
                                *(pExtChar - 1) = *pExtChar = *p = ZapChr;
                            break;
                        case 0x9e: // GREEK DASIA AND OXIA (Sk) -- not a letter
                            if (clean)
                                *(pExtChar - 1) = *pExtChar = *p = ZapChr;
                            break;
                        case 0x9f: // GREEK DASIA AND PERISPOMENI (Sk) -- not a letter
                            if (clean)
                                *(pExtChar - 1) = *pExtChar = *p = ZapChr;
                            break;
                        case 0xa8: // GREEK CAPITAL LETTER UPSILON WITH VRACHY
                            (*p) = 0xa0;
                            break;
                        case 0xa9: // GREEK CAPITAL LETTER UPSILON WITH MACRON
                            (*p) = 0xa1;
                            break;
                        case 0xaa: // GREEK CAPITAL LETTER UPSILON WITH VARIA (crosses to byte2 0xbd)
                            *pExtChar = 0xbd;
                            (*p) = 0xba;
                            break;
                        case 0xab: // GREEK CAPITAL LETTER UPSILON WITH OXIA (crosses to byte2 0xbd)
                            *pExtChar = 0xbd;
                            (*p) = 0xbb;
                            break;
                        case 0xac: // GREEK CAPITAL LETTER RHO WITH DASIA
                            (*p) = 0xa5;
                            break;
                        case 0xad: // GREEK DIALYTIKA AND VARIA (Sk) -- not a letter
                            if (clean)
                                *(pExtChar - 1) = *pExtChar = *p = ZapChr;
                            break;
                        case 0xae: // GREEK DIALYTIKA AND OXIA (Sk) -- not a letter
                            if (clean)
                                *(pExtChar - 1) = *pExtChar = *p = ZapChr;
                            break;
                        case 0xaf: // GREEK VARIA (Sk) -- not a letter
                            if (clean)
                                *(pExtChar - 1) = *pExtChar = *p = ZapChr;
                            break;
                        case 0xb8: // GREEK CAPITAL LETTER OMICRON WITH VARIA (crosses to byte2 0xbd)
                            *pExtChar = 0xbd;
                            (*p) = 0xb8;
                            break;
                        case 0xb9: // GREEK CAPITAL LETTER OMICRON WITH OXIA (crosses to byte2 0xbd)
                            *pExtChar = 0xbd;
                            (*p) = 0xb9;
                            break;
                        case 0xba: // GREEK CAPITAL LETTER OMEGA WITH VARIA (crosses to byte2 0xbd)
                            *pExtChar = 0xbd;
                            (*p) = 0xbc;
                            break;
                        case 0xbb: // GREEK CAPITAL LETTER OMEGA WITH OXIA (crosses to byte2 0xbd)
                            *pExtChar = 0xbd;
                            (*p) = 0xbd;
                            break;
                        case 0xbc: // GREEK CAPITAL LETTER OMEGA WITH PROSGEGRAMMENI
                            (*p) = 0xb3;
                            break;
                        case 0xbd: // GREEK OXIA (Sk) -- not a letter
                            if (clean)
                                *(pExtChar - 1) = *pExtChar = *p = ZapChr;
                            break;
                        case 0xbe: // GREEK DASIA (Sk) -- not a letter
                            if (clean)
                                *(pExtChar - 1) = *pExtChar = *p = ZapChr;
                            break;
                        default:
                            break;
                        }
                        break;

                    default:
                        break;
                    }
                    }
                    break;

                case 0xf0: // Four byte code
                    pExtChar = p;
                    p++;
                    if (p < end) { // else: sequence truncated at slice end — leave untouched
                    switch (*pExtChar) {
                    case 0x90:
                        pExtChar = p;
                        p++;
                        if (p < end) { // else: sequence truncated at slice end — leave untouched
                        switch (*pExtChar) {
                        case 0x92: // Osage uppercase
                            if ((*p >= 0xb0)
                                && (*p <= 0xbf)) {
                                *pExtChar = 0x93;
                                (*p) -= 0x18;
                            }
                            break;
                        case 0x93: // Osage lowercase range
                            if ((*p >= 0x80)
                                && (*p <= 0x93))
                                (*p) += 0x18;
                            break;
                        default:
                            break;
                        }
                        }
                        break;
                    case 0x9e: // FIX: was stray case outside this switch
                        pExtChar = p;
                        p++;
                        if (p < end) { // else: sequence truncated at slice end — leave untouched
                        switch (*pExtChar) {
                        case 0xa4: // Adlam uppercase
                            if ((*p >= 0x80)
                                && (*p <= 0xa1))
                                (*p) += 0x22;
                            break;
                        default:
                            break;
                        }
                        }
                        break;
                    default:
                        break;
                    }
                    }
                    break;

                default:
                    break;
                }
                }
                pExtChar = 0;
            }
            p++;
        }
    }
    return pString;
}
#endif



// Convert a UTF-8 buffer slice to lower case in-place. Buffer-length
// variant for the word parser: pString is NOT NUL-terminated and MUST
// NOT be read past pString[length-1]. There is no *p/*pString sentinel
// check anywhere in this function — length is the only end-of-data
// signal. Callers that still have a NUL-terminated C string should keep
// using _utf_StrToLower(); this variant exists specifically for slices
// carved out of a larger buffer (e.g. word-parser spans) where a NUL
// byte may not exist at all, or where a stray 0x00 inside the slice
// must NOT be treated as a terminator.
//
// EMBEDDED NUL BYTES ARE DATA, NOT A TERMINATOR
//   Unlike the original, this variant may be handed a buffer that
//   already has 0x00 bytes inside [pString, pString+length) — e.g. once
//   ParseWords' word-separator zapping is merged in to save a pass,
//   IsWordSep()/!IsTermChr() positions are zapped to '\0' rather than
//   left as their original byte. 0x00 is never treated as a stop
//   condition anywhere in the loop or in any of the byte-value
//   comparisons below (0x00 simply fails every UTF-8 lead/continuation
//   range test, the same way any other non-matching byte would).
//
// length — byte length of the region to process. Mandatory, always a
//   byte count, never a codepoint count. Zero-length is a valid no-op.
//   This is an exact length, not an upper bound on some larger
//   NUL-terminated string: contrast with _utf_StrToLower()'s optional
//   length, which the caller guarantees is <= strlen(pString).
//
// ZapChr — the fill byte written in "clean" mode wherever the original
//   hardcoded space (_SP). Pass _SP for the historic behaviour (safe
//   for plain-text indexing), or '\0' once this routine is merged with
//   ParseWords' tokenization pass, so cleaning and word-separator
//   zapping agree on one sentinel value instead of needing a second
//   pass to convert one into the other.
//
// OUT-OF-BOUNDS SAFETY CONTRACT
//   Every lookahead byte (2nd/3rd/4th byte of a multi-byte sequence) is
//   guarded by a "p < end" check before it is ever dereferenced. If a
//   multi-byte sequence is truncated by the end of the slice (i.e. the
//   lead byte is the last byte in [pString, pString+length)), that
//   trailing partial sequence is left untouched rather than read past
//   the boundary. This differs from _utf_StrToLower(), which relies on
//   the NUL terminator to make the equivalent unguarded reads safe.
//
// LENGTH-STABILITY CONTRACT
//   Every uppercase→lowercase mapping in this function must produce a UTF-8
//   sequence of exactly the same byte length as the original.  This is
//   required because callers (indexers, _utf_strncasecmp) rely on byte
//   offsets into the original string remaining valid after lowercasing.
//   All current mappings satisfy this: they only mutate bytes within an
//   already-decoded sequence of fixed length (e.g. 0xce→0xcf is still a
//   2-byte lead, 0xe1 0x82→0xe1 0x83 is still a 3-byte sequence).
//
//   If a future script requires a mapping that changes sequence length
//   (e.g. a 3-byte uppercase to a 2-byte lowercase), the shorter result
//   MUST be padded with trailing ZapChr bytes to fill the original byte
//   footprint, preserving the byte offset of every subsequent codepoint.
//   Use _UTF8_LOWER_PAD() for that case:
//
//     _UTF8_LOWER_PAD(seq_start, new_len, old_len, fill)
//       Fills bytes [new_len .. old_len-1] at seq_start with fill (pass
//       ZapChr from the caller, not a hardcoded _SP).
//       seq_start must point to the lead byte of the original sequence.
//#define _UTF8_LOWER_PAD(base, new_len, old_len, fill) \
//    do { for (int _i = (new_len); _i < (old_len); _i++) (base)[_i] = (fill); } while (0)

// _IB_UTF8_SILENT_ZAP
//
// A SECOND, fixed sentinel distinct from the caller-supplied ZapChr.
// ZapChr marks genuine word-breaking separators (space, punctuation,
// symbols) -- content the tokenizer should treat as ending a word.
// _IB_UTF8_SILENT_ZAP marks content that is zapped for the same reason
// (it's not a letter/digit) but must NOT break a word: combining marks
// (Unicode category Mn/Me -- accents, niqqud, tashkeel, and anything
// else that's logically attached to the letter it modifies rather than
// a character in its own right) and format characters (Cf -- invisible
// joiners/marks). "שלום" with niqqud, or "café" typed as e + combining
// acute, must fold to one continuous term with the mark silently
// removed, not fracture into fragments at every stripped mark.
//
// Fixed rather than caller-configurable because, unlike ZapChr (which
// legitimately differs by context -- ' ' for plain-text indexing, '\0'
// once merged with tokenization), this sentinel's meaning never changes:
// it always means "skip this byte, do not end the word". Chosen as 0x01
// (SOH) specifically because it can never collide with either value
// ZapChr is actually used with in this codebase (' ' or '\0'); if a
// caller ever needs ZapChr == 0x01 for some reason, this constant needs
// to move to a value that isn't.
//
// _ib_IsUTF8TermChrFast and the ParseWordsUTF8 tokenizer both need to
// know about this constant -- see their own comments for how each uses
// it. Keep this definition in sync if it's hoisted into a shared header.
#define _IB_UTF8_SILENT_ZAP ((unsigned char)0x01)

unsigned char *_utf_StrToLowerBuf(unsigned char *pString, unsigned length, const bool clean, unsigned char ZapChr)
{
    if (pString && length) {
        unsigned char       *p   = pString;
        const unsigned char *end = pString + length;
        unsigned char *pExtChar = 0;
        while (p < end) {
            if (*p < 0x20) {
                if (clean) *p = ZapChr; // Zap control chars
            } else if ((*p >= 0x41) && (*p <= 0x5a)) // US ASCII
                (*p) += 0x20;
            else if (*p > 0xc0) {
                pExtChar = p;
                p++;
                if (p < end) { // else: lead byte truncated at slice end — leave untouched
                switch (*pExtChar) {
                case 0xc2: // Latin-1 Supplement controls & punctuation — this
                           // block (U+0080-00BF: C1 controls, NBSP, ¡ ¢ £ ¤ ¥
                           // ¦ § ¨ © ª « ¬ SHY ® ¯ ° ± ² ³ ´ µ ¶ · ¸ ¹ º » ¼ ½
                           // ¾ ¿) contains ZERO letters. Previously fell to
                           // default: break and passed through unzapped even
                           // with clean=true — breaks any "survived clean ⇒
                           // term char" shortcut downstream.
                    if (clean)
                        *pExtChar = *p = ZapChr;
                    break;
                case 0xc3: // Latin 1
                    if ((*p >= 0x80)
                        && (*p <= 0x9e)
                        && (*p != 0x97))
                        (*p) += 0x20; // US ASCII shift
                    else if (clean && ((*p == 0x97) || (*p == 0xb7)))
                        // × U+00D7 MULTIPLICATION SIGN and ÷ U+00F7 DIVISION
                        // SIGN are symbols, not letters, hiding inside this
                        // otherwise-letters block. Previously fell through
                        // untouched even with clean=true.
                        *pExtChar = *p = ZapChr;
                    break;
                case 0xc4: // Latin Extended
                    if ((*p >= 0x80)
                        && (*p <= 0xb7)
                        && (!(*p % 2))) // Even
                        (*p)++; // Next char is lwr
                    else if ((*p >= 0xb9)
                        && (*p <= 0xbe)
                        && (*p % 2)) // Odd
                        (*p)++; // Next char is lwr
                    else if (*p == 0xbf) {
                        *pExtChar = 0xc5;
                        (*p) = 0x80;
                    }
                    break;
                case 0xc5: // Latin Extended
                    if ((*p >= 0x80)
                        && (*p <= 0x88)
                        && (*p % 2)) // Odd
                        (*p)++; // Next char is lwr
                    else if ((*p >= 0x8a)
                        && (*p <= 0xb7)
                        && (!(*p % 2))) // Even
                        (*p)++; // Next char is lwr
                    else if ((*p >= 0xb9)
                        && (*p <= 0xbe)
                        && (*p % 2)) // Odd
                        (*p)++; // Next char is lwr
                    break;
                case 0xc6: // Latin Extended
                    switch (*p) {
                    case 0x82: case 0x84: case 0x87: case 0x8b: case 0x91:
                    case 0x98: case 0xa0: case 0xa2: case 0xa4: case 0xa7:
                    case 0xac: case 0xaf: case 0xb3: case 0xb5: case 0xb8:
                    case 0xbc:
                        (*p)++; // Next char is lwr
                        break;
                    default:
                        break;
                    }
                    break;
                case 0xc7: // Latin Extended
                    if (*p == 0x84)
                        (*p) = 0x86;
                    else if (*p == 0x85)
                        (*p)++; // Next char is lwr
                    else if (*p == 0x87)
                        (*p) = 0x89;
                    else if (*p == 0x88)
                        (*p)++; // Next char is lwr
                    else if (*p == 0x8a)
                        (*p) = 0x8c;
                    else if (*p == 0x8b)
                        (*p)++; // Next char is lwr
                    else if ((*p >= 0x8d)
                        && (*p <= 0x9c)
                        && (*p % 2)) // Odd
                        (*p)++; // Next char is lwr
                    else if ((*p >= 0x9e)
                        && (*p <= 0xaf)
                        && (!(*p % 2))) // Even
                        (*p)++; // Next char is lwr
                    else if (*p == 0xb1)
                        (*p) = 0xb3;
                    else if (*p == 0xb2)
                        (*p)++; // Next char is lwr
                    else if (*p == 0xb4)
                        (*p)++; // Next char is lwr
                    else if (*p == 0xb8)
                        (*p)++; // Next char is lwr
                    else if (*p == 0xba)
                        (*p)++; // Next char is lwr
                    else if (*p == 0xbc)
                        (*p)++; // Next char is lwr
                    else if (*p == 0xbe)
                        (*p)++; // Next char is lwr
                    break;
                case 0xc8: // Latin Extended
                    if ((*p >= 0x80)
                        && (*p <= 0x9f)
                        && (!(*p % 2))) // Even
                        (*p)++; // Next char is lwr
                    else if ((*p >= 0xa2)
                        && (*p <= 0xb3)
                        && (!(*p % 2))) // Even
                        (*p)++; // Next char is lwr
                    else if (*p == 0xbb)
                        (*p)++; // Next char is lwr
                    break;

                // Latin modifier letters (U+02C2..U+02FF range subset)
                case 0xcb:
                    if (clean && (
                        (*p >= 0x82 && *p <= 0x85) ||
                        (*p >= 0x92 && *p <= 0x9f) ||
                        (*p >= 0xa5 && *p <= 0xab) ||
                        (*p >= 0xaf && *p <= 0xbf)))
                        *pExtChar = *p = ZapChr;
                    break;

                // Combining Diacritical Marks (U+0300..U+036F)
                case 0xcc: // U+0300-033F: Combining Diacritical Marks (all Mn) --
                           // silent, must not break a word (e.g. NFD "café"
                           // as e + combining acute must stay one term).
                    if (clean && (*p >= 0x80 && *p <= 0xbf))
                        *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                    break;

                case 0xcd:
                    if ((*p >= 0x80)
                        && (*p <= 0xaf)) {
                        // U+0340-036F: tail of the Combining Diacritical
                        // Marks block (same block as case 0xcc, split
                        // across the 0xCC/0xCD lead-byte boundary).
                        // Includes COMBINING GREEK PERISPOMENI (U+0342),
                        // COMBINING GREEK KORONIS (U+0343), COMBINING
                        // GREEK DIALYTIKA TONOS (U+0344), and COMBINING
                        // GREEK YPOGEGRAMMENI (U+0345) — the marks used
                        // for decomposed/NFD polytonic Greek. Silent --
                        // same rule as case 0xcc -- must not break a word.
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    }
                    switch (*p) {
                    case 0xb0: // HETA
                    case 0xb2: // ARCHAIC SAMPI
                    case 0xb6: // PAMPHYLIAN DIGAMMA
                        (*p)++; // Next char is lwr
                        break;
                    case 0xb4: // GREEK NUMERAL SIGN (Lm) — not a full letter
                    case 0xb5: // GREEK LOWER NUMERAL SIGN (Sk)
                    case 0xba: // GREEK YPOGEGRAMMENI, spacing form (Lm)
                    case 0xbe: // GREEK QUESTION MARK (Po) — looks like ';'
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    default:
                        if (*p == 0xbf) { // GREEK CAPITAL LETTER YOT
                            *pExtChar = 0xcf;
                            (*p) = 0xb3; // -> U+03F3 GREEK LETTER YOT
                        }
                        break;
                    }
                    break;
                case 0xce: // Greek & Coptic
                    if (*p == 0x86)
                        (*p) = 0xac;
                    else if (*p == 0x88)
                        (*p) = 0xad;
                    else if (*p == 0x89)
                        (*p) = 0xae;
                    else if (*p == 0x8a)
                        (*p) = 0xaf;
                    else if (*p == 0x8c) {
                        *pExtChar = 0xcf;
                        (*p) = 0x8c;
                    }
                    else if (*p == 0x8e) {
                        *pExtChar = 0xcf;
                        (*p) = 0x8d;
                    }
                    else if (*p == 0x8f) {
                        *pExtChar = 0xcf;
                        (*p) = 0x8e;
                    }
                    else if ((*p >= 0x91)
                        && (*p <= 0x9f))
                        (*p) += 0x20; // US ASCII shift
                    else if ((*p >= 0xa0)
                        && (*p <= 0xab)
                        && (*p != 0xa2)) {
                        *pExtChar = 0xcf;
                        (*p) -= 0x20;
                    }
                    else if (clean) {
                        switch (*p) {
                        // Zap Greek punctuation/diacritics that are not letters
                        case 0x84: // Greek Tonos
                        case 0x85: // Greek Dialytika Tonos
                        case 0x87: // Greek Ano Teleia (U+0387) — punctuation,
                                   // functions like a semicolon. NOT the
                                   // same as COMBINING GREEK PERISPOMENI
                                   // (U+0342, a real polytonic accent) —
                                   // that lives in case 0xcd's combining-
                                   // marks range and is zapped there.
                            *pExtChar = *p = ZapChr;
                            break;
                        }
                    }
                    break;
                case 0xcf: // Greek & Coptic
                    if (*p == 0x8f)
                        (*p) = 0x97; // FIX: Ϗ U+03CF -> ϗ U+03D7 (was 0xb4,
                                     // which corrupted Kai Symbol into
                                     // Capital Theta Symbol). This ligature
                                     // for "και" appears in papyri and
                                     // inscriptions.
                    // FIX: removed erroneous "else if (*p == 0x91) (*p)++;"
                    // here. U+03D1 (ϑ GREEK THETA SYMBOL) is ALREADY
                    // lowercase (its simple lowercase mapping is itself) —
                    // it must pass through unchanged. The removed line
                    // mutated it into U+03D2 (ϒ GREEK UPSILON WITH HOOK
                    // SYMBOL), an unrelated, unattested letter used in
                    // classical textual-criticism apparatus.
                    else if ((*p >= 0x98)
                        && (*p <= 0xaf)
                        && (!(*p % 2))) // Even
                        (*p)++; // Next char is lwr
                    else if (*p == 0xb4) {
                        // FIX: Ϝ U+03F4 GREEK CAPITAL THETA SYMBOL's real
                        // simple lowercase mapping is U+03B8 (regular θ),
                        // NOT U+03D1 (the theta SYMBOL, a different, caseless
                        // glyph variant) — verified against UCD, not just
                        // the visual "these look related" assumption. That
                        // crosses back into case 0xce's block.
                        *pExtChar = 0xce;
                        (*p) = 0xb8;
                    }
                    else if (*p == 0xb7)
                        (*p)++; // Next char is lwr
                    else if (*p == 0xb9)
                        (*p) = 0xb2;
                    else if (*p == 0xba) // FIX: was erroneously keyed on 0xbb,
                        (*p)++;          // which corrupted the ALREADY-lower
                                         // Ϻ/ϻ SAN pair's small form into
                                         // Ϲ GREEK RHO WITH STROKE SYMBOL.
                                         // Ϻ U+03FA (capital SAN, this byte)
                                         // -> ϻ U+03FB (small SAN).
                    else if (*p == 0xbd) {
                        *pExtChar = 0xcd;
                        (*p) = 0xbb;
                    }
                    else if (*p == 0xbe) {
                        *pExtChar = 0xcd;
                        (*p) = 0xbc;
                    }
                    else if (*p == 0xbf) {
                        *pExtChar = 0xcd;
                        (*p) = 0xbd;
                    }
                    // Greek Reversed Lunate Epsilon Symbol
                    else if (clean && *p == 0xb6)
                        *pExtChar = (*p) = ZapChr;
                    break;
                case 0xd0:
                    switch (*p) {
                    case 0x80: // CYRILLIC CAPITAL LETTER IE WITH GRAVE (crosses to lead 0xd1)
                        *pExtChar = 0xd1;
                        (*p) = 0x90;
                        break;
                    case 0x81: // CYRILLIC CAPITAL LETTER IO (crosses to lead 0xd1)
                        *pExtChar = 0xd1;
                        (*p) = 0x91;
                        break;
                    case 0x82: // CYRILLIC CAPITAL LETTER DJE (crosses to lead 0xd1)
                        *pExtChar = 0xd1;
                        (*p) = 0x92;
                        break;
                    case 0x83: // CYRILLIC CAPITAL LETTER GJE (crosses to lead 0xd1)
                        *pExtChar = 0xd1;
                        (*p) = 0x93;
                        break;
                    case 0x84: // CYRILLIC CAPITAL LETTER UKRAINIAN IE (crosses to lead 0xd1)
                        *pExtChar = 0xd1;
                        (*p) = 0x94;
                        break;
                    case 0x85: // CYRILLIC CAPITAL LETTER DZE (crosses to lead 0xd1)
                        *pExtChar = 0xd1;
                        (*p) = 0x95;
                        break;
                    case 0x86: // CYRILLIC CAPITAL LETTER BYELORUSSIAN-UKRAINIAN I (crosses to lead 0xd1)
                        *pExtChar = 0xd1;
                        (*p) = 0x96;
                        break;
                    case 0x87: // CYRILLIC CAPITAL LETTER YI (crosses to lead 0xd1)
                        *pExtChar = 0xd1;
                        (*p) = 0x97;
                        break;
                    case 0x88: // CYRILLIC CAPITAL LETTER JE (crosses to lead 0xd1)
                        *pExtChar = 0xd1;
                        (*p) = 0x98;
                        break;
                    case 0x89: // CYRILLIC CAPITAL LETTER LJE (crosses to lead 0xd1)
                        *pExtChar = 0xd1;
                        (*p) = 0x99;
                        break;
                    case 0x8a: // CYRILLIC CAPITAL LETTER NJE (crosses to lead 0xd1)
                        *pExtChar = 0xd1;
                        (*p) = 0x9a;
                        break;
                    case 0x8b: // CYRILLIC CAPITAL LETTER TSHE (crosses to lead 0xd1)
                        *pExtChar = 0xd1;
                        (*p) = 0x9b;
                        break;
                    case 0x8c: // CYRILLIC CAPITAL LETTER KJE (crosses to lead 0xd1)
                        *pExtChar = 0xd1;
                        (*p) = 0x9c;
                        break;
                    case 0x8d: // CYRILLIC CAPITAL LETTER I WITH GRAVE (crosses to lead 0xd1)
                        *pExtChar = 0xd1;
                        (*p) = 0x9d;
                        break;
                    case 0x8e: // CYRILLIC CAPITAL LETTER SHORT U (crosses to lead 0xd1)
                        *pExtChar = 0xd1;
                        (*p) = 0x9e;
                        break;
                    case 0x8f: // CYRILLIC CAPITAL LETTER DZHE (crosses to lead 0xd1)
                        *pExtChar = 0xd1;
                        (*p) = 0x9f;
                        break;
                    case 0x90: // CYRILLIC CAPITAL LETTER A
                        (*p) = 0xb0;
                        break;
                    case 0x91: // CYRILLIC CAPITAL LETTER BE
                        (*p) = 0xb1;
                        break;
                    case 0x92: // CYRILLIC CAPITAL LETTER VE
                        (*p) = 0xb2;
                        break;
                    case 0x93: // CYRILLIC CAPITAL LETTER GHE
                        (*p) = 0xb3;
                        break;
                    case 0x94: // CYRILLIC CAPITAL LETTER DE
                        (*p) = 0xb4;
                        break;
                    case 0x95: // CYRILLIC CAPITAL LETTER IE
                        (*p) = 0xb5;
                        break;
                    case 0x96: // CYRILLIC CAPITAL LETTER ZHE
                        (*p) = 0xb6;
                        break;
                    case 0x97: // CYRILLIC CAPITAL LETTER ZE
                        (*p) = 0xb7;
                        break;
                    case 0x98: // CYRILLIC CAPITAL LETTER I
                        (*p) = 0xb8;
                        break;
                    case 0x99: // CYRILLIC CAPITAL LETTER SHORT I
                        (*p) = 0xb9;
                        break;
                    case 0x9a: // CYRILLIC CAPITAL LETTER KA
                        (*p) = 0xba;
                        break;
                    case 0x9b: // CYRILLIC CAPITAL LETTER EL
                        (*p) = 0xbb;
                        break;
                    case 0x9c: // CYRILLIC CAPITAL LETTER EM
                        (*p) = 0xbc;
                        break;
                    case 0x9d: // CYRILLIC CAPITAL LETTER EN
                        (*p) = 0xbd;
                        break;
                    case 0x9e: // CYRILLIC CAPITAL LETTER O
                        (*p) = 0xbe;
                        break;
                    case 0x9f: // CYRILLIC CAPITAL LETTER PE
                        (*p) = 0xbf;
                        break;
                    case 0xa0: // CYRILLIC CAPITAL LETTER ER (crosses to lead 0xd1)
                        *pExtChar = 0xd1;
                        (*p) = 0x80;
                        break;
                    case 0xa1: // CYRILLIC CAPITAL LETTER ES (crosses to lead 0xd1)
                        *pExtChar = 0xd1;
                        (*p) = 0x81;
                        break;
                    case 0xa2: // CYRILLIC CAPITAL LETTER TE (crosses to lead 0xd1)
                        *pExtChar = 0xd1;
                        (*p) = 0x82;
                        break;
                    case 0xa3: // CYRILLIC CAPITAL LETTER U (crosses to lead 0xd1)
                        *pExtChar = 0xd1;
                        (*p) = 0x83;
                        break;
                    case 0xa4: // CYRILLIC CAPITAL LETTER EF (crosses to lead 0xd1)
                        *pExtChar = 0xd1;
                        (*p) = 0x84;
                        break;
                    case 0xa5: // CYRILLIC CAPITAL LETTER HA (crosses to lead 0xd1)
                        *pExtChar = 0xd1;
                        (*p) = 0x85;
                        break;
                    case 0xa6: // CYRILLIC CAPITAL LETTER TSE (crosses to lead 0xd1)
                        *pExtChar = 0xd1;
                        (*p) = 0x86;
                        break;
                    case 0xa7: // CYRILLIC CAPITAL LETTER CHE (crosses to lead 0xd1)
                        *pExtChar = 0xd1;
                        (*p) = 0x87;
                        break;
                    case 0xa8: // CYRILLIC CAPITAL LETTER SHA (crosses to lead 0xd1)
                        *pExtChar = 0xd1;
                        (*p) = 0x88;
                        break;
                    case 0xa9: // CYRILLIC CAPITAL LETTER SHCHA (crosses to lead 0xd1)
                        *pExtChar = 0xd1;
                        (*p) = 0x89;
                        break;
                    case 0xaa: // CYRILLIC CAPITAL LETTER HARD SIGN (crosses to lead 0xd1)
                        *pExtChar = 0xd1;
                        (*p) = 0x8a;
                        break;
                    case 0xab: // CYRILLIC CAPITAL LETTER YERU (crosses to lead 0xd1)
                        *pExtChar = 0xd1;
                        (*p) = 0x8b;
                        break;
                    case 0xac: // CYRILLIC CAPITAL LETTER SOFT SIGN (crosses to lead 0xd1)
                        *pExtChar = 0xd1;
                        (*p) = 0x8c;
                        break;
                    case 0xad: // CYRILLIC CAPITAL LETTER E (crosses to lead 0xd1)
                        *pExtChar = 0xd1;
                        (*p) = 0x8d;
                        break;
                    case 0xae: // CYRILLIC CAPITAL LETTER YU (crosses to lead 0xd1)
                        *pExtChar = 0xd1;
                        (*p) = 0x8e;
                        break;
                    case 0xaf: // CYRILLIC CAPITAL LETTER YA (crosses to lead 0xd1)
                        *pExtChar = 0xd1;
                        (*p) = 0x8f;
                        break;
                    default:
                        break;
                    }
                    break;

                case 0xd1:
                    switch (*p) {
                    case 0xa0: // CYRILLIC CAPITAL LETTER OMEGA
                        (*p) = 0xa1;
                        break;
                    case 0xa2: // CYRILLIC CAPITAL LETTER YAT
                        (*p) = 0xa3;
                        break;
                    case 0xa4: // CYRILLIC CAPITAL LETTER IOTIFIED E
                        (*p) = 0xa5;
                        break;
                    case 0xa6: // CYRILLIC CAPITAL LETTER LITTLE YUS
                        (*p) = 0xa7;
                        break;
                    case 0xa8: // CYRILLIC CAPITAL LETTER IOTIFIED LITTLE YUS
                        (*p) = 0xa9;
                        break;
                    case 0xaa: // CYRILLIC CAPITAL LETTER BIG YUS
                        (*p) = 0xab;
                        break;
                    case 0xac: // CYRILLIC CAPITAL LETTER IOTIFIED BIG YUS
                        (*p) = 0xad;
                        break;
                    case 0xae: // CYRILLIC CAPITAL LETTER KSI
                        (*p) = 0xaf;
                        break;
                    case 0xb0: // CYRILLIC CAPITAL LETTER PSI
                        (*p) = 0xb1;
                        break;
                    case 0xb2: // CYRILLIC CAPITAL LETTER FITA
                        (*p) = 0xb3;
                        break;
                    case 0xb4: // CYRILLIC CAPITAL LETTER IZHITSA
                        (*p) = 0xb5;
                        break;
                    case 0xb6: // CYRILLIC CAPITAL LETTER IZHITSA WITH DOUBLE GRAVE ACCENT
                        (*p) = 0xb7;
                        break;
                    case 0xb8: // CYRILLIC CAPITAL LETTER UK
                        (*p) = 0xb9;
                        break;
                    case 0xba: // CYRILLIC CAPITAL LETTER ROUND OMEGA
                        (*p) = 0xbb;
                        break;
                    case 0xbc: // CYRILLIC CAPITAL LETTER OMEGA WITH TITLO
                        (*p) = 0xbd;
                        break;
                    case 0xbe: // CYRILLIC CAPITAL LETTER OT
                        (*p) = 0xbf;
                        break;
                    default:
                        break;
                    }
                    break;

                case 0xd2:
                    switch (*p) {
                    case 0x80: // CYRILLIC CAPITAL LETTER KOPPA
                        (*p) = 0x81;
                        break;
                    case 0x82: // CYRILLIC THOUSANDS SIGN (So) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0x83: // COMBINING CYRILLIC TITLO (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x84: // COMBINING CYRILLIC PALATALIZATION (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x85: // COMBINING CYRILLIC DASIA PNEUMATA (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x86: // COMBINING CYRILLIC PSILI PNEUMATA (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x87: // COMBINING CYRILLIC POKRYTIE (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x88: // COMBINING CYRILLIC HUNDRED THOUSANDS SIGN (Me) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x89: // COMBINING CYRILLIC MILLIONS SIGN (Me) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x8a: // CYRILLIC CAPITAL LETTER SHORT I WITH TAIL
                        (*p) = 0x8b;
                        break;
                    case 0x8c: // CYRILLIC CAPITAL LETTER SEMISOFT SIGN
                        (*p) = 0x8d;
                        break;
                    case 0x8e: // CYRILLIC CAPITAL LETTER ER WITH TICK
                        (*p) = 0x8f;
                        break;
                    case 0x90: // CYRILLIC CAPITAL LETTER GHE WITH UPTURN
                        (*p) = 0x91;
                        break;
                    case 0x92: // CYRILLIC CAPITAL LETTER GHE WITH STROKE
                        (*p) = 0x93;
                        break;
                    case 0x94: // CYRILLIC CAPITAL LETTER GHE WITH MIDDLE HOOK
                        (*p) = 0x95;
                        break;
                    case 0x96: // CYRILLIC CAPITAL LETTER ZHE WITH DESCENDER
                        (*p) = 0x97;
                        break;
                    case 0x98: // CYRILLIC CAPITAL LETTER ZE WITH DESCENDER
                        (*p) = 0x99;
                        break;
                    case 0x9a: // CYRILLIC CAPITAL LETTER KA WITH DESCENDER
                        (*p) = 0x9b;
                        break;
                    case 0x9c: // CYRILLIC CAPITAL LETTER KA WITH VERTICAL STROKE
                        (*p) = 0x9d;
                        break;
                    case 0x9e: // CYRILLIC CAPITAL LETTER KA WITH STROKE
                        (*p) = 0x9f;
                        break;
                    case 0xa0: // CYRILLIC CAPITAL LETTER BASHKIR KA
                        (*p) = 0xa1;
                        break;
                    case 0xa2: // CYRILLIC CAPITAL LETTER EN WITH DESCENDER
                        (*p) = 0xa3;
                        break;
                    case 0xa4: // CYRILLIC CAPITAL LIGATURE EN GHE
                        (*p) = 0xa5;
                        break;
                    case 0xa6: // CYRILLIC CAPITAL LETTER PE WITH MIDDLE HOOK
                        (*p) = 0xa7;
                        break;
                    case 0xa8: // CYRILLIC CAPITAL LETTER ABKHASIAN HA
                        (*p) = 0xa9;
                        break;
                    case 0xaa: // CYRILLIC CAPITAL LETTER ES WITH DESCENDER
                        (*p) = 0xab;
                        break;
                    case 0xac: // CYRILLIC CAPITAL LETTER TE WITH DESCENDER
                        (*p) = 0xad;
                        break;
                    case 0xae: // CYRILLIC CAPITAL LETTER STRAIGHT U
                        (*p) = 0xaf;
                        break;
                    case 0xb0: // CYRILLIC CAPITAL LETTER STRAIGHT U WITH STROKE
                        (*p) = 0xb1;
                        break;
                    case 0xb2: // CYRILLIC CAPITAL LETTER HA WITH DESCENDER
                        (*p) = 0xb3;
                        break;
                    case 0xb4: // CYRILLIC CAPITAL LIGATURE TE TSE
                        (*p) = 0xb5;
                        break;
                    case 0xb6: // CYRILLIC CAPITAL LETTER CHE WITH DESCENDER
                        (*p) = 0xb7;
                        break;
                    case 0xb8: // CYRILLIC CAPITAL LETTER CHE WITH VERTICAL STROKE
                        (*p) = 0xb9;
                        break;
                    case 0xba: // CYRILLIC CAPITAL LETTER SHHA
                        (*p) = 0xbb;
                        break;
                    case 0xbc: // CYRILLIC CAPITAL LETTER ABKHASIAN CHE
                        (*p) = 0xbd;
                        break;
                    case 0xbe: // CYRILLIC CAPITAL LETTER ABKHASIAN CHE WITH DESCENDER
                        (*p) = 0xbf;
                        break;
                    default:
                        break;
                    }
                    break;

                case 0xd3:
                    switch (*p) {
                    case 0x80: // CYRILLIC LETTER PALOCHKA
                        (*p) = 0x8f;
                        break;
                    case 0x81: // CYRILLIC CAPITAL LETTER ZHE WITH BREVE
                        (*p) = 0x82;
                        break;
                    case 0x83: // CYRILLIC CAPITAL LETTER KA WITH HOOK
                        (*p) = 0x84;
                        break;
                    case 0x85: // CYRILLIC CAPITAL LETTER EL WITH TAIL
                        (*p) = 0x86;
                        break;
                    case 0x87: // CYRILLIC CAPITAL LETTER EN WITH HOOK
                        (*p) = 0x88;
                        break;
                    case 0x89: // CYRILLIC CAPITAL LETTER EN WITH TAIL
                        (*p) = 0x8a;
                        break;
                    case 0x8b: // CYRILLIC CAPITAL LETTER KHAKASSIAN CHE
                        (*p) = 0x8c;
                        break;
                    case 0x8d: // CYRILLIC CAPITAL LETTER EM WITH TAIL
                        (*p) = 0x8e;
                        break;
                    case 0x90: // CYRILLIC CAPITAL LETTER A WITH BREVE
                        (*p) = 0x91;
                        break;
                    case 0x92: // CYRILLIC CAPITAL LETTER A WITH DIAERESIS
                        (*p) = 0x93;
                        break;
                    case 0x94: // CYRILLIC CAPITAL LIGATURE A IE
                        (*p) = 0x95;
                        break;
                    case 0x96: // CYRILLIC CAPITAL LETTER IE WITH BREVE
                        (*p) = 0x97;
                        break;
                    case 0x98: // CYRILLIC CAPITAL LETTER SCHWA
                        (*p) = 0x99;
                        break;
                    case 0x9a: // CYRILLIC CAPITAL LETTER SCHWA WITH DIAERESIS
                        (*p) = 0x9b;
                        break;
                    case 0x9c: // CYRILLIC CAPITAL LETTER ZHE WITH DIAERESIS
                        (*p) = 0x9d;
                        break;
                    case 0x9e: // CYRILLIC CAPITAL LETTER ZE WITH DIAERESIS
                        (*p) = 0x9f;
                        break;
                    case 0xa0: // CYRILLIC CAPITAL LETTER ABKHASIAN DZE
                        (*p) = 0xa1;
                        break;
                    case 0xa2: // CYRILLIC CAPITAL LETTER I WITH MACRON
                        (*p) = 0xa3;
                        break;
                    case 0xa4: // CYRILLIC CAPITAL LETTER I WITH DIAERESIS
                        (*p) = 0xa5;
                        break;
                    case 0xa6: // CYRILLIC CAPITAL LETTER O WITH DIAERESIS
                        (*p) = 0xa7;
                        break;
                    case 0xa8: // CYRILLIC CAPITAL LETTER BARRED O
                        (*p) = 0xa9;
                        break;
                    case 0xaa: // CYRILLIC CAPITAL LETTER BARRED O WITH DIAERESIS
                        (*p) = 0xab;
                        break;
                    case 0xac: // CYRILLIC CAPITAL LETTER E WITH DIAERESIS
                        (*p) = 0xad;
                        break;
                    case 0xae: // CYRILLIC CAPITAL LETTER U WITH MACRON
                        (*p) = 0xaf;
                        break;
                    case 0xb0: // CYRILLIC CAPITAL LETTER U WITH DIAERESIS
                        (*p) = 0xb1;
                        break;
                    case 0xb2: // CYRILLIC CAPITAL LETTER U WITH DOUBLE ACUTE
                        (*p) = 0xb3;
                        break;
                    case 0xb4: // CYRILLIC CAPITAL LETTER CHE WITH DIAERESIS
                        (*p) = 0xb5;
                        break;
                    case 0xb6: // CYRILLIC CAPITAL LETTER GHE WITH DESCENDER
                        (*p) = 0xb7;
                        break;
                    case 0xb8: // CYRILLIC CAPITAL LETTER YERU WITH DIAERESIS
                        (*p) = 0xb9;
                        break;
                    case 0xba: // CYRILLIC CAPITAL LETTER GHE WITH STROKE AND HOOK
                        (*p) = 0xbb;
                        break;
                    case 0xbc: // CYRILLIC CAPITAL LETTER HA WITH HOOK
                        (*p) = 0xbd;
                        break;
                    case 0xbe: // CYRILLIC CAPITAL LETTER HA WITH STROKE
                        (*p) = 0xbf;
                        break;
                    default:
                        break;
                    }
                    break;

                case 0xd4:
                    switch (*p) {
                    case 0x80: // CYRILLIC CAPITAL LETTER KOMI DE
                        (*p) = 0x81;
                        break;
                    case 0x82: // CYRILLIC CAPITAL LETTER KOMI DJE
                        (*p) = 0x83;
                        break;
                    case 0x84: // CYRILLIC CAPITAL LETTER KOMI ZJE
                        (*p) = 0x85;
                        break;
                    case 0x86: // CYRILLIC CAPITAL LETTER KOMI DZJE
                        (*p) = 0x87;
                        break;
                    case 0x88: // CYRILLIC CAPITAL LETTER KOMI LJE
                        (*p) = 0x89;
                        break;
                    case 0x8a: // CYRILLIC CAPITAL LETTER KOMI NJE
                        (*p) = 0x8b;
                        break;
                    case 0x8c: // CYRILLIC CAPITAL LETTER KOMI SJE
                        (*p) = 0x8d;
                        break;
                    case 0x8e: // CYRILLIC CAPITAL LETTER KOMI TJE
                        (*p) = 0x8f;
                        break;
                    case 0x90: // CYRILLIC CAPITAL LETTER REVERSED ZE
                        (*p) = 0x91;
                        break;
                    case 0x92: // CYRILLIC CAPITAL LETTER EL WITH HOOK
                        (*p) = 0x93;
                        break;
                    case 0x94: // CYRILLIC CAPITAL LETTER LHA
                        (*p) = 0x95;
                        break;
                    case 0x96: // CYRILLIC CAPITAL LETTER RHA
                        (*p) = 0x97;
                        break;
                    case 0x98: // CYRILLIC CAPITAL LETTER YAE
                        (*p) = 0x99;
                        break;
                    case 0x9a: // CYRILLIC CAPITAL LETTER QA
                        (*p) = 0x9b;
                        break;
                    case 0x9c: // CYRILLIC CAPITAL LETTER WE
                        (*p) = 0x9d;
                        break;
                    case 0x9e: // CYRILLIC CAPITAL LETTER ALEUT KA
                        (*p) = 0x9f;
                        break;
                    case 0xa0: // CYRILLIC CAPITAL LETTER EL WITH MIDDLE HOOK
                        (*p) = 0xa1;
                        break;
                    case 0xa2: // CYRILLIC CAPITAL LETTER EN WITH MIDDLE HOOK
                        (*p) = 0xa3;
                        break;
                    case 0xa4: // CYRILLIC CAPITAL LETTER PE WITH DESCENDER
                        (*p) = 0xa5;
                        break;
                    case 0xa6: // CYRILLIC CAPITAL LETTER SHHA WITH DESCENDER
                        (*p) = 0xa7;
                        break;
                    case 0xa8: // CYRILLIC CAPITAL LETTER EN WITH LEFT HOOK
                        (*p) = 0xa9;
                        break;
                    case 0xaa: // CYRILLIC CAPITAL LETTER DZZHE
                        (*p) = 0xab;
                        break;
                    case 0xac: // CYRILLIC CAPITAL LETTER DCHE
                        (*p) = 0xad;
                        break;
                    case 0xae: // CYRILLIC CAPITAL LETTER EL WITH DESCENDER
                        (*p) = 0xaf;
                        break;
                    case 0xb1: // ARMENIAN CAPITAL LETTER AYB (crosses to lead 0xd5)
                        *pExtChar = 0xd5;
                        (*p) = 0xa1;
                        break;
                    case 0xb2: // ARMENIAN CAPITAL LETTER BEN (crosses to lead 0xd5)
                        *pExtChar = 0xd5;
                        (*p) = 0xa2;
                        break;
                    case 0xb3: // ARMENIAN CAPITAL LETTER GIM (crosses to lead 0xd5)
                        *pExtChar = 0xd5;
                        (*p) = 0xa3;
                        break;
                    case 0xb4: // ARMENIAN CAPITAL LETTER DA (crosses to lead 0xd5)
                        *pExtChar = 0xd5;
                        (*p) = 0xa4;
                        break;
                    case 0xb5: // ARMENIAN CAPITAL LETTER ECH (crosses to lead 0xd5)
                        *pExtChar = 0xd5;
                        (*p) = 0xa5;
                        break;
                    case 0xb6: // ARMENIAN CAPITAL LETTER ZA (crosses to lead 0xd5)
                        *pExtChar = 0xd5;
                        (*p) = 0xa6;
                        break;
                    case 0xb7: // ARMENIAN CAPITAL LETTER EH (crosses to lead 0xd5)
                        *pExtChar = 0xd5;
                        (*p) = 0xa7;
                        break;
                    case 0xb8: // ARMENIAN CAPITAL LETTER ET (crosses to lead 0xd5)
                        *pExtChar = 0xd5;
                        (*p) = 0xa8;
                        break;
                    case 0xb9: // ARMENIAN CAPITAL LETTER TO (crosses to lead 0xd5)
                        *pExtChar = 0xd5;
                        (*p) = 0xa9;
                        break;
                    case 0xba: // ARMENIAN CAPITAL LETTER ZHE (crosses to lead 0xd5)
                        *pExtChar = 0xd5;
                        (*p) = 0xaa;
                        break;
                    case 0xbb: // ARMENIAN CAPITAL LETTER INI (crosses to lead 0xd5)
                        *pExtChar = 0xd5;
                        (*p) = 0xab;
                        break;
                    case 0xbc: // ARMENIAN CAPITAL LETTER LIWN (crosses to lead 0xd5)
                        *pExtChar = 0xd5;
                        (*p) = 0xac;
                        break;
                    case 0xbd: // ARMENIAN CAPITAL LETTER XEH (crosses to lead 0xd5)
                        *pExtChar = 0xd5;
                        (*p) = 0xad;
                        break;
                    case 0xbe: // ARMENIAN CAPITAL LETTER CA (crosses to lead 0xd5)
                        *pExtChar = 0xd5;
                        (*p) = 0xae;
                        break;
                    case 0xbf: // ARMENIAN CAPITAL LETTER KEN (crosses to lead 0xd5)
                        *pExtChar = 0xd5;
                        (*p) = 0xaf;
                        break;
                    default:
                        break;
                    }
                    break;

                case 0xd5:
                    switch (*p) {
                    case 0x80: // ARMENIAN CAPITAL LETTER HO
                        (*p) = 0xb0;
                        break;
                    case 0x81: // ARMENIAN CAPITAL LETTER JA
                        (*p) = 0xb1;
                        break;
                    case 0x82: // ARMENIAN CAPITAL LETTER GHAD
                        (*p) = 0xb2;
                        break;
                    case 0x83: // ARMENIAN CAPITAL LETTER CHEH
                        (*p) = 0xb3;
                        break;
                    case 0x84: // ARMENIAN CAPITAL LETTER MEN
                        (*p) = 0xb4;
                        break;
                    case 0x85: // ARMENIAN CAPITAL LETTER YI
                        (*p) = 0xb5;
                        break;
                    case 0x86: // ARMENIAN CAPITAL LETTER NOW
                        (*p) = 0xb6;
                        break;
                    case 0x87: // ARMENIAN CAPITAL LETTER SHA
                        (*p) = 0xb7;
                        break;
                    case 0x88: // ARMENIAN CAPITAL LETTER VO
                        (*p) = 0xb8;
                        break;
                    case 0x89: // ARMENIAN CAPITAL LETTER CHA
                        (*p) = 0xb9;
                        break;
                    case 0x8a: // ARMENIAN CAPITAL LETTER PEH
                        (*p) = 0xba;
                        break;
                    case 0x8b: // ARMENIAN CAPITAL LETTER JHEH
                        (*p) = 0xbb;
                        break;
                    case 0x8c: // ARMENIAN CAPITAL LETTER RA
                        (*p) = 0xbc;
                        break;
                    case 0x8d: // ARMENIAN CAPITAL LETTER SEH
                        (*p) = 0xbd;
                        break;
                    case 0x8e: // ARMENIAN CAPITAL LETTER VEW
                        (*p) = 0xbe;
                        break;
                    case 0x8f: // ARMENIAN CAPITAL LETTER TIWN
                        (*p) = 0xbf;
                        break;
                    case 0x90: // ARMENIAN CAPITAL LETTER REH (crosses to lead 0xd6)
                        *pExtChar = 0xd6;
                        (*p) = 0x80;
                        break;
                    case 0x91: // ARMENIAN CAPITAL LETTER CO (crosses to lead 0xd6)
                        *pExtChar = 0xd6;
                        (*p) = 0x81;
                        break;
                    case 0x92: // ARMENIAN CAPITAL LETTER YIWN (crosses to lead 0xd6)
                        *pExtChar = 0xd6;
                        (*p) = 0x82;
                        break;
                    case 0x93: // ARMENIAN CAPITAL LETTER PIWR (crosses to lead 0xd6)
                        *pExtChar = 0xd6;
                        (*p) = 0x83;
                        break;
                    case 0x94: // ARMENIAN CAPITAL LETTER KEH (crosses to lead 0xd6)
                        *pExtChar = 0xd6;
                        (*p) = 0x84;
                        break;
                    case 0x95: // ARMENIAN CAPITAL LETTER OH (crosses to lead 0xd6)
                        *pExtChar = 0xd6;
                        (*p) = 0x85;
                        break;
                    case 0x96: // ARMENIAN CAPITAL LETTER FEH (crosses to lead 0xd6)
                        *pExtChar = 0xd6;
                        (*p) = 0x86;
                        break;
                    case 0x99: // ARMENIAN MODIFIER LETTER LEFT HALF RING (Lm) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0x9a: // ARMENIAN APOSTROPHE (Po) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0x9b: // ARMENIAN EMPHASIS MARK (Po) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0x9c: // ARMENIAN EXCLAMATION MARK (Po) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0x9d: // ARMENIAN COMMA (Po) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0x9e: // ARMENIAN QUESTION MARK (Po) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0x9f: // ARMENIAN ABBREVIATION MARK (Po) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    default:
                        break;
                    }
                    break;

                case 0xd6:
                    switch (*p) {
                    case 0x89: // ARMENIAN FULL STOP (Po) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0x8a: // ARMENIAN HYPHEN (Pd) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0x8d: // RIGHT-FACING ARMENIAN ETERNITY SIGN (So) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0x8e: // LEFT-FACING ARMENIAN ETERNITY SIGN (So) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0x8f: // ARMENIAN DRAM SIGN (Sc) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0x91: // HEBREW ACCENT ETNAHTA (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x92: // HEBREW ACCENT SEGOL (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x93: // HEBREW ACCENT SHALSHELET (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x94: // HEBREW ACCENT ZAQEF QATAN (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x95: // HEBREW ACCENT ZAQEF GADOL (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x96: // HEBREW ACCENT TIPEHA (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x97: // HEBREW ACCENT REVIA (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x98: // HEBREW ACCENT ZARQA (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x99: // HEBREW ACCENT PASHTA (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x9a: // HEBREW ACCENT YETIV (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x9b: // HEBREW ACCENT TEVIR (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x9c: // HEBREW ACCENT GERESH (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x9d: // HEBREW ACCENT GERESH MUQDAM (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x9e: // HEBREW ACCENT GERSHAYIM (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x9f: // HEBREW ACCENT QARNEY PARA (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xa0: // HEBREW ACCENT TELISHA GEDOLA (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xa1: // HEBREW ACCENT PAZER (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xa2: // HEBREW ACCENT ATNAH HAFUKH (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xa3: // HEBREW ACCENT MUNAH (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xa4: // HEBREW ACCENT MAHAPAKH (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xa5: // HEBREW ACCENT MERKHA (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xa6: // HEBREW ACCENT MERKHA KEFULA (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xa7: // HEBREW ACCENT DARGA (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xa8: // HEBREW ACCENT QADMA (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xa9: // HEBREW ACCENT TELISHA QETANA (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xaa: // HEBREW ACCENT YERAH BEN YOMO (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xab: // HEBREW ACCENT OLE (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xac: // HEBREW ACCENT ILUY (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xad: // HEBREW ACCENT DEHI (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xae: // HEBREW ACCENT ZINOR (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xaf: // HEBREW MARK MASORA CIRCLE (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xb0: // HEBREW POINT SHEVA (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xb1: // HEBREW POINT HATAF SEGOL (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xb2: // HEBREW POINT HATAF PATAH (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xb3: // HEBREW POINT HATAF QAMATS (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xb4: // HEBREW POINT HIRIQ (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xb5: // HEBREW POINT TSERE (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xb6: // HEBREW POINT SEGOL (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xb7: // HEBREW POINT PATAH (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xb8: // HEBREW POINT QAMATS (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xb9: // HEBREW POINT HOLAM (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xba: // HEBREW POINT HOLAM HASER FOR VAV (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xbb: // HEBREW POINT QUBUTS (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xbc: // HEBREW POINT DAGESH OR MAPIQ (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xbd: // HEBREW POINT METEG (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xbe: // HEBREW PUNCTUATION MAQAF (Pd) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0xbf: // HEBREW POINT RAFE (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    default:
                        break;
                    }
                    break;

                case 0xd7:
                    switch (*p) {
                    case 0x80: // HEBREW PUNCTUATION PASEQ (Po) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0x81: // HEBREW POINT SHIN DOT (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x82: // HEBREW POINT SIN DOT (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x83: // HEBREW PUNCTUATION SOF PASUQ (Po) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0x84: // HEBREW MARK UPPER DOT (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x85: // HEBREW MARK LOWER DOT (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x86: // HEBREW PUNCTUATION NUN HAFUKHA (Po) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0x87: // HEBREW POINT QAMATS QATAN (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xb3: // HEBREW PUNCTUATION GERESH (Po) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0xb4: // HEBREW PUNCTUATION GERSHAYIM (Po) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    default:
                        break;
                    }
                    break;

                case 0xd8:
                    switch (*p) {
                    case 0x80: // ARABIC NUMBER SIGN (Cf) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x81: // ARABIC SIGN SANAH (Cf) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x82: // ARABIC FOOTNOTE MARKER (Cf) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x83: // ARABIC SIGN SAFHA (Cf) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x84: // ARABIC SIGN SAMVAT (Cf) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x85: // ARABIC NUMBER MARK ABOVE (Cf) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x86: // ARABIC-INDIC CUBE ROOT (Sm) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0x87: // ARABIC-INDIC FOURTH ROOT (Sm) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0x88: // ARABIC RAY (Sm) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0x89: // ARABIC-INDIC PER MILLE SIGN (Po) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0x8a: // ARABIC-INDIC PER TEN THOUSAND SIGN (Po) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0x8b: // AFGHANI SIGN (Sc) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0x8c: // ARABIC COMMA (Po) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0x8d: // ARABIC DATE SEPARATOR (Po) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0x8e: // ARABIC POETIC VERSE SIGN (So) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0x8f: // ARABIC SIGN MISRA (So) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0x90: // ARABIC SIGN SALLALLAHOU ALAYHE WASSALLAM (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x91: // ARABIC SIGN ALAYHE ASSALLAM (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x92: // ARABIC SIGN RAHMATULLAH ALAYHE (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x93: // ARABIC SIGN RADI ALLAHOU ANHU (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x94: // ARABIC SIGN TAKHALLUS (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x95: // ARABIC SMALL HIGH TAH (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x96: // ARABIC SMALL HIGH LIGATURE ALEF WITH LAM WITH YEH (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x97: // ARABIC SMALL HIGH ZAIN (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x98: // ARABIC SMALL FATHA (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x99: // ARABIC SMALL DAMMA (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x9a: // ARABIC SMALL KASRA (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x9b: // ARABIC SEMICOLON (Po) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0x9c: // ARABIC LETTER MARK (Cf) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x9d: // ARABIC END OF TEXT MARK (Po) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0x9e: // ARABIC TRIPLE DOT PUNCTUATION MARK (Po) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0x9f: // ARABIC QUESTION MARK (Po) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    default:
                        break;
                    }
                    break;

                case 0xd9:
                    switch (*p) {
                    case 0x80: // ARABIC TATWEEL (Lm) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0x8b: // ARABIC FATHATAN (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x8c: // ARABIC DAMMATAN (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x8d: // ARABIC KASRATAN (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x8e: // ARABIC FATHA (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x8f: // ARABIC DAMMA (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x90: // ARABIC KASRA (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x91: // ARABIC SHADDA (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x92: // ARABIC SUKUN (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x93: // ARABIC MADDAH ABOVE (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x94: // ARABIC HAMZA ABOVE (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x95: // ARABIC HAMZA BELOW (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x96: // ARABIC SUBSCRIPT ALEF (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x97: // ARABIC INVERTED DAMMA (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x98: // ARABIC MARK NOON GHUNNA (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x99: // ARABIC ZWARAKAY (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x9a: // ARABIC VOWEL SIGN SMALL V ABOVE (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x9b: // ARABIC VOWEL SIGN INVERTED SMALL V ABOVE (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x9c: // ARABIC VOWEL SIGN DOT BELOW (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x9d: // ARABIC REVERSED DAMMA (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x9e: // ARABIC FATHA WITH TWO DOTS (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x9f: // ARABIC WAVY HAMZA BELOW (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xaa: // ARABIC PERCENT SIGN (Po) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0xab: // ARABIC DECIMAL SEPARATOR (Po) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0xac: // ARABIC THOUSANDS SEPARATOR (Po) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0xad: // ARABIC FIVE POINTED STAR (Po) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0xb0: // ARABIC LETTER SUPERSCRIPT ALEF (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    default:
                        break;
                    }
                    break;

                case 0xda:
                    switch (*p) {
                    default:
                        break;
                    }
                    break;

                case 0xdb:
                    switch (*p) {
                    case 0x94: // ARABIC FULL STOP (Po) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0x96: // ARABIC SMALL HIGH LIGATURE SAD WITH LAM WITH ALEF MAKSURA (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x97: // ARABIC SMALL HIGH LIGATURE QAF WITH LAM WITH ALEF MAKSURA (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x98: // ARABIC SMALL HIGH MEEM INITIAL FORM (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x99: // ARABIC SMALL HIGH LAM ALEF (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x9a: // ARABIC SMALL HIGH JEEM (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x9b: // ARABIC SMALL HIGH THREE DOTS (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x9c: // ARABIC SMALL HIGH SEEN (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x9d: // ARABIC END OF AYAH (Cf) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x9e: // ARABIC START OF RUB EL HIZB (So) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0x9f: // ARABIC SMALL HIGH ROUNDED ZERO (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xa0: // ARABIC SMALL HIGH UPRIGHT RECTANGULAR ZERO (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xa1: // ARABIC SMALL HIGH DOTLESS HEAD OF KHAH (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xa2: // ARABIC SMALL HIGH MEEM ISOLATED FORM (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xa3: // ARABIC SMALL LOW SEEN (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xa4: // ARABIC SMALL HIGH MADDA (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xa5: // ARABIC SMALL WAW (Lm) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0xa6: // ARABIC SMALL YEH (Lm) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0xa7: // ARABIC SMALL HIGH YEH (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xa8: // ARABIC SMALL HIGH NOON (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xa9: // ARABIC PLACE OF SAJDAH (So) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0xaa: // ARABIC EMPTY CENTRE LOW STOP (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xab: // ARABIC EMPTY CENTRE HIGH STOP (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xac: // ARABIC ROUNDED HIGH STOP WITH FILLED CENTRE (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xad: // ARABIC SMALL LOW MEEM (Mn) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xbd: // ARABIC SIGN SINDHI AMPERSAND (So) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0xbe: // ARABIC SIGN SINDHI POSTPOSITION MEN (So) -- not a letter/digit
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    default:
                        break;
                    }
                    break;

                case 0xdc:
                    switch (*p) {
                    case 0x80: // SYRIAC END OF PARAGRAPH (Po)
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0x81: // SYRIAC SUPRALINEAR FULL STOP (Po)
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0x82: // SYRIAC SUBLINEAR FULL STOP (Po)
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0x83: // SYRIAC SUPRALINEAR COLON (Po)
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0x84: // SYRIAC SUBLINEAR COLON (Po)
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0x85: // SYRIAC HORIZONTAL COLON (Po)
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0x86: // SYRIAC COLON SKEWED LEFT (Po)
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0x87: // SYRIAC COLON SKEWED RIGHT (Po)
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0x88: // SYRIAC SUPRALINEAR COLON SKEWED LEFT (Po)
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0x89: // SYRIAC SUBLINEAR COLON SKEWED RIGHT (Po)
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0x8a: // SYRIAC CONTRACTION (Po)
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0x8b: // SYRIAC HARKLEAN OBELUS (Po)
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0x8c: // SYRIAC HARKLEAN METOBELUS (Po)
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0x8d: // SYRIAC HARKLEAN ASTERISCUS (Po)
                        if (clean)
                            *pExtChar = *p = ZapChr;
                        break;
                    case 0x8f: // SYRIAC ABBREVIATION MARK (Cf)
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x91: // SYRIAC LETTER SUPERSCRIPT ALAPH (Mn)
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xb0: // SYRIAC PTHAHA ABOVE (Mn)
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xb1: // SYRIAC PTHAHA BELOW (Mn)
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xb2: // SYRIAC PTHAHA DOTTED (Mn)
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xb3: // SYRIAC ZQAPHA ABOVE (Mn)
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xb4: // SYRIAC ZQAPHA BELOW (Mn)
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xb5: // SYRIAC ZQAPHA DOTTED (Mn)
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xb6: // SYRIAC RBASA ABOVE (Mn)
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xb7: // SYRIAC RBASA BELOW (Mn)
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xb8: // SYRIAC DOTTED ZLAMA HORIZONTAL (Mn)
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xb9: // SYRIAC DOTTED ZLAMA ANGULAR (Mn)
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xba: // SYRIAC HBASA ABOVE (Mn)
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xbb: // SYRIAC HBASA BELOW (Mn)
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xbc: // SYRIAC HBASA-ESASA DOTTED (Mn)
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xbd: // SYRIAC ESASA ABOVE (Mn)
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xbe: // SYRIAC ESASA BELOW (Mn)
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0xbf: // SYRIAC RWAHA (Mn)
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    default:
                        break;
                    }
                    break;

                case 0xdd:
                    switch (*p) {
                    case 0x80: // SYRIAC FEMININE DOT (Mn)
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x81: // SYRIAC QUSHSHAYA (Mn)
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x82: // SYRIAC RUKKAKHA (Mn)
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x83: // SYRIAC TWO VERTICAL DOTS ABOVE (Mn)
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x84: // SYRIAC TWO VERTICAL DOTS BELOW (Mn)
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x85: // SYRIAC THREE DOTS ABOVE (Mn)
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x86: // SYRIAC THREE DOTS BELOW (Mn)
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x87: // SYRIAC OBLIQUE LINE ABOVE (Mn)
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x88: // SYRIAC OBLIQUE LINE BELOW (Mn)
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x89: // SYRIAC MUSIC (Mn)
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    case 0x8a: // SYRIAC BARREKH (Mn)
                        if (clean)
                            *pExtChar = *p = _IB_UTF8_SILENT_ZAP;
                        break;
                    default:
                        break;
                    }
                    break;

                case 0xdf: // NKo block
                    // Zap combining tones through NKo exclamation mark,
                    // but keep high/low tone apostrophes (U+07F4, U+07F5)
                    if (clean && (*p >= 0xab && *p <= 0xb9) &&
                        *p != 0xb4 && *p != 0xb5)
                        *pExtChar = *p = ZapChr;
                    break;

                // 3-byte sequences for Indic & other non-Latin scripts.
                // In clean mode the entire script block is zapped to space;
                // the pointer already sits on byte 2 of 3 after the outer p++,
                // so we advance once more to consume byte 3 before zapping.
                case 0xe0:
                    pExtChar = p;
                    p++;
                    if (p < end) { // else: sequence truncated at slice end — leave untouched
                    if (clean) {
                        switch (*pExtChar) {
                        case 0xa0: // Samaritan
                        case 0xa1: // Mandaic
                        case 0xa3: // Syriac Supplement
                        case 0xa4: // Devanagari Extended
                        case 0xa5: // Devanagari
                        case 0xa6: // Bengali
                        case 0xa7: // Gurmukhi
                        case 0xa8: // Gujarati
                        case 0xa9: // Oriya
                        case 0xaa: // Tamil
                        case 0xab: // Telugu
                        case 0xac: // Kannada
                        case 0xad: // Malayalam
                        case 0xae: // Sinhala
                        case 0xaf: // Thai
                        case 0xb0: // Lao
                        case 0xb1: // Tibetan
                        case 0xb2: // Myanmar
                        case 0xb3: // Georgian (old range)
                        case 0xb4: // Hangul Jamo
                        case 0xb5: // Ethiopic
                        case 0xb6: // Ethiopic Supplement
                        case 0xb7: // Cherokee
                        case 0xb8: // Unified Canadian Aboriginal Syllabics
                        case 0xb9: // Ogham
                        case 0xba: // Runic
                        case 0xbb: // Tagalog / Hanunoo / Buhid / Tagbanwa
                        case 0xbc: // Khmer
                        case 0xbd: // Mongolian
                        case 0xbe: // Unified Canadian Aboriginal Syllabics Ext
                        case 0xbf: // Limbu / Tai Le
                            // Zap all three bytes: pExtChar-1 (0xe0), pExtChar, p
                            *(pExtChar - 1) = *pExtChar = *p = ZapChr;
                            break;
                        default:
                            break;
                        }
                    }
                    }
                    break;

                case 0xe1: // Three byte code — Latin/Greek extended, Georgian, etc.
                    pExtChar = p;
                    p++;
                    if (p < end) { // else: sequence truncated at slice end — leave untouched
                    switch (*pExtChar) {
                    case 0x82: // Georgian
                        if ((*p >= 0xa0)
                            && (*p <= 0xbf)) {
                            *pExtChar = 0x83;
                            (*p) -= 0x10;
                        }
                        break;
                    case 0x83: // Georgian
                        if ((*p >= 0x80)
                            && ((*p <= 0x85)
                                || (*p == 0x87))
                            || (*p == 0x8d))
                            (*p) += 0x30;
                        break;
                    case 0xb8: // Latin Extended Additional
                        if ((*p >= 0x80)
                            && (*p <= 0xbf)
                            && (!(*p % 2))) // Even
                            (*p)++; // Next char is lwr
                        break;
                    case 0xb9: // Latin Extended Additional
                        if ((*p >= 0x80)
                            && (*p <= 0xbf)
                            && (!(*p % 2))) // Even
                            (*p)++; // Next char is lwr
                        break;
                    case 0xba: // Latin Extended Additional
                        if ((*p >= 0x80)
                            && (*p <= 0x94)
                            && (!(*p % 2))) // Even
                            (*p)++; // Next char is lwr
                        else if ((*p >= 0x9e)
                            && (*p <= 0xbf)
                            && (!(*p % 2))) // Even
                            (*p)++; // Next char is lwr
                        break;
                    case 0xbb: // Latin Extended Additional
                        if ((*p >= 0x80)
                            && (*p <= 0xbf)
                            && (!(*p % 2))) // Even
                            (*p)++; // Next char is lwr
                        break;
                    case 0xbc: // Greek Extended: ALPHA/EPSILON/ETA/IOTA
                        switch (*p) {
                        case 0x88: // GREEK CAPITAL LETTER ALPHA WITH PSILI
                            (*p) = 0x80;
                            break;
                        case 0x89: // GREEK CAPITAL LETTER ALPHA WITH DASIA
                            (*p) = 0x81;
                            break;
                        case 0x8a: // GREEK CAPITAL LETTER ALPHA WITH PSILI AND VARIA
                            (*p) = 0x82;
                            break;
                        case 0x8b: // GREEK CAPITAL LETTER ALPHA WITH DASIA AND VARIA
                            (*p) = 0x83;
                            break;
                        case 0x8c: // GREEK CAPITAL LETTER ALPHA WITH PSILI AND OXIA
                            (*p) = 0x84;
                            break;
                        case 0x8d: // GREEK CAPITAL LETTER ALPHA WITH DASIA AND OXIA
                            (*p) = 0x85;
                            break;
                        case 0x8e: // GREEK CAPITAL LETTER ALPHA WITH PSILI AND PERISPOMENI
                            (*p) = 0x86;
                            break;
                        case 0x8f: // GREEK CAPITAL LETTER ALPHA WITH DASIA AND PERISPOMENI
                            (*p) = 0x87;
                            break;
                        case 0x98: // GREEK CAPITAL LETTER EPSILON WITH PSILI
                            (*p) = 0x90;
                            break;
                        case 0x99: // GREEK CAPITAL LETTER EPSILON WITH DASIA
                            (*p) = 0x91;
                            break;
                        case 0x9a: // GREEK CAPITAL LETTER EPSILON WITH PSILI AND VARIA
                            (*p) = 0x92;
                            break;
                        case 0x9b: // GREEK CAPITAL LETTER EPSILON WITH DASIA AND VARIA
                            (*p) = 0x93;
                            break;
                        case 0x9c: // GREEK CAPITAL LETTER EPSILON WITH PSILI AND OXIA
                            (*p) = 0x94;
                            break;
                        case 0x9d: // GREEK CAPITAL LETTER EPSILON WITH DASIA AND OXIA
                            (*p) = 0x95;
                            break;
                        case 0xa8: // GREEK CAPITAL LETTER ETA WITH PSILI
                            (*p) = 0xa0;
                            break;
                        case 0xa9: // GREEK CAPITAL LETTER ETA WITH DASIA
                            (*p) = 0xa1;
                            break;
                        case 0xaa: // GREEK CAPITAL LETTER ETA WITH PSILI AND VARIA
                            (*p) = 0xa2;
                            break;
                        case 0xab: // GREEK CAPITAL LETTER ETA WITH DASIA AND VARIA
                            (*p) = 0xa3;
                            break;
                        case 0xac: // GREEK CAPITAL LETTER ETA WITH PSILI AND OXIA
                            (*p) = 0xa4;
                            break;
                        case 0xad: // GREEK CAPITAL LETTER ETA WITH DASIA AND OXIA
                            (*p) = 0xa5;
                            break;
                        case 0xae: // GREEK CAPITAL LETTER ETA WITH PSILI AND PERISPOMENI
                            (*p) = 0xa6;
                            break;
                        case 0xaf: // GREEK CAPITAL LETTER ETA WITH DASIA AND PERISPOMENI
                            (*p) = 0xa7;
                            break;
                        case 0xb8: // GREEK CAPITAL LETTER IOTA WITH PSILI
                            (*p) = 0xb0;
                            break;
                        case 0xb9: // GREEK CAPITAL LETTER IOTA WITH DASIA
                            (*p) = 0xb1;
                            break;
                        case 0xba: // GREEK CAPITAL LETTER IOTA WITH PSILI AND VARIA
                            (*p) = 0xb2;
                            break;
                        case 0xbb: // GREEK CAPITAL LETTER IOTA WITH DASIA AND VARIA
                            (*p) = 0xb3;
                            break;
                        case 0xbc: // GREEK CAPITAL LETTER IOTA WITH PSILI AND OXIA
                            (*p) = 0xb4;
                            break;
                        case 0xbd: // GREEK CAPITAL LETTER IOTA WITH DASIA AND OXIA
                            (*p) = 0xb5;
                            break;
                        case 0xbe: // GREEK CAPITAL LETTER IOTA WITH PSILI AND PERISPOMENI
                            (*p) = 0xb6;
                            break;
                        case 0xbf: // GREEK CAPITAL LETTER IOTA WITH DASIA AND PERISPOMENI
                            (*p) = 0xb7;
                            break;
                        default:
                            break;
                        }
                        break;

                    case 0xbd: // Greek Extended: OMICRON/UPSILON/OMEGA
                        switch (*p) {
                        case 0x88: // GREEK CAPITAL LETTER OMICRON WITH PSILI
                            (*p) = 0x80;
                            break;
                        case 0x89: // GREEK CAPITAL LETTER OMICRON WITH DASIA
                            (*p) = 0x81;
                            break;
                        case 0x8a: // GREEK CAPITAL LETTER OMICRON WITH PSILI AND VARIA
                            (*p) = 0x82;
                            break;
                        case 0x8b: // GREEK CAPITAL LETTER OMICRON WITH DASIA AND VARIA
                            (*p) = 0x83;
                            break;
                        case 0x8c: // GREEK CAPITAL LETTER OMICRON WITH PSILI AND OXIA
                            (*p) = 0x84;
                            break;
                        case 0x8d: // GREEK CAPITAL LETTER OMICRON WITH DASIA AND OXIA
                            (*p) = 0x85;
                            break;
                        case 0x99: // GREEK CAPITAL LETTER UPSILON WITH DASIA
                            (*p) = 0x91;
                            break;
                        case 0x9b: // GREEK CAPITAL LETTER UPSILON WITH DASIA AND VARIA
                            (*p) = 0x93;
                            break;
                        case 0x9d: // GREEK CAPITAL LETTER UPSILON WITH DASIA AND OXIA
                            (*p) = 0x95;
                            break;
                        case 0x9f: // GREEK CAPITAL LETTER UPSILON WITH DASIA AND PERISPOMENI
                            (*p) = 0x97;
                            break;
                        case 0xa8: // GREEK CAPITAL LETTER OMEGA WITH PSILI
                            (*p) = 0xa0;
                            break;
                        case 0xa9: // GREEK CAPITAL LETTER OMEGA WITH DASIA
                            (*p) = 0xa1;
                            break;
                        case 0xaa: // GREEK CAPITAL LETTER OMEGA WITH PSILI AND VARIA
                            (*p) = 0xa2;
                            break;
                        case 0xab: // GREEK CAPITAL LETTER OMEGA WITH DASIA AND VARIA
                            (*p) = 0xa3;
                            break;
                        case 0xac: // GREEK CAPITAL LETTER OMEGA WITH PSILI AND OXIA
                            (*p) = 0xa4;
                            break;
                        case 0xad: // GREEK CAPITAL LETTER OMEGA WITH DASIA AND OXIA
                            (*p) = 0xa5;
                            break;
                        case 0xae: // GREEK CAPITAL LETTER OMEGA WITH PSILI AND PERISPOMENI
                            (*p) = 0xa6;
                            break;
                        case 0xaf: // GREEK CAPITAL LETTER OMEGA WITH DASIA AND PERISPOMENI
                            (*p) = 0xa7;
                            break;
                        default:
                            break;
                        }
                        break;

                    case 0xbe: // Greek Extended: ALPHA+YPOGEGRAMMENI/ETA+YPOGEGRAMMENI/OMEGA+YPOGEGRAMMENI/ALPHA-MACRON-VRACHY/misplaced-VARIA-OXIA
                        switch (*p) {
                        case 0x88: // GREEK CAPITAL LETTER ALPHA WITH PSILI AND PROSGEGRAMMENI
                            (*p) = 0x80;
                            break;
                        case 0x89: // GREEK CAPITAL LETTER ALPHA WITH DASIA AND PROSGEGRAMMENI
                            (*p) = 0x81;
                            break;
                        case 0x8a: // GREEK CAPITAL LETTER ALPHA WITH PSILI AND VARIA AND PROSGEGRAMMENI
                            (*p) = 0x82;
                            break;
                        case 0x8b: // GREEK CAPITAL LETTER ALPHA WITH DASIA AND VARIA AND PROSGEGRAMMENI
                            (*p) = 0x83;
                            break;
                        case 0x8c: // GREEK CAPITAL LETTER ALPHA WITH PSILI AND OXIA AND PROSGEGRAMMENI
                            (*p) = 0x84;
                            break;
                        case 0x8d: // GREEK CAPITAL LETTER ALPHA WITH DASIA AND OXIA AND PROSGEGRAMMENI
                            (*p) = 0x85;
                            break;
                        case 0x8e: // GREEK CAPITAL LETTER ALPHA WITH PSILI AND PERISPOMENI AND PROSGEGRAMMENI
                            (*p) = 0x86;
                            break;
                        case 0x8f: // GREEK CAPITAL LETTER ALPHA WITH DASIA AND PERISPOMENI AND PROSGEGRAMMENI
                            (*p) = 0x87;
                            break;
                        case 0x98: // GREEK CAPITAL LETTER ETA WITH PSILI AND PROSGEGRAMMENI
                            (*p) = 0x90;
                            break;
                        case 0x99: // GREEK CAPITAL LETTER ETA WITH DASIA AND PROSGEGRAMMENI
                            (*p) = 0x91;
                            break;
                        case 0x9a: // GREEK CAPITAL LETTER ETA WITH PSILI AND VARIA AND PROSGEGRAMMENI
                            (*p) = 0x92;
                            break;
                        case 0x9b: // GREEK CAPITAL LETTER ETA WITH DASIA AND VARIA AND PROSGEGRAMMENI
                            (*p) = 0x93;
                            break;
                        case 0x9c: // GREEK CAPITAL LETTER ETA WITH PSILI AND OXIA AND PROSGEGRAMMENI
                            (*p) = 0x94;
                            break;
                        case 0x9d: // GREEK CAPITAL LETTER ETA WITH DASIA AND OXIA AND PROSGEGRAMMENI
                            (*p) = 0x95;
                            break;
                        case 0x9e: // GREEK CAPITAL LETTER ETA WITH PSILI AND PERISPOMENI AND PROSGEGRAMMENI
                            (*p) = 0x96;
                            break;
                        case 0x9f: // GREEK CAPITAL LETTER ETA WITH DASIA AND PERISPOMENI AND PROSGEGRAMMENI
                            (*p) = 0x97;
                            break;
                        case 0xa8: // GREEK CAPITAL LETTER OMEGA WITH PSILI AND PROSGEGRAMMENI
                            (*p) = 0xa0;
                            break;
                        case 0xa9: // GREEK CAPITAL LETTER OMEGA WITH DASIA AND PROSGEGRAMMENI
                            (*p) = 0xa1;
                            break;
                        case 0xaa: // GREEK CAPITAL LETTER OMEGA WITH PSILI AND VARIA AND PROSGEGRAMMENI
                            (*p) = 0xa2;
                            break;
                        case 0xab: // GREEK CAPITAL LETTER OMEGA WITH DASIA AND VARIA AND PROSGEGRAMMENI
                            (*p) = 0xa3;
                            break;
                        case 0xac: // GREEK CAPITAL LETTER OMEGA WITH PSILI AND OXIA AND PROSGEGRAMMENI
                            (*p) = 0xa4;
                            break;
                        case 0xad: // GREEK CAPITAL LETTER OMEGA WITH DASIA AND OXIA AND PROSGEGRAMMENI
                            (*p) = 0xa5;
                            break;
                        case 0xae: // GREEK CAPITAL LETTER OMEGA WITH PSILI AND PERISPOMENI AND PROSGEGRAMMENI
                            (*p) = 0xa6;
                            break;
                        case 0xaf: // GREEK CAPITAL LETTER OMEGA WITH DASIA AND PERISPOMENI AND PROSGEGRAMMENI
                            (*p) = 0xa7;
                            break;
                        case 0xb8: // GREEK CAPITAL LETTER ALPHA WITH VRACHY
                            (*p) = 0xb0;
                            break;
                        case 0xb9: // GREEK CAPITAL LETTER ALPHA WITH MACRON
                            (*p) = 0xb1;
                            break;
                        case 0xba: // GREEK CAPITAL LETTER ALPHA WITH VARIA (crosses to byte2 0xbd)
                            *pExtChar = 0xbd;
                            (*p) = 0xb0;
                            break;
                        case 0xbb: // GREEK CAPITAL LETTER ALPHA WITH OXIA (crosses to byte2 0xbd)
                            *pExtChar = 0xbd;
                            (*p) = 0xb1;
                            break;
                        case 0xbc: // GREEK CAPITAL LETTER ALPHA WITH PROSGEGRAMMENI
                            (*p) = 0xb3;
                            break;
                        case 0xbd: // GREEK KORONIS (Sk) -- not a letter
                            if (clean)
                                *(pExtChar - 1) = *pExtChar = *p = ZapChr;
                            break;
                        case 0xbf: // GREEK PSILI (Sk) -- not a letter
                            if (clean)
                                *(pExtChar - 1) = *pExtChar = *p = ZapChr;
                            break;
                        default:
                            break;
                        }
                        break;

                    case 0xbf: // Greek Extended: ETA+YPOGEGRAMMENI/IOTA-MACRON-VRACHY/UPSILON-MACRON-VRACHY/RHO+DASIA/OMEGA+YPOGEGRAMMENI/misplaced-VARIA-OXIA
                        switch (*p) {
                        case 0x80: // GREEK PERISPOMENI (Sk) -- not a letter
                            if (clean)
                                *(pExtChar - 1) = *pExtChar = *p = ZapChr;
                            break;
                        case 0x81: // GREEK DIALYTIKA AND PERISPOMENI (Sk) -- not a letter
                            if (clean)
                                *(pExtChar - 1) = *pExtChar = *p = ZapChr;
                            break;
                        case 0x88: // GREEK CAPITAL LETTER EPSILON WITH VARIA (crosses to byte2 0xbd)
                            *pExtChar = 0xbd;
                            (*p) = 0xb2;
                            break;
                        case 0x89: // GREEK CAPITAL LETTER EPSILON WITH OXIA (crosses to byte2 0xbd)
                            *pExtChar = 0xbd;
                            (*p) = 0xb3;
                            break;
                        case 0x8a: // GREEK CAPITAL LETTER ETA WITH VARIA (crosses to byte2 0xbd)
                            *pExtChar = 0xbd;
                            (*p) = 0xb4;
                            break;
                        case 0x8b: // GREEK CAPITAL LETTER ETA WITH OXIA (crosses to byte2 0xbd)
                            *pExtChar = 0xbd;
                            (*p) = 0xb5;
                            break;
                        case 0x8c: // GREEK CAPITAL LETTER ETA WITH PROSGEGRAMMENI
                            (*p) = 0x83;
                            break;
                        case 0x8d: // GREEK PSILI AND VARIA (Sk) -- not a letter
                            if (clean)
                                *(pExtChar - 1) = *pExtChar = *p = ZapChr;
                            break;
                        case 0x8e: // GREEK PSILI AND OXIA (Sk) -- not a letter
                            if (clean)
                                *(pExtChar - 1) = *pExtChar = *p = ZapChr;
                            break;
                        case 0x8f: // GREEK PSILI AND PERISPOMENI (Sk) -- not a letter
                            if (clean)
                                *(pExtChar - 1) = *pExtChar = *p = ZapChr;
                            break;
                        case 0x98: // GREEK CAPITAL LETTER IOTA WITH VRACHY
                            (*p) = 0x90;
                            break;
                        case 0x99: // GREEK CAPITAL LETTER IOTA WITH MACRON
                            (*p) = 0x91;
                            break;
                        case 0x9a: // GREEK CAPITAL LETTER IOTA WITH VARIA (crosses to byte2 0xbd)
                            *pExtChar = 0xbd;
                            (*p) = 0xb6;
                            break;
                        case 0x9b: // GREEK CAPITAL LETTER IOTA WITH OXIA (crosses to byte2 0xbd)
                            *pExtChar = 0xbd;
                            (*p) = 0xb7;
                            break;
                        case 0x9d: // GREEK DASIA AND VARIA (Sk) -- not a letter
                            if (clean)
                                *(pExtChar - 1) = *pExtChar = *p = ZapChr;
                            break;
                        case 0x9e: // GREEK DASIA AND OXIA (Sk) -- not a letter
                            if (clean)
                                *(pExtChar - 1) = *pExtChar = *p = ZapChr;
                            break;
                        case 0x9f: // GREEK DASIA AND PERISPOMENI (Sk) -- not a letter
                            if (clean)
                                *(pExtChar - 1) = *pExtChar = *p = ZapChr;
                            break;
                        case 0xa8: // GREEK CAPITAL LETTER UPSILON WITH VRACHY
                            (*p) = 0xa0;
                            break;
                        case 0xa9: // GREEK CAPITAL LETTER UPSILON WITH MACRON
                            (*p) = 0xa1;
                            break;
                        case 0xaa: // GREEK CAPITAL LETTER UPSILON WITH VARIA (crosses to byte2 0xbd)
                            *pExtChar = 0xbd;
                            (*p) = 0xba;
                            break;
                        case 0xab: // GREEK CAPITAL LETTER UPSILON WITH OXIA (crosses to byte2 0xbd)
                            *pExtChar = 0xbd;
                            (*p) = 0xbb;
                            break;
                        case 0xac: // GREEK CAPITAL LETTER RHO WITH DASIA
                            (*p) = 0xa5;
                            break;
                        case 0xad: // GREEK DIALYTIKA AND VARIA (Sk) -- not a letter
                            if (clean)
                                *(pExtChar - 1) = *pExtChar = *p = ZapChr;
                            break;
                        case 0xae: // GREEK DIALYTIKA AND OXIA (Sk) -- not a letter
                            if (clean)
                                *(pExtChar - 1) = *pExtChar = *p = ZapChr;
                            break;
                        case 0xaf: // GREEK VARIA (Sk) -- not a letter
                            if (clean)
                                *(pExtChar - 1) = *pExtChar = *p = ZapChr;
                            break;
                        case 0xb8: // GREEK CAPITAL LETTER OMICRON WITH VARIA (crosses to byte2 0xbd)
                            *pExtChar = 0xbd;
                            (*p) = 0xb8;
                            break;
                        case 0xb9: // GREEK CAPITAL LETTER OMICRON WITH OXIA (crosses to byte2 0xbd)
                            *pExtChar = 0xbd;
                            (*p) = 0xb9;
                            break;
                        case 0xba: // GREEK CAPITAL LETTER OMEGA WITH VARIA (crosses to byte2 0xbd)
                            *pExtChar = 0xbd;
                            (*p) = 0xbc;
                            break;
                        case 0xbb: // GREEK CAPITAL LETTER OMEGA WITH OXIA (crosses to byte2 0xbd)
                            *pExtChar = 0xbd;
                            (*p) = 0xbd;
                            break;
                        case 0xbc: // GREEK CAPITAL LETTER OMEGA WITH PROSGEGRAMMENI
                            (*p) = 0xb3;
                            break;
                        case 0xbd: // GREEK OXIA (Sk) -- not a letter
                            if (clean)
                                *(pExtChar - 1) = *pExtChar = *p = ZapChr;
                            break;
                        case 0xbe: // GREEK DASIA (Sk) -- not a letter
                            if (clean)
                                *(pExtChar - 1) = *pExtChar = *p = ZapChr;
                            break;
                        default:
                            break;
                        }
                        break;

                    default:
                        break;
                    }
                    }
                    break;

                case 0xf0: // Four byte code
                    pExtChar = p;
                    p++;
                    if (p < end) { // else: sequence truncated at slice end — leave untouched
                    switch (*pExtChar) {
                    case 0x90:
                        pExtChar = p;
                        p++;
                        if (p < end) { // else: sequence truncated at slice end — leave untouched
                        switch (*pExtChar) {
                        case 0x92: // Osage uppercase
                            if ((*p >= 0xb0)
                                && (*p <= 0xbf)) {
                                *pExtChar = 0x93;
                                (*p) -= 0x18;
                            }
                            break;
                        case 0x93: // Osage lowercase range
                            if ((*p >= 0x80)
                                && (*p <= 0x93))
                                (*p) += 0x18;
                            break;
                        default:
                            break;
                        }
                        }
                        break;
                    case 0x9e: // FIX: was stray case outside this switch
                        pExtChar = p;
                        p++;
                        if (p < end) { // else: sequence truncated at slice end — leave untouched
                        switch (*pExtChar) {
                        case 0xa4: // Adlam uppercase
                            if ((*p >= 0x80)
                                && (*p <= 0xa1))
                                (*p) += 0x22;
                            break;
                        default:
                            break;
                        }
                        }
                        break;
                    default:
                        break;
                    }
                    }
                    break;

                default:
                    break;
                }
                }
                pExtChar = 0;
            }
            p++;
        }
    }
    return pString;
}

// _ib_IsUTF8TermChrFast
//
// Fast term-char test for UTF-8 buffers that have ALREADY been through
// BOTH of these, in order:
//   1. _utf_StrToLowerBuf(..., clean=true, ZapChr)  -- folds/cleans every
//      recognized script, zaps everything else UTF-8-specific.
//   2. _ib_ZapNonTermASCII(..., ZapChr)              -- zaps the remaining
//      plain-ASCII punctuation/whitespace that pass 1 deliberately leaves
//      alone, WITHOUT touching DOT_WORDS_SIGNATURE bytes.
// It is NOT valid to call this on a buffer that skipped either pass, or
// that was lowered with clean=false — the dot-in-word neighbor check
// below depends on every non-letter, non-dot byte already being ZapChr.
// Use the original decode-based _ib_IsUTF8TermChr() if that contract
// can't be met.
//
// THE SHORTCUT
//   clean=true has already answered "is this codepoint a letter we index"
//   for every multi-byte sequence: anything it didn't zap to ZapChr is
//   either a case-folded letter, or a script whose ENTIRE byte-value range
//   under that lead byte is confirmed (by Unicode block boundaries, not by
//   whether the fold switch happens to handle every value) to contain no
//   non-letter codepoints. So for those specific lead bytes we can return
//   the sequence length directly — no to_ucs4(), no IsTermChar(cp) table
//   lookup — because we already know the answer is "yes, and it's N bytes
//   long".
//
// WHY THE WHITELIST IS SCOPED THE WAY IT IS
//   "This lead byte survived clean()" and "this lead byte's ENTIRE block is
//   letters/digits-only" are two different facts, and conflating them is
//   exactly the bug that _utf_StrToLowerBuf's original case 0xc3 had: it
//   folds letters correctly but silently left × (U+00D7) and ÷ (U+00F7) —
//   pure symbols — sitting unzapped inside the same lead byte, because the
//   fold switch only cared about which values need case-shifting, not
//   whether every value in the block is a letter. As of this writing, the
//   following have been exhaustively audited against Unicode's UCD (every
//   assigned codepoint mechanically diffed, not spot-checked) and folded
//   into _utf_StrToLowerBuf: Latin-1 Supplement, Latin Extended-A/B, Greek
//   & Coptic, Greek Extended (polytonic), Cyrillic, Cyrillic Supplement,
//   Armenian, Hebrew, Arabic, Syriac, and Arabic Supplement. Arabic-Indic
//   and Extended Arabic-Indic digits (Nd category) are deliberately left
//   as term-worthy pass-through, same treatment as ASCII digits, not
//   folded or zapped. Neither Syriac nor Arabic have case, so those
//   audits are letter-pass-through/mark-zap/punctuation-zap only, no
//   fold logic.
//
//   The same class of gap is presumed to exist, unaudited, in every lead
//   byte NOT in TERM_LEAD_2BYTE below:
//     - 0xca: IPA Extensions (letters) THEN Spacing Modifier Letters
//       (modifier glyphs, not full letters) starting mid-block at U+02B0 —
//       genuinely mixed, not safe to whitelist.
//     - 0xcb: partially handled already ("subset" per the existing case
//       0xcb comment) — incomplete audit, not safe to whitelist.
//     - 0xde, 0xdf (Thaana, NKo): only partially handled ("misc" zap
//       ranges) — not exhaustively audited.
//   Extend TERM_LEAD_2BYTE only after checking the real Unicode block
//   boundaries for a given lead byte, the same way this list was built.
//   Anything not whitelisted falls back to the slow decode path below, so
//   correctness never silently depends on an unaudited block.
//
//   NOTE: this whitelist matters for the main dispatch below (determining
//   a fresh multi-byte character's term-ness and byte length). It does
//   NOT matter for the dot-in-word check any more — that's now a plain
//   ZapChr comparison and works correctly regardless of whether the
//   neighboring script has been audited into this whitelist, PROVIDED
//   _utf_StrToLowerBuf's clean-mode zapping for that script is complete
//   (audited or not, "was it zapped" is exactly what the byte comparison
//   reads).
//
// 3-byte and 4-byte sequences (0xe0-0xf4 leads) always take the slow path
// in the main dispatch below, EXCEPT where explicitly audited (Greek
// Extended, 0xe1's 0xbc-0xbf, is folded/zapped correctly by
// _utf_StrToLowerBuf but is not yet added to a 3-byte confirmed-term
// whitelist here — the dispatch below only whitelists 2-byte leads so
// far). Growing that is future work, same audit discipline as above.
//
// Returns:
//   > 0  — Buffer is (the start of) a term character; value is its byte
//          length, so the caller can advance Position by that many bytes.
//   0    — Buffer is a word separator, or Buffer >= End.

#include <cstdint>

// _ib_ZapNonTermASCII
//
// Companion pass to _utf_StrToLowerBuf's clean-mode zapping, and a
// required prerequisite for _ib_IsUTF8TermChrFast's simplified dot-in-word
// check (see that function's header comment). Where _utf_StrToLowerBuf(
// clean=true) zaps everything UTF-8/script-specific that isn't a letter,
// this pass zaps the remaining plain-ASCII punctuation and whitespace that
// clean-mode deliberately leaves alone (space, comma, colon, digits if
// IsTermChar excludes them, etc.) — EXCEPT DOT_WORDS_SIGNATURE bytes
// ('.', '_', '&', '@', '/', '-', ';', ':', '+'), which are left untouched
// on purpose: their fate is decided later, by whether a real letter is
// adjacent, not by a blanket zap here.
//
// Run this AFTER _utf_StrToLowerBuf and BEFORE tokenizing. Once both have
// run, every byte in the buffer is in exactly one of three states:
// ZapChr, part of a letter, or an undecided DOT_WORDS_SIGNATURE byte —
// which is exactly what lets the dot-in-word test become a plain byte
// comparison instead of a classification call.
//
// Only touches ASCII bytes (< 0x80); multi-byte sequences and their
// continuation bytes are left exactly as _utf_StrToLowerBuf left them.
void _ib_ZapNonTermASCII(unsigned char *pString, unsigned length, unsigned char ZapChr)
{
    if (!pString || !length)
        return;
    unsigned char       *p   = pString;
    const unsigned char *end = pString + length;
    for (; p < end; p++) {
        if (*p == _IB_UTF8_SILENT_ZAP)
            continue; // already resolved by pass 1 (a stripped combining
                       // mark/format char) -- must NOT be reclassified or
                       // overwritten here, or the silent/word-breaking
                       // distinction pass 1 just made is lost and every
                       // stripped diacritic goes back to acting as a word
                       // break (this was a real bug caught by testing:
                       // Hebrew niqqud/Arabic tashkeel fragmenting words).
        if (*p < 0x80 && *p != ZapChr && !IsTermChar(*p) && !IsDotInWord(*p))
            *p = ZapChr;
    }
}

// Lead bytes whose ENTIRE U+xx80-xxBF range is confirmed letters-only by
// Unicode block boundaries (Latin Extended-A: U+0100-017F, Latin
// Extended-B core: U+0180-024F). Grow this only after the same kind of
// verification — do not add a lead byte just because the fold switch
// handles it.
static inline bool _utf8_lead_confirmed_term_2byte(unsigned char lead)
{
    switch (lead) {
    case 0xc2: case 0xc3: // Latin-1 Supplement (0xc2 fully zapped if non-letter;
                           // 0xc3's ×/÷ symbols now zapped too)
    case 0xc4: case 0xc5: // Latin Extended-A
    case 0xc6: case 0xc7: // Latin Extended-B
    case 0xcd: case 0xce: case 0xcf: // Greek & Coptic (combining-marks tail,
                                      // numeral signs, question mark, etc.
                                      // all zapped; 3 real fold bugs fixed)
    case 0xd0: case 0xd1: case 0xd2: case 0xd3: // Cyrillic
    case 0xd4: case 0xd5: // Cyrillic Supplement / Armenian (23-letter fold
                           // bug fixed)
    case 0xd6: case 0xd7: // Hebrew (points/accents/punctuation zapped)
    case 0xd8: case 0xd9: case 0xda: case 0xdb: // Arabic (diacritics/
                           // punctuation/format chars zapped; Arabic-Indic
                           // digits pass through as term-worthy, same as
                           // ASCII digits)
    case 0xdc: case 0xdd: // Syriac + Arabic Supplement (letters pass
                           // through untouched -- neither script has
                           // case; vowel points/marks silent, punctuation
                           // word-breaking, one Cf format char silent)
        return true;
    default:
        return false;
    }
}

// Sequence length from a 2-byte-range lead byte. Caller guarantees
// lead is in 0xc2-0xdf (this table is only consulted for confirmed leads,
// all of which are 2-byte in the current whitelist).
static inline int _utf8_seqlen_2byte(unsigned char /*lead*/) { return 2; }

int _ib_IsUTF8TermChrFast(const unsigned char *Buffer, const unsigned char *End, unsigned char ZapChr)
{
    if (!Buffer || Buffer >= End)
        return 0;

    const unsigned char lead = *Buffer;

    // Fast reject: already zapped by clean() — whatever ZapChr was chosen
    // (space for plain indexing, '\0' once merged with ParseWords).
    if (lead == ZapChr)
        return 0;

    // Fast reject: a silently-zapped combining mark / format char (see
    // _IB_UTF8_SILENT_ZAP's own comment in utf_strtolower_buf.c). This
    // function only answers "is THIS byte a term start" — it does NOT
    // decide whether a silent byte should break a word or not, because
    // that decision needs to look past a whole RUN of silent bytes, which
    // requires loop state this single-position function doesn't have.
    // ParseWordsUTF8's tokenizer loop handles that: it skips runs of
    // _IB_UTF8_SILENT_ZAP transparently and does NOT end the word there,
    // unlike an ordinary ZapChr. Returning 0 here is still correct in
    // isolation — a silent-zapped byte is never itself a term start.
    if (lead == _IB_UTF8_SILENT_ZAP)
        return 0;

    // Fast path: pure ASCII. Unchanged from the original 8-bit/UTF-8
    // routine's ASCII behaviour — same macros, same dot-in-word semantics —
    // except the after-dot lookahead also now recognises a following
    // multi-byte letter (see below), which the byte-only test never could.
    if (lead < 0x80) {
        if (IsTermChar(lead))
            return 1;

        if (IsDotInWord(lead)) {
            // SIMPLIFIED NEIGHBOR CHECK -- valid only once the buffer has
            // been through BOTH _utf_StrToLowerBuf(clean=true, ZapChr)
            // AND _ib_ZapNonTermASCII(..., ZapChr) (see that function's
            // comment). Once both have run, every byte in the buffer is
            // in exactly one of three states: ZapChr, part of a letter
            // (ASCII or multi-byte, case-folded), or an as-yet-undecided
            // DOT_WORDS_SIGNATURE byte. That means "is the character
            // after this dot/hyphen a letter" reduces to "is the next
            // byte != ZapChr" -- true uniformly for ASCII and for
            // multi-byte lead bytes, because a multi-byte sequence's lead
            // byte is ZapChr if and only if the WHOLE sequence was zapped
            // (verified exhaustively elsewhere: no partial zaps exist
            // anywhere in _utf_StrToLowerBuf). No per-script
            // classification, no confirmed-term whitelist, no decode --
            // just a byte comparison. This also naturally handles a run
            // like "e.g." without any separate two-level lookahead: each
            // dot's own forward neighbor is checked independently, so the
            // first dot (followed by 'g') absorbs, and the second dot
            // (followed by ZapChr, once the trailing space is zapped)
            // does not.
            //
            // This only decides whether to ABSORB the dot going forward.
            // No backward check is needed: in a correct single left-to-
            // right pass, a dot preceded by a real letter would already
            // have been absorbed while extending that letter's term, so
            // it never reaches this function as a fresh position with a
            // non-zapped predecessor.
            const unsigned char *next = Buffer + 1;
            if (next < End && *next != ZapChr)
                return 1;
        }
        return 0;
    }

    // Fast path: confirmed-letters-only 2-byte lead byte, per the audited
    // whitelist above.
    if (_utf8_lead_confirmed_term_2byte(lead))
        return _utf8_seqlen_2byte(lead);

    // Slow path: everything else — unaudited 2-byte leads, and all 3/4-byte
    // sequences. Identical to the original _ib_IsUTF8TermChr's decode step.
    uint32_t cp = 0;
    int bytes = _to_ucs4(Buffer, End, &cp);
    if (bytes < 0)
        return 0;
    return IsTermChar(cp) ? bytes : 0;
}


int _to_ucs4(const unsigned char *chr, const unsigned char *end, uint32_t *cp)
{
    if (cp)
        *cp = 0;

    if (!chr || !end || chr >= end)
        return 0;

    const unsigned char c0 = chr[0];

    // ASCII
    if (c0 < 0x80)
    {
        if (cp)
            *cp = c0;
        return 1;
    }

    // 2-byte UTF-8
    if (c0 >= 0xC2 && c0 <= 0xDF)
    {
        if (chr + 1 >= end)
            return 0;

        const unsigned char c1 = chr[1];

        if ((c1 & 0xC0) != 0x80)
            return 0;

        const uint32_t value =
            ((uint32_t)(c0 & 0x1F) << 6) |
             (uint32_t)(c1 & 0x3F);

        if (cp)
            *cp = value;

        return 2;
    }

    // 3-byte UTF-8
    if (c0 >= 0xE0 && c0 <= 0xEF)
    {
        if (chr + 2 >= end)
            return 0;

        const unsigned char c1 = chr[1];
        const unsigned char c2 = chr[2];

        if ((c1 & 0xC0) != 0x80 ||
            (c2 & 0xC0) != 0x80)
            return 0;

        // Overlong sequence.
        if (c0 == 0xE0 && c1 < 0xA0)
            return 0;

        // UTF-16 surrogate range.
        if (c0 == 0xED && c1 >= 0xA0)
            return 0;

        const uint32_t value =
            ((uint32_t)(c0 & 0x0F) << 12) |
            ((uint32_t)(c1 & 0x3F) << 6) |
             (uint32_t)(c2 & 0x3F);

        if (cp)
            *cp = value;

        return 3;
    }

    // 4-byte UTF-8
    if (c0 >= 0xF0 && c0 <= 0xF4)
    {
        if (chr + 3 >= end)
            return 0;

        const unsigned char c1 = chr[1];
        const unsigned char c2 = chr[2];
        const unsigned char c3 = chr[3];

        if ((c1 & 0xC0) != 0x80 ||
            (c2 & 0xC0) != 0x80 ||
            (c3 & 0xC0) != 0x80)
            return 0;

        // Overlong sequence.
        if (c0 == 0xF0 && c1 < 0x90)
            return 0;

        // Beyond U+10FFFF.
        if (c0 == 0xF4 && c1 > 0x8F)
            return 0;

        const uint32_t value =
            ((uint32_t)(c0 & 0x07) << 18) |
            ((uint32_t)(c1 & 0x3F) << 12) |
            ((uint32_t)(c2 & 0x3F) << 6) |
             (uint32_t)(c3 & 0x3F);

        if (cp)
            *cp = value;

        return 4;
    }

    // Continuation byte, illegal lead byte, etc.
    return 0;
}

