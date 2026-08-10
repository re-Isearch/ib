// DOCTYPE::ParseWordsUTF8
//
// UTF-8-aware counterpart to the 8-bit DOCTYPE::ParseWords. Three linear
// sweeps over DataBuffer:
//
// PASS 1 — _utf_StrToLowerBuf(DataBuffer, DataLength, /*clean=*/true, ZapChr)
//   Case-folds every recognized script and zaps everything that isn't a
//   letter (control chars, combining marks, modifier letters, symbol
//   blocks, unrecognized scripts) to ZapChr, verified exhaustively so a
//   multi-byte sequence is never partially zapped.
//
// PASS 2 — _ib_ZapNonTermASCII(DataBuffer, DataLength, ZapChr)
//   Zaps the plain-ASCII punctuation/whitespace pass 1 deliberately leaves
//   alone, except DOT_WORDS_SIGNATURE bytes ('.', '_', '&', '@', '/', '-',
//   ';', ':', '+') — those are resolved contextually in pass 3, not zapped
//   blindly here.
//
// PASS 3 — tokenize
//   The skip-loop below is a plain byte scan for ZapChr, EXCEPT when it
//   lands on an unresolved dot-class byte: that gets classified right
//   there via _ib_IsUTF8TermChrFast's neighbor check, and if it's NOT
//   absorbed by a following letter (e.g. a trailing "auto-" before a
//   space, or the final period after "e.g."), it gets zapped on the spot
//   and the scan continues. This is the piece that makes leaving
//   dot-class bytes unresolved in pass 2 safe: every dot-class byte gets
//   resolved exactly once, right when its context is first examined,
//   rather than being left to be mis-picked-up as a spurious one-byte
//   "term" on a later iteration.
//
//   Beyond that, this is the same loop as the 8-bit ParseWords:
//   Position advances by _ib_IsUTF8TermChrFast's returned byte length
//   (a continuation byte is never a valid place to resume classification),
//   and there is no per-byte lowering here — pass 1 already folded the
//   whole buffer.
//
// CORRECTNESS DEPENDS ON PASS 1+2 AUDIT COVERAGE
//   Once a byte is neither ZapChr nor a dot-class byte, this pass trusts
//   it's a letter without reclassifying it. That trust is only as good as
//   _utf_StrToLowerBuf's per-script audit, which as of this writing is
//   exhaustive (UCD-diffed) for Latin, Greek & Coptic, Greek Extended,
//   Cyrillic, Armenian, Hebrew, Arabic, Syriac, and Arabic Supplement —
//   including getting the combining-mark/format-char distinction right
//   (_IB_UTF8_SILENT_ZAP) so stripped niqqud/tashkeel/Syriac vowel
//   points/decomposed accents don't fracture a word.
//   Scripts NOT yet audited this way (e.g. Thaana, NKo, Devanagari and
//   other Indic scripts) still carry the same risk this note originally
//   flagged: a not-yet-audited non-letter symbol in one of those would
//   currently be silently treated as a term start. Revisit this note as
//   more scripts get the same exhaustive treatment.
//
// ZapChr is fixed at '\0' here (not _SP), so this function's own zapping
// and _utf_StrToLowerBuf's clean-mode zapping agree on one sentinel.

#include "doctype.hxx"
#include "common.hxx"
#include "process.hxx"
#include "doc_conf.hxx"
#include "lang-codes.hxx"

#define _IB_UTF8_SILENT_ZAP ((unsigned char)0x01)


#if 0 /* Moved to utf8_utils */
static inline int _to_ucs4(const unsigned char *chr, const unsigned char *end, uint32_t *cp)
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
#endif

