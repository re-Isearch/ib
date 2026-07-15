// ---------------------------------------------------------------------------
// _utf8_codepoint_len
//   Return the byte length of the UTF-8 sequence starting at *p,
//   or 1 for any invalid / continuation byte (safe forward-walk).
// ---------------------------------------------------------------------------
static inline int _utf8_codepoint_len(const UCHR *p)
{
    if      ((*p & 0x80) == 0x00) return 1; // 0xxxxxxx  ASCII
    else if ((*p & 0xe0) == 0xc0) return 2; // 110xxxxx
    else if ((*p & 0xf0) == 0xe0) return 3; // 1110xxxx
    else if ((*p & 0xf8) == 0xf0) return 4; // 11110xxx
    return 1; // continuation or invalid — advance one byte
}

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
static inline int _utf8_lower_cp(const UCHR *src, UCHR dst[5])
{
    int len = _utf8_codepoint_len(src);
    for (int i = 0; i < len; i++) dst[i] = src[i];
    dst[len] = '\0';                          // temporary sentinel for _utf_StrToLower
    _utf_StrToLower((UCHR *)dst, false);      // in-place lowercase, no clean
    return len;
}

// ---------------------------------------------------------------------------
// _utf8_to_ucs32
//   Decode one UTF-8 sequence (already length-checked by _utf8_codepoint_len)
//   into its UCS-32 codepoint value.  p must point at a valid lead byte.
//   Invalid / truncated sequences return the raw lead byte (still non-zero,
//   still unique, so comparisons remain meaningful).
// ---------------------------------------------------------------------------
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
static inline INT _utf_strncasecmp(const UCHR *p1, const UCHR *p2, const INT n,
// Whether more term chars follow in p2 after the match, byte-length of p2 match
                                   bool *look = NULL, size_t *p2_bytes = NULL)
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
