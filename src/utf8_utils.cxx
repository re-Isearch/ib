

int _ib_IsUTF8TermChr(const unsigned char *Buffer);
unsigned char *_utf_StrToLower(unsigned char *pString, const bool clean,
	unsigned length = 0);
unsigned char *_utf_StrToUpper(unsigned char *pString,
	unsigned length = 0);
INT _utf_strncasecmp(const UCHR *p1, const UCHR *p2, const INT n,
	bool *look = NULL, size_t *p2_bytes = NULL);

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
#define _SP ' '

static inline int _utf8_lower_cp(const UCHR *src, UCHR dst[5])
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
INT _utf_strncasecmp(const UCHR *p1, const UCHR *p2, const INT n,
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

unsigned char *_utf_StrToLower(unsigned char *pString, const bool clean,
                                      unsigned length = 0)
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
unsigned char *_utf_StrToUpper(unsigned char *pString,
                                      unsigned length = 0)
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