GPTYPE DOCTYPE::ParseWordsUTF8(UCHR* DataBuffer, GPTYPE DataLength,
        GPTYPE DataOffset, GPTYPE* GpBuffer, GPTYPE GpLength)
{
  GPTYPE GpListSize = 0;
  const UCHR zapChar = '\0';

  if (DataBuffer == NULL)
    {
      if (DataLength)
        message_log (LOG_ERROR, "ParseWordsUTF8: ***** NULL Data buffer ***** !");
      return 0;
    }
  if (GpBuffer == NULL || GpLength == 0)
    {
      message_log (LOG_ERROR, "**** Program ERROR: ParseWordsUTF8 Buffer%s? (Len=%d)", GpBuffer ? "" : " NULL", (int)GpLength);
      return 0; // ERROR
    }

#ifdef _WIN32
  message_log (LOG_DEBUG, "ParseWordsUTF8: Len: Data=%lld Buff=%lld / off=%lld",
        (long long)DataLength, (long long)GpLength, (long long)DataOffset);
#endif

  // PASS 1: lowercase + UTF-8-specific cleanup.
  // NOTE: _utf_StrToLowerBuf's length parameter is `unsigned`; if GPTYPE
  // can exceed UINT_MAX on this platform this cast needs revisiting.
  _utf_StrToLowerBuf(reinterpret_cast<unsigned char*>(DataBuffer),
                      (unsigned)DataLength, /*clean=*/true, (unsigned char)zapChar);

  // PASS 2: zap the remaining ASCII separators (dot-class bytes excluded
  // on purpose -- resolved contextually below).
  _ib_ZapNonTermASCII(reinterpret_cast<unsigned char*>(DataBuffer),
                       (unsigned)DataLength, (unsigned char)zapChar);

  const unsigned char *DataEnd = reinterpret_cast<const unsigned char*>(DataBuffer) + DataLength;

  // PASS 3: tokenize.
  for (REGISTER GPTYPE Position = 0; Position < DataLength;)
    {
      // Skip already-zapped bytes, resolving any dot-class byte we land
      // on along the way: classify it via its forward-neighbor test, and
      // if it's NOT absorbed by a following letter, zap it right here and
      // keep scanning. This is what stops a trailing "auto-" or "e.g."'s
      // final period from surviving as a stray unzapped byte that would
      // otherwise be mis-picked-up as its own one-character "term" on
      // the next iteration. _IB_UTF8_SILENT_ZAP bytes are skipped here
      // too -- at a fresh (non-extending) position there's no word in
      // progress to preserve continuity for, so a stray silent-zapped
      // byte (e.g. an orphaned combining mark with no base letter) is
      // just skipped like any other non-term byte.
      for (;;)
        {
          if (Position >= DataLength) break;
          if (DataBuffer[Position] == zapChar) { Position++; continue; }
          if (DataBuffer[Position] == _IB_UTF8_SILENT_ZAP) { Position++; continue; }
          if (IsDotInWord(DataBuffer[Position]))
            {
              int dotLen = _ib_IsUTF8TermChrFast(reinterpret_cast<const unsigned char*>(DataBuffer) + Position, DataEnd, (unsigned char)zapChar);
              if (dotLen == 0)
                {
                  DataBuffer[Position] = zapChar; // not mid-word -- resolved as separator
                  Position++;
                  continue;
                }
              // else: absorbed -- falls through, this position is the
              // start of a term (possibly a leading dot-class byte).
            }
          break;
        }

      // If the word is not in the stoplist then add it to GpBuffer
      if (( (DataLength - Position) > 0) &&
        DataBuffer[Position] &&
        (!(IsStopWord(&DataBuffer[Position], DataLength - Position))) )
        {
          if (GpListSize >= GpLength)
            {
              // Should NOT HAPPEN!!
              message_log (LOG_PANIC, "%s ParseWordsUTF8: Memory overrun (%lu >= %lu)[Position=%lu of %lu][%d-bit]!",
                        Doctype.c_str(), (long)GpListSize, (long)GpLength,
                        (long)Position, (long)DataLength,
                        8*sizeof(GPTYPE));
              abort(); // Stop process!
            }
          else if (GpListSize > DataLength)
            {
              message_log (LOG_ERROR, "INTERNAL ERROR STATE: Words Indexed=%lu / Position=%lu / DataLength=%lu",
                (long)GpListSize, (long)Position, (long)DataLength );
              message_log (LOG_PANIC, "Detected an ABSURD _ib_IsUTF8TermChrFast() callback function!");
              abort(); // Stop process
            }
          GpBuffer[GpListSize++] = DataOffset + Position;
          if (GpListSize <= 0)
            message_log (LOG_PANIC, "%s ParseWordsUTF8: Address space overrun (>%lu)[%d-bit]!",
                Doctype.c_str(), (long)GpLength, 8*sizeof(GPTYPE));
        }

      // Skip over the word
      if (Position >= DataLength)
        break;
      int itr = _ib_IsUTF8TermChrFast(reinterpret_cast<const unsigned char*>(DataBuffer) + Position, DataEnd, (unsigned char)zapChar);
      if (itr == 0)
        {
	  unsigned char *p = reinterpret_cast<unsigned char *>(DataBuffer) + Position;
	  uint32_t cp = 0; 
	  const int n = _to_ucs4(p, DataEnd, &cp);


	  if (n <= 0)
	    {

#if 1

{
const unsigned char *base = reinterpret_cast<const unsigned char *>(DataBuffer);
const unsigned char *p = base + Position;

message_log(LOG_ERROR,
    "(1)ITR=%d pos=%zu "
    "prev=%02X %02X %02X "
    "lead=%02X next=%02X %02X %02X",
    itr,
    Position,
    Position >= 3 ? base[Position - 3] : 0,
    Position >= 2 ? base[Position - 2] : 0,
    Position >= 1 ? base[Position - 1] : 0,
    p[0],
    p + 1 < DataEnd ? p[1] : 0,
    p + 2 < DataEnd ? p[2] : 0,
    p + 3 < DataEnd ? p[3] : 0);
}

	      message_log(LOG_ERROR, "(2)ITR=0 pos=%zu lead=%02X bytes=%02X %02X %02X %02X "
                "decoded=%d cp=U+%04X zap=%02X\n",
                        (size_t)Position,
                        p < DataEnd ? p[0] : 0,
                        p + 1 < DataEnd ? p[1] : 0,
                        p + 2 < DataEnd ? p[2] : 0,
                        p + 3 < DataEnd ? p[3] : 0,
                        n,
                        (unsigned)cp, 
                        (unsigned)(unsigned char)zapChar);
#endif
	      message_log(LOG_PANIC,
		"Invalid UTF-8 encountered while indexing in UTF-8 mode. "
		"Check the document encoding and ingest pipeline.");
	      /*
	       * Recovery: zap one bad byte so that we always make progress.
	       */
	      *p = (unsigned char)zapChar;
	      ++Position;
	      continue;
	    }

	  /*
	   * Valid UTF-8, but not a term character.
	   *
	   * This should normally have been removed by the cleaning pass.
	   * Zap the entire UTF-8 sequence, never just its lead byte.
	   */
	   memset(p, (unsigned char)zapChar, (size_t)n);

	   Position += (size_t)n;
	   continue;
        }
      while ( itr )
        {
          // No per-byte lowering here -- pass 1 already folded the whole
          // buffer.
          Position += itr; // Advance by the FULL byte length of the term
                            // character just identified.

          // Skip any run of silently-zapped bytes (stripped combining
          // marks/format chars -- niqqud, tashkeel, decomposed accents,
          // etc.) WITHOUT ending the word. This is the difference between
          // "auto- und" (a real separator, word ends) and "שלום" with
          // niqqud in between each letter (marks are transparent, word
          // continues) -- both are ZapChr-adjacent, but only one of them
          // is a genuine word boundary. A run because a single mark can
          // itself be multiple bytes, and _IB_UTF8_SILENT_ZAP fills every
          // byte of a zapped sequence (same all-or-nothing invariant as
          // ZapChr), so stepping one byte at a time through the run is
          // sufficient -- no length tracking needed.
          while (Position < DataLength && DataBuffer[Position] == _IB_UTF8_SILENT_ZAP)
            Position++;

          if (Position >= DataLength) break;
          if (IsDotInWord(DataBuffer[Position]))
            {
              // Same forward-neighbor resolution as the outer skip-loop,
              // but reached here because we're mid-extension: a dot/hyphen
              // absorbed into the CURRENT term if what follows is a
              // letter, otherwise zapped and the term ends here.
              itr = _ib_IsUTF8TermChrFast(reinterpret_cast<const unsigned char*>(DataBuffer) + Position, DataEnd, (unsigned char)zapChar);
              if (!itr)
                {
                  DataBuffer[Position] = zapChar;
                  break;
                }
              continue;
            }
          itr = _ib_IsUTF8TermChrFast(reinterpret_cast<const unsigned char*>(DataBuffer) + Position, DataEnd, (unsigned char)zapChar);
        }
      // NOTE: If the first character was NOT a TermChr then we'll be at the same
      //       position. This only can happen when a callback makes no sense.

   }
   return GpListSize;
}  // Return # of GP's added to GpBuffer
