/*
Copyright (c) 2020-21 Project re-Isearch and its contributors: See CONTRIBUTORS.
It is made available and licensed under the Apache 2.0 license: see LICENSE
*/
#pragma ident  "@(#)fcache.cxx  1.19 02/24/01 17:32:27 BSN"

//#include <arpa/inet.h>
#include "common.hxx"
#include "fcache.hxx"


static inline GPTYPE GP(const GPTYPE *ptr, int y = 0)
{
# ifdef O_BUILD_IB64
  const BYTE *x = (const BYTE *)ptr;
  return (((GPTYPE)x[y])   << 56) + (((GPTYPE)x[y+1]) << 48) +
         (((GPTYPE)x[y+2]) << 40) + (((GPTYPE)x[y+3]) << 32) +
         (((GPTYPE)x[y+4]) << 24) + (((GPTYPE)x[y+5]) << 16) +
          (((GPTYPE)x[y+6]) << 8) + x[y+7];

# else
  const BYTE *x = (const BYTE *)ptr;
  return (((GPTYPE)x[y])   << 24) + (((GPTYPE)x[y+1]) << 16) +
         (((GPTYPE)x[y+2]) << 8)  + x[y+3];
#endif
}

#define GpOf(_x) GP(&_x)


FCACHE::FCACHE(const PIDBOBJ parent)
{
  Parent = parent;
  Fp = NULL;
}

FCACHE::FCACHE(const PIDBOBJ parent, const STRING& fieldName, bool useDisk)
{
  Parent = parent;
  Fp = NULL;
  LoadFieldCache(fieldName, useDisk);
}

FCACHE::~FCACHE ()
{
  if (Fp) Parent->ffclose(Fp);
}



bool FCACHE::GetFieldName(STRING *fieldNamePtr) const
{
  if (fieldNamePtr)
    *fieldNamePtr = FieldName;
  return !(FieldTotal == 0 || FieldName.IsEmpty());
}

bool FCACHE::SetFieldName(const STRING& fieldName, bool useDisk)
{
  return LoadFieldCache(fieldName, useDisk) != 0;
}

// Load the Attribute Cache..
#if 1

size_t FCACHE::LoadFieldCache(const STRING& fieldName, bool useDisk)
{
  if (!FieldName.CaseEquals(fieldName))
    {
      STRING Fn;

      FieldName   = fieldName;
      FieldTotal  = 0;
      Disk        = true;
      BaseAddress = NULL;

#if 0
      bool resolved = !FieldName.IsEmpty() && Parent->DfdtGetFileName(FieldName, &Fn);
      message_log(LOG_INFO, "LoadFieldCache: field=%s Parent=%p resolved=%d Fn=%s",
                  FieldName.c_str(), (void*)Parent, resolved, Fn.c_str());
#endif

      if (!FieldName.IsEmpty() && Parent->DfdtGetFileName(FieldName, &Fn))
        {
          if (!useDisk)
            {
              BaseAddress = Sessions.GetMemoryBase(Fn, MapRandom);

#if 0
const size_t sessSize = Sessions.Size(Fn);
message_log(LOG_INFO, "LoadFieldCache: mmap attempt Fn=%s BaseAddress=%p Size=%zu",
            Fn.c_str(), (void*)BaseAddress, sessSize);
#endif

              if (BaseAddress)
                {
                  message_log(LOG_DEBUG, "Using mapped Field Cache for %s", FieldName.c_str());
                  Disk       = false;
                  FieldTotal = Sessions.Size(Fn) / sizeof(FC);
                }
              else
                message_log(LOG_ERRNO, "Could not map field table %s (%s)", Fn.c_str(), FieldName.c_str());
            }

          if (Disk)
            {
              if (Fp)
                {
                  Parent->ffclose(Fp);
                  Fp = NULL;
                }

              if ((Fp = Parent->ffopen(Fn, "rb")) != NULL)
                FieldTotal = GetFileSize(Fp) / sizeof(FC);
              else
                message_log(LOG_ERRNO, "Could not open field table %s (%s)", Fn.c_str(), FieldName.c_str());
            }
        }
      // else: no field available -- FieldTotal stays 0, Disk stays true,
      // BaseAddress stays NULL. Nothing to unmap explicitly; the pool
      // still owns whatever was previously mapped for other fields.
    }
  // else: same field as last call -- Disk, FieldTotal, BaseAddress, Fp
  // are all already correct from the prior successful load. Do nothing.

  return FieldTotal;
}


