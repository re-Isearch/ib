//
// HTMLEntities.cc
//
// Implementation of HTMLEntities
//
//
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <memory.h>

#include "common.hxx"
#include "HTMLEntities.hxx"

#define EXPERIMENTAL 1

extern bool _is_globalUTF8;


// ---------------------------------------------------------------------------
// Encode one Unicode scalar value as UTF-8.
// Returns number of bytes written, or 0 for an invalid scalar.
// ---------------------------------------------------------------------------
static size_t _html_ucs4_to_utf8(unsigned long cp, char out[4])
{
  if (cp <= 0x7f)
    {
      out[0] = (char)cp;
      return 1;
    }

  if (cp <= 0x7ff)
    {
      out[0] = (char)(0xc0 | (cp >> 6));
      out[1] = (char)(0x80 | (cp & 0x3f));
      return 2;
    }

  // UTF-16 surrogate range is not valid Unicode scalar data.
  if (cp >= 0xd800 && cp <= 0xdfff)
    return 0;

  if (cp <= 0xffff)
    {
      out[0] = (char)(0xe0 | (cp >> 12));
      out[1] = (char)(0x80 | ((cp >> 6) & 0x3f));
      out[2] = (char)(0x80 | (cp & 0x3f));
      return 3;
    }

  if (cp <= 0x10ffff)
    {
      out[0] = (char)(0xf0 | (cp >> 18));
      out[1] = (char)(0x80 | ((cp >> 12) & 0x3f));
      out[2] = (char)(0x80 | ((cp >> 6) & 0x3f));
      out[3] = (char)(0x80 | (cp & 0x3f));
      return 4;
    }

  return 0;
}


// ---------------------------------------------------------------------------
// Decode one UTF-8 character.
//
// Returns the number of source bytes consumed, or 0 if malformed/truncated.
// ---------------------------------------------------------------------------
static size_t _html_utf8_to_ucs4(const char *p, const char *end, unsigned long *cp)
{
  if (!p || p >= end || !cp)
    return 0;

  const unsigned char c0 = (unsigned char)p[0];

  if (c0 < 0x80)
    {
      *cp = c0;
      return 1;
    }

  // 2-byte sequence
  if (c0 >= 0xc2 && c0 <= 0xdf)
    {
      if (p + 1 >= end)
        return 0;

      const unsigned char c1 = (unsigned char)p[1];

      if ((c1 & 0xc0) != 0x80)
        return 0;

      *cp = ((unsigned long)(c0 & 0x1f) << 6) |
            ((unsigned long)(c1 & 0x3f));

      return 2;
    }

  // 3-byte sequence
  if (c0 >= 0xe0 && c0 <= 0xef)
    {
      if (p + 2 >= end)
        return 0;

      const unsigned char c1 = (unsigned char)p[1];
      const unsigned char c2 = (unsigned char)p[2];

      if ((c1 & 0xc0) != 0x80 ||
          (c2 & 0xc0) != 0x80)
        return 0;

      // Reject overlong sequences.
      if (c0 == 0xe0 && c1 < 0xa0)
        return 0;

      // Reject UTF-16 surrogate range.
      if (c0 == 0xed && c1 >= 0xa0)
        return 0;

      *cp = ((unsigned long)(c0 & 0x0f) << 12) |
            ((unsigned long)(c1 & 0x3f) << 6) |
            ((unsigned long)(c2 & 0x3f));

      return 3;
    }

  // 4-byte sequence
  if (c0 >= 0xf0 && c0 <= 0xf4)
    {
      if (p + 3 >= end)
        return 0;

      const unsigned char c1 = (unsigned char)p[1];
      const unsigned char c2 = (unsigned char)p[2];
      const unsigned char c3 = (unsigned char)p[3];

      if ((c1 & 0xc0) != 0x80 ||
          (c2 & 0xc0) != 0x80 ||
          (c3 & 0xc0) != 0x80)
        return 0;

      // Reject overlong 4-byte sequences.
      if (c0 == 0xf0 && c1 < 0x90)
        return 0;

      // U+10FFFF is the maximum Unicode scalar.
      if (c0 == 0xf4 && c1 > 0x8f)
        return 0;

      *cp = ((unsigned long)(c0 & 0x07) << 18) |
            ((unsigned long)(c1 & 0x3f) << 12) |
            ((unsigned long)(c2 & 0x3f) << 6) |
            ((unsigned long)(c3 & 0x3f));

      return 4;
    }

  return 0;
}


