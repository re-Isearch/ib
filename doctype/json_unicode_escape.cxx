// ---------------------------------------------------------------------
// JSON \uXXXX escape resolution, IN PLACE, offset-preserving.
//
// Retrieval/reconstruction call sites (GetTerm, GetRecordData,
// ReadFile) want the same GP-address-stable transform, but with a
// display-safe fill byte instead of DOCTYPE::ParseWords' NUL:
//
//     ResolveJSONUnicodeEscapes(DataBuffer, DataLength, ' ');
//
// This is deliberately decoupled from field extraction: it doesn't
// know or care about keys, quotes, or JSON structure. It only finds
// \uXXXX (and \uXXXX\uYYYY surrogate pairs), rewrites them to their
// decoded UTF-8 bytes in place, and pads whatever's left over with
// ZapChr. It never shrinks or grows DataLength, so every byte offset
// in DataBuffer -- and therefore every GP address ParseWords computes
// from it -- lands exactly where it did before this ran, regardless
// of which ZapChr a given call site chose.
//
// Anything malformed (truncated escape, bad hex digits, an unpaired
// lone surrogate) is left untouched or blanked to ZapChr rather than
// risk mis-decoding -- worst case it tokenizes as harmless junk.
// ---------------------------------------------------------------------

#include <cstring> // memchr

// 256-entry lookup: hex digit value for any byte, -1 if not a hex digit.
// One indexed load instead of a 3-way branch chain -- matters a lot in
// scripts (Cyrillic, Abkhaz, etc.) where nearly every character is
// individually \uXXXX-escaped, so this runs 4x per character, back to
// back, for the entire string.
static const signed char _json_hex_lut[256] = {
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
   0,  1,  2,  3,  4,  5,  6,  7,  8,  9, -1, -1, -1, -1, -1, -1,
  -1, 10, 11, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, 10, 11, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
};

static inline int _json_hex_nibble(UCHR c)
{
  return _json_hex_lut[(unsigned char)c];
}

// Parse exactly 4 hex digits starting at p. Returns the codepoint
// (0..0xFFFF), or -1 if any of the 4 characters isn't a hex digit.
static inline int _json_parse_u4(const UCHR* p)
{
  const int h0 = _json_hex_nibble(p[0]);
  const int h1 = _json_hex_nibble(p[1]);
  const int h2 = _json_hex_nibble(p[2]);
  const int h3 = _json_hex_nibble(p[3]);
  if (h0 < 0 || h1 < 0 || h2 < 0 || h3 < 0)
    return -1;
  return (h0 << 12) | (h1 << 8) | (h2 << 4) | h3;
}

// Encode `cp` as UTF-8 into `out` (caller guarantees >= 4 bytes of
// room). Returns the number of bytes written (1-4).
static inline int _utf8_encode(UINT4 cp, UCHR* out)
{
  if (cp <= 0x7F) {
    out[0] = (UCHR)cp;
    return 1;
  }
  if (cp <= 0x7FF) {
    out[0] = (UCHR)(0xC0 | (cp >> 6));
    out[1] = (UCHR)(0x80 | (cp & 0x3F));
    return 2;
  }
  if (cp <= 0xFFFF) {
    out[0] = (UCHR)(0xE0 | (cp >> 12));
    out[1] = (UCHR)(0x80 | ((cp >> 6) & 0x3F));
    out[2] = (UCHR)(0x80 | (cp & 0x3F));
    return 3;
  }
  out[0] = (UCHR)(0xF0 | (cp >> 18));
  out[1] = (UCHR)(0x80 | ((cp >> 12) & 0x3F));
  out[2] = (UCHR)(0x80 | ((cp >> 6) & 0x3F));
  out[3] = (UCHR)(0x80 | (cp & 0x3F));
  return 4;
}

// Try to decode a single \uXXXX (or \uXXXX\uYYYY surrogate pair)
// starting at DataBuffer[Read]. Pure -- doesn't touch the buffer.
// On success returns true and sets *cp/*consumed (consumed is 6 or 12).
// On failure returns false.
static inline bool _json_try_decode_escape(const UCHR* DataBuffer, GPTYPE Read,
                                            GPTYPE DataLength, int* cp_out, GPTYPE* consumed_out)
{
  if (Read + 6 > DataLength)
    return false;
  if (DataBuffer[Read] != '\\' || DataBuffer[Read+1] != 'u')
    return false;

  int cp = _json_parse_u4(DataBuffer + Read + 2);
  if (cp < 0)
    return false;

  GPTYPE consumed = 6;

  if (cp >= 0xD800 && cp <= 0xDBFF) {
    // High surrogate: only means something if immediately followed
    // by a \uYYYY low surrogate. Otherwise it's unpaired/malformed.
    if (Read + 12 <= DataLength &&
        DataBuffer[Read+6] == '\\' && DataBuffer[Read+7] == 'u') {
      const int lo = _json_parse_u4(DataBuffer + Read + 8);
      if (lo >= 0xDC00 && lo <= 0xDFFF) {
        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
        consumed = 12;
      } else {
        return false; // unpaired high surrogate
      }
    } else {
      return false; // unpaired high surrogate
    }
  } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
    return false; // lone low surrogate -- never valid on its own
  }

  *cp_out = cp;
  *consumed_out = consumed;
  return true;
}