#elif 0
size_t FCACHE::LoadFieldCache(const STRING& fieldName, bool useDisk)
{
  if (!FieldName.CaseEquals(fieldName))
    {
      STRING Fn;

      FieldName  = fieldName;
      FieldTotal = 0;
      Disk       = true;
      BaseAddress = NULL;      // no longer valid for the new field until (re)set below

      if (!FieldName.IsEmpty() && Parent->DfdtGetFileName(FieldName, &Fn))
        {
          if (!useDisk)
            {
              // Ask the pool for this field's mapping. Cheap if already
              // resident (MRU touch, no syscall); maps fresh + evicts LRU
              // if not. Old field's mapping is NOT torn down here — it
              // just becomes one step less-recently-used in the pool.
              BaseAddress = Sessions.GetMemoryBase(Fn, MapRandom);

              if (BaseAddress)
                {
                  message_log(LOG_DEBUG, "Using mapped Field Cache for %s", FieldName.c_str());
                  Disk       = false;
                  FieldTotal = Sessions.Size(Fn) / sizeof(FC);
                }
              else
                message_log(LOG_ERRNO, "Could not map field table %s (%s)", Fn.c_str(), FieldName.c_str());
            }

          if (Disk)
            {
              // Fp itself is already handle-cached inside Parent (FPT),
              // keyed by filename -- so repeatedly calling ffopen() across
              // field switches reuses cached handles rather than reopening.
              if (Fp)
                {
                  Parent->ffclose(Fp);
                  Fp = NULL;
                }

              if ((Fp = Parent->ffopen(Fn, "rb")) != NULL)
                FieldTotal = GetFileSize(Fp) / sizeof(FC);
              else
                message_log(LOG_ERRNO, "Could not open field table %s (%s)", Fn.c_str(), FieldName.c_str());
            }
        }
    }
  else
    {
      // Same field as last call -- re-derive whether we're in mapped
      // or disk mode by checking whether the pool still holds it
      // (it may have been LRU-evicted since we last asked).
      if (!useDisk)
        {
          BaseAddress = Sessions.GetMemoryBase(FieldName, MapRandom);
          if (BaseAddress)
            {
              Disk       = false;
              FieldTotal = Sessions.Size(FieldName) / sizeof(FC);
              return FieldTotal;
            }
        }
      Disk = true; // fall through to Fp, which FPT keeps cached regardless
    }

  return FieldTotal;
}


#else
size_t FCACHE::LoadFieldCache(const STRING& fieldName, bool useDisk)
{
  if (!FieldName.CaseEquals(fieldName)) {
    STRING          Fn;

    if (Fp)
      {
	Parent->ffclose(Fp);
	Fp = NULL;
      }
    FieldTotal = 0;
    Disk = true;
    if (!(FieldName = fieldName).IsEmpty() && Parent->DfdtGetFileName(FieldName, &Fn))
      {
	// Have the path to the field table
	if (!useDisk)
	  {
	    if (Cache.CreateMap(Fn, MapRandom))
	      {
		message_log (LOG_DEBUG, "Created Field Cache for %s", FieldName.c_str());
		Disk = false;
		FieldTotal =  Cache.Size() / sizeof (FC);
	      }
	    else
	      message_log(LOG_ERRNO, "Could not map field table %s (%s)", Fn.c_str(), FieldName.c_str());
	  }
	if (Disk)
	  {
	    Cache.Unmap(); // Close old memory map..
	    if (( Fp = Parent->ffopen(Fn, "rb")) != NULL)
	      {
		FieldTotal = GetFileSize(Fp) / sizeof(FC);
		Cache.Unmap(); // Close old memory map..
	      }
	    else
	      {
		message_log(LOG_ERRNO, "Could not open field table %s (%s)", Fn.c_str(), FieldName.c_str());
	      }
	  }
      }
    else // No field available
      Cache.Unmap(); // Close old memory map..
  } else
    Disk = (FieldTotal != 0);
  return FieldTotal;
}
#endif