// ---------------------------------------------------------------------------
// Extract an entity name from:
//
//     &copy;
//     &#169;
//     &#x00A9;
//
// entity[] receives "copy", "#169", "#x00A9", etc.
//
// Returns total number of SOURCE bytes occupied by the entity, including
// '&' and ';'. Returns 0 if this is not a well-formed entity.
//
// Keep the historic 10-character entity-name limit.
// ---------------------------------------------------------------------------
static size_t _html_extract_entity(const char *src, const char *end, char entity[12])
{
  if (!src || src >= end || *src != '&')
    return 0;

  const char *p = src + 1;
  size_t i = 0;

  while (p < end && i < 10)
    {
      const unsigned char ch = (unsigned char)*p;

      if (!(isalnum(ch) || ch == '.' || ch == '-' || ch == '#'))
        break;

      entity[i++] = *p++;
    }

  entity[i] = '\0';

  // Same basic length restriction as the existing translate() helper,
  // but require an actual semicolon rather than blindly skipping a byte.
  if (i == 0 || i >= 10)
    return 0;

  if (p >= end || *p != ';')
    return 0;

  return (size_t)(p - src) + 1;       // include ';'
}


// ---------------------------------------------------------------------------
// Decode a numeric entity directly to a Unicode scalar.
//
// entity does NOT contain '&' or ';'.
//
// Supports:
//     #169
//     #xA9 / #XA9
//     #o251               (historic extension)
// ---------------------------------------------------------------------------
static bool _html_numeric_entity(const char *entity, unsigned long *cp)
{
  if (!entity || entity[0] != '#' || !cp)
    return false;

  const char *p = entity + 1;
  int base = 10;

  if (*p == 'x' || *p == 'X')
    {
      base = 16;
      ++p;
    }
  else if (*p == 'o' || *p == 'O')
    {
      base = 8;
      ++p;
    }

  if (!*p)
    return false;

  char *endp = NULL;
  unsigned long value = strtoul(p, &endp, base);

  if (endp == p || *endp != '\0')
    return false;

  if (value > 0x10ffff)
    return false;

  if (value >= 0xd800 && value <= 0xdfff)
    return false;

  *cp = value;
  return true;
}



//*****************************************************************************
HTMLEntities::HTMLEntities()
{
  trans = new Dictionary(131, 10.0f);
  init();
}


//*****************************************************************************
HTMLEntities::~HTMLEntities()
{
  trans->Release();
  delete trans;
}

//*****************************************************************************

static const char *Entities[255];

STRING HTMLEntities::entity(unsigned short Ch) const
{
  if (Ch < 256)
    {
      const char *tp =  Entities[Ch];
      if (tp)
	return STRING("&") + tp + ';';
      return STRING((char)Ch);
    }
  return STRING().form("&#%04d;", Ch);
}


