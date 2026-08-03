#pragma once

/* Copyright (c) 2026 Project CoreQuarry and its contributors: See CONTRIBUTORS.
It is made available and licensed under the Apache 2.0 license: see LICENSE */


#include <algorithm>
#include <cassert>
#include <memory>
#include <vector>

#include "ib_defs.hxx"
#include "fct.hxx"

class FCLIST;

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
  using container_type = std::vector<FC>;
  using const_iterator = container_type::const_iterator;

  FCHITS() = default;

  explicit FCHITS(const FC& fc)
    : Buffer(1, fc), Unique(true), Sorted(true)
  {
  }


  explicit FCHITS(const FCLIST& list);

  FCHITS& operator=(const FCLIST& list);
  FCHITS& operator=(const FC& fc);

  FCHITS& operator-=(GPTYPE offset)
  {
    if (offset == 0)
      return *this;
    for (FC& fc : Buffer)
      fc -= offset;
    /*
     * Subtracting the same constant from every coordinate preserves:
     *   - order
     *   - equality
     *   - normalization
     */
    return *this;
  }

  void SortByFc()
  {
    if (Sorted)
      return;

    std::sort(Buffer.begin(), Buffer.end(), FcLess());
    Sorted = true;

    // Sorting does not guarantee uniqueness.
    Unique = std::adjacent_find(Buffer.begin(), Buffer.end()) == Buffer.end();
  }

  void Assign(const FCLIST& list);
  void Assign(const FCT& table);

  void CopyTo(FCLIST* list) const;

  void Reserve(size_t count)
  {
    Buffer.reserve(count);
  }

 size_t Capacity() const
  {
   return Buffer.capacity();
  }

  void Clear()
  {
    // Retain capacity when this storage is uniquely owned.
    Buffer.clear();
    Unique = true;
    Sorted = true;
  }

  void AddEntryFast(const FC& hit)
  {
    if (!Buffer.empty())
    {
      const FC& previous = Buffer.back();

      if (previous == hit)
        return;

      if (FcLess()(hit, previous))
      {
        Sorted = false;
        Unique = false; // uniqueness can no longer be proven cheaply
      }
      else if (!Sorted)
      {
        Unique = false;
      }
    }

    Buffer.push_back(hit);
  }

  void Append(const FCHITS& other) ;
  void Append(const FCT& other);


  // GetEntry(1) / SetEntry(1, fc)   legacy 1-based
  // hits[0] / hits.At(0)            modern 0-based

  // Coventional C/C++ style 0-based
  const FC& operator[](size_t index) const { return Buffer[index];    }
  const FC& At(size_t index) const         { return Buffer.at(index); }

  // These are 1-based!
  bool GetEntry(const size_t Index, FC* FcRecord) const
  {
    if (FcRecord == NULL || Index == 0 || Index > Buffer.size())
      return false;

    *FcRecord = Buffer[Index - 1];
    return true;
  }
  const FC& GetEntry(const size_t Index) const
  {
    if (Index != 0 && Index <= Buffer.size())
      return Buffer[Index - 1];

    // Legacy invalid-entry sentinel.
    static const FC EmptyFc;
    return EmptyFc;
  }

  bool SetEntry(size_t Index, const FC& fc)
  {
    if (Index == 0 || Index > Buffer.size())
      return false;

    Buffer[Index - 1] = fc;

    // Replacement may alter ordering or introduce duplicates.
    Sorted = false;
    Unique = false;

    return true;
  }


  bool IsSorted() const     { return Sorted; }
  bool IsNormalized() const { return Sorted && Unique; }

  void Normalize()
  {
    if (!Sorted)
     {
        // We use std::sort rather than a custom radix sort  because even
        // with 65k hits (an absurdly high umber of hits):
        // 65,536 × log2(65,536) ≈ 1,048,576 comparisons
        // which is comparatively small on modern hardware
        std::sort(Buffer.begin(), Buffer.end(), FcLess());
        Sorted = true;
    }

  if (!Unique)
    {
      Buffer.erase(
          std::unique(Buffer.begin(), Buffer.end()),
          Buffer.end());

      Unique = true;
    }
  }

  void MergeEntries() { Normalize(); }

  // Both collections must already be normalized.
  void MergeSortedUnique(const FCHITS& other)
  {
    assert(IsNormalized());
    assert(other.IsNormalized());

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
    Sorted = true;
    Unique = true;
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

  void Write(FILE* fp) const;
  bool Read( FILE* fp);

private:
  void RecalculateState();

  container_type Buffer;

  bool Unique = true;
  bool Sorted = true; 
};


