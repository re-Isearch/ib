/*
Copyright (c) 2020-26 Project re-Isearch and its contributors: See CONTRIBUTORS.
It is made available and licensed under the Apache 2.0 license: see LICENSE
*/
#include "mergeunit.hxx"
#include "common.hxx"
#include <algorithm>
#include <cstring>
#include <limits>

MERGEUNIT::~MERGEUNIT()
{
  if (indexFp) fclose(indexFp);
  if (sisFp) fclose(sisFp);
}

bool MERGEUNIT::Error(const char *reason) const
{
  message_log(LOG_ERROR, "Merge: %s ('%s', '%s')", reason, indexName.c_str(), sisName.c_str());
  return false;
}

bool MERGEUNIT::Open(const STRING& index, const STRING& sis, UINT2 magic)
{
  if (indexFp || sisFp) return Error("input was opened twice");
  indexName = index; sisName = sis;
  indexFp = fopen(index, "rb");
  sisFp = fopen(sis, "rb");
  if (!indexFp || !sisFp) return Error("cannot open input");

  indexBuffer.resize(128*1024);
  sisBuffer.resize(64*1024);
  if (setvbuf(indexFp, indexBuffer.data(), _IOFBF, indexBuffer.size()) != 0 ||
      setvbuf(sisFp, sisBuffer.data(), _IOFBF, sisBuffer.size()) != 0)
    return Error("cannot allocate input buffering");

  const off_t indexSize = GetFileSize(indexFp), sisSize = GetFileSize(sisFp);
  if (indexSize < 2 || (indexSize-2) % sizeof(GPTYPE) || sisSize < 2)
    return Error("truncated input or invalid posting width");
  if (static_cast<unsigned long long>((indexSize-2)/sizeof(GPTYPE)) >
      std::numeric_limits<GPTYPE>::max())
    return Error("input exceeds posting address capacity");
  UCHR header[2];
  if (fread(header, 1, 2, indexFp) != 2 ||
      UINT2((UINT2(header[0]) << 8) | header[1]) != magic)
    return Error("incompatible index magic or posting byte order");
  if (fread(header, 1, 2, sisFp) != 2) return Error("truncated SIS header");
  width = header[0]; charsetId = header[1];
  charset = charsetId;
  const size_t rowSize = width+1+sizeof(GPTYPE);
  if (!width || !charset.Ok() || (sisSize-2) % rowSize)
    return Error("invalid SIS header or row size");
  postingCount = GPTYPE((indexSize-2)/sizeof(GPTYPE));
  remainingRows = size_t((sisSize-2)/rowSize);
  row.resize(rowSize);
  return Advance();
}

bool MERGEUNIT::Advance()
{
  std::array<UCHR, 255> previous;
  size_t previousLength = 0;
  const bool hadPrevious = valid;
  if (hadPrevious) {
    previousLength = KeyLength();
    std::memcpy(previous.data(), Key(), previousLength);
    nextPosting = headEnd+1;
  }
  valid = false;
  if (!remainingRows)
    return nextPosting == postingCount || Error("SIS does not cover all postings");
  if (fread(row.data(), 1, row.size(), sisFp) != row.size())
    return Error("short SIS read");
  --remainingRows;
  headEnd = getGPTYPE(row.data(), width+1);
  if (headEnd < nextPosting || headEnd >= postingCount)
    return Error("invalid or non-monotonic posting endpoint");
  if (hadPrevious &&
      charset.CompareSIS(previous.data(), previousLength, Key(), KeyLength()) > 0)
    return Error("SIS keys are out of order");
  valid = true;
  return true;
}

bool MERGEUNIT::CopyPostings(FILE *output, std::vector<UCHR>& buffer)
{
  if (!valid || buffer.empty()) return Error("invalid posting copy");
  // Avoid multiplying an entire list length by sizeof(GPTYPE) in size_t.
  GPTYPE remaining = HeadCount();
  const size_t perBlock = buffer.size()/sizeof(GPTYPE);
  if (!perBlock) return Error("posting buffer is too small");
  while (remaining) {
    const size_t count = size_t(std::min<GPTYPE>(remaining, perBlock));
    if (fread(buffer.data(), sizeof(GPTYPE), count, indexFp) != count)
      return Error("short posting read");
    if (fwrite(buffer.data(), sizeof(GPTYPE), count, output) != count)
      return Error("short posting write");
    remaining -= count;
  }
  return true;
}