//*****************************************************************************
#if 0
inline const int _convert(const int Ch)
{
  switch (Ch) {
    case 8217: return '\xb4';
    case 8364: return '\x80';  //Euro
    case 8194: case 8195: case 8201: return ' ';
    case 8211: case 8212: return '-';
    case 8216: case 8217: return '\'');
                                        else if(_nChar == 8217) _sChar = _T("'");^M
                                        else if(_nChar == 8218) _sChar = _T("'");^M
                                        else if(_nChar == 8220) _sChar = _T("\"");^M
                                        else if(_nChar == 8221) _sChar = _T("\"");^M
                                        else if(_nChar == 8222) _sChar = _T("\"");^M
                                        else if(_nChar == 8230) _sChar = _T("...");^M
                                        else if(_nChar == 8240) _sChar = _T("o/oo");^M
                                        else if(_nChar == 8249) _sChar = _T("<");^M
                                        else if(_nChar == 8250) _sChar = _T(">");^M
                                        else if(_nChar == 8482) _sChar = _T("(TM)");^M
                                        else if(_nChar == 8722) _sChar = _T("-");^M
                                        else if(_nChar == 8594) _sChar = _T("->");^M
                                        else                                    _nChar = 0;^M
}
#endif


#if 1

int HTMLEntities::translate(const char *entity) const
{
#if EXPERIMENTAL
  long val = -1;
#else
  long val = ' ';
#endif

  if (entity && *entity)
    {
      Object *object;

      if (*entity == '#')
        {
          if (isdigit((unsigned char)entity[1]))
            val = strtol(entity + 1, NULL, 10);

          else if ((entity[1] == 'x' || entity[1] == 'X') &&
                   isxdigit((unsigned char)entity[2]))
            val = strtol(entity + 2, NULL, 16);

          else if (entity[1] == 'o' &&
                   isdigit((unsigned char)entity[2]))
            val = strtol(entity + 2, NULL, 8);

          else
            val = strtol(entity + 1, NULL, 0);

          // Preserve the historical 8-bit behaviour only outside UTF-8 mode.
          if (!_is_globalUTF8 && val == 8364)
            val = 0x80;
        }
      else if ((object = trans->Find(entity)) != NULL)
        {
          val = (long)object;

          // The table stores these two in old Windows-1252 form.
          if (_is_globalUTF8)
            {
              if (strcmp(entity, "euro") == 0)
                val = 0x20ac;
              else if (strcmp(entity, "trade") == 0)
                val = 0x2122;
            }
        }
    }

  if (_is_globalUTF8 &&
      (val < 0 || val > 0x10ffff ||
       (val >= 0xd800 && val <= 0xdfff)))
    return -1;

  return (int)val;
}



#else


int HTMLEntities::translate(const char *entity) const
{
#if EXPERIMENTAL
  long val =  -1;
#else
  long val = ' ';           // Unrecognized entity.  Change it into a space...
#endif
  if (entity && *entity)
    {
      Object *object;
      if (*entity == '#')
	{
	  if (isdigit(entity[1]))
	    return atoi(entity + 1); // This looks like a numeric entity.  That's fine.
	  if ((entity[1] == 'x' || entity[1] == 'X') && isxdigit (entity[2])) // hex?
	    val = (int)strtol(entity+2, (char **)NULL, 16);
          else if (entity[1] == 'o' && isdigit (entity[2])) // oct?
            val = (int)strtol(entity+2, (char **)NULL, 8);
	  else
	    val = (int)strtol(entity+1, (char **)NULL, 0); // What is it?
	  if (val == 8364) val = 0x80; // EURO symbol
	}
      else if ((object = trans->Find(entity)) != NULL)
	val = (long)object;
    }
  return (int)val;
}
#endif

//*****************************************************************************

