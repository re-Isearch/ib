#pragma once
#include <stdlib.h>

#ifndef _SP
# define _SP ' '
#endif

unsigned char *_utf_StrToLower(unsigned char *pString, const bool clean, unsigned length = 0);
unsigned char *_utf_StrToUpper(unsigned char *pString, unsigned length = 0);
unsigned char *_utf_StrToUpperBuf(unsigned char *pString, unsigned length, const bool clean, unsigned char ZapChr = _SP);
unsigned char *_utf_StrToLowerBuf(unsigned char *pString, unsigned length, const bool clean, unsigned char ZapChr = _SP);

int _ib_IsUTF8TermChr(const unsigned char *Buffer);
INT _utf_strncasecmp(const unsigned char *p1, const unsigned char *p2, const int n, bool *look = NULL, size_t *p2_bytes = NULL);
void _ib_ZapNonTermASCII(unsigned char *pString, unsigned length, unsigned char ZapChr);
int _ib_IsUTF8TermChrFast(const unsigned char *Buffer, const unsigned char *End, unsigned char ZapChr = _SP);

uint32_t to_ucs4(const unsigned char *chr, uint32_t *cp);
