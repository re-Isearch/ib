/*
Copyright (c) 2020-26 Project re-Isearch and its contributors: See CONTRIBUTORS.
It is made available and licensed under the Apache 2.0 license: see LICENSE
*/
#ifndef MERGEUNIT_HXX
#define MERGEUNIT_HXX

#include "defs.hxx"
#include "string.hxx"
#include "lang-codes.hxx"
#include <algorithm>
#include <array>
#include <vector>

// One sequential SIS/INX input. Keys are already folded; source records are
// never opened. Posting bytes retain the byte order recorded in the INX magic.
class MERGEUNIT {
public:
  MERGEUNIT() = default;
  ~MERGEUNIT();
  MERGEUNIT(const MERGEUNIT&) = delete;
  MERGEUNIT& operator=(const MERGEUNIT&) = delete;

  bool Open(const STRING& index, const STRING& sis, UINT2 magic);
  bool Advance();
  bool CopyPostings(FILE *output, std::vector<UCHR>& buffer);
  bool Empty() const { return !valid; }
  const UCHR *Key() const { return row.data()+1; }
  size_t KeyLength() const { return std::min(size_t(row[0]), width); }
  const UCHR *Row() const { return row.data(); }
  size_t Width() const { return width; }
  BYTE CharsetId() const { return charsetId; }
  GPTYPE PostingCount() const { return postingCount; }
  GPTYPE HeadCount() const { return headEnd-nextPosting+1; }

private:
  bool Error(const char *reason) const;
  FILE *indexFp = nullptr, *sisFp = nullptr;
  STRING indexName, sisName;
  std::vector<char> indexBuffer, sisBuffer;
  std::vector<UCHR> row;
  size_t width = 0, remainingRows = 0;
  BYTE charsetId = 0;
  CHARSET charset;
  GPTYPE postingCount = 0, nextPosting = 0, headEnd = 0;
  bool valid = false;
};

#endif