void HTMLEntities::init()
{
  const struct {
    const char    *entity;
    unsigned char  equiv;
  } entities[] = {
  { "quot",        34},
  { "num",         35},
  { "dollar",      36},
  { "percnt",      37},
  { "amp",         38},
  { "apos",        39}, // <--- NOT in HTML (XML 1.0)
  { "lpar",        40},
  { "rpar",        41},
  { "ast",         42},
  { "plus",        43},
  { "comma",       44},
  { "hyphen",      45},
  { "mdash",       45},
  { "period",      46},
  { "sol",         47},
  { "colon",       58},
  { "semi",        59},
  { "lt",          60}, 
  { "equals",      61},
  { "gt",          62},
  { "quest",       63},
  { "commat",      64},
  { "lsqb",        91},
  { "bsol",        92},
  { "sbsol",       92},
  { "rsqb",        93},
  { "lowbar",      95},
  { "lcub",        123},
  { "verbar",      124},
  { "rcub",        125},
  { "euro",        128},
  { "trade",       153},
  { "nbsp",        ' ' /*160*/},
  { "iexcl",       161},
  { "cent",        162},
  { "pound",       163},
  { "curren",      164},
  { "yen",         165},
  { "brvbar",      166},
  { "sect",        167},
  { "die",         168},
  { "Dot",         168},
  { "uml",         168},
  { "copy",        169},
  { "ordf",        170},
  { "laquo",       171},
  { "not",         172},
  { "shy",         173},
  { "reg",         174},
  { "hibar",       175},
  { "macr",        175},
  { "deg",         176},
  { "plusmn",      177},
  { "sup2",        178},
  { "sup3",        179},
  { "acute",       180},
  { "micro",       181},
  { "para",        182},
  { "middot",      183},
  { "cedil",       184},
  { "sup1",        185},
  { "ordm",        186},
  { "raquo",       187},
  { "frac14",      188},
  { "frac12",      189},
  { "frac34",      190},
  { "iquest",      191},
  { "Agrave",      192},
  { "Aacute",      193},
  { "Acirc",       194},
  { "Atilde",      195},
  { "Auml",        196},
  { "Aring",       197},
  { "AElig",       198},
  { "Ccedil",      199},
  { "Egrave",      200},
  { "Eacute",      201},
  { "Ecirc",       202},
  { "Euml",        203},
  { "Iacute",      204},
  { "Igrave",      205},
  { "Icirc",       206},
  { "Iuml",        207},
  { "ETH",         208},
  { "Ntilde",      209},
  { "Ograve",      210},
  { "Oacute",      211},
  { "Ocirc",       212},
  { "Otilde",      213},
  { "Ouml",        214},
  { "times",       215},
  { "Oslash",      216},
  { "Ugrave",      217},
  { "Uacute",      218},
  { "Ucirc",       219},
  { "Uuml",        220},
  { "Yacute",      221},
  { "THORN",       222},
  { "szlig",       223},
  { "agrave",      224},
  { "aacute",      225},
  { "acirc",       226},
  { "atilde",      227},
  { "auml",        228},
  { "aring",       229},
  { "aelig",       230},
  { "ccedil",      231},
  { "egrave",      232},
  { "eacute",      233},
  { "ecirc",       234},
  { "euml",        235},
  { "igrave",      236},
  { "iacute",      237},
  { "icirc",       238},
  { "iuml",        239},
  { "eth",         240},
  { "ntilde",      241},
  { "ograve",      242},
  { "oacute",      243},
  { "ocirc",       244},
  { "otilde",      245},
  { "ouml",        246},
  { "divide",      247},
  { "oslash",      248},
  { "ugrave",      249},
  { "uacute",      250},
  { "ucirc",       251},
  { "uuml",        252},
  { "yacute",      253},
  { "thorn",       254},
  { "yuml",        255}
  };
#define SIZEOF(X) (sizeof(X)/sizeof(X[0]))
  for (size_t i = 0; i < SIZEOF(entities); i++)
    {
      trans->Add(entities[i].entity, ((Object *) ((long) entities[i].equiv)));
      Entities[i] = entities[i].entity;
    }
}

void HTMLEntities::add(const char *entity, long value)
{
  trans->Add(entity, (Object *)value);
}

