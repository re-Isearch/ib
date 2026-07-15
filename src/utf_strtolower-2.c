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

static unsigned char *_utf_StrToLower(unsigned char *pString, const bool clean,
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