bool FCACHE::ValidateInField(const GPTYPE HitGp)
{
  if (FieldName.GetLength() == 0)
    {
      return true;
    }
  else if (!Disk && BaseAddress)
    {
      return ValidateInField(HitGp, (const GPTYPE *)BaseAddress, FieldTotal);
    }
  else if (Fp)
    {
      return ValidateInField(HitGp, Fp, FieldTotal);
    }
  return ValidateInField(HitGp, FieldName);
}

bool FCACHE::ValidateInField(const FC& HitFc)
{
  if (FieldName.GetLength() == 0)
    return true;
  else if (!Disk && BaseAddress)
    return ValidateInField(HitFc, (const void *)BaseAddress, FieldTotal * sizeof(FC));
  else if (Fp)
    return ValidateInField(HitFc, Fp, FieldTotal);
  return ValidateInField(HitFc, FieldName);
}


// Is the gp in the field..
bool FCACHE::ValidateInField(const GPTYPE HitGp, const STRING& fieldName, bool useDisk)
{
  if (fieldName.GetLength() == 0)
    return true;
  return LoadFieldCache(fieldName, useDisk) ? ValidateInField(HitGp) : false;
}

// Is a range in the field..
bool FCACHE::ValidateInField (const FC& HitFc, const STRING& fieldName, bool useDisk)
{
  if (fieldName.GetLength() == 0)
    return true;
  return LoadFieldCache(fieldName, useDisk) ? ValidateInField(HitFc) : false;
}

size_t FCACHE::GetZones (const GPTYPE HitGp, const STRING& fieldName, FCT *Zones)
{
  if (FieldName.GetLength() == 0)
    return 0;

  FC range;
  size_t i = Parent->GetMainMdt ()->LookupByGp (HitGp, &range);
  size_t TotalZones = 0;

  if (i == 0 || LoadFieldCache(fieldName, false) == 0 || !Ok())
    return TotalZones;

  bool result = false;
  size_t Low = 0;
  size_t High = FieldTotal - 1;
  INT X = High / 2, OX;
  const GPTYPE *Buffer = (GPTYPE *) BaseAddress;

  GPTYPE GpS, GpE;

  bool found = false;
  // Find Any...
  do {
    OX = X;
    GpS=GpOf (Buffer[X*sizeof(FC)/sizeof(GPTYPE)]);
    GpE=GpOf (Buffer[X*sizeof(FC)/sizeof(GPTYPE)+1]);
    if ( (HitGp >= GpS) && (HitGp <= GpE) )
      {
	found = true;
	break;
      }
    if (HitGp < GpS) High = X;
    else Low = X + 1;
    // Check bounds
    if ( (size_t)((X = (Low + High) / 2)) >= FieldTotal)
      X = FieldTotal - 1; // Max.
  } while (X != OX);

  if (found)
    {
      INT Low = X;
      INT High = X;

      // Get all the fields in the file...
      GPTYPE lRange = range.GetFieldStart();
      GPTYPE hRange = range.GetFieldEnd();
      while (Low >= 0)
	{
	  GpS = GpOf (Buffer[Low*sizeof(FC)/sizeof(GPTYPE)]);
	  if (lRange < GpS)
	    break;
	  Low--;
	}
      while (High <= (INT)FieldTotal)
	{
	  GpE = GpOf (Buffer[High*sizeof(FC)/sizeof(GPTYPE)+1]);
	  if (hRange > GpE)
	    break;
	  High++;
	}
      // Now collect...
      for (X = Low+1; X < High; X++)
	{
	  GpS=GpOf(Buffer[X*sizeof(FC)/sizeof(GPTYPE)]);
	  GpE=GpOf(Buffer[X*sizeof(FC)/sizeof(GPTYPE)+1]);
	  if ( (HitGp >= GpS) && (HitGp <= GpE) )
	    {
	      Zones->AddEntry( FC(GpS,GpE) );
	      TotalZones++;
	    }
	}
    }
  return TotalZones;
}

