/*
Copyright (c) 2020-26 Project re-Isearch and its contributors: See CONTRIBUTORS.
It is made available and licensed under the Apache 2.0 license: see LICENSE
*/
#ifndef MERGE_HXX
#define MERGE_HXX

#include "defs.hxx"
#include "string.hxx"
#include <vector>

// Buffered multiway zipper. All state belongs to this job; publication and
// shard scheduling belong to INDEX. Intermediate runs are never published.
class MERGE {
public:
  struct INPUT { STRING index, sis; };
  struct STATS {
    GPTYPE postings = 0;
    size_t terms = 0, passes = 0, comparisons = 0;
  };
  static constexpr size_t MaxFanIn = 64;

  bool Zip(const std::vector<INPUT>& inputs, const INPUT& output, UINT2 magic,
           unsigned long long capacity, long commonThreshold,
           std::vector<STRING> *commonWords = nullptr, STATS *stats = nullptr);
  // Called only after INDEX has established that an optimizer owner is dead.
  static void RemoveTemporaries(const INPUT& output);
};

#endif