//*****************************************************************************
// This method does the same as the translate method, but it will also advance
// the character pointer to the next character after the entity.
int HTMLEntities::translate(size_t *offset, char **entityStart) const
{
   char            entity[12];
   size_t          i = 0;
   char           *orig = *entityStart;
   char           *pos =  orig;


   if (*pos== '&')
      pos++;		// Don't need the '&' that starts the entity

   while ( (isalnum(*pos) || *pos == '.' || *pos == '-' || *pos == '#')
	&& i < 10) {
      entity[i++] =  *pos++;
   }
   entity[i] = '\0';
   if (i >= 10 || i == 0) {
      //
      // This must be a bogus entity.  It can't be more than 10 characters
      // long.  Well, just assume it was an error and return just the '&'.
      //
      *entityStart = orig + 1;
      return '&'; // No need to increment white space
   }
/*
   if (*pos == ';')
      pos++;		// A final ';' is used up.
*/

//cerr << "Translate (" << entity << ")";
 int result = translate (entity); // Translate

//cerr << "--> " << (char)result << " val=" << result << endl;

#if EXPERIMENTAL
  if (result == -1)
    {
      pos = orig;
      result = *orig;
    }
#endif

  // Set the positions
  *offset += pos - orig; // Need extra white space after term
  *entityStart = pos + 1;

  return result; 
}


#if 1