bool FCACHE::ValidateInField(const FC& HitFc, STRSTACK Stack, bool useDisk)
{
  STRING fieldName;

  while (!Stack.IsEmpty())
    {
      if (Stack.Pop(&fieldName))
	{
	  if (!ValidateInField(HitFc, fieldName, useDisk))
	    {
	      return false;
	    }
	}
    }
  return true;
}

// Is the gp in the field structure..
bool FCACHE::ValidateInField(const GPTYPE HitGp, STRSTACK& Stack, bool useDisk)
{
  bool result = true;

  if (!Stack.IsEmpty())
    {
      STRING fieldName;
      if (Stack.Pop(&fieldName))
	{
	  FCT Zones;
	  size_t count = GetZones (HitGp, fieldName, &Zones);
	  result = false;
	  for (const FCLIST *p = Zones, *itor = p->Next(); itor != p; itor = itor->Next())	
	    {
	      if (ValidateInField(itor->Value(), Stack, useDisk))
		{
		  result = true;
		  break;
		}
	    }
	}
    }
  return result;
}


bool FCACHE::ValidateInField (const GPTYPE HitGp, const STRING& fieldName) const
{
  // Special case. Everyhing is in every "" field since we intrepret that to mean
  // full.
  if (fieldName.GetLength() == 0)
    return true;

  bool Status = false; 
  STRING      Fn;
  if (true == Parent->DfdtGetFileName (fieldName, &Fn))
    {
      FILE *fp = Parent->ffopen (Fn, "rb");
      if (fp)
	{
	  Status = ValidateInField (HitGp, fp);
	  Parent->ffclose (fp);  
	}
    }
  return Status;
}


bool FCACHE::ValidateInField (const GPTYPE HitGp, const void *Buffer, const size_t Length) const
{
  bool result = false;
  const GPTYPE *B = (const GPTYPE *)Buffer;
  const size_t Total = Length/sizeof(FC);

  if (Total > 0 && B)
    {
      size_t Low = 0;
      size_t High = Total - 1;
      size_t X = High / 2;
      size_t OX;
      GPTYPE GpS, GpE;

      do {
        OX = X;
        GpS=GpOf(B[X*sizeof(FC)/sizeof(GPTYPE)]);
        GpE=GpOf(B[X*sizeof(FC)/sizeof(GPTYPE)+1]);

        if ( (HitGp >= GpS) && (HitGp <= GpE) )
          {
            result = true;
          }
        else
          {
            if (HitGp < GpS) High = X;
            else Low = X + 1;
            // Check bounds
            if ((X = (Low + High) / 2) >= Total)
              X = Total - 1; // Max.
          }
      } while (X != OX && !result);
    }
  return result;
}

bool FCACHE::ValidateInField (const GPTYPE HitGp, const GPTYPE *Buffer, const size_t Total) const
{
  bool result = false;

  if (Total > 0 && Buffer)
    {
      size_t Low = 0;
      size_t High = Total - 1;
      size_t X = High / 2, OX;
      GPTYPE GpS, GpE;

      do {
	OX = X;
	GpS=Buffer[X*sizeof(FC)/sizeof(GPTYPE)];
	GpE=Buffer[X*sizeof(FC)/sizeof(GPTYPE)+1];
	if ( (HitGp >= GpS) && (HitGp <= GpE) )
	  {
	    result = true;
	  }
	else
	  {
	    if (HitGp < GpS) High = X;
	    else Low = X + 1;
	    // Check bounds
	    if ((X = (Low + High) / 2) >= Total)
	      X = Total - 1; // Max.
	  }
      } while (X != OX && !result);
    }
  return result;
}