// Single pass, in place, no allocation.
//
// ZapChr is the fill byte written into whatever slack a run's decode
// frees up. Callers pick it based on what they're doing with the
// result, not what this function does internally:
//   - Indexing (ParseWords):        ZapChr = '\0', matching
//     DOCTYPE::ParseWords' own zap convention, so the padding is
//     correctly treated as a word separator there.
//   - Retrieval/display (GetTerm,
//     GetRecordData, ReadFile):      ZapChr = ' ', so a reconstructed
//     record can be handed to anything that treats it as a C-string
//     (printf %s, cout<<char*, etc.) without silently truncating at
//     the first embedded NUL.
// Either way DataLength is unchanged and every run's start offset is
// preserved, so GP addresses computed against one ZapChr choice are
// still valid when the same bytes are reconstructed with the other.
//
// The scan for the next '\' uses memchr() rather than a hand-rolled
// loop: memchr is already SIMD-accelerated on every libc worth
// targeting (SSE2/AVX2 on x86, NEON on Apple Silicon), so writing our
// own vector code here would just be reimplementing what the platform
// already gives you. The decode logic below it stays scalar on
// purpose -- it's branchy and variable-stride per escape, which is a
// poor fit for SIMD regardless; the lookup table above is the
// practical win there instead of hand-vectorizing.
//
// IMPORTANT: consecutive \uXXXX escapes are decoded as a single RUN,
// not independently. JSON dumps in non-Latin scripts routinely escape
// every character of a word individually (e.g. "Чачба" as five
// back-to-back \uXXXX), and if each escape were padded with ZapChr
// right after its own decoded bytes, the fill bytes would land
// *between every decoded character* -- and for the ZapChr='\0' case
// that's a word separator to ParseWords, so it would shatter one
// multi-character word into a run of bogus one-character "words" at
// index time. So instead: decode a maximal run of consecutive valid
// escapes together, packing the UTF-8 bytes contiguously from the
// run's start, and pad only the leftover slack at the very END of the
// run. The run's start offset -- which is what a GP address for that
// word actually points to -- never moves, so offset preservation
// still holds exactly, regardless of which ZapChr was used.
void ResolveJSONUnicodeEscapes(UCHR* DataBuffer, GPTYPE DataLength, UCHR ZapChr)
{
  GPTYPE Position = 0;

  while (Position < DataLength) {
    void* hit = memchr(DataBuffer + Position, '\\', DataLength - Position);
    if (hit == NULL)
      break; // no more backslashes anywhere in the rest of the buffer

    Position = (GPTYPE)((UCHR*)hit - DataBuffer);

    if (Position + 1 >= DataLength || DataBuffer[Position+1] != 'u') {
      Position++; // ordinary backslash, not a \u escape -- leave it
      continue;
    }
    if (Position + 6 > DataLength) {
      Position++; // truncated at buffer end -- nothing to salvage
      continue;
    }

    int    cp;
    GPTYPE consumed;
    if (!_json_try_decode_escape(DataBuffer, Position, DataLength, &cp, &consumed)) {
      // Looked like \uXXXX (or the start of a surrogate pair) but was
      // broken -- bad hex digits, or an unpaired surrogate. Blank
      // just this one escape so it doesn't tokenize as junk.
      memset(DataBuffer + Position, ZapChr, 6);
      Position += 6;
      continue;
    }

    // Valid first escape -- ride it as far as consecutive valid
    // escapes take us, packing decoded bytes contiguously as we go.
    GPTYPE Read  = Position;
    GPTYPE Write = Position;

    for (;;) {
      UCHR      utf8[4];
      const int n = _utf8_encode((UINT4)cp, utf8);
      memcpy(DataBuffer + Write, utf8, (size_t)n);
      Write += n;
      Read  += consumed;

      if (!_json_try_decode_escape(DataBuffer, Read, DataLength, &cp, &consumed))
        break; // run ends here: buffer end, non-escape, or a malformed one
    }

    if (Write < Read)
      memset(DataBuffer + Write, ZapChr, (size_t)(Read - Write));

    Position = Read;
  }
}
