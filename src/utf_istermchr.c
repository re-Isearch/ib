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