class HITTABLE
{
public:
  using const_iterator = FCHITS::const_iterator;

  // Append-only adapter for external hit producers.
  class SINK
  {
  public:
    explicit SINK(HITTABLE& table)
      : Table(table)
    {
    }

    void AddEntry(const FC& hit)
    {
      Table.AddEntryFast(hit);
    }

    void operator()(const FC& hit)
    {
      Table.AddEntryFast(hit);
    }

    SINK& operator<<(const FC& hit)
    {
      Table.AddEntryFast(hit);
      return *this;
    }

  private:
    HITTABLE& Table;
  };

  HITTABLE()
    : p_(std::make_shared<FCHITS>())
  {
  }

  HITTABLE(const FC& hit)
    : p_(std::make_shared<FCHITS>(hit))
  {
  }

  explicit HITTABLE(const FCLIST& list)
    : p_(std::make_shared<FCHITS>(list))
  {
  }

  HITTABLE(const HITTABLE&) = default;
  HITTABLE(HITTABLE&&) = default;

  HITTABLE& operator=(const HITTABLE&) = default;
  HITTABLE& operator=(HITTABLE&&) = default;

  HITTABLE& operator=(const FCLIST& list)
  {
    p_ = std::make_shared<FCHITS>(list);
    return *this;
  }
  HITTABLE& operator=(const FC& fc)
  {
    FCHITS& hits = Writable();
    hits = fc;
    return *this;
  }

  // Bridge FCT
  HITTABLE& operator=(const FCT& table)
  {
    auto replacement = std::make_shared<FCHITS>();
    replacement->Assign(table.GetFCLIST());
    p_ = std::move(replacement);
    return *this;
  }

  HITTABLE& operator-=(GPTYPE offset)
  {
    if (offset != 0 && !IsEmpty())
      Writable() -= offset;

    return *this;
  }

  bool GetEntry(const size_t Index, FC* FcRecord) const
  {
    return Readable().GetEntry(Index, FcRecord);
  }


  const FC& GetEntry(const size_t Index) const
  {
    return Readable().GetEntry(Index);
  }

  HITTABLE& SortByFc()
  {
    if (!Readable().IsSorted())
      Writable().SortByFc();
    return *this;
  }

  void Assign(const FCT& table);

  void CopyTo(FCLIST* list) const
  {
    Readable().CopyTo(list);
  }

  size_t Capacity() const
  {
    return Readable().Capacity();
  }

  void Reserve(size_t count)
  {
    // Do not detach when the existing shared storage already has room.
    if (count > Readable().Capacity())
      Writable().Reserve(count);
  }

  SINK GetSink()
  {
    return SINK(*this);
  }

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

  // For backwards compatability
  void AddEntry(const FC& hit)
  {
    AddEntryFast(hit);
  }
  void AddEntry(const FCT& other)
  {
    Writable().Append(other);
  }

  void AddEntry(const HITTABLE& other)
  {
    if (other.IsEmpty())
      return;

    // Protect self-append because vector growth invalidates its iterators.
    if (this == &other)
      {
        const FCHITS snapshot(Readable());
        Writable().Append(snapshot);
        return;
      }

    /*
     * If two distinct handles share the same backing store, Writable()
     * detaches this object first, leaving 'other' on the original store.
     */
    Writable().Append(other.Readable());
  }

  void Normalize()
  {
    if (!Readable().IsNormalized())
      Writable().Normalize();
  }

  void MergeEntries() { Normalize(); }


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

  void Write(FILE* fp) const
  {
    Readable().Write(fp);
  }

  bool Read(FILE* fp)
  {
    auto replacement = std::make_shared<FCHITS>();

    if (!replacement->Read(fp))
      return false;

    p_ = std::move(replacement);
    return true;
  }


  bool IsSorted() const
  {
    return Readable().IsNormalized();
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