bool FCACHE::ValidateInField (const GPTYPE HitGp, FILE *fp, const size_t total) const
{
  bool Status = false;
  if (fp)
    {
      const size_t Total = total == 0 ? GetFileSize(fp) / sizeof (FC) : total;
      size_t       Low = 0;
      size_t       High = Total - 1;
      size_t       X = High / 2;
      size_t       OldX;
      FC           Fc;

      // Does the field exist and have elements? If so then search
      if (Total > 0) do
	{
	  OldX = X;
	  if (-1 == fseek (fp, X * sizeof(FC), SEEK_SET)) /* @@ */
	    {
	      // Seek Error
	      if (++X >= Total)
		X = Total - 1; // Max
	    }
	  else
	    {
	      ::Read(&Fc, fp);
	      if ((Status = Fc.Contains(HitGp)) == true)
		break;
	      if (HitGp < Fc)
		High = X;
	      else
		Low = X + 1;
	      // Check bounds
	      if ((X = (Low + High) / 2) >= Total)
		X = Total - 1; // Max.
	    }
	}
      while (X != OldX);
    }
  return Status;
}

// Have the field coordinates for a hit. Check that it is inside
// another field coordinate (for another field).

bool FCACHE::ValidateInField (const FC& HitFc, const void *Buffer, const size_t Length) const
{
  const GPTYPE *B = (const GPTYPE *)Buffer;
  const size_t Total = Length/sizeof(FC);
  bool result = false;

  if (Total && B)
    {
      size_t Low = 0, High = Total - 1;
      size_t X = High / 2, OX;
      const GPTYPE start = HitFc.GetFieldStart();
      const GPTYPE end   = HitFc.GetFieldEnd ();
      GPTYPE GpS, GpE;

      do {
	OX = X;
	GpS=GpOf(B[X*sizeof(FC)/sizeof(GPTYPE)]);
	GpE=GpOf(B[X*sizeof(FC)/sizeof(GPTYPE)+1]);
	if (GpS >= start && GpE <= end)
	  {
	    result = true;
	    break;
	  }
	else
	  {
	    if (start < GpS && end < GpE)
	      High = X;
	    else
	      Low = X + 1;
	    // Check bounds
	    if ((X = (Low + High) / 2) >= Total)
	      X = Total - 1; // Max.
	  }
      } while (X != OX);
    }
  return result;
}

bool FCACHE::ValidateInField (const FC& HitFc, const GPTYPE *Buffer, const size_t Total) const
{
  if (Total == 0 || Buffer == NULL)
    return false;

  bool result = false;
  size_t Low = 0, High = Total - 1;
  size_t X = High / 2, OX;
  const GPTYPE start = HitFc.GetFieldStart();
  const GPTYPE end   = HitFc.GetFieldEnd ();

  do {
    OX = X;
    GPTYPE GpS=Buffer[X*sizeof(FC)/sizeof(GPTYPE)];
    GPTYPE GpE=Buffer[X*sizeof(FC)/sizeof(GPTYPE)+1];
    if (GpS >= start && GpE <= end)
      {
        result = true;
        break;
      }
    else
      {
        if (start < GpS && end < GpE)
          High = X;
        else
          Low = X + 1;
        // Check bounds
        if ((X = (Low + High) / 2) >= Total)
          X = Total - 1; // Max.
      }
  } while (X != OX);
  return result;
}


