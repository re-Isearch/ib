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
#define _UTF8_LOWER_PAD(base, new_len, old_len, fill) \
    do { for (int _i = (new_len); _i < (old_len); _i++) (base)[_i] = (fill); } while (0)

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

static unsigned char *_utf_StrToLowerBuf(unsigned char *pString, unsigned length, const bool clean, unsigned char ZapChr = _SP)
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