void HTMLEntities::normalize(char *input, size_t len) const
{
  if (!input || len == 0)
    return;

  char       *src = input;
  char       *dst = input;
  const char *end = input + len;

  /*
   * Number of bytes removed by entity contraction which have not yet
   * been restored as spaces.
   *
   * Example:
   *
   *     abc&#233;def
   *
   * In UTF-8:
   *
   *     &#233;       = 6 source bytes
   *     C3 A9        = 2 output bytes
   *
   * therefore pendingPad += 4.
   *
   * We retain those four bytes until the next non-term character so
   * that the indexed word remains:
   *
   *     abcédef
   *
   * instead of:
   *
   *     abcé    def
   *
   * This is the same basic intention as the old pos logic, but without
   * assuming an entity replacement is exactly one byte.
   */
  size_t pendingPad = 0;

  while (src < end)
    {
      /*
       * Preserve the old normalize() behaviour for an embedded NUL:
       * turn it into whitespace and stop processing.
       */
      if (*src == '\0')
        {
          size_t room = (size_t)(end - dst);
          size_t spaces = pendingPad + 1;

          if (spaces > room)
            spaces = room;

          if (spaces)
            memset(dst, ' ', spaces);

          return;
        }

      char          output[4];
      size_t        srcLen = 1;
      size_t        outLen = 1;
      unsigned long cp = 0;

      /*
       * ---------------------------------------------------------------
       * ENTITY
       * ---------------------------------------------------------------
       */
      if (*src == '&')
        {
          char entity[12];

          const size_t entityLen =
              _html_extract_entity(src, end, entity);

          if (entityLen)
            {
              bool translated = false;

              /*
               * UTF-8 mode:
               *
               * Numeric references are Unicode scalar values, so decode
               * them directly.  Do NOT run them through the old 8-bit
               * translate() behaviour.
               */
              if (_is_globalUTF8 && entity[0] == '#')
                {
                  translated =
                      _html_numeric_entity(entity, &cp);
                }
              else
                {
                  const int value = translate(entity);

                  if (value >= 0)
                    {
                      translated = true;

                      if (_is_globalUTF8)
                        {
                          /*
                           * Most of the existing entity table values from
                           * 0x00..0xff map directly to the corresponding
                           * Unicode Latin-1 scalar.
                           *
                           * Two entries are historic Windows-1252 values,
                           * however:
                           *
                           *     euro  = 128
                           *     trade = 153
                           *
                           * In UTF-8 mode they must become the real Unicode
                           * codepoints.
                           */
                          if (strcmp(entity, "euro") == 0)
                            cp = 0x20ac;       // EURO SIGN
                          else if (strcmp(entity, "trade") == 0)
                            cp = 0x2122;       // TRADE MARK SIGN
                          else
                            cp = (unsigned long)value;
                        }
                      else
                        {
                          cp = (unsigned char)value;
                        }
                    }
                }

              if (translated)
                {
                  srcLen = entityLen;

                  if (_is_globalUTF8)
                    {
                      outLen = _html_ucs4_to_utf8(cp, output);

                      /*
                       * Invalid Unicode scalar: treat it as a separator.
                       * It is much safer than leaking malformed bytes into
                       * ParseWordsUTF8.
                       */
                      if (!outLen)
                        {
                          cp = ' ';
                          output[0] = ' ';
                          outLen = 1;
                        }
                    }
                  else
                    {
                      /*
                       * Preserve historical single-byte behaviour outside
                       * global UTF-8 mode.
                       */
                      output[0] = (char)(unsigned char)cp;
                      outLen = 1;
                    }
                }
              else
                {
                  /*
                   * Unknown/bogus entity:
                   *
                   * Preserve the '&' literally and consume only that byte.
                   * The rest will be examined normally on subsequent loops.
                   */
                  cp = '&';
                  output[0] = '&';
                  srcLen = 1;
                  outLen = 1;
                }
            }
          else
            {
              /*
               * Not actually a well-formed entity.
               */
              cp = '&';
              output[0] = '&';
              srcLen = 1;
              outLen = 1;
            }
        }

      /*
       * ---------------------------------------------------------------
       * ORDINARY INPUT
       * ---------------------------------------------------------------
       */
      else if (_is_globalUTF8 &&
               ((unsigned char)*src >= 0x80))
        {
          /*
           * Decode the whole existing UTF-8 character so that:
           *
           *   1. we never classify continuation bytes independently;
           *   2. pending entity padding is not accidentally inserted
           *      inside an existing UTF-8 sequence.
           */
          srcLen = _html_utf8_to_ucs4(src, end, &cp);

          if (srcLen)
            {
              outLen = srcLen;

              for (size_t i = 0; i < srcLen; ++i)
                output[i] = src[i];
            }
          else
            {
              /*
               * Malformed UTF-8 byte in UTF-8 mode.
               *
               * Do not propagate it into ParseWordsUTF8.  Replace one
               * offending byte by a normal separator and continue.
               */
              cp = ' ';
              output[0] = ' ';
              srcLen = 1;
              outLen = 1;
            }
        }
      else
        {
          /*
           * ASCII, or historical single-byte mode.
           */
          cp = (unsigned char)*src;
          output[0] = *src;
          srcLen = 1;
          outLen = 1;
        }


      /*
       * A replacement must never consume fewer input bytes than it emits,
       * because normalization is performed in-place.
       *
       * Standard HTML/XML entities naturally satisfy this:
       *
       *     &#169;      6 bytes -> C2 A9       2 bytes
       *     &#x20AC;    8 bytes -> E2 82 AC    3 bytes
       */
      if (outLen > srcLen)
        {
          /*
           * This could only happen with a pathological/custom entity.
           * Avoid an in-place buffer expansion.
           */
          cp = ' ';
          output[0] = ' ';
          outLen = 1;
        }

      pendingPad += srcLen - outLen;


      /*
       * Determine whether this Unicode codepoint belongs inside a term.
       *
       * IsDotInWord is an ASCII signature test, so only apply it to
       * ASCII codepoints.
       */
      const bool isTerm =
          IsTermChar((int)cp) ||
          (cp < 0x80 &&
           IsDotInWord((unsigned char)cp));


      /*
       * If an entity contracted the input and we have now reached a
       * word boundary, restore the removed bytes as spaces BEFORE the
       * boundary character.
       *
       * Example:
       *
       *     abc&#233;def xyz
       *
       * becomes conceptually:
       *
       *     abcédef...... xyz
       *
       * where the dots represent padding spaces.
       */
      if (pendingPad && !isTerm)
        {
          size_t room = (size_t)(end - dst);
          size_t pad = pendingPad;

          if (pad > room)
            pad = room;

          if (pad)
            {
              memset(dst, ' ', pad);
              dst += pad;
            }

          pendingPad -= pad;
        }


      /*
       * Copy the character/replacement itself.
       */
      {
        size_t room = (size_t)(end - dst);

        if (outLen > room)
          outLen = room;

        if (outLen)
          {
            memcpy(dst, output, outLen);
            dst += outLen;
          }
      }

      src += srcLen;
    }


  /*
   * If the buffer ended while still inside a term, restore any deferred
   * entity-contraction bytes at the end of the term.
   */
  if (pendingPad && dst < end)
    {
      size_t room = (size_t)(end - dst);

      if (pendingPad > room)
        pendingPad = room;

      memset(dst, ' ', pendingPad);
      dst += pendingPad;
    }


  /*
   * Normally dst == end here.  This final fill is defensive and ensures
   * that no stale bytes survive if malformed input caused an unusual
   * contraction.
   */
  if (dst < end)
    memset(dst, ' ', (size_t)(end - dst));
}


