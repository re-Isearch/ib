#pragma once
#include <stdlib.h>

#ifndef _SP
# define _SP ' '
#endif

unsigned char *_utf_StrToLower(unsigned char *pString, const bool clean, unsigned length = 0);
unsigned char *_utf_StrToUpper(unsigned char *pString, unsigned length = 0);
unsigned char *_utf_StrToUpperBuf(unsigned char *pString, unsigned length, const bool clean, unsigned char ZapChr = _SP);
unsigned char *_utf_StrToLowerBuf(unsigned char *pString, unsigned length, const bool clean, unsigned char ZapChr = _SP);


enum TERM_STATE { TERM_START, TERM_INSIDE, TERM_AFTER_DOT };

int _ib_IsUTF8TermChr(const unsigned char *Buffer);
INT _utf_strncasecmp(const unsigned char *p1, const unsigned char *p2, const int n, bool *look = NULL, size_t *p2_bytes = NULL);
void _ib_ZapNonTermASCII(unsigned char *pString, unsigned length, unsigned char ZapChr);
int _ib_IsUTF8TermChrFast(const unsigned char *Buffer, const unsigned char *End,
	unsigned char ZapChr = _SP, TERM_STATE state = TERM_START);

uint32_t to_ucs4(const unsigned char *chr, uint32_t *cp);
int      _to_ucs4(const unsigned char *chr, const unsigned char *end, uint32_t *cp);


inline size_t _ib_UTF8CharBytes(const unsigned char *p, size_t left)
{
  if (p == NULL || left == 0 || *p == '\0')
    return 0;

  if (*p < 0x80)
    return 1;

  if ((*p & 0xE0) == 0xC0)
    return left >= 2 ? 2 : 1;

  if ((*p & 0xF0) == 0xE0)
    return left >= 3 ? 3 : 1;

  if ((*p & 0xF8) == 0xF0)
    return left >= 4 ? 4 : 1;

  // Invalid lead/continuation byte: consume one byte safely.
  return 1;
}
