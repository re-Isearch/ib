/* Copyright (c) 2026 Project CoreQuarry and its contributors: See CONTRIBUTORS.
It is made available and licensed under the Apache 2.0 license: see LICENSE */


#include <limits>

#include "fchits.hxx"
#include "magic.hxx"


void FCHITS::Write(FILE* fp) const
{
  const size_t total = GetTotalEntries();
  const size_t persistentMaximum =
      static_cast<size_t>(std::numeric_limits<UINT4>::max());

  // Hit count is per record. UINT2 would normally suffice, but UINT4
  // avoids an artificial 65535-hit limit. UINT8 provides no practical
  // value for this persistent representation.
  if (total > persistentMaximum)
    {
      message_log(
          LOG_PANIC,
          "FCHITS has %llu entries; persistent format supports only UINT4",
          static_cast<unsigned long long>(total));
      return;
    }

  ::Write(static_cast<UCHR>(objHITS), fp);
  ::Write(static_cast<UINT4>(total), fp);

  for (const hit_type& hit : *this)
    hit.Write(fp);
}

bool FCHITS::Read(FILE* fp)
{
  if (fp == NULL)
    return false;

  const obj_t obj = getObjID(fp);
  if (obj != objHITS)
    {
      PushBackObjID(obj, fp);
      return false;
    }

  UINT4 newTotal = 0;

  ::Read(&newTotal, fp) ;
  // if (newTotal == 0) return false;

  container_type replacement;
  replacement.reserve(static_cast<size_t>(newTotal));

  bool sorted = true;
  bool unique = true;
  const FcLess less;

  for (UINT4 i = 0; i < newTotal; ++i)
    {
      hit_type fc;

      if (!fc.Read(fp))
        return false;

      if (!replacement.empty())
        {
          const FC& previous = replacement.back();

          if (unique && previous == fc)
	    unique = false;
	  if (sorted && less(fc, previous)) {
	    sorted = false;
	    unique = false;
	  }
        }

      // Preserve the serialized data exactly, including duplicates.
      replacement.push_back(fc);
    }

  Buffer.swap(replacement);
  Sorted = sorted;
  Unique = unique;

  return true;
}


FCHITS::FCHITS(const FCLIST& list)
{
  Assign(list);
}


FCHITS& FCHITS::operator=(const FCLIST& list)
{
  Assign(list);
  return *this;
}


void FCHITS::Assign(const FCLIST& list)
{
  container_type replacement;
  replacement.reserve(list.GetTotalEntries());

  bool sorted = true;
  bool unique = true;
  const FcLess less;

  for (const hit_type& fc : list)
    {
      if (!replacement.empty())
        {
          const hit_type& previous = replacement.back();

          if (unique && previous == fc)
	    unique = false;
	  if (sorted && less(fc, previous)) {
            sorted = false;
	    unique = false; // Can't tell
	  }
        }

      // Preserve the FCLIST exactly. AddEntryFast() intentionally
      // suppresses adjacent duplicates and therefore is not used here.
      replacement.push_back(fc);
    }

  Buffer.swap(replacement);
  Sorted = sorted;
  Unique = unique;
}


void FCHITS::CopyTo(FCLIST* list) const
{
  if (list == NULL)
    return;

  list->Clear();

  for (const FC& fc : Buffer)
    list->AddEntry(fc);
}


void FCHITS::Append(const FCHITS& other)
{
  if (this == &other)
    {
      const container_type copy(Buffer);

      for (const hit_type& fc : copy)
        AddEntryFast(fc);

      return;
    }

  Reserve(Buffer.size() + other.Buffer.size());

  for (const hit_type& fc : other.Buffer)
    AddEntryFast(fc);
}


void FCHITS::Append(const FCT& other)
{
  Reserve(Buffer.size() + other.GetTotalEntries());

  for (const FC& fc : other)
    AddEntryFast(fc);
}


void FCHITS::RecalculateState()
{
  Sorted = true;
  Unique = true;

  if (Buffer.size() < 2)
    return;

  const FcLess less;

  for (size_t i = 1; i < Buffer.size(); ++i)
    {
      const hit_type& previous = Buffer[i - 1];
      const hit_type& current  = Buffer[i];

      if (previous == current || less(current, previous))
        {
	  Sorted = false;
	  Unique = false;
          return;
        }
    }
}


void FCHITS::Assign(const FCT& table)
  {
    container_type replacement;
    replacement.reserve(table.GetTotalEntries());
  
    for (const FC& fc : table)
      replacement.push_back(fc);
      
    Buffer.swap(replacement);
    RecalculateState(); 
  }   


FCHITS& FCHITS::operator=(const hit_type& fc)
{
  Buffer.clear();       // Retain existing capacity
  Buffer.push_back(fc);
  Sorted = true;
  Unique = true;
  return *this;
}