#else

void HTMLEntities::normalize(char *input, size_t len) const
{
  size_t          pos   = 0;
  size_t          count = 0;
  REGISTER char  *buffer= input;
  REGISTER char  *ptr   = buffer;

  do {
    if (*ptr == '\0')
      {
	*buffer++ = ' ';
	ptr++;
	break; // Don't bother doing more!
      }
    else
      {
	unsigned char ch;
	ch = (unsigned char)(*buffer = ((*ptr == '&') ? translate(&pos, &ptr) : *ptr++));
	if (pos && !IsTermChar(ch) && !IsDotInWord(ch))
	  {
	    memset(buffer, ' ', pos);
	    buffer[pos] = (char)ch;
	    buffer += pos + 1;
	    count  += pos;
	    pos = 0;
          }
	else
	  buffer++;
      }
  } while (++count < len);
  if (ptr > buffer)
    memset(buffer, ' ', ptr - buffer - 1); // Added -1
}
#endif


void HTMLEntities::normalize2(char *input, size_t len) const
{
  size_t          pos   = 0;
  size_t          count = 0;
  REGISTER char  *buffer= input;
  REGISTER char  *ptr   = buffer;

 if (len > 0) // Need something to normalize
  do {
    if (*ptr == '\0')
      {
	*buffer++ = ' ';
	ptr++;
	break; // Don't bother doing more!
      }
    else
      {
	*buffer = ((*ptr == '&') ? translate(&pos, &ptr) : *ptr++);
	if (pos && (isspace((unsigned char)*buffer) || *buffer == '\0'))
	  {
	    char ch = *buffer;
	    memset(buffer, ' ', pos);
	    buffer[pos] = ch;
	    buffer += pos + 1;
	    count  += pos;
	    pos = 0;
          }
	else
	  buffer++;
      }
  } while (++count < len);
  if (ptr > buffer)
    memset(buffer, ' ', ptr - buffer - 1); // Added -1
}



void HTMLEntities::normalize(char *buffer) const
{
  size_t pos = 0;
  char *ptr = buffer;
  while (*ptr)
    {
      if (*ptr == '&')
	*buffer = translate(&pos, &ptr);
      else
	*buffer = *ptr++;
      if (pos && isspace((unsigned char)*buffer))
	{
	  memset(buffer, ' ', pos+1);
	  buffer += pos + 1;
	  pos = 0;
	}
      else buffer++;
    }
  if (ptr > buffer)
    memset(buffer, ' ', ptr - buffer - 1); // Added -1
}

#if DEBUG

main(int argc, char **argv)
{
  char tmp[1024];
  HTMLEntities Test;

  strcpy(tmp, argv[1] );
  printf("Input = '%s'\n", argv[1]);
  Test.normalize(tmp) ;
  printf("Output= '%s'\n", tmp ); 
}

#endif
