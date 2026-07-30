#pragma once

#include <algorithm>
#include <cassert>
#include <memory>
#include <vector>

struct FcLess
{
  bool operator()(const FC& a, const FC& b) const
  {
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
  using container_type  = std::vector<FC>;
  using const_iterator  = container_type::const_iterator;

  FCHITS() = default;

  explicit FCHITS(const FC& fc)
    : Buffer(1, fc)
  {
  }

  void Reserve(size_t count)
  {
    Buffer.reserve(count);
  }

  void Clear()
  {
    // Retain capacity when this storage is uniquely owned.
    Buffer.clear();
    Normalized = true;
  }

  void AddEntryFast(const FC& hit)
  {
    if (!Buffer.empty())
      {
        if (Buffer.back() == hit)
          return;

        if (FcLess()(hit, Buffer.back()))
          Normalized = false;
      }

    Buffer.push_back(hit);
  }

  bool IsNormalized() const
  {
    return Normalized;
  }

  void Normalize()
  {
    if (Normalized)
      return;

    std::sort(Buffer.begin(), Buffer.end(), FcLess());

    Buffer.erase(
        std::unique(Buffer.begin(), Buffer.end()),
        Buffer.end());

    Normalized = true;
  }

  // Both collections must already be normalized.
  void MergeSortedUnique(const FCHITS& other)
  {
    assert(Normalized);
    assert(other.Normalized);

    container_type merged;
    merged.reserve(Buffer.size() + other.Buffer.size());

    size_t left  = 0;
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
            candidate = &Buffer[left];
            ++left;
            ++right;
          }

        if (merged.empty() || merged.back() != *candidate)
          merged.push_back(*candidate);
      }

    Buffer.swap(merged);
    Normalized = true;
  }

  size_t GetTotalEntries() const
  {
    return Buffer.size();
  }

  size_t Size() const
  {
    return Buffer.size();
  }

  bool IsEmpty() const
  {
    return Buffer.empty();
  }

  const_iterator begin() const { return Buffer.begin(); }
  const_iterator end() const   { return Buffer.end(); }

  void Write(PFILE fp) const;

private:
  container_type Buffer;

  // True means both sorted and duplicate-free.
  bool Normalized = true;
};

class HITTABLE
{
public:
  using const_iterator = FCHITS::const_iterator;

  HITTABLE()
    : p_(std::make_shared<FCHITS>())
  {
  }

  HITTABLE(const FC& hit)
    : p_(std::make_shared<FCHITS>(hit))
  {
  }

  HITTABLE(const HITTABLE&) = default;
  HITTABLE(HITTABLE&&) = default;

  HITTABLE& operator=(const HITTABLE&) = default;
  HITTABLE& operator=(HITTABLE&&) = default;

  size_t GetTotalEntries() const
  {
    return Readable().GetTotalEntries();
  }

  size_t Size() const
  {
    return Readable().Size();
  }

  bool IsEmpty() const
  {
    return Readable().IsEmpty();
  }

  bool IsNormalized() const
  {
    return Readable().IsNormalized();
  }

  const_iterator begin() const
  {
    return Readable().begin();
  }

  const_iterator end() const
  {
    return Readable().end();
  }

  void Reserve(size_t count)
  {
    Writable().Reserve(count);
  }

  void Clear()
  {
    if (!p_ || p_.use_count() != 1)
      {
        // No reason to copy shared contents merely to erase them.
        p_ = std::make_shared<FCHITS>();
      }
    else
      {
        // Preserve capacity when uniquely owned.
        p_->Clear();
      }
  }

  void AddEntryFast(const FC& hit)
  {
    Writable().AddEntryFast(hit);
  }

  void Normalize()
  {
    if (!Readable().IsNormalized())
      Writable().Normalize();
  }

  void MergeSortedUnique(const HITTABLE& other)
  {
    Normalize();

    if (other.IsNormalized())
      {
        Writable().MergeSortedUnique(other.Readable());
      }
    else
      {
        // Preserve the const source while satisfying the merge contract.
        HITTABLE normalized(other);
        normalized.Normalize();

        Writable().MergeSortedUnique(normalized.Readable());
      }
  }

  void Write(PFILE fp) const
  {
    Readable().Write(fp);
  }

private:
  const FCHITS& Readable() const
  {
    if (p_)
      return *p_;

    // Makes a moved-from HITTABLE behave as an empty table.
    static const FCHITS empty;
    return empty;
  }

  FCHITS& Writable()
  {
    if (!p_)
      {
        p_ = std::make_shared<FCHITS>();
      }
    else if (p_.use_count() != 1)
      {
        p_ = std::make_shared<FCHITS>(*p_);
      }

    return *p_;
  }

  std::shared_ptr<FCHITS> p_;
};
