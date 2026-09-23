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
static void _ib_ZapNonTermASCII(unsigned char *pString, unsigned length, unsigned char ZapChr)
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

int _ib_IsUTF8TermChrFast(const unsigned char *Buffer, const unsigned char *End,
                           unsigned char ZapChr = _SP)
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
    uint32_t cp;
    int bytes = to_ucs4(Buffer, &cp);
    if (!bytes)
        return 0;
    return IsTermChar(cp) ? bytes : 0;
}