bool FCACHE::ValidateInField (const FC& HitFc, FILE *fp, const size_t total) const
{
  bool Status = false;
  size_t Total = total;
  if (Total == 0)
    Total =  GetFileSize(fp) / sizeof (FC);
  if (fp && Total)
    {
      size_t Low = 0;
      size_t High = Total - 1;
      size_t X = High / 2;
      size_t OldX;
      FC Fc;

      do
	{
	  OldX = X;
	  if (-1 == fseek (fp, X * sizeof(FC), SEEK_SET)) /* @@ */
	    {
	      // Seek Error
	      if (++X >= Total)
		X = Total - 1; // Max
	    }
	  else
	    {
	      ::Read(&Fc, fp);
	      if ((Status = Fc.Contains(HitFc)) == true)
		break;
	      if (HitFc < Fc)
		High = X;
	      else
		Low = X + 1;
	      // Check bounds
	      if ((X = (Low + High) / 2) >= Total)
		X = Total - 1; // Max.
	    }
	}
      while (X != OldX);
    }
  // else Error!
  return Status;
}

bool FCACHE::ValidateInField (const FC& HitFc, const STRING& fieldName) const
{
  if (fieldName.GetLength() == 0) return true;

  bool Status = false;
  STRING Fn;
  if (true == Parent->DfdtGetFileName (fieldName, &Fn))
    {
      PFILE fp = Parent->ffopen (Fn, "rb");
      if (fp)
	{
	  size_t Total =  GetFileSize(fp) / sizeof (FC);
	  Status = ValidateInField (HitFc, fp, Total);
	  Parent->ffclose (fp);
	}
    }
  return Status;
}




// Return the coordinates of the hit in the field
FC FCACHE::FcInField (const GPTYPE HitGp, FILE *fp) const
{
  size_t Total =  GetFileSize(fp) / sizeof (FC);
  if (Total > 0 && fp)
    {
      size_t Low = 0;
      size_t High = Total - 1;
      size_t X = High / 2;
      size_t OldX;
      FC     Fc;

      do
	{
	  OldX = X;
	  if (-1 == fseek (fp, X * sizeof(FC), SEEK_SET)) /* @@ */
	    {
	      // Seek Error
	      if (++X >= Total)
		X = Total - 1; // Max
	    }
	  else
	    {
	      ::Read(&Fc, fp);
	      if (Fc.Contains(HitGp))
		return Fc;
	      if (HitGp < Fc)
		High = X;
	      else
		Low = X + 1;
	      // Check bounds
	      if ((X = (Low + High) / 2) >= Total)
		X = Total - 1; // Max.
	    }
	}
      while (X != OldX);
    }
  // NOT FOUND
  return FC(0,0);
}



FC FCACHE::FcInField_Mapped(const GPTYPE HitGp) const
{
  const FC* Base = (const FC*)BaseAddress;
  size_t   Total = FieldTotal;


  if (Total == 0 || Base == NULL)
    return FC(0,0);

  size_t Low = 0;
  size_t High = Total - 1;
  size_t X = High / 2;
  size_t OldX;

  do
    {
      OldX = X;
      const FC& Fc = Base[X];          // no seek, no read — direct memory access

      if (Fc.Contains(HitGp))
        return Fc;
      if (HitGp < Fc)
        High = X;
      else
        Low = X + 1;

      if ((X = (Low + High) / 2) >= Total)
        X = Total - 1;
    }
  while (X != OldX);

  return FC(0,0);
}


// Returns the FC at position idx within the currently loaded field.
// Transparently reads from the mapped buffer or the disk stream,
// whichever mode LoadFieldCache left us in.
FC FCACHE::GetRecordFc(size_t idx) const
{
  if (idx < FieldTotal) {   // out-of-range guard

    if (!Disk && BaseAddress) {
      const size_t offset = idx * sizeof(FC);
      GPTYPE start = getGPTYPE(BaseAddress, offset);
      GPTYPE end   = getGPTYPE(BaseAddress, offset + sizeof(GPTYPE));
      return FC(start, end);
    }

    if (Fp)
      {
        FC fc;
        if (fseek(Fp, (off_t)idx * sizeof(FC), SEEK_SET) == 0)
          fc.Read(Fp);
        return fc;
      }
  }
  return FC(0, 0);
}



