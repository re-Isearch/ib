#pragma once
#include <algorithm>
#include <vector>

struct FcLess
{
  bool operator()(const FC& a, const FC& b) const
  {
    // Preserve the existing FctFcCompare ordering:
    // field end ascending, then field start descending.
    if (a.GetFieldEnd() < b.GetFieldEnd())
      return true;

    if (a.GetFieldEnd() > b.GetFieldEnd())
      return false;

    return a.GetFieldStart() > b.GetFieldStart();
  }
};

class FCHITS
{
public:
  typedef std::vector<FC>::const_iterator const_iterator;

  void Reserve(size_t count)
  {
    Buffer.reserve(count);
  }

  void Clear()
  {
    // Retains allocated capacity for reuse.
    Buffer.clear();
    Sorted = true;
  }

  void AddEntryFast(const FC& record)
  {
    if (!Buffer.empty())
      {
        if (Buffer.back() == record)
          return;

        if (FcLess()(record, Buffer.back()))
          Sorted = false;
      }

    Buffer.push_back(record);
  }

  void Normalize()
  {
    if (!Sorted)
      std::sort(Buffer.begin(), Buffer.end(), FcLess());

    Buffer.erase(
      std::unique(Buffer.begin(), Buffer.end()),
      Buffer.end());

    Sorted = true;
  }

  // Both inputs should be normalized before this operation.
  void MergeSortedUnique(const FCHITS& other)
  {
    std::vector<FC> merged;
    merged.reserve(Buffer.size() + other.Buffer.size());

    size_t left = 0;
    size_t right = 0;
    const FcLess less;

    while (left < Buffer.size() || right < other.Buffer.size())
      {
        const FC* candidate;

        if (right == other.Buffer.size() ||
            (left < Buffer.size() &&
             less(Buffer[left], other.Buffer[right])))
          {
            candidate = &Buffer[left++];
          }
        else if (left == Buffer.size() ||
                 less(other.Buffer[right], Buffer[left]))
          {
            candidate = &other.Buffer[right++];
          }
        else
          {
            // Equal element in both inputs.
            candidate = &Buffer[left];
            ++left;
            ++right;
          }

        if (merged.empty() || merged.back() != *candidate)
          merged.push_back(*candidate);
      }

    Buffer.swap(merged);
    Sorted = true;
  }

  size_t Size() const
  {
    return Buffer.size();
  }

  const_iterator begin() const { return Buffer.begin(); }
  const_iterator end() const   { return Buffer.end(); }

private:
  std::vector<FC> Buffer;
  bool Sorted = true;
};
