// _utf_StrToUpper
//
// Uppercase a UTF-8 string. General-purpose routine: pass length=0 (the
// default) to treat pString as NUL-terminated -- safe to call on an
// ordinary C string, exactly like the original _utf_StrToLower did.
// Pass an explicit nonzero length to treat pString as an exact byte-
// length buffer instead: NOT NUL-terminated, no *p/*pString check
// anywhere in that mode, same buffer contract as _utf_StrToLowerBuf
// (safe on a buffer that may contain embedded NUL bytes from prior
// zapping). One function, two modes, selected by whether length is 0.
//
// Deliberately has NO clean/ZapChr parameters and does no zapping at
// all -- this is pure case conversion, nothing is stripped or replaced.
//
// LENGTH-STABILITY CONTRACT: every mapping here changes byte VALUES in
// place, never a sequence's byte length or WIDTH. Two distinct ways a
// naive uppercase mapping could break this are both filtered out at
// generation time, not just the obvious one:
//   - Multi-character expansions (e.g. ß U+00DF -> "SS") are skipped;
//     ß is left untouched rather than expanded.
//   - Same-character but WIDER-encoding targets are also skipped, and
//     this one is easy to miss: some 2-byte Latin Extended-B letters
//     (e.g. U+023F) have an uppercase that only exists in the 3-byte
//     range (U+2C7E) -- Unicode added the capital form later, in a
//     different block, without reusing 2-byte space. Naively cross-
//     mapping by codepoint alone (as this file's fold tables do for
//     same-width crossings, e.g. Latin-1 -> Latin Extended-A) would
//     silently corrupt these into garbage bytes. Every candidate
//     mapping here was checked to encode to the SAME byte width as its
//     source before being included; anything that doesn't is left
//     untouched, exactly like the multi-character case.
//
// SCOPE: mirrors _utf_StrToLowerBuf's audited scripts, generated
// directly from Unicode's UCD and verified exhaustively against it, the
// same way as the lowering function -- Latin (every 2-byte lead 0xc2-
// 0xdf was scanned exhaustively for real case pairs, not a curated
// list, after two rounds of finding leads assumed "no case" that
// actually had one hiding in them -- e.g. 0xc2 has the MICRO SIGN ->
// Greek Mu, and 0xd6 has Armenian's tail letters mixed in with Hebrew),
// Greek & Coptic, Greek Extended (polytonic), Cyrillic, Armenian,
// Georgian (Mkhedruli -> Mtavruli), Latin Extended Additional, Osage,
// and Adlam. Hebrew, Arabic proper, Syriac, and Arabic Supplement have
// no case at all, so nothing is needed for them -- every byte already
// passes through unchanged correctly. Scripts _utf_StrToLowerBuf hasn't
// audited yet (Thaana, NKo, Indic scripts) aren't covered here either --
// extend both functions together, same audit discipline.
//
// OUT-OF-BOUNDS SAFETY (buffer mode only): every lookahead byte is
// guarded by "!end || p < end" -- vacuously true in NUL-terminated mode
// (end is NULL, so the NUL terminator is what actually stops things,
// same reasoning as the original _utf_StrToLower), and a real boundary
// check in buffer mode. A multi-byte sequence truncated exactly at the
// buffer end is left untouched rather than read past the boundary.
static unsigned char *_utf_StrToUpper(unsigned char *pString, unsigned length = 0)
{
    if (!pString)
        return pString;
    unsigned char       *p   = pString;
    const unsigned char *end = length ? (pString + length) : NULL;
    unsigned char *pExtChar = 0;

    while (end ? (p < end) : (*p != '\0')) {
        if ((*p >= 'a') && (*p <= 'z')) {
            (*p) -= 0x20;
        }
        else if (*p > 0xc0) {
            pExtChar = p;
            p++;
            if (!end || p < end) { // else: sequence truncated at buffer end -- leave untouched
            switch (*pExtChar) {
                case 0xc2:
                    switch (*p) {
                    case 0xb5: // MICRO SIGN (crosses to lead 0xce)
                        *pExtChar = 0xce;
                        (*p) = 0x9c;
                        break;
                    default:
                        break;
                    }
                    break;

                case 0xc3:
                    switch (*p) {
                    case 0xa0: // LATIN SMALL LETTER A WITH GRAVE
                        (*p) = 0x80;
                        break;
                    case 0xa1: // LATIN SMALL LETTER A WITH ACUTE
                        (*p) = 0x81;
                        break;
                    case 0xa2: // LATIN SMALL LETTER A WITH CIRCUMFLEX
                        (*p) = 0x82;
                        break;
                    case 0xa3: // LATIN SMALL LETTER A WITH TILDE
                        (*p) = 0x83;
                        break;
                    case 0xa4: // LATIN SMALL LETTER A WITH DIAERESIS
                        (*p) = 0x84;
                        break;
                    case 0xa5: // LATIN SMALL LETTER A WITH RING ABOVE
                        (*p) = 0x85;
                        break;
                    case 0xa6: // LATIN SMALL LETTER AE
                        (*p) = 0x86;
                        break;
                    case 0xa7: // LATIN SMALL LETTER C WITH CEDILLA
                        (*p) = 0x87;
                        break;
                    case 0xa8: // LATIN SMALL LETTER E WITH GRAVE
                        (*p) = 0x88;
                        break;
                    case 0xa9: // LATIN SMALL LETTER E WITH ACUTE
                        (*p) = 0x89;
                        break;
                    case 0xaa: // LATIN SMALL LETTER E WITH CIRCUMFLEX
                        (*p) = 0x8a;
                        break;
                    case 0xab: // LATIN SMALL LETTER E WITH DIAERESIS
                        (*p) = 0x8b;
                        break;
                    case 0xac: // LATIN SMALL LETTER I WITH GRAVE
                        (*p) = 0x8c;
                        break;
                    case 0xad: // LATIN SMALL LETTER I WITH ACUTE
                        (*p) = 0x8d;
                        break;
                    case 0xae: // LATIN SMALL LETTER I WITH CIRCUMFLEX
                        (*p) = 0x8e;
                        break;
                    case 0xaf: // LATIN SMALL LETTER I WITH DIAERESIS
                        (*p) = 0x8f;
                        break;
                    case 0xb0: // LATIN SMALL LETTER ETH
                        (*p) = 0x90;
                        break;
                    case 0xb1: // LATIN SMALL LETTER N WITH TILDE
                        (*p) = 0x91;
                        break;
                    case 0xb2: // LATIN SMALL LETTER O WITH GRAVE
                        (*p) = 0x92;
                        break;
                    case 0xb3: // LATIN SMALL LETTER O WITH ACUTE
                        (*p) = 0x93;
                        break;
                    case 0xb4: // LATIN SMALL LETTER O WITH CIRCUMFLEX
                        (*p) = 0x94;
                        break;
                    case 0xb5: // LATIN SMALL LETTER O WITH TILDE
                        (*p) = 0x95;
                        break;
                    case 0xb6: // LATIN SMALL LETTER O WITH DIAERESIS
                        (*p) = 0x96;
                        break;
                    case 0xb8: // LATIN SMALL LETTER O WITH STROKE
                        (*p) = 0x98;
                        break;
                    case 0xb9: // LATIN SMALL LETTER U WITH GRAVE
                        (*p) = 0x99;
                        break;
                    case 0xba: // LATIN SMALL LETTER U WITH ACUTE
                        (*p) = 0x9a;
                        break;
                    case 0xbb: // LATIN SMALL LETTER U WITH CIRCUMFLEX
                        (*p) = 0x9b;
                        break;
                    case 0xbc: // LATIN SMALL LETTER U WITH DIAERESIS
                        (*p) = 0x9c;
                        break;
                    case 0xbd: // LATIN SMALL LETTER Y WITH ACUTE
                        (*p) = 0x9d;
                        break;
                    case 0xbe: // LATIN SMALL LETTER THORN
                        (*p) = 0x9e;
                        break;
                    case 0xbf: // LATIN SMALL LETTER Y WITH DIAERESIS (crosses to lead 0xc5)
                        *pExtChar = 0xc5;
                        (*p) = 0xb8;
                        break;
                    default:
                        break;
                    }
                    break;

                case 0xc4:
                    switch (*p) {
                    case 0x81: // LATIN SMALL LETTER A WITH MACRON
                        (*p) = 0x80;
                        break;
                    case 0x83: // LATIN SMALL LETTER A WITH BREVE
                        (*p) = 0x82;
                        break;
                    case 0x85: // LATIN SMALL LETTER A WITH OGONEK
                        (*p) = 0x84;
                        break;
                    case 0x87: // LATIN SMALL LETTER C WITH ACUTE
                        (*p) = 0x86;
                        break;
                    case 0x89: // LATIN SMALL LETTER C WITH CIRCUMFLEX
                        (*p) = 0x88;
                        break;
                    case 0x8b: // LATIN SMALL LETTER C WITH DOT ABOVE
                        (*p) = 0x8a;
                        break;
                    case 0x8d: // LATIN SMALL LETTER C WITH CARON
                        (*p) = 0x8c;
                        break;
                    case 0x8f: // LATIN SMALL LETTER D WITH CARON
                        (*p) = 0x8e;
                        break;
                    case 0x91: // LATIN SMALL LETTER D WITH STROKE
                        (*p) = 0x90;
                        break;
                    case 0x93: // LATIN SMALL LETTER E WITH MACRON
                        (*p) = 0x92;
                        break;
                    case 0x95: // LATIN SMALL LETTER E WITH BREVE
                        (*p) = 0x94;
                        break;
                    case 0x97: // LATIN SMALL LETTER E WITH DOT ABOVE
                        (*p) = 0x96;
                        break;
                    case 0x99: // LATIN SMALL LETTER E WITH OGONEK
                        (*p) = 0x98;
                        break;
                    case 0x9b: // LATIN SMALL LETTER E WITH CARON
                        (*p) = 0x9a;
                        break;
                    case 0x9d: // LATIN SMALL LETTER G WITH CIRCUMFLEX
                        (*p) = 0x9c;
                        break;
                    case 0x9f: // LATIN SMALL LETTER G WITH BREVE
                        (*p) = 0x9e;
                        break;
                    case 0xa1: // LATIN SMALL LETTER G WITH DOT ABOVE
                        (*p) = 0xa0;
                        break;
                    case 0xa3: // LATIN SMALL LETTER G WITH CEDILLA
                        (*p) = 0xa2;
                        break;
                    case 0xa5: // LATIN SMALL LETTER H WITH CIRCUMFLEX
                        (*p) = 0xa4;
                        break;
                    case 0xa7: // LATIN SMALL LETTER H WITH STROKE
                        (*p) = 0xa6;
                        break;
                    case 0xa9: // LATIN SMALL LETTER I WITH TILDE
                        (*p) = 0xa8;
                        break;
                    case 0xab: // LATIN SMALL LETTER I WITH MACRON
                        (*p) = 0xaa;
                        break;
                    case 0xad: // LATIN SMALL LETTER I WITH BREVE
                        (*p) = 0xac;
                        break;
                    case 0xaf: // LATIN SMALL LETTER I WITH OGONEK
                        (*p) = 0xae;
                        break;
                    case 0xb3: // LATIN SMALL LIGATURE IJ
                        (*p) = 0xb2;
                        break;
                    case 0xb5: // LATIN SMALL LETTER J WITH CIRCUMFLEX
                        (*p) = 0xb4;
                        break;
                    case 0xb7: // LATIN SMALL LETTER K WITH CEDILLA
                        (*p) = 0xb6;
                        break;
                    case 0xba: // LATIN SMALL LETTER L WITH ACUTE
                        (*p) = 0xb9;
                        break;
                    case 0xbc: // LATIN SMALL LETTER L WITH CEDILLA
                        (*p) = 0xbb;
                        break;
                    case 0xbe: // LATIN SMALL LETTER L WITH CARON
                        (*p) = 0xbd;
                        break;
                    default:
                        break;
                    }
                    break;

                case 0xc5:
                    switch (*p) {
                    case 0x80: // LATIN SMALL LETTER L WITH MIDDLE DOT (crosses to lead 0xc4)
                        *pExtChar = 0xc4;
                        (*p) = 0xbf;
                        break;
                    case 0x82: // LATIN SMALL LETTER L WITH STROKE
                        (*p) = 0x81;
                        break;
                    case 0x84: // LATIN SMALL LETTER N WITH ACUTE
                        (*p) = 0x83;
                        break;
                    case 0x86: // LATIN SMALL LETTER N WITH CEDILLA
                        (*p) = 0x85;
                        break;
                    case 0x88: // LATIN SMALL LETTER N WITH CARON
                        (*p) = 0x87;
                        break;
                    case 0x8b: // LATIN SMALL LETTER ENG
                        (*p) = 0x8a;
                        break;
                    case 0x8d: // LATIN SMALL LETTER O WITH MACRON
                        (*p) = 0x8c;
                        break;
                    case 0x8f: // LATIN SMALL LETTER O WITH BREVE
                        (*p) = 0x8e;
                        break;
                    case 0x91: // LATIN SMALL LETTER O WITH DOUBLE ACUTE
                        (*p) = 0x90;
                        break;
                    case 0x93: // LATIN SMALL LIGATURE OE
                        (*p) = 0x92;
                        break;
                    case 0x95: // LATIN SMALL LETTER R WITH ACUTE
                        (*p) = 0x94;
                        break;
                    case 0x97: // LATIN SMALL LETTER R WITH CEDILLA
                        (*p) = 0x96;
                        break;
                    case 0x99: // LATIN SMALL LETTER R WITH CARON
                        (*p) = 0x98;
                        break;
                    case 0x9b: // LATIN SMALL LETTER S WITH ACUTE
                        (*p) = 0x9a;
                        break;
                    case 0x9d: // LATIN SMALL LETTER S WITH CIRCUMFLEX
                        (*p) = 0x9c;
                        break;
                    case 0x9f: // LATIN SMALL LETTER S WITH CEDILLA
                        (*p) = 0x9e;
                        break;
                    case 0xa1: // LATIN SMALL LETTER S WITH CARON
                        (*p) = 0xa0;
                        break;
                    case 0xa3: // LATIN SMALL LETTER T WITH CEDILLA
                        (*p) = 0xa2;
                        break;
                    case 0xa5: // LATIN SMALL LETTER T WITH CARON
                        (*p) = 0xa4;
                        break;
                    case 0xa7: // LATIN SMALL LETTER T WITH STROKE
                        (*p) = 0xa6;
                        break;
                    case 0xa9: // LATIN SMALL LETTER U WITH TILDE
                        (*p) = 0xa8;
                        break;
                    case 0xab: // LATIN SMALL LETTER U WITH MACRON
                        (*p) = 0xaa;
                        break;
                    case 0xad: // LATIN SMALL LETTER U WITH BREVE
                        (*p) = 0xac;
                        break;
                    case 0xaf: // LATIN SMALL LETTER U WITH RING ABOVE
                        (*p) = 0xae;
                        break;
                    case 0xb1: // LATIN SMALL LETTER U WITH DOUBLE ACUTE
                        (*p) = 0xb0;
                        break;
                    case 0xb3: // LATIN SMALL LETTER U WITH OGONEK
                        (*p) = 0xb2;
                        break;
                    case 0xb5: // LATIN SMALL LETTER W WITH CIRCUMFLEX
                        (*p) = 0xb4;
                        break;
                    case 0xb7: // LATIN SMALL LETTER Y WITH CIRCUMFLEX
                        (*p) = 0xb6;
                        break;
                    case 0xba: // LATIN SMALL LETTER Z WITH ACUTE
                        (*p) = 0xb9;
                        break;
                    case 0xbc: // LATIN SMALL LETTER Z WITH DOT ABOVE
                        (*p) = 0xbb;
                        break;
                    case 0xbe: // LATIN SMALL LETTER Z WITH CARON
                        (*p) = 0xbd;
                        break;
                    default:
                        break;
                    }
                    break;

                case 0xc6:
                    switch (*p) {
                    case 0x80: // LATIN SMALL LETTER B WITH STROKE (crosses to lead 0xc9)
                        *pExtChar = 0xc9;
                        (*p) = 0x83;
                        break;
                    case 0x83: // LATIN SMALL LETTER B WITH TOPBAR
                        (*p) = 0x82;
                        break;
                    case 0x85: // LATIN SMALL LETTER TONE SIX
                        (*p) = 0x84;
                        break;
                    case 0x88: // LATIN SMALL LETTER C WITH HOOK
                        (*p) = 0x87;
                        break;
                    case 0x8c: // LATIN SMALL LETTER D WITH TOPBAR
                        (*p) = 0x8b;
                        break;
                    case 0x92: // LATIN SMALL LETTER F WITH HOOK
                        (*p) = 0x91;
                        break;
                    case 0x95: // LATIN SMALL LETTER HV (crosses to lead 0xc7)
                        *pExtChar = 0xc7;
                        (*p) = 0xb6;
                        break;
                    case 0x99: // LATIN SMALL LETTER K WITH HOOK
                        (*p) = 0x98;
                        break;
                    case 0x9a: // LATIN SMALL LETTER L WITH BAR (crosses to lead 0xc8)
                        *pExtChar = 0xc8;
                        (*p) = 0xbd;
                        break;
                    case 0x9e: // LATIN SMALL LETTER N WITH LONG RIGHT LEG (crosses to lead 0xc8)
                        *pExtChar = 0xc8;
                        (*p) = 0xa0;
                        break;
                    case 0xa1: // LATIN SMALL LETTER O WITH HORN
                        (*p) = 0xa0;
                        break;
                    case 0xa3: // LATIN SMALL LETTER OI
                        (*p) = 0xa2;
                        break;
                    case 0xa5: // LATIN SMALL LETTER P WITH HOOK
                        (*p) = 0xa4;
                        break;
                    case 0xa8: // LATIN SMALL LETTER TONE TWO
                        (*p) = 0xa7;
                        break;
                    case 0xad: // LATIN SMALL LETTER T WITH HOOK
                        (*p) = 0xac;
                        break;
                    case 0xb0: // LATIN SMALL LETTER U WITH HORN
                        (*p) = 0xaf;
                        break;
                    case 0xb4: // LATIN SMALL LETTER Y WITH HOOK
                        (*p) = 0xb3;
                        break;
                    case 0xb6: // LATIN SMALL LETTER Z WITH STROKE
                        (*p) = 0xb5;
                        break;
                    case 0xb9: // LATIN SMALL LETTER EZH REVERSED
                        (*p) = 0xb8;
                        break;
                    case 0xbd: // LATIN SMALL LETTER TONE FIVE
                        (*p) = 0xbc;
                        break;
                    case 0xbf: // LATIN LETTER WYNN (crosses to lead 0xc7)
                        *pExtChar = 0xc7;
                        (*p) = 0xb7;
                        break;
                    default:
                        break;
                    }
                    break;

                case 0xc7:
                    switch (*p) {
                    case 0x85: // LATIN CAPITAL LETTER D WITH SMALL LETTER Z WITH CARON
                        (*p) = 0x84;
                        break;
                    case 0x86: // LATIN SMALL LETTER DZ WITH CARON
                        (*p) = 0x84;
                        break;
                    case 0x88: // LATIN CAPITAL LETTER L WITH SMALL LETTER J
                        (*p) = 0x87;
                        break;
                    case 0x89: // LATIN SMALL LETTER LJ
                        (*p) = 0x87;
                        break;
                    case 0x8b: // LATIN CAPITAL LETTER N WITH SMALL LETTER J
                        (*p) = 0x8a;
                        break;
                    case 0x8c: // LATIN SMALL LETTER NJ
                        (*p) = 0x8a;
                        break;
                    case 0x8e: // LATIN SMALL LETTER A WITH CARON
                        (*p) = 0x8d;
                        break;
                    case 0x90: // LATIN SMALL LETTER I WITH CARON
                        (*p) = 0x8f;
                        break;
                    case 0x92: // LATIN SMALL LETTER O WITH CARON
                        (*p) = 0x91;
                        break;
                    case 0x94: // LATIN SMALL LETTER U WITH CARON
                        (*p) = 0x93;
                        break;
                    case 0x96: // LATIN SMALL LETTER U WITH DIAERESIS AND MACRON
                        (*p) = 0x95;
                        break;
                    case 0x98: // LATIN SMALL LETTER U WITH DIAERESIS AND ACUTE
                        (*p) = 0x97;
                        break;
                    case 0x9a: // LATIN SMALL LETTER U WITH DIAERESIS AND CARON
                        (*p) = 0x99;
                        break;
                    case 0x9c: // LATIN SMALL LETTER U WITH DIAERESIS AND GRAVE
                        (*p) = 0x9b;
                        break;
                    case 0x9d: // LATIN SMALL LETTER TURNED E (crosses to lead 0xc6)
                        *pExtChar = 0xc6;
                        (*p) = 0x8e;
                        break;
                    case 0x9f: // LATIN SMALL LETTER A WITH DIAERESIS AND MACRON
                        (*p) = 0x9e;
                        break;
                    case 0xa1: // LATIN SMALL LETTER A WITH DOT ABOVE AND MACRON
                        (*p) = 0xa0;
                        break;
                    case 0xa3: // LATIN SMALL LETTER AE WITH MACRON
                        (*p) = 0xa2;
                        break;
                    case 0xa5: // LATIN SMALL LETTER G WITH STROKE
                        (*p) = 0xa4;
                        break;
                    case 0xa7: // LATIN SMALL LETTER G WITH CARON
                        (*p) = 0xa6;
                        break;
                    case 0xa9: // LATIN SMALL LETTER K WITH CARON
                        (*p) = 0xa8;
                        break;
                    case 0xab: // LATIN SMALL LETTER O WITH OGONEK
                        (*p) = 0xaa;
                        break;
                    case 0xad: // LATIN SMALL LETTER O WITH OGONEK AND MACRON
                        (*p) = 0xac;
                        break;
                    case 0xaf: // LATIN SMALL LETTER EZH WITH CARON
                        (*p) = 0xae;
                        break;
                    case 0xb2: // LATIN CAPITAL LETTER D WITH SMALL LETTER Z
                        (*p) = 0xb1;
                        break;
                    case 0xb3: // LATIN SMALL LETTER DZ
                        (*p) = 0xb1;
                        break;
                    case 0xb5: // LATIN SMALL LETTER G WITH ACUTE
                        (*p) = 0xb4;
                        break;
                    case 0xb9: // LATIN SMALL LETTER N WITH GRAVE
                        (*p) = 0xb8;
                        break;
                    case 0xbb: // LATIN SMALL LETTER A WITH RING ABOVE AND ACUTE
                        (*p) = 0xba;
                        break;
                    case 0xbd: // LATIN SMALL LETTER AE WITH ACUTE
                        (*p) = 0xbc;
                        break;
                    case 0xbf: // LATIN SMALL LETTER O WITH STROKE AND ACUTE
                        (*p) = 0xbe;
                        break;
                    default:
                        break;
                    }
                    break;

                case 0xc8:
                    switch (*p) {
                    case 0x81: // LATIN SMALL LETTER A WITH DOUBLE GRAVE
                        (*p) = 0x80;
                        break;
                    case 0x83: // LATIN SMALL LETTER A WITH INVERTED BREVE
                        (*p) = 0x82;
                        break;
                    case 0x85: // LATIN SMALL LETTER E WITH DOUBLE GRAVE
                        (*p) = 0x84;
                        break;
                    case 0x87: // LATIN SMALL LETTER E WITH INVERTED BREVE
                        (*p) = 0x86;
                        break;
                    case 0x89: // LATIN SMALL LETTER I WITH DOUBLE GRAVE
                        (*p) = 0x88;
                        break;
                    case 0x8b: // LATIN SMALL LETTER I WITH INVERTED BREVE
                        (*p) = 0x8a;
                        break;
                    case 0x8d: // LATIN SMALL LETTER O WITH DOUBLE GRAVE
                        (*p) = 0x8c;
                        break;
                    case 0x8f: // LATIN SMALL LETTER O WITH INVERTED BREVE
                        (*p) = 0x8e;
                        break;
                    case 0x91: // LATIN SMALL LETTER R WITH DOUBLE GRAVE
                        (*p) = 0x90;
                        break;
                    case 0x93: // LATIN SMALL LETTER R WITH INVERTED BREVE
                        (*p) = 0x92;
                        break;
                    case 0x95: // LATIN SMALL LETTER U WITH DOUBLE GRAVE
                        (*p) = 0x94;
                        break;
                    case 0x97: // LATIN SMALL LETTER U WITH INVERTED BREVE
                        (*p) = 0x96;
                        break;
                    case 0x99: // LATIN SMALL LETTER S WITH COMMA BELOW
                        (*p) = 0x98;
                        break;
                    case 0x9b: // LATIN SMALL LETTER T WITH COMMA BELOW
                        (*p) = 0x9a;
                        break;
                    case 0x9d: // LATIN SMALL LETTER YOGH
                        (*p) = 0x9c;
                        break;
                    case 0x9f: // LATIN SMALL LETTER H WITH CARON
                        (*p) = 0x9e;
                        break;
                    case 0xa3: // LATIN SMALL LETTER OU
                        (*p) = 0xa2;
                        break;
                    case 0xa5: // LATIN SMALL LETTER Z WITH HOOK
                        (*p) = 0xa4;
                        break;
                    case 0xa7: // LATIN SMALL LETTER A WITH DOT ABOVE
                        (*p) = 0xa6;
                        break;
                    case 0xa9: // LATIN SMALL LETTER E WITH CEDILLA
                        (*p) = 0xa8;
                        break;
                    case 0xab: // LATIN SMALL LETTER O WITH DIAERESIS AND MACRON
                        (*p) = 0xaa;
                        break;
                    case 0xad: // LATIN SMALL LETTER O WITH TILDE AND MACRON
                        (*p) = 0xac;
                        break;
                    case 0xaf: // LATIN SMALL LETTER O WITH DOT ABOVE
                        (*p) = 0xae;
                        break;
                    case 0xb1: // LATIN SMALL LETTER O WITH DOT ABOVE AND MACRON
                        (*p) = 0xb0;
                        break;
                    case 0xb3: // LATIN SMALL LETTER Y WITH MACRON
                        (*p) = 0xb2;
                        break;
                    case 0xbc: // LATIN SMALL LETTER C WITH STROKE
                        (*p) = 0xbb;
                        break;
                    default:
                        break;
                    }
                    break;

                case 0xc9:
                    switch (*p) {
                    case 0x82: // LATIN SMALL LETTER GLOTTAL STOP
                        (*p) = 0x81;
                        break;
                    case 0x87: // LATIN SMALL LETTER E WITH STROKE
                        (*p) = 0x86;
                        break;
                    case 0x89: // LATIN SMALL LETTER J WITH STROKE
                        (*p) = 0x88;
                        break;
                    case 0x8b: // LATIN SMALL LETTER Q WITH HOOK TAIL
                        (*p) = 0x8a;
                        break;
                    case 0x8d: // LATIN SMALL LETTER R WITH STROKE
                        (*p) = 0x8c;
                        break;
                    case 0x8f: // LATIN SMALL LETTER Y WITH STROKE
                        (*p) = 0x8e;
                        break;
                    case 0x93: // LATIN SMALL LETTER B WITH HOOK (crosses to lead 0xc6)
                        *pExtChar = 0xc6;
                        (*p) = 0x81;
                        break;
                    case 0x94: // LATIN SMALL LETTER OPEN O (crosses to lead 0xc6)
                        *pExtChar = 0xc6;
                        (*p) = 0x86;
                        break;
                    case 0x96: // LATIN SMALL LETTER D WITH TAIL (crosses to lead 0xc6)
                        *pExtChar = 0xc6;
                        (*p) = 0x89;
                        break;
                    case 0x97: // LATIN SMALL LETTER D WITH HOOK (crosses to lead 0xc6)
                        *pExtChar = 0xc6;
                        (*p) = 0x8a;
                        break;
                    case 0x99: // LATIN SMALL LETTER SCHWA (crosses to lead 0xc6)
                        *pExtChar = 0xc6;
                        (*p) = 0x8f;
                        break;
                    case 0x9b: // LATIN SMALL LETTER OPEN E (crosses to lead 0xc6)
                        *pExtChar = 0xc6;
                        (*p) = 0x90;
                        break;
                    case 0xa0: // LATIN SMALL LETTER G WITH HOOK (crosses to lead 0xc6)
                        *pExtChar = 0xc6;
                        (*p) = 0x93;
                        break;
                    case 0xa3: // LATIN SMALL LETTER GAMMA (crosses to lead 0xc6)
                        *pExtChar = 0xc6;
                        (*p) = 0x94;
                        break;
                    case 0xa8: // LATIN SMALL LETTER I WITH STROKE (crosses to lead 0xc6)
                        *pExtChar = 0xc6;
                        (*p) = 0x97;
                        break;
                    case 0xa9: // LATIN SMALL LETTER IOTA (crosses to lead 0xc6)
                        *pExtChar = 0xc6;
                        (*p) = 0x96;
                        break;
                    case 0xaf: // LATIN SMALL LETTER TURNED M (crosses to lead 0xc6)
                        *pExtChar = 0xc6;
                        (*p) = 0x9c;
                        break;
                    case 0xb2: // LATIN SMALL LETTER N WITH LEFT HOOK (crosses to lead 0xc6)
                        *pExtChar = 0xc6;
                        (*p) = 0x9d;
                        break;
                    case 0xb5: // LATIN SMALL LETTER BARRED O (crosses to lead 0xc6)
                        *pExtChar = 0xc6;
                        (*p) = 0x9f;
                        break;
                    default:
                        break;
                    }
                    break;

                case 0xca:
                    switch (*p) {
                    case 0x80: // LATIN LETTER SMALL CAPITAL R (crosses to lead 0xc6)
                        *pExtChar = 0xc6;
                        (*p) = 0xa6;
                        break;
                    case 0x83: // LATIN SMALL LETTER ESH (crosses to lead 0xc6)
                        *pExtChar = 0xc6;
                        (*p) = 0xa9;
                        break;
                    case 0x88: // LATIN SMALL LETTER T WITH RETROFLEX HOOK (crosses to lead 0xc6)
                        *pExtChar = 0xc6;
                        (*p) = 0xae;
                        break;
                    case 0x89: // LATIN SMALL LETTER U BAR (crosses to lead 0xc9)
                        *pExtChar = 0xc9;
                        (*p) = 0x84;
                        break;
                    case 0x8a: // LATIN SMALL LETTER UPSILON (crosses to lead 0xc6)
                        *pExtChar = 0xc6;
                        (*p) = 0xb1;
                        break;
                    case 0x8b: // LATIN SMALL LETTER V WITH HOOK (crosses to lead 0xc6)
                        *pExtChar = 0xc6;
                        (*p) = 0xb2;
                        break;
                    case 0x8c: // LATIN SMALL LETTER TURNED V (crosses to lead 0xc9)
                        *pExtChar = 0xc9;
                        (*p) = 0x85;
                        break;
                    case 0x92: // LATIN SMALL LETTER EZH (crosses to lead 0xc6)
                        *pExtChar = 0xc6;
                        (*p) = 0xb7;
                        break;
                    default:
                        break;
                    }
                    break;

                case 0xcd:
                    switch (*p) {
                    case 0x85: // COMBINING GREEK YPOGEGRAMMENI (crosses to lead 0xce)
                        *pExtChar = 0xce;
                        (*p) = 0x99;
                        break;
                    case 0xb1: // GREEK SMALL LETTER HETA
                        (*p) = 0xb0;
                        break;
                    case 0xb3: // GREEK SMALL LETTER ARCHAIC SAMPI
                        (*p) = 0xb2;
                        break;
                    case 0xb7: // GREEK SMALL LETTER PAMPHYLIAN DIGAMMA
                        (*p) = 0xb6;
                        break;
                    case 0xbb: // GREEK SMALL REVERSED LUNATE SIGMA SYMBOL (crosses to lead 0xcf)
                        *pExtChar = 0xcf;
                        (*p) = 0xbd;
                        break;
                    case 0xbc: // GREEK SMALL DOTTED LUNATE SIGMA SYMBOL (crosses to lead 0xcf)
                        *pExtChar = 0xcf;
                        (*p) = 0xbe;
                        break;
                    case 0xbd: // GREEK SMALL REVERSED DOTTED LUNATE SIGMA SYMBOL (crosses to lead 0xcf)
                        *pExtChar = 0xcf;
                        (*p) = 0xbf;
                        break;
                    default:
                        break;
                    }
                    break;

                case 0xce:
                    switch (*p) {
                    case 0xac: // GREEK SMALL LETTER ALPHA WITH TONOS
                        (*p) = 0x86;
                        break;
                    case 0xad: // GREEK SMALL LETTER EPSILON WITH TONOS
                        (*p) = 0x88;
                        break;
                    case 0xae: // GREEK SMALL LETTER ETA WITH TONOS
                        (*p) = 0x89;
                        break;
                    case 0xaf: // GREEK SMALL LETTER IOTA WITH TONOS
                        (*p) = 0x8a;
                        break;
                    case 0xb1: // GREEK SMALL LETTER ALPHA
                        (*p) = 0x91;
                        break;
                    case 0xb2: // GREEK SMALL LETTER BETA
                        (*p) = 0x92;
                        break;
                    case 0xb3: // GREEK SMALL LETTER GAMMA
                        (*p) = 0x93;
                        break;
                    case 0xb4: // GREEK SMALL LETTER DELTA
                        (*p) = 0x94;
                        break;
                    case 0xb5: // GREEK SMALL LETTER EPSILON
                        (*p) = 0x95;
                        break;
                    case 0xb6: // GREEK SMALL LETTER ZETA
                        (*p) = 0x96;
                        break;
                    case 0xb7: // GREEK SMALL LETTER ETA
                        (*p) = 0x97;
                        break;
                    case 0xb8: // GREEK SMALL LETTER THETA
                        (*p) = 0x98;
                        break;
                    case 0xb9: // GREEK SMALL LETTER IOTA
                        (*p) = 0x99;
                        break;
                    case 0xba: // GREEK SMALL LETTER KAPPA
                        (*p) = 0x9a;
                        break;
                    case 0xbb: // GREEK SMALL LETTER LAMDA
                        (*p) = 0x9b;
                        break;
                    case 0xbc: // GREEK SMALL LETTER MU
                        (*p) = 0x9c;
                        break;
                    case 0xbd: // GREEK SMALL LETTER NU
                        (*p) = 0x9d;
                        break;
                    case 0xbe: // GREEK SMALL LETTER XI
                        (*p) = 0x9e;
                        break;
                    case 0xbf: // GREEK SMALL LETTER OMICRON
                        (*p) = 0x9f;
                        break;
                    default:
                        break;
                    }
                    break;

                case 0xcf:
                    switch (*p) {
                    case 0x80: // GREEK SMALL LETTER PI (crosses to lead 0xce)
                        *pExtChar = 0xce;
                        (*p) = 0xa0;
                        break;
                    case 0x81: // GREEK SMALL LETTER RHO (crosses to lead 0xce)
                        *pExtChar = 0xce;
                        (*p) = 0xa1;
                        break;
                    case 0x82: // GREEK SMALL LETTER FINAL SIGMA (crosses to lead 0xce)
                        *pExtChar = 0xce;
                        (*p) = 0xa3;
                        break;
                    case 0x83: // GREEK SMALL LETTER SIGMA (crosses to lead 0xce)
                        *pExtChar = 0xce;
                        (*p) = 0xa3;
                        break;
                    case 0x84: // GREEK SMALL LETTER TAU (crosses to lead 0xce)
                        *pExtChar = 0xce;
                        (*p) = 0xa4;
                        break;
                    case 0x85: // GREEK SMALL LETTER UPSILON (crosses to lead 0xce)
                        *pExtChar = 0xce;
                        (*p) = 0xa5;
                        break;
                    case 0x86: // GREEK SMALL LETTER PHI (crosses to lead 0xce)
                        *pExtChar = 0xce;
                        (*p) = 0xa6;
                        break;
                    case 0x87: // GREEK SMALL LETTER CHI (crosses to lead 0xce)
                        *pExtChar = 0xce;
                        (*p) = 0xa7;
                        break;
                    case 0x88: // GREEK SMALL LETTER PSI (crosses to lead 0xce)
                        *pExtChar = 0xce;
                        (*p) = 0xa8;
                        break;
                    case 0x89: // GREEK SMALL LETTER OMEGA (crosses to lead 0xce)
                        *pExtChar = 0xce;
                        (*p) = 0xa9;
                        break;
                    case 0x8a: // GREEK SMALL LETTER IOTA WITH DIALYTIKA (crosses to lead 0xce)
                        *pExtChar = 0xce;
                        (*p) = 0xaa;
                        break;
                    case 0x8b: // GREEK SMALL LETTER UPSILON WITH DIALYTIKA (crosses to lead 0xce)
                        *pExtChar = 0xce;
                        (*p) = 0xab;
                        break;
                    case 0x8c: // GREEK SMALL LETTER OMICRON WITH TONOS (crosses to lead 0xce)
                        *pExtChar = 0xce;
                        (*p) = 0x8c;
                        break;
                    case 0x8d: // GREEK SMALL LETTER UPSILON WITH TONOS (crosses to lead 0xce)
                        *pExtChar = 0xce;
                        (*p) = 0x8e;
                        break;
                    case 0x8e: // GREEK SMALL LETTER OMEGA WITH TONOS (crosses to lead 0xce)
                        *pExtChar = 0xce;
                        (*p) = 0x8f;
                        break;
                    case 0x90: // GREEK BETA SYMBOL (crosses to lead 0xce)
                        *pExtChar = 0xce;
                        (*p) = 0x92;
                        break;
                    case 0x91: // GREEK THETA SYMBOL (crosses to lead 0xce)
                        *pExtChar = 0xce;
                        (*p) = 0x98;
                        break;
                    case 0x95: // GREEK PHI SYMBOL (crosses to lead 0xce)
                        *pExtChar = 0xce;
                        (*p) = 0xa6;
                        break;
                    case 0x96: // GREEK PI SYMBOL (crosses to lead 0xce)
                        *pExtChar = 0xce;
                        (*p) = 0xa0;
                        break;
                    case 0x97: // GREEK KAI SYMBOL
                        (*p) = 0x8f;
                        break;
                    case 0x99: // GREEK SMALL LETTER ARCHAIC KOPPA
                        (*p) = 0x98;
                        break;
                    case 0x9b: // GREEK SMALL LETTER STIGMA
                        (*p) = 0x9a;
                        break;
                    case 0x9d: // GREEK SMALL LETTER DIGAMMA
                        (*p) = 0x9c;
                        break;
                    case 0x9f: // GREEK SMALL LETTER KOPPA
                        (*p) = 0x9e;
                        break;
                    case 0xa1: // GREEK SMALL LETTER SAMPI
                        (*p) = 0xa0;
                        break;
                    case 0xa3: // COPTIC SMALL LETTER SHEI
                        (*p) = 0xa2;
                        break;
                    case 0xa5: // COPTIC SMALL LETTER FEI
                        (*p) = 0xa4;
                        break;
                    case 0xa7: // COPTIC SMALL LETTER KHEI
                        (*p) = 0xa6;
                        break;
                    case 0xa9: // COPTIC SMALL LETTER HORI
                        (*p) = 0xa8;
                        break;
                    case 0xab: // COPTIC SMALL LETTER GANGIA
                        (*p) = 0xaa;
                        break;
                    case 0xad: // COPTIC SMALL LETTER SHIMA
                        (*p) = 0xac;
                        break;
                    case 0xaf: // COPTIC SMALL LETTER DEI
                        (*p) = 0xae;
                        break;
                    case 0xb0: // GREEK KAPPA SYMBOL (crosses to lead 0xce)
                        *pExtChar = 0xce;
                        (*p) = 0x9a;
                        break;
                    case 0xb1: // GREEK RHO SYMBOL (crosses to lead 0xce)
                        *pExtChar = 0xce;
                        (*p) = 0xa1;
                        break;
                    case 0xb2: // GREEK LUNATE SIGMA SYMBOL
                        (*p) = 0xb9;
                        break;
                    case 0xb3: // GREEK LETTER YOT (crosses to lead 0xcd)
                        *pExtChar = 0xcd;
                        (*p) = 0xbf;
                        break;
                    case 0xb5: // GREEK LUNATE EPSILON SYMBOL (crosses to lead 0xce)
                        *pExtChar = 0xce;
                        (*p) = 0x95;
                        break;
                    case 0xb8: // GREEK SMALL LETTER SHO
                        (*p) = 0xb7;
                        break;
                    case 0xbb: // GREEK SMALL LETTER SAN
                        (*p) = 0xba;
                        break;
                    default:
                        break;
                    }
                    break;

                case 0xd0:
                    switch (*p) {
                    case 0xb0: // CYRILLIC SMALL LETTER A
                        (*p) = 0x90;
                        break;
                    case 0xb1: // CYRILLIC SMALL LETTER BE
                        (*p) = 0x91;
                        break;
                    case 0xb2: // CYRILLIC SMALL LETTER VE
                        (*p) = 0x92;
                        break;
                    case 0xb3: // CYRILLIC SMALL LETTER GHE
                        (*p) = 0x93;
                        break;
                    case 0xb4: // CYRILLIC SMALL LETTER DE
                        (*p) = 0x94;
                        break;
                    case 0xb5: // CYRILLIC SMALL LETTER IE
                        (*p) = 0x95;
                        break;
                    case 0xb6: // CYRILLIC SMALL LETTER ZHE
                        (*p) = 0x96;
                        break;
                    case 0xb7: // CYRILLIC SMALL LETTER ZE
                        (*p) = 0x97;
                        break;
                    case 0xb8: // CYRILLIC SMALL LETTER I
                        (*p) = 0x98;
                        break;
                    case 0xb9: // CYRILLIC SMALL LETTER SHORT I
                        (*p) = 0x99;
                        break;
                    case 0xba: // CYRILLIC SMALL LETTER KA
                        (*p) = 0x9a;
                        break;
                    case 0xbb: // CYRILLIC SMALL LETTER EL
                        (*p) = 0x9b;
                        break;
                    case 0xbc: // CYRILLIC SMALL LETTER EM
                        (*p) = 0x9c;
                        break;
                    case 0xbd: // CYRILLIC SMALL LETTER EN
                        (*p) = 0x9d;
                        break;
                    case 0xbe: // CYRILLIC SMALL LETTER O
                        (*p) = 0x9e;
                        break;
                    case 0xbf: // CYRILLIC SMALL LETTER PE
                        (*p) = 0x9f;
                        break;
                    default:
                        break;
                    }
                    break;

                case 0xd1:
                    switch (*p) {
                    case 0x80: // CYRILLIC SMALL LETTER ER (crosses to lead 0xd0)
                        *pExtChar = 0xd0;
                        (*p) = 0xa0;
                        break;
                    case 0x81: // CYRILLIC SMALL LETTER ES (crosses to lead 0xd0)
                        *pExtChar = 0xd0;
                        (*p) = 0xa1;
                        break;
                    case 0x82: // CYRILLIC SMALL LETTER TE (crosses to lead 0xd0)
                        *pExtChar = 0xd0;
                        (*p) = 0xa2;
                        break;
                    case 0x83: // CYRILLIC SMALL LETTER U (crosses to lead 0xd0)
                        *pExtChar = 0xd0;
                        (*p) = 0xa3;
                        break;
                    case 0x84: // CYRILLIC SMALL LETTER EF (crosses to lead 0xd0)
                        *pExtChar = 0xd0;
                        (*p) = 0xa4;
                        break;
                    case 0x85: // CYRILLIC SMALL LETTER HA (crosses to lead 0xd0)
                        *pExtChar = 0xd0;
                        (*p) = 0xa5;
                        break;
                    case 0x86: // CYRILLIC SMALL LETTER TSE (crosses to lead 0xd0)
                        *pExtChar = 0xd0;
                        (*p) = 0xa6;
                        break;
                    case 0x87: // CYRILLIC SMALL LETTER CHE (crosses to lead 0xd0)
                        *pExtChar = 0xd0;
                        (*p) = 0xa7;
                        break;
                    case 0x88: // CYRILLIC SMALL LETTER SHA (crosses to lead 0xd0)
                        *pExtChar = 0xd0;
                        (*p) = 0xa8;
                        break;
                    case 0x89: // CYRILLIC SMALL LETTER SHCHA (crosses to lead 0xd0)
                        *pExtChar = 0xd0;
                        (*p) = 0xa9;
                        break;
                    case 0x8a: // CYRILLIC SMALL LETTER HARD SIGN (crosses to lead 0xd0)
                        *pExtChar = 0xd0;
                        (*p) = 0xaa;
                        break;
                    case 0x8b: // CYRILLIC SMALL LETTER YERU (crosses to lead 0xd0)
                        *pExtChar = 0xd0;
                        (*p) = 0xab;
                        break;
                    case 0x8c: // CYRILLIC SMALL LETTER SOFT SIGN (crosses to lead 0xd0)
                        *pExtChar = 0xd0;
                        (*p) = 0xac;
                        break;
                    case 0x8d: // CYRILLIC SMALL LETTER E (crosses to lead 0xd0)
                        *pExtChar = 0xd0;
                        (*p) = 0xad;
                        break;
                    case 0x8e: // CYRILLIC SMALL LETTER YU (crosses to lead 0xd0)
                        *pExtChar = 0xd0;
                        (*p) = 0xae;
                        break;
                    case 0x8f: // CYRILLIC SMALL LETTER YA (crosses to lead 0xd0)
                        *pExtChar = 0xd0;
                        (*p) = 0xaf;
                        break;
                    case 0x90: // CYRILLIC SMALL LETTER IE WITH GRAVE (crosses to lead 0xd0)
                        *pExtChar = 0xd0;
                        (*p) = 0x80;
                        break;
                    case 0x91: // CYRILLIC SMALL LETTER IO (crosses to lead 0xd0)
                        *pExtChar = 0xd0;
                        (*p) = 0x81;
                        break;
                    case 0x92: // CYRILLIC SMALL LETTER DJE (crosses to lead 0xd0)
                        *pExtChar = 0xd0;
                        (*p) = 0x82;
                        break;
                    case 0x93: // CYRILLIC SMALL LETTER GJE (crosses to lead 0xd0)
                        *pExtChar = 0xd0;
                        (*p) = 0x83;
                        break;
                    case 0x94: // CYRILLIC SMALL LETTER UKRAINIAN IE (crosses to lead 0xd0)
                        *pExtChar = 0xd0;
                        (*p) = 0x84;
                        break;
                    case 0x95: // CYRILLIC SMALL LETTER DZE (crosses to lead 0xd0)
                        *pExtChar = 0xd0;
                        (*p) = 0x85;
                        break;
                    case 0x96: // CYRILLIC SMALL LETTER BYELORUSSIAN-UKRAINIAN I (crosses to lead 0xd0)
                        *pExtChar = 0xd0;
                        (*p) = 0x86;
                        break;
                    case 0x97: // CYRILLIC SMALL LETTER YI (crosses to lead 0xd0)
                        *pExtChar = 0xd0;
                        (*p) = 0x87;
                        break;
                    case 0x98: // CYRILLIC SMALL LETTER JE (crosses to lead 0xd0)
                        *pExtChar = 0xd0;
                        (*p) = 0x88;
                        break;
                    case 0x99: // CYRILLIC SMALL LETTER LJE (crosses to lead 0xd0)
                        *pExtChar = 0xd0;
                        (*p) = 0x89;
                        break;
                    case 0x9a: // CYRILLIC SMALL LETTER NJE (crosses to lead 0xd0)
                        *pExtChar = 0xd0;
                        (*p) = 0x8a;
                        break;
                    case 0x9b: // CYRILLIC SMALL LETTER TSHE (crosses to lead 0xd0)
                        *pExtChar = 0xd0;
                        (*p) = 0x8b;
                        break;
                    case 0x9c: // CYRILLIC SMALL LETTER KJE (crosses to lead 0xd0)
                        *pExtChar = 0xd0;
                        (*p) = 0x8c;
                        break;
                    case 0x9d: // CYRILLIC SMALL LETTER I WITH GRAVE (crosses to lead 0xd0)
                        *pExtChar = 0xd0;
                        (*p) = 0x8d;
                        break;
                    case 0x9e: // CYRILLIC SMALL LETTER SHORT U (crosses to lead 0xd0)
                        *pExtChar = 0xd0;
                        (*p) = 0x8e;
                        break;
                    case 0x9f: // CYRILLIC SMALL LETTER DZHE (crosses to lead 0xd0)
                        *pExtChar = 0xd0;
                        (*p) = 0x8f;
                        break;
                    case 0xa1: // CYRILLIC SMALL LETTER OMEGA
                        (*p) = 0xa0;
                        break;
                    case 0xa3: // CYRILLIC SMALL LETTER YAT
                        (*p) = 0xa2;
                        break;
                    case 0xa5: // CYRILLIC SMALL LETTER IOTIFIED E
                        (*p) = 0xa4;
                        break;
                    case 0xa7: // CYRILLIC SMALL LETTER LITTLE YUS
                        (*p) = 0xa6;
                        break;
                    case 0xa9: // CYRILLIC SMALL LETTER IOTIFIED LITTLE YUS
                        (*p) = 0xa8;
                        break;
                    case 0xab: // CYRILLIC SMALL LETTER BIG YUS
                        (*p) = 0xaa;
                        break;
                    case 0xad: // CYRILLIC SMALL LETTER IOTIFIED BIG YUS
                        (*p) = 0xac;
                        break;
                    case 0xaf: // CYRILLIC SMALL LETTER KSI
                        (*p) = 0xae;
                        break;
                    case 0xb1: // CYRILLIC SMALL LETTER PSI
                        (*p) = 0xb0;
                        break;
                    case 0xb3: // CYRILLIC SMALL LETTER FITA
                        (*p) = 0xb2;
                        break;
                    case 0xb5: // CYRILLIC SMALL LETTER IZHITSA
                        (*p) = 0xb4;
                        break;
                    case 0xb7: // CYRILLIC SMALL LETTER IZHITSA WITH DOUBLE GRAVE ACCENT
                        (*p) = 0xb6;
                        break;
                    case 0xb9: // CYRILLIC SMALL LETTER UK
                        (*p) = 0xb8;
                        break;
                    case 0xbb: // CYRILLIC SMALL LETTER ROUND OMEGA
                        (*p) = 0xba;
                        break;
                    case 0xbd: // CYRILLIC SMALL LETTER OMEGA WITH TITLO
                        (*p) = 0xbc;
                        break;
                    case 0xbf: // CYRILLIC SMALL LETTER OT
                        (*p) = 0xbe;
                        break;
                    default:
                        break;
                    }
                    break;

                case 0xd2:
                    switch (*p) {
                    case 0x81: // CYRILLIC SMALL LETTER KOPPA
                        (*p) = 0x80;
                        break;
                    case 0x8b: // CYRILLIC SMALL LETTER SHORT I WITH TAIL
                        (*p) = 0x8a;
                        break;
                    case 0x8d: // CYRILLIC SMALL LETTER SEMISOFT SIGN
                        (*p) = 0x8c;
                        break;
                    case 0x8f: // CYRILLIC SMALL LETTER ER WITH TICK
                        (*p) = 0x8e;
                        break;
                    case 0x91: // CYRILLIC SMALL LETTER GHE WITH UPTURN
                        (*p) = 0x90;
                        break;
                    case 0x93: // CYRILLIC SMALL LETTER GHE WITH STROKE
                        (*p) = 0x92;
                        break;
                    case 0x95: // CYRILLIC SMALL LETTER GHE WITH MIDDLE HOOK
                        (*p) = 0x94;
                        break;
                    case 0x97: // CYRILLIC SMALL LETTER ZHE WITH DESCENDER
                        (*p) = 0x96;
                        break;
                    case 0x99: // CYRILLIC SMALL LETTER ZE WITH DESCENDER
                        (*p) = 0x98;
                        break;
                    case 0x9b: // CYRILLIC SMALL LETTER KA WITH DESCENDER
                        (*p) = 0x9a;
                        break;
                    case 0x9d: // CYRILLIC SMALL LETTER KA WITH VERTICAL STROKE
                        (*p) = 0x9c;
                        break;
                    case 0x9f: // CYRILLIC SMALL LETTER KA WITH STROKE
                        (*p) = 0x9e;
                        break;
                    case 0xa1: // CYRILLIC SMALL LETTER BASHKIR KA
                        (*p) = 0xa0;
                        break;
                    case 0xa3: // CYRILLIC SMALL LETTER EN WITH DESCENDER
                        (*p) = 0xa2;
                        break;
                    case 0xa5: // CYRILLIC SMALL LIGATURE EN GHE
                        (*p) = 0xa4;
                        break;
                    case 0xa7: // CYRILLIC SMALL LETTER PE WITH MIDDLE HOOK
                        (*p) = 0xa6;
                        break;
                    case 0xa9: // CYRILLIC SMALL LETTER ABKHASIAN HA
                        (*p) = 0xa8;
                        break;
                    case 0xab: // CYRILLIC SMALL LETTER ES WITH DESCENDER
                        (*p) = 0xaa;
                        break;
                    case 0xad: // CYRILLIC SMALL LETTER TE WITH DESCENDER
                        (*p) = 0xac;
                        break;
                    case 0xaf: // CYRILLIC SMALL LETTER STRAIGHT U
                        (*p) = 0xae;
                        break;
                    case 0xb1: // CYRILLIC SMALL LETTER STRAIGHT U WITH STROKE
                        (*p) = 0xb0;
                        break;
                    case 0xb3: // CYRILLIC SMALL LETTER HA WITH DESCENDER
                        (*p) = 0xb2;
                        break;
                    case 0xb5: // CYRILLIC SMALL LIGATURE TE TSE
                        (*p) = 0xb4;
                        break;
                    case 0xb7: // CYRILLIC SMALL LETTER CHE WITH DESCENDER
                        (*p) = 0xb6;
                        break;
                    case 0xb9: // CYRILLIC SMALL LETTER CHE WITH VERTICAL STROKE
                        (*p) = 0xb8;
                        break;
                    case 0xbb: // CYRILLIC SMALL LETTER SHHA
                        (*p) = 0xba;
                        break;
                    case 0xbd: // CYRILLIC SMALL LETTER ABKHASIAN CHE
                        (*p) = 0xbc;
                        break;
                    case 0xbf: // CYRILLIC SMALL LETTER ABKHASIAN CHE WITH DESCENDER
                        (*p) = 0xbe;
                        break;
                    default:
                        break;
                    }
                    break;

                case 0xd3:
                    switch (*p) {
                    case 0x82: // CYRILLIC SMALL LETTER ZHE WITH BREVE
                        (*p) = 0x81;
                        break;
                    case 0x84: // CYRILLIC SMALL LETTER KA WITH HOOK
                        (*p) = 0x83;
                        break;
                    case 0x86: // CYRILLIC SMALL LETTER EL WITH TAIL
                        (*p) = 0x85;
                        break;
                    case 0x88: // CYRILLIC SMALL LETTER EN WITH HOOK
                        (*p) = 0x87;
                        break;
                    case 0x8a: // CYRILLIC SMALL LETTER EN WITH TAIL
                        (*p) = 0x89;
                        break;
                    case 0x8c: // CYRILLIC SMALL LETTER KHAKASSIAN CHE
                        (*p) = 0x8b;
                        break;
                    case 0x8e: // CYRILLIC SMALL LETTER EM WITH TAIL
                        (*p) = 0x8d;
                        break;
                    case 0x8f: // CYRILLIC SMALL LETTER PALOCHKA
                        (*p) = 0x80;
                        break;
                    case 0x91: // CYRILLIC SMALL LETTER A WITH BREVE
                        (*p) = 0x90;
                        break;
                    case 0x93: // CYRILLIC SMALL LETTER A WITH DIAERESIS
                        (*p) = 0x92;
                        break;
                    case 0x95: // CYRILLIC SMALL LIGATURE A IE
                        (*p) = 0x94;
                        break;
                    case 0x97: // CYRILLIC SMALL LETTER IE WITH BREVE
                        (*p) = 0x96;
                        break;
                    case 0x99: // CYRILLIC SMALL LETTER SCHWA
                        (*p) = 0x98;
                        break;
                    case 0x9b: // CYRILLIC SMALL LETTER SCHWA WITH DIAERESIS
                        (*p) = 0x9a;
                        break;
                    case 0x9d: // CYRILLIC SMALL LETTER ZHE WITH DIAERESIS
                        (*p) = 0x9c;
                        break;
                    case 0x9f: // CYRILLIC SMALL LETTER ZE WITH DIAERESIS
                        (*p) = 0x9e;
                        break;
                    case 0xa1: // CYRILLIC SMALL LETTER ABKHASIAN DZE
                        (*p) = 0xa0;
                        break;
                    case 0xa3: // CYRILLIC SMALL LETTER I WITH MACRON
                        (*p) = 0xa2;
                        break;
                    case 0xa5: // CYRILLIC SMALL LETTER I WITH DIAERESIS
                        (*p) = 0xa4;
                        break;
                    case 0xa7: // CYRILLIC SMALL LETTER O WITH DIAERESIS
                        (*p) = 0xa6;
                        break;
                    case 0xa9: // CYRILLIC SMALL LETTER BARRED O
                        (*p) = 0xa8;
                        break;
                    case 0xab: // CYRILLIC SMALL LETTER BARRED O WITH DIAERESIS
                        (*p) = 0xaa;
                        break;
                    case 0xad: // CYRILLIC SMALL LETTER E WITH DIAERESIS
                        (*p) = 0xac;
                        break;
                    case 0xaf: // CYRILLIC SMALL LETTER U WITH MACRON
                        (*p) = 0xae;
                        break;
                    case 0xb1: // CYRILLIC SMALL LETTER U WITH DIAERESIS
                        (*p) = 0xb0;
                        break;
                    case 0xb3: // CYRILLIC SMALL LETTER U WITH DOUBLE ACUTE
                        (*p) = 0xb2;
                        break;
                    case 0xb5: // CYRILLIC SMALL LETTER CHE WITH DIAERESIS
                        (*p) = 0xb4;
                        break;
                    case 0xb7: // CYRILLIC SMALL LETTER GHE WITH DESCENDER
                        (*p) = 0xb6;
                        break;
                    case 0xb9: // CYRILLIC SMALL LETTER YERU WITH DIAERESIS
                        (*p) = 0xb8;
                        break;
                    case 0xbb: // CYRILLIC SMALL LETTER GHE WITH STROKE AND HOOK
                        (*p) = 0xba;
                        break;
                    case 0xbd: // CYRILLIC SMALL LETTER HA WITH HOOK
                        (*p) = 0xbc;
                        break;
                    case 0xbf: // CYRILLIC SMALL LETTER HA WITH STROKE
                        (*p) = 0xbe;
                        break;
                    default:
                        break;
                    }
                    break;

                case 0xd4:
                    switch (*p) {
                    case 0x81: // CYRILLIC SMALL LETTER KOMI DE
                        (*p) = 0x80;
                        break;
                    case 0x83: // CYRILLIC SMALL LETTER KOMI DJE
                        (*p) = 0x82;
                        break;
                    case 0x85: // CYRILLIC SMALL LETTER KOMI ZJE
                        (*p) = 0x84;
                        break;
                    case 0x87: // CYRILLIC SMALL LETTER KOMI DZJE
                        (*p) = 0x86;
                        break;
                    case 0x89: // CYRILLIC SMALL LETTER KOMI LJE
                        (*p) = 0x88;
                        break;
                    case 0x8b: // CYRILLIC SMALL LETTER KOMI NJE
                        (*p) = 0x8a;
                        break;
                    case 0x8d: // CYRILLIC SMALL LETTER KOMI SJE
                        (*p) = 0x8c;
                        break;
                    case 0x8f: // CYRILLIC SMALL LETTER KOMI TJE
                        (*p) = 0x8e;
                        break;
                    case 0x91: // CYRILLIC SMALL LETTER REVERSED ZE
                        (*p) = 0x90;
                        break;
                    case 0x93: // CYRILLIC SMALL LETTER EL WITH HOOK
                        (*p) = 0x92;
                        break;
                    case 0x95: // CYRILLIC SMALL LETTER LHA
                        (*p) = 0x94;
                        break;
                    case 0x97: // CYRILLIC SMALL LETTER RHA
                        (*p) = 0x96;
                        break;
                    case 0x99: // CYRILLIC SMALL LETTER YAE
                        (*p) = 0x98;
                        break;
                    case 0x9b: // CYRILLIC SMALL LETTER QA
                        (*p) = 0x9a;
                        break;
                    case 0x9d: // CYRILLIC SMALL LETTER WE
                        (*p) = 0x9c;
                        break;
                    case 0x9f: // CYRILLIC SMALL LETTER ALEUT KA
                        (*p) = 0x9e;
                        break;
                    case 0xa1: // CYRILLIC SMALL LETTER EL WITH MIDDLE HOOK
                        (*p) = 0xa0;
                        break;
                    case 0xa3: // CYRILLIC SMALL LETTER EN WITH MIDDLE HOOK
                        (*p) = 0xa2;
                        break;
                    case 0xa5: // CYRILLIC SMALL LETTER PE WITH DESCENDER
                        (*p) = 0xa4;
                        break;
                    case 0xa7: // CYRILLIC SMALL LETTER SHHA WITH DESCENDER
                        (*p) = 0xa6;
                        break;
                    case 0xa9: // CYRILLIC SMALL LETTER EN WITH LEFT HOOK
                        (*p) = 0xa8;
                        break;
                    case 0xab: // CYRILLIC SMALL LETTER DZZHE
                        (*p) = 0xaa;
                        break;
                    case 0xad: // CYRILLIC SMALL LETTER DCHE
                        (*p) = 0xac;
                        break;
                    case 0xaf: // CYRILLIC SMALL LETTER EL WITH DESCENDER
                        (*p) = 0xae;
                        break;
                    default:
                        break;
                    }
                    break;

                case 0xd5:
                    switch (*p) {
                    case 0xa1: // ARMENIAN SMALL LETTER AYB (crosses to lead 0xd4)
                        *pExtChar = 0xd4;
                        (*p) = 0xb1;
                        break;
                    case 0xa2: // ARMENIAN SMALL LETTER BEN (crosses to lead 0xd4)
                        *pExtChar = 0xd4;
                        (*p) = 0xb2;
                        break;
                    case 0xa3: // ARMENIAN SMALL LETTER GIM (crosses to lead 0xd4)
                        *pExtChar = 0xd4;
                        (*p) = 0xb3;
                        break;
                    case 0xa4: // ARMENIAN SMALL LETTER DA (crosses to lead 0xd4)
                        *pExtChar = 0xd4;
                        (*p) = 0xb4;
                        break;
                    case 0xa5: // ARMENIAN SMALL LETTER ECH (crosses to lead 0xd4)
                        *pExtChar = 0xd4;
                        (*p) = 0xb5;
                        break;
                    case 0xa6: // ARMENIAN SMALL LETTER ZA (crosses to lead 0xd4)
                        *pExtChar = 0xd4;
                        (*p) = 0xb6;
                        break;
                    case 0xa7: // ARMENIAN SMALL LETTER EH (crosses to lead 0xd4)
                        *pExtChar = 0xd4;
                        (*p) = 0xb7;
                        break;
                    case 0xa8: // ARMENIAN SMALL LETTER ET (crosses to lead 0xd4)
                        *pExtChar = 0xd4;
                        (*p) = 0xb8;
                        break;
                    case 0xa9: // ARMENIAN SMALL LETTER TO (crosses to lead 0xd4)
                        *pExtChar = 0xd4;
                        (*p) = 0xb9;
                        break;
                    case 0xaa: // ARMENIAN SMALL LETTER ZHE (crosses to lead 0xd4)
                        *pExtChar = 0xd4;
                        (*p) = 0xba;
                        break;
                    case 0xab: // ARMENIAN SMALL LETTER INI (crosses to lead 0xd4)
                        *pExtChar = 0xd4;
                        (*p) = 0xbb;
                        break;
                    case 0xac: // ARMENIAN SMALL LETTER LIWN (crosses to lead 0xd4)
                        *pExtChar = 0xd4;
                        (*p) = 0xbc;
                        break;
                    case 0xad: // ARMENIAN SMALL LETTER XEH (crosses to lead 0xd4)
                        *pExtChar = 0xd4;
                        (*p) = 0xbd;
                        break;
                    case 0xae: // ARMENIAN SMALL LETTER CA (crosses to lead 0xd4)
                        *pExtChar = 0xd4;
                        (*p) = 0xbe;
                        break;
                    case 0xaf: // ARMENIAN SMALL LETTER KEN (crosses to lead 0xd4)
                        *pExtChar = 0xd4;
                        (*p) = 0xbf;
                        break;
                    case 0xb0: // ARMENIAN SMALL LETTER HO
                        (*p) = 0x80;
                        break;
                    case 0xb1: // ARMENIAN SMALL LETTER JA
                        (*p) = 0x81;
                        break;
                    case 0xb2: // ARMENIAN SMALL LETTER GHAD
                        (*p) = 0x82;
                        break;
                    case 0xb3: // ARMENIAN SMALL LETTER CHEH
                        (*p) = 0x83;
                        break;
                    case 0xb4: // ARMENIAN SMALL LETTER MEN
                        (*p) = 0x84;
                        break;
                    case 0xb5: // ARMENIAN SMALL LETTER YI
                        (*p) = 0x85;
                        break;
                    case 0xb6: // ARMENIAN SMALL LETTER NOW
                        (*p) = 0x86;
                        break;
                    case 0xb7: // ARMENIAN SMALL LETTER SHA
                        (*p) = 0x87;
                        break;
                    case 0xb8: // ARMENIAN SMALL LETTER VO
                        (*p) = 0x88;
                        break;
                    case 0xb9: // ARMENIAN SMALL LETTER CHA
                        (*p) = 0x89;
                        break;
                    case 0xba: // ARMENIAN SMALL LETTER PEH
                        (*p) = 0x8a;
                        break;
                    case 0xbb: // ARMENIAN SMALL LETTER JHEH
                        (*p) = 0x8b;
                        break;
                    case 0xbc: // ARMENIAN SMALL LETTER RA
                        (*p) = 0x8c;
                        break;
                    case 0xbd: // ARMENIAN SMALL LETTER SEH
                        (*p) = 0x8d;
                        break;
                    case 0xbe: // ARMENIAN SMALL LETTER VEW
                        (*p) = 0x8e;
                        break;
                    case 0xbf: // ARMENIAN SMALL LETTER TIWN
                        (*p) = 0x8f;
                        break;
                    default:
                        break;
                    }
                    break;

                case 0xd6:
                    switch (*p) {
                    case 0x80: // ARMENIAN SMALL LETTER REH (crosses to lead 0xd5)
                        *pExtChar = 0xd5;
                        (*p) = 0x90;
                        break;
                    case 0x81: // ARMENIAN SMALL LETTER CO (crosses to lead 0xd5)
                        *pExtChar = 0xd5;
                        (*p) = 0x91;
                        break;
                    case 0x82: // ARMENIAN SMALL LETTER YIWN (crosses to lead 0xd5)
                        *pExtChar = 0xd5;
                        (*p) = 0x92;
                        break;
                    case 0x83: // ARMENIAN SMALL LETTER PIWR (crosses to lead 0xd5)
                        *pExtChar = 0xd5;
                        (*p) = 0x93;
                        break;
                    case 0x84: // ARMENIAN SMALL LETTER KEH (crosses to lead 0xd5)
                        *pExtChar = 0xd5;
                        (*p) = 0x94;
                        break;
                    case 0x85: // ARMENIAN SMALL LETTER OH (crosses to lead 0xd5)
                        *pExtChar = 0xd5;
                        (*p) = 0x95;
                        break;
                    case 0x86: // ARMENIAN SMALL LETTER FEH (crosses to lead 0xd5)
                        *pExtChar = 0xd5;
                        (*p) = 0x96;
                        break;
                    default:
                        break;
                    }
                    break;
            case 0xe1: // Three byte code -- Georgian, Latin Extended Additional, Greek Extended
                pExtChar = p;
                p++;
                if (!end || p < end) { // else: sequence truncated at buffer end -- leave untouched
                switch (*pExtChar) {
                    case 0x83: // Georgian Mkhedruli
                        switch (*p) {
                        case 0x90: // GEORGIAN LETTER AN (crosses to byte2 0xb2)
                            *pExtChar = 0xb2;
                            (*p) = 0x90;
                            break;
                        case 0x91: // GEORGIAN LETTER BAN (crosses to byte2 0xb2)
                            *pExtChar = 0xb2;
                            (*p) = 0x91;
                            break;
                        case 0x92: // GEORGIAN LETTER GAN (crosses to byte2 0xb2)
                            *pExtChar = 0xb2;
                            (*p) = 0x92;
                            break;
                        case 0x93: // GEORGIAN LETTER DON (crosses to byte2 0xb2)
                            *pExtChar = 0xb2;
                            (*p) = 0x93;
                            break;
                        case 0x94: // GEORGIAN LETTER EN (crosses to byte2 0xb2)
                            *pExtChar = 0xb2;
                            (*p) = 0x94;
                            break;
                        case 0x95: // GEORGIAN LETTER VIN (crosses to byte2 0xb2)
                            *pExtChar = 0xb2;
                            (*p) = 0x95;
                            break;
                        case 0x96: // GEORGIAN LETTER ZEN (crosses to byte2 0xb2)
                            *pExtChar = 0xb2;
                            (*p) = 0x96;
                            break;
                        case 0x97: // GEORGIAN LETTER TAN (crosses to byte2 0xb2)
                            *pExtChar = 0xb2;
                            (*p) = 0x97;
                            break;
                        case 0x98: // GEORGIAN LETTER IN (crosses to byte2 0xb2)
                            *pExtChar = 0xb2;
                            (*p) = 0x98;
                            break;
                        case 0x99: // GEORGIAN LETTER KAN (crosses to byte2 0xb2)
                            *pExtChar = 0xb2;
                            (*p) = 0x99;
                            break;
                        case 0x9a: // GEORGIAN LETTER LAS (crosses to byte2 0xb2)
                            *pExtChar = 0xb2;
                            (*p) = 0x9a;
                            break;
                        case 0x9b: // GEORGIAN LETTER MAN (crosses to byte2 0xb2)
                            *pExtChar = 0xb2;
                            (*p) = 0x9b;
                            break;
                        case 0x9c: // GEORGIAN LETTER NAR (crosses to byte2 0xb2)
                            *pExtChar = 0xb2;
                            (*p) = 0x9c;
                            break;
                        case 0x9d: // GEORGIAN LETTER ON (crosses to byte2 0xb2)
                            *pExtChar = 0xb2;
                            (*p) = 0x9d;
                            break;
                        case 0x9e: // GEORGIAN LETTER PAR (crosses to byte2 0xb2)
                            *pExtChar = 0xb2;
                            (*p) = 0x9e;
                            break;
                        case 0x9f: // GEORGIAN LETTER ZHAR (crosses to byte2 0xb2)
                            *pExtChar = 0xb2;
                            (*p) = 0x9f;
                            break;
                        case 0xa0: // GEORGIAN LETTER RAE (crosses to byte2 0xb2)
                            *pExtChar = 0xb2;
                            (*p) = 0xa0;
                            break;
                        case 0xa1: // GEORGIAN LETTER SAN (crosses to byte2 0xb2)
                            *pExtChar = 0xb2;
                            (*p) = 0xa1;
                            break;
                        case 0xa2: // GEORGIAN LETTER TAR (crosses to byte2 0xb2)
                            *pExtChar = 0xb2;
                            (*p) = 0xa2;
                            break;
                        case 0xa3: // GEORGIAN LETTER UN (crosses to byte2 0xb2)
                            *pExtChar = 0xb2;
                            (*p) = 0xa3;
                            break;
                        case 0xa4: // GEORGIAN LETTER PHAR (crosses to byte2 0xb2)
                            *pExtChar = 0xb2;
                            (*p) = 0xa4;
                            break;
                        case 0xa5: // GEORGIAN LETTER KHAR (crosses to byte2 0xb2)
                            *pExtChar = 0xb2;
                            (*p) = 0xa5;
                            break;
                        case 0xa6: // GEORGIAN LETTER GHAN (crosses to byte2 0xb2)
                            *pExtChar = 0xb2;
                            (*p) = 0xa6;
                            break;
                        case 0xa7: // GEORGIAN LETTER QAR (crosses to byte2 0xb2)
                            *pExtChar = 0xb2;
                            (*p) = 0xa7;
                            break;
                        case 0xa8: // GEORGIAN LETTER SHIN (crosses to byte2 0xb2)
                            *pExtChar = 0xb2;
                            (*p) = 0xa8;
                            break;
                        case 0xa9: // GEORGIAN LETTER CHIN (crosses to byte2 0xb2)
                            *pExtChar = 0xb2;
                            (*p) = 0xa9;
                            break;
                        case 0xaa: // GEORGIAN LETTER CAN (crosses to byte2 0xb2)
                            *pExtChar = 0xb2;
                            (*p) = 0xaa;
                            break;
                        case 0xab: // GEORGIAN LETTER JIL (crosses to byte2 0xb2)
                            *pExtChar = 0xb2;
                            (*p) = 0xab;
                            break;
                        case 0xac: // GEORGIAN LETTER CIL (crosses to byte2 0xb2)
                            *pExtChar = 0xb2;
                            (*p) = 0xac;
                            break;
                        case 0xad: // GEORGIAN LETTER CHAR (crosses to byte2 0xb2)
                            *pExtChar = 0xb2;
                            (*p) = 0xad;
                            break;
                        case 0xae: // GEORGIAN LETTER XAN (crosses to byte2 0xb2)
                            *pExtChar = 0xb2;
                            (*p) = 0xae;
                            break;
                        case 0xaf: // GEORGIAN LETTER JHAN (crosses to byte2 0xb2)
                            *pExtChar = 0xb2;
                            (*p) = 0xaf;
                            break;
                        case 0xb0: // GEORGIAN LETTER HAE (crosses to byte2 0xb2)
                            *pExtChar = 0xb2;
                            (*p) = 0xb0;
                            break;
                        case 0xb1: // GEORGIAN LETTER HE (crosses to byte2 0xb2)
                            *pExtChar = 0xb2;
                            (*p) = 0xb1;
                            break;
                        case 0xb2: // GEORGIAN LETTER HIE (crosses to byte2 0xb2)
                            *pExtChar = 0xb2;
                            (*p) = 0xb2;
                            break;
                        case 0xb3: // GEORGIAN LETTER WE (crosses to byte2 0xb2)
                            *pExtChar = 0xb2;
                            (*p) = 0xb3;
                            break;
                        case 0xb4: // GEORGIAN LETTER HAR (crosses to byte2 0xb2)
                            *pExtChar = 0xb2;
                            (*p) = 0xb4;
                            break;
                        case 0xb5: // GEORGIAN LETTER HOE (crosses to byte2 0xb2)
                            *pExtChar = 0xb2;
                            (*p) = 0xb5;
                            break;
                        case 0xb6: // GEORGIAN LETTER FI (crosses to byte2 0xb2)
                            *pExtChar = 0xb2;
                            (*p) = 0xb6;
                            break;
                        case 0xb7: // GEORGIAN LETTER YN (crosses to byte2 0xb2)
                            *pExtChar = 0xb2;
                            (*p) = 0xb7;
                            break;
                        case 0xb8: // GEORGIAN LETTER ELIFI (crosses to byte2 0xb2)
                            *pExtChar = 0xb2;
                            (*p) = 0xb8;
                            break;
                        case 0xb9: // GEORGIAN LETTER TURNED GAN (crosses to byte2 0xb2)
                            *pExtChar = 0xb2;
                            (*p) = 0xb9;
                            break;
                        case 0xba: // GEORGIAN LETTER AIN (crosses to byte2 0xb2)
                            *pExtChar = 0xb2;
                            (*p) = 0xba;
                            break;
                        case 0xbd: // GEORGIAN LETTER AEN (crosses to byte2 0xb2)
                            *pExtChar = 0xb2;
                            (*p) = 0xbd;
                            break;
                        case 0xbe: // GEORGIAN LETTER HARD SIGN (crosses to byte2 0xb2)
                            *pExtChar = 0xb2;
                            (*p) = 0xbe;
                            break;
                        case 0xbf: // GEORGIAN LETTER LABIAL SIGN (crosses to byte2 0xb2)
                            *pExtChar = 0xb2;
                            (*p) = 0xbf;
                            break;
                        default:
                            break;
                        }
                        break;

                    case 0xb8: // Latin Extended Additional
                        switch (*p) {
                        case 0x81: // LATIN SMALL LETTER A WITH RING BELOW
                            (*p) = 0x80;
                            break;
                        case 0x83: // LATIN SMALL LETTER B WITH DOT ABOVE
                            (*p) = 0x82;
                            break;
                        case 0x85: // LATIN SMALL LETTER B WITH DOT BELOW
                            (*p) = 0x84;
                            break;
                        case 0x87: // LATIN SMALL LETTER B WITH LINE BELOW
                            (*p) = 0x86;
                            break;
                        case 0x89: // LATIN SMALL LETTER C WITH CEDILLA AND ACUTE
                            (*p) = 0x88;
                            break;
                        case 0x8b: // LATIN SMALL LETTER D WITH DOT ABOVE
                            (*p) = 0x8a;
                            break;
                        case 0x8d: // LATIN SMALL LETTER D WITH DOT BELOW
                            (*p) = 0x8c;
                            break;
                        case 0x8f: // LATIN SMALL LETTER D WITH LINE BELOW
                            (*p) = 0x8e;
                            break;
                        case 0x91: // LATIN SMALL LETTER D WITH CEDILLA
                            (*p) = 0x90;
                            break;
                        case 0x93: // LATIN SMALL LETTER D WITH CIRCUMFLEX BELOW
                            (*p) = 0x92;
                            break;
                        case 0x95: // LATIN SMALL LETTER E WITH MACRON AND GRAVE
                            (*p) = 0x94;
                            break;
                        case 0x97: // LATIN SMALL LETTER E WITH MACRON AND ACUTE
                            (*p) = 0x96;
                            break;
                        case 0x99: // LATIN SMALL LETTER E WITH CIRCUMFLEX BELOW
                            (*p) = 0x98;
                            break;
                        case 0x9b: // LATIN SMALL LETTER E WITH TILDE BELOW
                            (*p) = 0x9a;
                            break;
                        case 0x9d: // LATIN SMALL LETTER E WITH CEDILLA AND BREVE
                            (*p) = 0x9c;
                            break;
                        case 0x9f: // LATIN SMALL LETTER F WITH DOT ABOVE
                            (*p) = 0x9e;
                            break;
                        case 0xa1: // LATIN SMALL LETTER G WITH MACRON
                            (*p) = 0xa0;
                            break;
                        case 0xa3: // LATIN SMALL LETTER H WITH DOT ABOVE
                            (*p) = 0xa2;
                            break;
                        case 0xa5: // LATIN SMALL LETTER H WITH DOT BELOW
                            (*p) = 0xa4;
                            break;
                        case 0xa7: // LATIN SMALL LETTER H WITH DIAERESIS
                            (*p) = 0xa6;
                            break;
                        case 0xa9: // LATIN SMALL LETTER H WITH CEDILLA
                            (*p) = 0xa8;
                            break;
                        case 0xab: // LATIN SMALL LETTER H WITH BREVE BELOW
                            (*p) = 0xaa;
                            break;
                        case 0xad: // LATIN SMALL LETTER I WITH TILDE BELOW
                            (*p) = 0xac;
                            break;
                        case 0xaf: // LATIN SMALL LETTER I WITH DIAERESIS AND ACUTE
                            (*p) = 0xae;
                            break;
                        case 0xb1: // LATIN SMALL LETTER K WITH ACUTE
                            (*p) = 0xb0;
                            break;
                        case 0xb3: // LATIN SMALL LETTER K WITH DOT BELOW
                            (*p) = 0xb2;
                            break;
                        case 0xb5: // LATIN SMALL LETTER K WITH LINE BELOW
                            (*p) = 0xb4;
                            break;
                        case 0xb7: // LATIN SMALL LETTER L WITH DOT BELOW
                            (*p) = 0xb6;
                            break;
                        case 0xb9: // LATIN SMALL LETTER L WITH DOT BELOW AND MACRON
                            (*p) = 0xb8;
                            break;
                        case 0xbb: // LATIN SMALL LETTER L WITH LINE BELOW
                            (*p) = 0xba;
                            break;
                        case 0xbd: // LATIN SMALL LETTER L WITH CIRCUMFLEX BELOW
                            (*p) = 0xbc;
                            break;
                        case 0xbf: // LATIN SMALL LETTER M WITH ACUTE
                            (*p) = 0xbe;
                            break;
                        default:
                            break;
                        }
                        break;

                    case 0xb9: // Latin Extended Additional
                        switch (*p) {
                        case 0x81: // LATIN SMALL LETTER M WITH DOT ABOVE
                            (*p) = 0x80;
                            break;
                        case 0x83: // LATIN SMALL LETTER M WITH DOT BELOW
                            (*p) = 0x82;
                            break;
                        case 0x85: // LATIN SMALL LETTER N WITH DOT ABOVE
                            (*p) = 0x84;
                            break;
                        case 0x87: // LATIN SMALL LETTER N WITH DOT BELOW
                            (*p) = 0x86;
                            break;
                        case 0x89: // LATIN SMALL LETTER N WITH LINE BELOW
                            (*p) = 0x88;
                            break;
                        case 0x8b: // LATIN SMALL LETTER N WITH CIRCUMFLEX BELOW
                            (*p) = 0x8a;
                            break;
                        case 0x8d: // LATIN SMALL LETTER O WITH TILDE AND ACUTE
                            (*p) = 0x8c;
                            break;
                        case 0x8f: // LATIN SMALL LETTER O WITH TILDE AND DIAERESIS
                            (*p) = 0x8e;
                            break;
                        case 0x91: // LATIN SMALL LETTER O WITH MACRON AND GRAVE
                            (*p) = 0x90;
                            break;
                        case 0x93: // LATIN SMALL LETTER O WITH MACRON AND ACUTE
                            (*p) = 0x92;
                            break;
                        case 0x95: // LATIN SMALL LETTER P WITH ACUTE
                            (*p) = 0x94;
                            break;
                        case 0x97: // LATIN SMALL LETTER P WITH DOT ABOVE
                            (*p) = 0x96;
                            break;
                        case 0x99: // LATIN SMALL LETTER R WITH DOT ABOVE
                            (*p) = 0x98;
                            break;
                        case 0x9b: // LATIN SMALL LETTER R WITH DOT BELOW
                            (*p) = 0x9a;
                            break;
                        case 0x9d: // LATIN SMALL LETTER R WITH DOT BELOW AND MACRON
                            (*p) = 0x9c;
                            break;
                        case 0x9f: // LATIN SMALL LETTER R WITH LINE BELOW
                            (*p) = 0x9e;
                            break;
                        case 0xa1: // LATIN SMALL LETTER S WITH DOT ABOVE
                            (*p) = 0xa0;
                            break;
                        case 0xa3: // LATIN SMALL LETTER S WITH DOT BELOW
                            (*p) = 0xa2;
                            break;
                        case 0xa5: // LATIN SMALL LETTER S WITH ACUTE AND DOT ABOVE
                            (*p) = 0xa4;
                            break;
                        case 0xa7: // LATIN SMALL LETTER S WITH CARON AND DOT ABOVE
                            (*p) = 0xa6;
                            break;
                        case 0xa9: // LATIN SMALL LETTER S WITH DOT BELOW AND DOT ABOVE
                            (*p) = 0xa8;
                            break;
                        case 0xab: // LATIN SMALL LETTER T WITH DOT ABOVE
                            (*p) = 0xaa;
                            break;
                        case 0xad: // LATIN SMALL LETTER T WITH DOT BELOW
                            (*p) = 0xac;
                            break;
                        case 0xaf: // LATIN SMALL LETTER T WITH LINE BELOW
                            (*p) = 0xae;
                            break;
                        case 0xb1: // LATIN SMALL LETTER T WITH CIRCUMFLEX BELOW
                            (*p) = 0xb0;
                            break;
                        case 0xb3: // LATIN SMALL LETTER U WITH DIAERESIS BELOW
                            (*p) = 0xb2;
                            break;
                        case 0xb5: // LATIN SMALL LETTER U WITH TILDE BELOW
                            (*p) = 0xb4;
                            break;
                        case 0xb7: // LATIN SMALL LETTER U WITH CIRCUMFLEX BELOW
                            (*p) = 0xb6;
                            break;
                        case 0xb9: // LATIN SMALL LETTER U WITH TILDE AND ACUTE
                            (*p) = 0xb8;
                            break;
                        case 0xbb: // LATIN SMALL LETTER U WITH MACRON AND DIAERESIS
                            (*p) = 0xba;
                            break;
                        case 0xbd: // LATIN SMALL LETTER V WITH TILDE
                            (*p) = 0xbc;
                            break;
                        case 0xbf: // LATIN SMALL LETTER V WITH DOT BELOW
                            (*p) = 0xbe;
                            break;
                        default:
                            break;
                        }
                        break;

                    case 0xba: // Latin Extended Additional
                        switch (*p) {
                        case 0x81: // LATIN SMALL LETTER W WITH GRAVE
                            (*p) = 0x80;
                            break;
                        case 0x83: // LATIN SMALL LETTER W WITH ACUTE
                            (*p) = 0x82;
                            break;
                        case 0x85: // LATIN SMALL LETTER W WITH DIAERESIS
                            (*p) = 0x84;
                            break;
                        case 0x87: // LATIN SMALL LETTER W WITH DOT ABOVE
                            (*p) = 0x86;
                            break;
                        case 0x89: // LATIN SMALL LETTER W WITH DOT BELOW
                            (*p) = 0x88;
                            break;
                        case 0x8b: // LATIN SMALL LETTER X WITH DOT ABOVE
                            (*p) = 0x8a;
                            break;
                        case 0x8d: // LATIN SMALL LETTER X WITH DIAERESIS
                            (*p) = 0x8c;
                            break;
                        case 0x8f: // LATIN SMALL LETTER Y WITH DOT ABOVE
                            (*p) = 0x8e;
                            break;
                        case 0x91: // LATIN SMALL LETTER Z WITH CIRCUMFLEX
                            (*p) = 0x90;
                            break;
                        case 0x93: // LATIN SMALL LETTER Z WITH DOT BELOW
                            (*p) = 0x92;
                            break;
                        case 0x95: // LATIN SMALL LETTER Z WITH LINE BELOW
                            (*p) = 0x94;
                            break;
                        case 0x9b: // LATIN SMALL LETTER LONG S WITH DOT ABOVE (crosses to byte2 0xb9)
                            *pExtChar = 0xb9;
                            (*p) = 0xa0;
                            break;
                        case 0xa1: // LATIN SMALL LETTER A WITH DOT BELOW
                            (*p) = 0xa0;
                            break;
                        case 0xa3: // LATIN SMALL LETTER A WITH HOOK ABOVE
                            (*p) = 0xa2;
                            break;
                        case 0xa5: // LATIN SMALL LETTER A WITH CIRCUMFLEX AND ACUTE
                            (*p) = 0xa4;
                            break;
                        case 0xa7: // LATIN SMALL LETTER A WITH CIRCUMFLEX AND GRAVE
                            (*p) = 0xa6;
                            break;
                        case 0xa9: // LATIN SMALL LETTER A WITH CIRCUMFLEX AND HOOK ABOVE
                            (*p) = 0xa8;
                            break;
                        case 0xab: // LATIN SMALL LETTER A WITH CIRCUMFLEX AND TILDE
                            (*p) = 0xaa;
                            break;
                        case 0xad: // LATIN SMALL LETTER A WITH CIRCUMFLEX AND DOT BELOW
                            (*p) = 0xac;
                            break;
                        case 0xaf: // LATIN SMALL LETTER A WITH BREVE AND ACUTE
                            (*p) = 0xae;
                            break;
                        case 0xb1: // LATIN SMALL LETTER A WITH BREVE AND GRAVE
                            (*p) = 0xb0;
                            break;
                        case 0xb3: // LATIN SMALL LETTER A WITH BREVE AND HOOK ABOVE
                            (*p) = 0xb2;
                            break;
                        case 0xb5: // LATIN SMALL LETTER A WITH BREVE AND TILDE
                            (*p) = 0xb4;
                            break;
                        case 0xb7: // LATIN SMALL LETTER A WITH BREVE AND DOT BELOW
                            (*p) = 0xb6;
                            break;
                        case 0xb9: // LATIN SMALL LETTER E WITH DOT BELOW
                            (*p) = 0xb8;
                            break;
                        case 0xbb: // LATIN SMALL LETTER E WITH HOOK ABOVE
                            (*p) = 0xba;
                            break;
                        case 0xbd: // LATIN SMALL LETTER E WITH TILDE
                            (*p) = 0xbc;
                            break;
                        case 0xbf: // LATIN SMALL LETTER E WITH CIRCUMFLEX AND ACUTE
                            (*p) = 0xbe;
                            break;
                        default:
                            break;
                        }
                        break;

                    case 0xbb: // Latin Extended Additional
                        switch (*p) {
                        case 0x81: // LATIN SMALL LETTER E WITH CIRCUMFLEX AND GRAVE
                            (*p) = 0x80;
                            break;
                        case 0x83: // LATIN SMALL LETTER E WITH CIRCUMFLEX AND HOOK ABOVE
                            (*p) = 0x82;
                            break;
                        case 0x85: // LATIN SMALL LETTER E WITH CIRCUMFLEX AND TILDE
                            (*p) = 0x84;
                            break;
                        case 0x87: // LATIN SMALL LETTER E WITH CIRCUMFLEX AND DOT BELOW
                            (*p) = 0x86;
                            break;
                        case 0x89: // LATIN SMALL LETTER I WITH HOOK ABOVE
                            (*p) = 0x88;
                            break;
                        case 0x8b: // LATIN SMALL LETTER I WITH DOT BELOW
                            (*p) = 0x8a;
                            break;
                        case 0x8d: // LATIN SMALL LETTER O WITH DOT BELOW
                            (*p) = 0x8c;
                            break;
                        case 0x8f: // LATIN SMALL LETTER O WITH HOOK ABOVE
                            (*p) = 0x8e;
                            break;
                        case 0x91: // LATIN SMALL LETTER O WITH CIRCUMFLEX AND ACUTE
                            (*p) = 0x90;
                            break;
                        case 0x93: // LATIN SMALL LETTER O WITH CIRCUMFLEX AND GRAVE
                            (*p) = 0x92;
                            break;
                        case 0x95: // LATIN SMALL LETTER O WITH CIRCUMFLEX AND HOOK ABOVE
                            (*p) = 0x94;
                            break;
                        case 0x97: // LATIN SMALL LETTER O WITH CIRCUMFLEX AND TILDE
                            (*p) = 0x96;
                            break;
                        case 0x99: // LATIN SMALL LETTER O WITH CIRCUMFLEX AND DOT BELOW
                            (*p) = 0x98;
                            break;
                        case 0x9b: // LATIN SMALL LETTER O WITH HORN AND ACUTE
                            (*p) = 0x9a;
                            break;
                        case 0x9d: // LATIN SMALL LETTER O WITH HORN AND GRAVE
                            (*p) = 0x9c;
                            break;
                        case 0x9f: // LATIN SMALL LETTER O WITH HORN AND HOOK ABOVE
                            (*p) = 0x9e;
                            break;
                        case 0xa1: // LATIN SMALL LETTER O WITH HORN AND TILDE
                            (*p) = 0xa0;
                            break;
                        case 0xa3: // LATIN SMALL LETTER O WITH HORN AND DOT BELOW
                            (*p) = 0xa2;
                            break;
                        case 0xa5: // LATIN SMALL LETTER U WITH DOT BELOW
                            (*p) = 0xa4;
                            break;
                        case 0xa7: // LATIN SMALL LETTER U WITH HOOK ABOVE
                            (*p) = 0xa6;
                            break;
                        case 0xa9: // LATIN SMALL LETTER U WITH HORN AND ACUTE
                            (*p) = 0xa8;
                            break;
                        case 0xab: // LATIN SMALL LETTER U WITH HORN AND GRAVE
                            (*p) = 0xaa;
                            break;
                        case 0xad: // LATIN SMALL LETTER U WITH HORN AND HOOK ABOVE
                            (*p) = 0xac;
                            break;
                        case 0xaf: // LATIN SMALL LETTER U WITH HORN AND TILDE
                            (*p) = 0xae;
                            break;
                        case 0xb1: // LATIN SMALL LETTER U WITH HORN AND DOT BELOW
                            (*p) = 0xb0;
                            break;
                        case 0xb3: // LATIN SMALL LETTER Y WITH GRAVE
                            (*p) = 0xb2;
                            break;
                        case 0xb5: // LATIN SMALL LETTER Y WITH DOT BELOW
                            (*p) = 0xb4;
                            break;
                        case 0xb7: // LATIN SMALL LETTER Y WITH HOOK ABOVE
                            (*p) = 0xb6;
                            break;
                        case 0xb9: // LATIN SMALL LETTER Y WITH TILDE
                            (*p) = 0xb8;
                            break;
                        case 0xbb: // LATIN SMALL LETTER MIDDLE-WELSH LL
                            (*p) = 0xba;
                            break;
                        case 0xbd: // LATIN SMALL LETTER MIDDLE-WELSH V
                            (*p) = 0xbc;
                            break;
                        case 0xbf: // LATIN SMALL LETTER Y WITH LOOP
                            (*p) = 0xbe;
                            break;
                        default:
                            break;
                        }
                        break;

                    case 0xbc: // Greek Extended: ALPHA/EPSILON/ETA/IOTA
                        switch (*p) {
                        case 0x80: // GREEK SMALL LETTER ALPHA WITH PSILI
                            (*p) = 0x88;
                            break;
                        case 0x81: // GREEK SMALL LETTER ALPHA WITH DASIA
                            (*p) = 0x89;
                            break;
                        case 0x82: // GREEK SMALL LETTER ALPHA WITH PSILI AND VARIA
                            (*p) = 0x8a;
                            break;
                        case 0x83: // GREEK SMALL LETTER ALPHA WITH DASIA AND VARIA
                            (*p) = 0x8b;
                            break;
                        case 0x84: // GREEK SMALL LETTER ALPHA WITH PSILI AND OXIA
                            (*p) = 0x8c;
                            break;
                        case 0x85: // GREEK SMALL LETTER ALPHA WITH DASIA AND OXIA
                            (*p) = 0x8d;
                            break;
                        case 0x86: // GREEK SMALL LETTER ALPHA WITH PSILI AND PERISPOMENI
                            (*p) = 0x8e;
                            break;
                        case 0x87: // GREEK SMALL LETTER ALPHA WITH DASIA AND PERISPOMENI
                            (*p) = 0x8f;
                            break;
                        case 0x90: // GREEK SMALL LETTER EPSILON WITH PSILI
                            (*p) = 0x98;
                            break;
                        case 0x91: // GREEK SMALL LETTER EPSILON WITH DASIA
                            (*p) = 0x99;
                            break;
                        case 0x92: // GREEK SMALL LETTER EPSILON WITH PSILI AND VARIA
                            (*p) = 0x9a;
                            break;
                        case 0x93: // GREEK SMALL LETTER EPSILON WITH DASIA AND VARIA
                            (*p) = 0x9b;
                            break;
                        case 0x94: // GREEK SMALL LETTER EPSILON WITH PSILI AND OXIA
                            (*p) = 0x9c;
                            break;
                        case 0x95: // GREEK SMALL LETTER EPSILON WITH DASIA AND OXIA
                            (*p) = 0x9d;
                            break;
                        case 0xa0: // GREEK SMALL LETTER ETA WITH PSILI
                            (*p) = 0xa8;
                            break;
                        case 0xa1: // GREEK SMALL LETTER ETA WITH DASIA
                            (*p) = 0xa9;
                            break;
                        case 0xa2: // GREEK SMALL LETTER ETA WITH PSILI AND VARIA
                            (*p) = 0xaa;
                            break;
                        case 0xa3: // GREEK SMALL LETTER ETA WITH DASIA AND VARIA
                            (*p) = 0xab;
                            break;
                        case 0xa4: // GREEK SMALL LETTER ETA WITH PSILI AND OXIA
                            (*p) = 0xac;
                            break;
                        case 0xa5: // GREEK SMALL LETTER ETA WITH DASIA AND OXIA
                            (*p) = 0xad;
                            break;
                        case 0xa6: // GREEK SMALL LETTER ETA WITH PSILI AND PERISPOMENI
                            (*p) = 0xae;
                            break;
                        case 0xa7: // GREEK SMALL LETTER ETA WITH DASIA AND PERISPOMENI
                            (*p) = 0xaf;
                            break;
                        case 0xb0: // GREEK SMALL LETTER IOTA WITH PSILI
                            (*p) = 0xb8;
                            break;
                        case 0xb1: // GREEK SMALL LETTER IOTA WITH DASIA
                            (*p) = 0xb9;
                            break;
                        case 0xb2: // GREEK SMALL LETTER IOTA WITH PSILI AND VARIA
                            (*p) = 0xba;
                            break;
                        case 0xb3: // GREEK SMALL LETTER IOTA WITH DASIA AND VARIA
                            (*p) = 0xbb;
                            break;
                        case 0xb4: // GREEK SMALL LETTER IOTA WITH PSILI AND OXIA
                            (*p) = 0xbc;
                            break;
                        case 0xb5: // GREEK SMALL LETTER IOTA WITH DASIA AND OXIA
                            (*p) = 0xbd;
                            break;
                        case 0xb6: // GREEK SMALL LETTER IOTA WITH PSILI AND PERISPOMENI
                            (*p) = 0xbe;
                            break;
                        case 0xb7: // GREEK SMALL LETTER IOTA WITH DASIA AND PERISPOMENI
                            (*p) = 0xbf;
                            break;
                        default:
                            break;
                        }
                        break;

                    case 0xbd: // Greek Extended: OMICRON/UPSILON/OMEGA
                        switch (*p) {
                        case 0x80: // GREEK SMALL LETTER OMICRON WITH PSILI
                            (*p) = 0x88;
                            break;
                        case 0x81: // GREEK SMALL LETTER OMICRON WITH DASIA
                            (*p) = 0x89;
                            break;
                        case 0x82: // GREEK SMALL LETTER OMICRON WITH PSILI AND VARIA
                            (*p) = 0x8a;
                            break;
                        case 0x83: // GREEK SMALL LETTER OMICRON WITH DASIA AND VARIA
                            (*p) = 0x8b;
                            break;
                        case 0x84: // GREEK SMALL LETTER OMICRON WITH PSILI AND OXIA
                            (*p) = 0x8c;
                            break;
                        case 0x85: // GREEK SMALL LETTER OMICRON WITH DASIA AND OXIA
                            (*p) = 0x8d;
                            break;
                        case 0x91: // GREEK SMALL LETTER UPSILON WITH DASIA
                            (*p) = 0x99;
                            break;
                        case 0x93: // GREEK SMALL LETTER UPSILON WITH DASIA AND VARIA
                            (*p) = 0x9b;
                            break;
                        case 0x95: // GREEK SMALL LETTER UPSILON WITH DASIA AND OXIA
                            (*p) = 0x9d;
                            break;
                        case 0x97: // GREEK SMALL LETTER UPSILON WITH DASIA AND PERISPOMENI
                            (*p) = 0x9f;
                            break;
                        case 0xa0: // GREEK SMALL LETTER OMEGA WITH PSILI
                            (*p) = 0xa8;
                            break;
                        case 0xa1: // GREEK SMALL LETTER OMEGA WITH DASIA
                            (*p) = 0xa9;
                            break;
                        case 0xa2: // GREEK SMALL LETTER OMEGA WITH PSILI AND VARIA
                            (*p) = 0xaa;
                            break;
                        case 0xa3: // GREEK SMALL LETTER OMEGA WITH DASIA AND VARIA
                            (*p) = 0xab;
                            break;
                        case 0xa4: // GREEK SMALL LETTER OMEGA WITH PSILI AND OXIA
                            (*p) = 0xac;
                            break;
                        case 0xa5: // GREEK SMALL LETTER OMEGA WITH DASIA AND OXIA
                            (*p) = 0xad;
                            break;
                        case 0xa6: // GREEK SMALL LETTER OMEGA WITH PSILI AND PERISPOMENI
                            (*p) = 0xae;
                            break;
                        case 0xa7: // GREEK SMALL LETTER OMEGA WITH DASIA AND PERISPOMENI
                            (*p) = 0xaf;
                            break;
                        case 0xb0: // GREEK SMALL LETTER ALPHA WITH VARIA (crosses to byte2 0xbe)
                            *pExtChar = 0xbe;
                            (*p) = 0xba;
                            break;
                        case 0xb1: // GREEK SMALL LETTER ALPHA WITH OXIA (crosses to byte2 0xbe)
                            *pExtChar = 0xbe;
                            (*p) = 0xbb;
                            break;
                        case 0xb2: // GREEK SMALL LETTER EPSILON WITH VARIA (crosses to byte2 0xbf)
                            *pExtChar = 0xbf;
                            (*p) = 0x88;
                            break;
                        case 0xb3: // GREEK SMALL LETTER EPSILON WITH OXIA (crosses to byte2 0xbf)
                            *pExtChar = 0xbf;
                            (*p) = 0x89;
                            break;
                        case 0xb4: // GREEK SMALL LETTER ETA WITH VARIA (crosses to byte2 0xbf)
                            *pExtChar = 0xbf;
                            (*p) = 0x8a;
                            break;
                        case 0xb5: // GREEK SMALL LETTER ETA WITH OXIA (crosses to byte2 0xbf)
                            *pExtChar = 0xbf;
                            (*p) = 0x8b;
                            break;
                        case 0xb6: // GREEK SMALL LETTER IOTA WITH VARIA (crosses to byte2 0xbf)
                            *pExtChar = 0xbf;
                            (*p) = 0x9a;
                            break;
                        case 0xb7: // GREEK SMALL LETTER IOTA WITH OXIA (crosses to byte2 0xbf)
                            *pExtChar = 0xbf;
                            (*p) = 0x9b;
                            break;
                        case 0xb8: // GREEK SMALL LETTER OMICRON WITH VARIA (crosses to byte2 0xbf)
                            *pExtChar = 0xbf;
                            (*p) = 0xb8;
                            break;
                        case 0xb9: // GREEK SMALL LETTER OMICRON WITH OXIA (crosses to byte2 0xbf)
                            *pExtChar = 0xbf;
                            (*p) = 0xb9;
                            break;
                        case 0xba: // GREEK SMALL LETTER UPSILON WITH VARIA (crosses to byte2 0xbf)
                            *pExtChar = 0xbf;
                            (*p) = 0xaa;
                            break;
                        case 0xbb: // GREEK SMALL LETTER UPSILON WITH OXIA (crosses to byte2 0xbf)
                            *pExtChar = 0xbf;
                            (*p) = 0xab;
                            break;
                        case 0xbc: // GREEK SMALL LETTER OMEGA WITH VARIA (crosses to byte2 0xbf)
                            *pExtChar = 0xbf;
                            (*p) = 0xba;
                            break;
                        case 0xbd: // GREEK SMALL LETTER OMEGA WITH OXIA (crosses to byte2 0xbf)
                            *pExtChar = 0xbf;
                            (*p) = 0xbb;
                            break;
                        default:
                            break;
                        }
                        break;

                    case 0xbe: // Greek Extended: extra row, part 1
                        switch (*p) {
                        case 0xb0: // GREEK SMALL LETTER ALPHA WITH VRACHY
                            (*p) = 0xb8;
                            break;
                        case 0xb1: // GREEK SMALL LETTER ALPHA WITH MACRON
                            (*p) = 0xb9;
                            break;
                        default:
                            break;
                        }
                        break;

                    case 0xbf: // Greek Extended: extra row, part 2
                        switch (*p) {
                        case 0x90: // GREEK SMALL LETTER IOTA WITH VRACHY
                            (*p) = 0x98;
                            break;
                        case 0x91: // GREEK SMALL LETTER IOTA WITH MACRON
                            (*p) = 0x99;
                            break;
                        case 0xa0: // GREEK SMALL LETTER UPSILON WITH VRACHY
                            (*p) = 0xa8;
                            break;
                        case 0xa1: // GREEK SMALL LETTER UPSILON WITH MACRON
                            (*p) = 0xa9;
                            break;
                        case 0xa5: // GREEK SMALL LETTER RHO WITH DASIA
                            (*p) = 0xac;
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

            case 0xf0: // Four byte code -- Osage, Adlam
                pExtChar = p;
                p++;
                if (!end || p < end) { // else: sequence truncated at buffer end -- leave untouched
                switch (*pExtChar) {
                case 0x90: // Osage
                    pExtChar = p;
                    p++;
                    if (!end || p < end) { // else: sequence truncated at buffer end -- leave untouched
                    switch (*pExtChar) {
                        case 0x93: // Osage
                            switch (*p) {
                            case 0x98: // OSAGE SMALL LETTER A (crosses to byte2/3 0x90/0x92)
                                *pExtChar = 0x92;
                                (*p) = 0xb0;
                                break;
                            case 0x99: // OSAGE SMALL LETTER AI (crosses to byte2/3 0x90/0x92)
                                *pExtChar = 0x92;
                                (*p) = 0xb1;
                                break;
                            case 0x9a: // OSAGE SMALL LETTER AIN (crosses to byte2/3 0x90/0x92)
                                *pExtChar = 0x92;
                                (*p) = 0xb2;
                                break;
                            case 0x9b: // OSAGE SMALL LETTER AH (crosses to byte2/3 0x90/0x92)
                                *pExtChar = 0x92;
                                (*p) = 0xb3;
                                break;
                            case 0x9c: // OSAGE SMALL LETTER BRA (crosses to byte2/3 0x90/0x92)
                                *pExtChar = 0x92;
                                (*p) = 0xb4;
                                break;
                            case 0x9d: // OSAGE SMALL LETTER CHA (crosses to byte2/3 0x90/0x92)
                                *pExtChar = 0x92;
                                (*p) = 0xb5;
                                break;
                            case 0x9e: // OSAGE SMALL LETTER EHCHA (crosses to byte2/3 0x90/0x92)
                                *pExtChar = 0x92;
                                (*p) = 0xb6;
                                break;
                            case 0x9f: // OSAGE SMALL LETTER E (crosses to byte2/3 0x90/0x92)
                                *pExtChar = 0x92;
                                (*p) = 0xb7;
                                break;
                            case 0xa0: // OSAGE SMALL LETTER EIN (crosses to byte2/3 0x90/0x92)
                                *pExtChar = 0x92;
                                (*p) = 0xb8;
                                break;
                            case 0xa1: // OSAGE SMALL LETTER HA (crosses to byte2/3 0x90/0x92)
                                *pExtChar = 0x92;
                                (*p) = 0xb9;
                                break;
                            case 0xa2: // OSAGE SMALL LETTER HYA (crosses to byte2/3 0x90/0x92)
                                *pExtChar = 0x92;
                                (*p) = 0xba;
                                break;
                            case 0xa3: // OSAGE SMALL LETTER I (crosses to byte2/3 0x90/0x92)
                                *pExtChar = 0x92;
                                (*p) = 0xbb;
                                break;
                            case 0xa4: // OSAGE SMALL LETTER KA (crosses to byte2/3 0x90/0x92)
                                *pExtChar = 0x92;
                                (*p) = 0xbc;
                                break;
                            case 0xa5: // OSAGE SMALL LETTER EHKA (crosses to byte2/3 0x90/0x92)
                                *pExtChar = 0x92;
                                (*p) = 0xbd;
                                break;
                            case 0xa6: // OSAGE SMALL LETTER KYA (crosses to byte2/3 0x90/0x92)
                                *pExtChar = 0x92;
                                (*p) = 0xbe;
                                break;
                            case 0xa7: // OSAGE SMALL LETTER LA (crosses to byte2/3 0x90/0x92)
                                *pExtChar = 0x92;
                                (*p) = 0xbf;
                                break;
                            case 0xa8: // OSAGE SMALL LETTER MA
                                (*p) = 0x80;
                                break;
                            case 0xa9: // OSAGE SMALL LETTER NA
                                (*p) = 0x81;
                                break;
                            case 0xaa: // OSAGE SMALL LETTER O
                                (*p) = 0x82;
                                break;
                            case 0xab: // OSAGE SMALL LETTER OIN
                                (*p) = 0x83;
                                break;
                            case 0xac: // OSAGE SMALL LETTER PA
                                (*p) = 0x84;
                                break;
                            case 0xad: // OSAGE SMALL LETTER EHPA
                                (*p) = 0x85;
                                break;
                            case 0xae: // OSAGE SMALL LETTER SA
                                (*p) = 0x86;
                                break;
                            case 0xaf: // OSAGE SMALL LETTER SHA
                                (*p) = 0x87;
                                break;
                            case 0xb0: // OSAGE SMALL LETTER TA
                                (*p) = 0x88;
                                break;
                            case 0xb1: // OSAGE SMALL LETTER EHTA
                                (*p) = 0x89;
                                break;
                            case 0xb2: // OSAGE SMALL LETTER TSA
                                (*p) = 0x8a;
                                break;
                            case 0xb3: // OSAGE SMALL LETTER EHTSA
                                (*p) = 0x8b;
                                break;
                            case 0xb4: // OSAGE SMALL LETTER TSHA
                                (*p) = 0x8c;
                                break;
                            case 0xb5: // OSAGE SMALL LETTER DHA
                                (*p) = 0x8d;
                                break;
                            case 0xb6: // OSAGE SMALL LETTER U
                                (*p) = 0x8e;
                                break;
                            case 0xb7: // OSAGE SMALL LETTER WA
                                (*p) = 0x8f;
                                break;
                            case 0xb8: // OSAGE SMALL LETTER KHA
                                (*p) = 0x90;
                                break;
                            case 0xb9: // OSAGE SMALL LETTER GHA
                                (*p) = 0x91;
                                break;
                            case 0xba: // OSAGE SMALL LETTER ZA
                                (*p) = 0x92;
                                break;
                            case 0xbb: // OSAGE SMALL LETTER ZHA
                                (*p) = 0x93;
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
                case 0x9e: // Adlam
                    pExtChar = p;
                    p++;
                    if (!end || p < end) { // else: sequence truncated at buffer end -- leave untouched
                    switch (*pExtChar) {
                        case 0xa4: // Adlam
                            switch (*p) {
                            case 0xa2: // ADLAM SMALL LETTER ALIF
                                (*p) = 0x80;
                                break;
                            case 0xa3: // ADLAM SMALL LETTER DAALI
                                (*p) = 0x81;
                                break;
                            case 0xa4: // ADLAM SMALL LETTER LAAM
                                (*p) = 0x82;
                                break;
                            case 0xa5: // ADLAM SMALL LETTER MIIM
                                (*p) = 0x83;
                                break;
                            case 0xa6: // ADLAM SMALL LETTER BA
                                (*p) = 0x84;
                                break;
                            case 0xa7: // ADLAM SMALL LETTER SINNYIIYHE
                                (*p) = 0x85;
                                break;
                            case 0xa8: // ADLAM SMALL LETTER PE
                                (*p) = 0x86;
                                break;
                            case 0xa9: // ADLAM SMALL LETTER BHE
                                (*p) = 0x87;
                                break;
                            case 0xaa: // ADLAM SMALL LETTER RA
                                (*p) = 0x88;
                                break;
                            case 0xab: // ADLAM SMALL LETTER E
                                (*p) = 0x89;
                                break;
                            case 0xac: // ADLAM SMALL LETTER FA
                                (*p) = 0x8a;
                                break;
                            case 0xad: // ADLAM SMALL LETTER I
                                (*p) = 0x8b;
                                break;
                            case 0xae: // ADLAM SMALL LETTER O
                                (*p) = 0x8c;
                                break;
                            case 0xaf: // ADLAM SMALL LETTER DHA
                                (*p) = 0x8d;
                                break;
                            case 0xb0: // ADLAM SMALL LETTER YHE
                                (*p) = 0x8e;
                                break;
                            case 0xb1: // ADLAM SMALL LETTER WAW
                                (*p) = 0x8f;
                                break;
                            case 0xb2: // ADLAM SMALL LETTER NUN
                                (*p) = 0x90;
                                break;
                            case 0xb3: // ADLAM SMALL LETTER KAF
                                (*p) = 0x91;
                                break;
                            case 0xb4: // ADLAM SMALL LETTER YA
                                (*p) = 0x92;
                                break;
                            case 0xb5: // ADLAM SMALL LETTER U
                                (*p) = 0x93;
                                break;
                            case 0xb6: // ADLAM SMALL LETTER JIIM
                                (*p) = 0x94;
                                break;
                            case 0xb7: // ADLAM SMALL LETTER CHI
                                (*p) = 0x95;
                                break;
                            case 0xb8: // ADLAM SMALL LETTER HA
                                (*p) = 0x96;
                                break;
                            case 0xb9: // ADLAM SMALL LETTER QAAF
                                (*p) = 0x97;
                                break;
                            case 0xba: // ADLAM SMALL LETTER GA
                                (*p) = 0x98;
                                break;
                            case 0xbb: // ADLAM SMALL LETTER NYA
                                (*p) = 0x99;
                                break;
                            case 0xbc: // ADLAM SMALL LETTER TU
                                (*p) = 0x9a;
                                break;
                            case 0xbd: // ADLAM SMALL LETTER NHA
                                (*p) = 0x9b;
                                break;
                            case 0xbe: // ADLAM SMALL LETTER VA
                                (*p) = 0x9c;
                                break;
                            case 0xbf: // ADLAM SMALL LETTER KHA
                                (*p) = 0x9d;
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
    return pString;
}
