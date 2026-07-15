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
static unsigned char *_utf_StrToUpper(unsigned char *pString,
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
