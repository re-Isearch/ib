/*
Copyright (c) 2020-21 Project re-Isearch and its contributors: See CONTRIBUTORS.
It is made available and licensed under the Apache 2.0 license: see LICENSE
*/

// TODO: The INT4 references need to be replaced with a GPTYPE
//

/*@@@
File:		numsearch.cxx
Version:	$Revision: 1.3 $
Description:	Class INDEX - numeric search methods
@@@*/

#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <sys/stat.h>

#include "defs.hxx"
#include "string.hxx"
#include "vlist.hxx"
#include "strlist.hxx"
#include "common.hxx"
//#include "sw.hxx"
#include "soundex.hxx"
#include "nfield.hxx"
#include "nlist.hxx"
#include "intfield.hxx"
#include "intlist.hxx"
#include "attr.hxx"
#include "attrlist.hxx"
#include "dfd.hxx"
#include "dfdt.hxx"
#include "fc.hxx"
#include "fct.hxx"
#include "df.hxx"
#include "dft.hxx"
#include "record.hxx"
#include "mdtrec.hxx"
#include "mdt.hxx"
#include "result.hxx"
#include "idbobj.hxx"
#include "iresult.hxx"
#include "opobj.hxx"
#include "operand.hxx"
#include "rset.hxx"
#include "irset.hxx"
#include "opstack.hxx"
#include "squery.hxx"
#include "dtreg.hxx"
#include "rcache.hxx"
#include "index.hxx"
#include "typedscan.hxx"
#include "fprec.hxx"
#include "fpt.hxx"
#include "registry.hxx"
#include "idb.hxx"
#ifdef DICTIONARY
#include "dictionary.hxx"
#endif

#define DEBUG 0
#define NEW_CODE 1


#ifndef FIELD_WILD_MATCH
#define FIELD_WILD_MATCH(_x, _y)  (_x).FieldMatch(_y)
#endif

void INDEX::SortNumericFieldData()
{
  DFD DfdRecord;
  STRING Fn;
  FIELDTYPE FieldType;

  size_t total = Parent->DfdtGetTotalEntries();

  for (size_t x=1; x<=total; x++) {
    Parent->DfdtGetEntry(x,&DfdRecord);
    FieldType = DfdRecord.GetFieldType();
    // Don't want to process data that has no methods for numerical
    if (FieldType.IsText() || FieldType.IsExtern())
      continue;
    if (Parent->DfdtGetFileName(DfdRecord,&Fn) == false)
      {
        message_log (LOG_ERROR, "Could not find %s [%s] in DFD", DfdRecord.GetFieldName().c_str(), FieldType.c_str());
        continue; // ERROR
      }
    if (!FileExists(Fn))
      {
        message_log (LOG_DEBUG, "Field data for %s of type %s not found [%s].",
		DfdRecord.GetFieldName().c_str(), FieldType.c_str(), Fn.c_str());
	continue;
      }

    message_log (LOG_INFO, "Creating %s [%s]", DfdRecord.GetFieldName().c_str(), FieldType.c_str());

    if (FieldType.IsInteger()) {
      INTEGERLIST().WriteIndex(Fn);
    } else if (FieldType.IsCurrency()) {
      MONETARYLIST().WriteIndex(Fn);
    } else if (FieldType.IsNumerical() || FieldType.Equals(FIELDTYPE::boolean) ||
		FieldType.IsComputed() || FieldType.IsPhonetic() ||
		FieldType.IsHash() || FieldType.IsCaseHash() || FieldType.IsPrivHash() ||
		FieldType.IsLexiHash() || FieldType.IsSMILES() ) {
      NumericColumnMaps.Invalidate(Fn);
      if (!NUMERICLIST().WriteIndex(Fn))
        message_log(LOG_ERROR, "Could not build fixed-point numerical index '%s'", Fn.c_str());
    } else if (FieldType.IsNumericalRange()) {
      INTERVALLIST().WriteIndex(Fn);
    } else if (FieldType.IsDate()) {
      DATELIST().WriteIndex(Fn);
    } else if (FieldType.IsDateRange()) {
      INTERVALLIST IntList;
#if 1
      IntList.WriteIndex(Fn);
#else
      IntList.SetFileName(Fn);
      IntList.LoadTable(0,-1);
      if ((Count = IntList.GetCount()) > 0) { // was > 1
	::remove(Fn);

	IntList.SortByStart();
	IntList.WriteTable(0);
	IntList.SortByEnd();
	IntList.WriteTable(Count);
	IntList.SortByGP();
	IntList.WriteTable(2*Count);
      }
#endif
    } else if (FieldType.IsBox()) {
      BBOXLIST().WriteIndex(Fn);
    } else if (FieldType.IsGPoly()) {
      GPOLYLIST().WriteIndex(Fn);
    } else {
      message_log (LOG_PANIC, "INDEX: No indexing method defined for field type '%s'???",  FieldType.c_str());
      continue;
    }
  }
  return;
}

PIRSET INDEX::HashSearch(const STRING& Contents, const STRING& FieldName, INT4 Relation, bool useCase)
{
  const NUMERICOBJ  fKey = useCase ? encodeCaseHash(Contents) : encodeHash(Contents);
  message_log (LOG_DEBUG, "Numeric search Hash(\"%s\")->%F", Contents.c_str(), (double)fKey);

#ifdef FIELD_WILD_MATCH
  STRING          pattern(FieldName);
  const DFDT     *dfdtp = Parent->GetMainDfdt();

  if (!pattern.IsPlain() && dfdtp)
    {
      const size_t total = dfdtp->GetTotalEntries();
      STRING       field;
      FIELDTYPE    fType;
      IRSET       *irset0 = NULL;
      IRSET       *irset  = NULL;

      //pattern.ToUpper();
      //pattern.Replace("\\","/");
      for (size_t i=0; i<total; i++)
        {
          field = dfdtp->GetFieldName(i+1);
          fType = Parent->GetFieldType(field);
	  if ( (useCase && !fType.IsCaseHash()) || (!useCase && !fType.IsHash()))
	    continue;

          // Does it match?
          //field.Replace("\\", "/");
          if (field.GetLength() &&  FIELD_WILD_MATCH(pattern, field))
            {
              if ((irset0 = NumericSearch(fKey, field, Relation)) != NULL)
                {
                  if (irset)
                    {
                      irset->Or(*irset0);
                      delete irset0;
                    }
                  else irset = irset0;
                }
            }
        }
      if (irset) return irset;
    }
#endif
  return NumericSearch( fKey, FieldName, Relation);
}

PIRSET INDEX::LexiHashSearch(const STRING& Term, const STRING& FieldName, INT4 Relation)
{
  if (Parent == NULL) return NULL;

//cerr << "LexiHashSearch: " << Term << endl;
 
  const NUMERICOBJ fKey = encodeLexiHash(Term);

#ifdef FIELD_WILD_MATCH
  STRING          pattern(FieldName);
  const DFDT     *dfdtp = Parent->GetMainDfdt();

  if (!pattern.IsPlain() && dfdtp)
    {
      const size_t total = dfdtp->GetTotalEntries();
      STRING       field;
      FIELDTYPE    fType;
      IRSET       *irset0 = NULL;
      IRSET       *irset  = NULL;

      //pattern.ToUpper();
      //pattern.Replace("\\","/");
      for (size_t i=0; i<total; i++)
        {
          field = dfdtp->GetFieldName(i+1);
          fType = Parent->GetFieldType(field);
          // Is this field lexi?
          if (! fType.IsLexiHash())
            continue;

          // Does it match?
          //field.Replace("\\", "/");
          if (field.GetLength() &&  FIELD_WILD_MATCH(pattern, field))
            {
              if ((irset0 = NumericSearch(fKey, field, Relation)) != NULL)
                {
                  if (irset)
                    {
                      irset->Or(*irset0);
                      delete irset0;
                    }
                  else irset = irset0;
                }
            }
        }
      if (irset) return irset;
    }
#endif
  return NumericSearch(fKey, FieldName, Relation);
}



template <class Enumerator>
static void PopulateTypedResultSet(PIDBOBJ parent,
                                   const DATERANGE& dateRange,
                                   PIRSET pirset,
                                   Enumerator&& enumerate);

template <class Enumerator, class HitRange>
static void PopulateTypedResultSet(PIDBOBJ parent, const DATERANGE& dateRange,
                                  PIRSET result, Enumerator&& enumerate, HitRange&& hitRange);


static void MonetaryWholeUnitBounds(INT16 key, INT16 *low, INT16 *high)
{
  const INT16 tail = (INT16)MONETARYOBJ::TICKS_PER_UNIT - 1;

  if (key > 0)
    {
      *low = key;
      *high = key > MAX_INT16 - tail ? MAX_INT16 : key + tail;
    }
  else if (key < 0)
    {
      *high = key;
      *low = key < MIN_INT16 + tail ? MIN_INT16 : key - tail;
    }
  else
    {
      *low = -tail;
      *high = tail;
    }
}


PIRSET INDEX::MonetarySearch(const MONETARYOBJ& Price,
                             const STRING& FieldName,
                             INT4 Relation)
{
  if (Parent == NULL)
    return NULL;

  PIRSET pirset = new IRSET(Parent);

  if (!Price.Ok())
    {
      Parent->SetErrorCode(113);
      message_log(LOG_WARN,
                  "Invalid monetary value in search for field '%s'",
                  FieldName.c_str());
      return pirset;
    }

#ifdef FIELD_WILD_MATCH
  STRING pattern(FieldName);
  const DFDT *dfdtp = Parent->GetMainDfdt();

  if (!pattern.IsPlain() && dfdtp)
    {
      IRSET *result = NULL;
      const size_t total = dfdtp->GetTotalEntries();

      for (size_t i = 0; i < total; ++i)
        {
          const STRING field = dfdtp->GetFieldName(i + 1);
          const FIELDTYPE type = Parent->GetFieldType(field);

          if (!type.IsCurrency())
            continue;

          if (field.GetLength() && FIELD_WILD_MATCH(pattern, field))
            {
              IRSET *part = MonetarySearch(Price, field, Relation);

              if (part)
                {
                  if (result)
                    {
                      result->Or(*part);
                      delete part;
                    }
                  else
                    result = part;
                }
            }
        }

      if (result)
        {
          delete pirset;
          return result;
        }
    }
#endif

  const FIELDTYPE ft = Parent->GetFieldType(FieldName);

  if (!ft.IsCurrency())
    {
      Parent->SetErrorCode(113);
      message_log(LOG_DEBUG,
                  "Can't search monetary value in field '%s' of type '%s'",
                  FieldName.c_str(), ft.c_str());
      return pirset;
    }

  STRING Fn;
  if (!Parent->DfdtGetFileName(FieldName, ft, &Fn))
    {
      Parent->SetErrorCode(1);
      message_log(LOG_PANIC,
                  "Could not create monetary table name for field '%s'",
                  FieldName.c_str());
      return pirset;
    }

  if (!FileExists(Fn))
    {
      Parent->SetErrorCode(113);
      return pirset;
    }

  MONETARYLIST List;
  const INT16 key = Price.GetTicks();

  const clock_t startClock = clock();
  const auto keepRunning = [&]() -> bool
  {
    if (MaxCPU_ticks > 0 &&
        clock() - startClock > MaxCPU_ticks)
      {
        CPU_ResourcesExhausted();
        return false;
      }

    return true;
  };

  PopulateTypedResultSet(
      Parent,
      DateRange,
      pirset,
      [&](auto&& visitor)
      {
        // "12" denotes the whole-unit bucket; "12.00" is exact.
        // Other scalar relations retain ordinary ordered semantics.
        if (!Price.HasExplicitFraction() &&
            (Relation == ZRelEQ || Relation == ZRelNE))
          {
            INT16 low, high;
            MonetaryWholeUnitBounds(key, &low, &high);

            return List.VisitMappedRange(
                Fn,
                low,
                high,
                (ZRelation_t)Relation,
                MonetaryColumnMaps,
                keepRunning,
                visitor);
          }

        return List.VisitMappedMatches(
            Fn,
            key,
            (ZRelation_t)Relation,
            MonetaryColumnMaps,
            keepRunning,
            visitor);
      });

  return pirset;
}


PIRSET INDEX::MonetarySearchRange(const MONETARYRANGE& Range,
                                  const STRING& FieldName,
                                  INT4 Relation)
{
  if (Parent == NULL)
    return NULL;

  PIRSET pirset = new IRSET(Parent);

  if (!Range.Ok())
    return pirset;

#ifdef FIELD_WILD_MATCH
  STRING pattern(FieldName);
  const DFDT *dfdtp = Parent->GetMainDfdt();

  if (!pattern.IsPlain() && dfdtp)
    {
      IRSET *result = NULL;
      const size_t total = dfdtp->GetTotalEntries();

      for (size_t i = 0; i < total; ++i)
        {
          const STRING field = dfdtp->GetFieldName(i + 1);
          const FIELDTYPE type = Parent->GetFieldType(field);

          if (!type.IsCurrency())
            continue;

          if (field.GetLength() && FIELD_WILD_MATCH(pattern, field))
            {
              IRSET *part = MonetarySearchRange(Range, field, Relation);

              if (part)
                {
                  if (result)
                    {
                      result->Or(*part);
                      delete part;
                    }
                  else
                    result = part;
                }
            }
        }

      if (result)
        {
          delete pirset;
          return result;
        }
    }
#endif

  const FIELDTYPE ft = Parent->GetFieldType(FieldName);

  if (!ft.IsCurrency())
    {
      Parent->SetErrorCode(113);
      message_log(LOG_DEBUG,
                  "Can't search monetary range in field '%s' of type '%s'",
                  FieldName.c_str(), ft.c_str());
      return pirset;
    }

  STRING Fn;
  if (!Parent->DfdtGetFileName(FieldName, ft, &Fn))
    {
      Parent->SetErrorCode(1);
      message_log(LOG_PANIC,
                  "Could not create monetary table name for field '%s'",
                  FieldName.c_str());
      return pirset;
    }

  if (!FileExists(Fn))
    {
      Parent->SetErrorCode(113);
      return pirset;
    }

  MONETARYLIST List;
  const INT16 low = Range.GetStart().GetTicks();
  const INT16 high = Range.GetEnd().GetTicks();

  const clock_t startClock = clock();
  const auto keepRunning = [&]() -> bool
  {
    if (MaxCPU_ticks > 0 &&
        clock() - startClock > MaxCPU_ticks)
      {
        CPU_ResourcesExhausted();
        return false;
      }

    return true;
  };

  PopulateTypedResultSet(
      Parent,
      DateRange,
      pirset,
      [&](auto&& visitor)
      {
        return List.VisitMappedRange(
            Fn,
            low,
            high,
            (ZRelation_t)Relation,
            MonetaryColumnMaps,
            keepRunning,
            visitor);
      });

  return pirset;
}


//

PIRSET INDEX::NumericSearch(const NUMERICOBJ& key, const STRING& FieldName, INT4 Relation)
{
  if (!Parent) return NULL;
  PIRSET result = new IRSET(Parent);
  if (!key.Ok())
    {
      Parent->SetErrorCode(113);
      message_log(LOG_WARN, "Invalid or out-of-range numerical key for field '%s'", FieldName.c_str());
      return result;
    }
#ifdef FIELD_WILD_MATCH
  const STRING pattern(FieldName);
  const DFDT *fields = Parent->GetMainDfdt();
  if (!pattern.IsPlain() && fields)
    {
      for (size_t i = 1; i <= fields->GetTotalEntries(); ++i)
        {
          const STRING field = fields->GetFieldName(i);
          const FIELDTYPE type = Parent->GetFieldType(field);
          if (!type.IsNumeric() || !FIELD_WILD_MATCH(pattern, field)) continue;
          IRSET *part = NumericSearch(key, field, Relation);
          if (part) { result->Or(*part); delete part; }
        }
      return result;
    }
#endif
  const FIELDTYPE type = Parent->GetFieldType(FieldName);
  if (type.IsDate()) { delete result; return DateSearch(SRCH_DATE((DOUBLE)key), FieldName, Relation); }
  if (type.IsDateRange()) { delete result; return DoDateSearch(SRCH_DATE((DOUBLE)key).ISOdate(), FieldName, Relation, ZStructDate); }
  if (type.IsText()) { Parent->SetErrorCode(113); return result; }
  if (type.IsCurrency()) { delete result; return MonetarySearch(MONETARYOBJ(key), FieldName, Relation); }

  STRING filename, coordinates;
  if (!Parent->DfdtGetFileName(FieldName, type, &filename)) { Parent->SetErrorCode(1); return result; }
  if (!FileExists(filename)) { Parent->SetErrorCode(113); return result; }
  Parent->DfdtGetFileName(FieldName, &coordinates);
  FILE *fp = coordinates.IsEmpty() ? NULL : ffopen(coordinates, "rb");
  NUMERICLIST values;
  const clock_t started = clock();
  const auto keepRunning = [&]() {
    if (MaxCPU_ticks > 0 && clock()-started > MaxCPU_ticks) { CPU_ResourcesExhausted(); return false; }
    return true;
  };
  PopulateTypedResultSet(Parent, DateRange, result,
    [&](auto&& visitor) {
      const bool ok = values.VisitMappedMatches(filename, key.GetTicks(), ZRelation_t(Relation),
                                               NumericColumnMaps, keepRunning, visitor);
      if (!ok && keepRunning()) Parent->SetErrorCode(113);
      return ok;
    },
    [&](GPTYPE gp, UINT2) { return FieldCache->FcInField(gp, fp); });
  if (fp) ffclose(fp);
  return result;
}

// Dump the numbers in an index
size_t INDEX::IntegerScan(STRLIST *List, const STRING& FieldName, size_t Start, INT TotalRequested)
{
  return typed_scan::ScanField<INTEGER_INDEX_TRAITS>(Parent, List, FieldName,
    FIELDTYPE::integer, Start, TotalRequested, IntegerColumnMaps, typed_scan::IntegerTerm());
}

size_t INDEX::IntegerScan(SCANLIST *List, const STRING& FieldName, size_t Start, INT TotalRequested)
{
  return typed_scan::ScanField<INTEGER_INDEX_TRAITS>(Parent, List, FieldName,
    FIELDTYPE::integer, Start, TotalRequested, IntegerColumnMaps, typed_scan::IntegerTerm());
}

size_t INDEX::CurrencyScan(STRLIST *List, const STRING& FieldName, size_t Start, INT TotalRequested)
{
  return typed_scan::ScanField<MONETARY_INDEX_TRAITS>(Parent, List, FieldName,
    FIELDTYPE::currency, Start, TotalRequested, MonetaryColumnMaps, typed_scan::CurrencyTerm());
}

size_t INDEX::CurrencyScan(SCANLIST *List, const STRING& FieldName, size_t Start, INT TotalRequested)
{
  return typed_scan::ScanField<MONETARY_INDEX_TRAITS>(Parent, List, FieldName,
    FIELDTYPE::currency, Start, TotalRequested, MonetaryColumnMaps, typed_scan::CurrencyTerm());
}

size_t INDEX::NumericalScan(STRLIST *output, const STRING& field, size_t start, INT requested)
{
  if (!Parent || !output || requested == 0) return 0;
  const FIELDTYPE type = Parent->GetFieldType(field);
  if (type.IsInteger()) return IntegerScan(output, field, start, requested);
  if (type.IsCurrency()) return CurrencyScan(output, field, start, requested);
  if (type.IsDate()) return DateScan(output, field, start, requested);
  if (!(type.IsNumerical() || type.IsComputed() || type == FIELDTYPE::boolean)) return 0;
  return typed_scan::ScanField<NUMERIC_INDEX_TRAITS>(Parent, output, field, type, start, requested,
                                                  NumericColumnMaps, typed_scan::NumericTerm());
}

size_t INDEX::NumericalScan(SCANLIST *output, const STRING& field, size_t start, INT requested)
{
  if (!Parent || !output || requested == 0) return 0;
  const FIELDTYPE type = Parent->GetFieldType(field);
  if (type.IsInteger()) return IntegerScan(output, field, start, requested);
  if (type.IsCurrency()) return CurrencyScan(output, field, start, requested);
  if (type.IsDate()) return DateScan(output, field, start, requested);
  if (!(type.IsNumerical() || type.IsComputed() || type == FIELDTYPE::boolean)) return 0;
  return typed_scan::ScanField<NUMERIC_INDEX_TRAITS>(Parent, output, field, type, start, requested,
                                                  NumericColumnMaps, typed_scan::NumericTerm());
}

template <class Enumerator, class HitRange>
static void PopulateTypedResultSet(PIDBOBJ parent, const DATERANGE& dateRange,
                                  PIRSET pirset, Enumerator&& enumerate, HitRange&& hitRange)
{
  if (parent == NULL || pirset == NULL)
    return;

  const UCHR virtualIndex = (UCHR)parent->GetVolume(NULL);
  PMDT mdt = parent->GetMainMdt();

  if (mdt == NULL)
    return;

  IRESULT iresult;
  iresult.SetVirtualIndex(virtualIndex);
  iresult.SetMdt(mdt);
  iresult.SetHitCount(1);
  iresult.SetAuxCount(1);
  iresult.SetScore(0);

  MDT::GP_CURSOR gpCursor(*mdt);

  size_t current_w = 0;
  FC current_record;

  bool isDeleted = false;
  SRCH_DATE rec_date;

  enumerate(
      [&](GPTYPE gp, UINT2 span)
      {
        //
        // Typed ordered-list enumerators emit in GP order. Reuse the current MDT
        // span while the next hit remains in the same record.
        //
        if (current_w == 0 || !current_record.Contains(gp))
          {
            FC record;
            const size_t w = gpCursor.Lookup(gp, &record);

            if (w == 0)
              {
                current_w = 0;
                current_record = FC();
                return;
              }

            current_w      = w;
            current_record = record;

            if (dateRange.Defined())
              {
                MDTREC mdtrec;

                if (mdt->GetEntry(current_w, &mdtrec))
                  {
                    rec_date  = mdtrec.GetDate();
                    isDeleted = mdtrec.GetDeleted();
                  }
                else
                  {
                    rec_date.Clear();
                    isDeleted = true;
                  }
              }
          }

        if (dateRange.Defined())
          {
            if (isDeleted)
              return;

            if (rec_date.Ok() && !dateRange.Contains(rec_date))
              return;
          }

        iresult.SetMdtIndex(current_w);
        iresult.SetHitTable(FCHIT(hitRange(gp, span)));

        pirset->FastAddEntry(std::move(iresult));

        // FastAddEntry(rvalue) may move the backing HITTABLE into the IRSET.
        // Re-prime the reusable producer state for the next occurrence.
        iresult.SetVirtualIndex(virtualIndex);
        iresult.SetMdt(mdt);
        iresult.SetHitCount(1);
        iresult.SetAuxCount(1);
        iresult.SetScore(0);
      });
}


template <class Enumerator>
static void PopulateTypedResultSet(PIDBOBJ parent, const DATERANGE& dateRange,
                                  PIRSET result, Enumerator&& enumerate)
{
  PopulateTypedResultSet(parent, dateRange, result, enumerate,
                        [](GPTYPE gp, UINT2 span) { return FC(gp, gp+span); });
}


PIRSET INDEX::IntegerSearch(const INT16 Key, const STRING& FieldName, INT4 Relation)
{
  if (Parent == NULL)
    return NULL;

  PIRSET pirset = new IRSET(Parent);

#ifdef FIELD_WILD_MATCH
  STRING pattern(FieldName);
  const DFDT *dfdtp = Parent->GetMainDfdt();

  if (!pattern.IsPlain() && dfdtp)
    {
      IRSET *result = NULL;

      const size_t total = dfdtp->GetTotalEntries();

      for (size_t i = 0; i < total; ++i)
        {
          const STRING field = dfdtp->GetFieldName(i + 1);
          const FIELDTYPE type = Parent->GetFieldType(field);

          if (!type.IsInteger())
            continue;

          if (field.GetLength() && FIELD_WILD_MATCH(pattern, field))
            {
              IRSET *part = IntegerSearch(Key, field, Relation);

              if (part)
                {
                  if (result)
                    {
                      result->Or(*part);
                      delete part;
                    }
                  else
                    result = part;
                }
            }
        }

      if (result)
        {
          delete pirset;
          return result;
        }
    }
#endif

  const FIELDTYPE ft = Parent->GetFieldType(FieldName);

  if (!ft.IsInteger())
    {
      Parent->SetErrorCode(113);
      message_log(LOG_DEBUG,
                  "Can't search integer value in field '%s' of type '%s'",
                  FieldName.c_str(), ft.c_str());
      return pirset;
    }

  STRING Fn;
  if (!Parent->DfdtGetFileName(FieldName, ft, &Fn))
    {
      Parent->SetErrorCode(1);
      message_log(LOG_PANIC,
                  "Could not create integer table name for field '%s'",
                  FieldName.c_str());
      return pirset;
    }

  if (!FileExists(Fn))
    {
      Parent->SetErrorCode(113);
      return pirset;
    }

  INTEGERLIST List;

  const clock_t startClock = clock();

  const auto keepRunning = [&]() -> bool
  {
    if (MaxCPU_ticks > 0 &&
        clock() - startClock > MaxCPU_ticks)
      {
        CPU_ResourcesExhausted();
        return false;
      }

    return true;
  };

  PopulateTypedResultSet(
      Parent,
      DateRange,
      pirset,
      [&](auto&& visitor)
      {
        return List.VisitMappedMatches(
            Fn,
            Key,
            (ZRelation_t)Relation,
            IntegerColumnMaps,
            keepRunning,
            visitor);
      });

  return pirset;
}


PIRSET INDEX::IntegerSearchRange(const INTEGERRANGE& Range,
                                 const STRING& FieldName,
                                 INT4 Relation)
{
  if (Parent == NULL)
    return NULL;

  PIRSET pirset = new IRSET(Parent);

  if (!Range.Ok())
    return pirset;

#ifdef FIELD_WILD_MATCH
  STRING pattern(FieldName);
  const DFDT *dfdtp = Parent->GetMainDfdt();

  if (!pattern.IsPlain() && dfdtp)
    {
      IRSET *result = NULL;

      const size_t total = dfdtp->GetTotalEntries();

      for (size_t i = 0; i < total; ++i)
        {
          const STRING field = dfdtp->GetFieldName(i + 1);
          const FIELDTYPE type = Parent->GetFieldType(field);

          if (!type.IsInteger())
            continue;

          if (field.GetLength() && FIELD_WILD_MATCH(pattern, field))
            {
              IRSET *part = IntegerSearchRange(
                  Range, field, Relation);

              if (part)
                {
                  if (result)
                    {
                      result->Or(*part);
                      delete part;
                    }
                  else
                    result = part;
                }
            }
        }

      if (result)
        {
          delete pirset;
          return result;
        }
    }
#endif

  const FIELDTYPE ft = Parent->GetFieldType(FieldName);

  if (!ft.IsInteger())
    {
      Parent->SetErrorCode(113);
      message_log(LOG_DEBUG,
                  "Can't search integer range in field '%s' of type '%s'",
                  FieldName.c_str(), ft.c_str());
      return pirset;
    }

  STRING Fn;
  if (!Parent->DfdtGetFileName(FieldName, ft, &Fn))
    {
      Parent->SetErrorCode(1);
      message_log(LOG_PANIC,
                  "Could not create integer table name for field '%s'",
                  FieldName.c_str());
      return pirset;
    }

  if (!FileExists(Fn))
    {
      Parent->SetErrorCode(113);
      return pirset;
    }

  INTEGERLIST List;

  const INT16 low  = (INT16)Range.GetStart();
  const INT16 high = (INT16)Range.GetEnd();

  const clock_t startClock = clock();

  const auto keepRunning = [&]() -> bool
  {
    if (MaxCPU_ticks > 0 &&
        clock() - startClock > MaxCPU_ticks)
      {
        CPU_ResourcesExhausted();
        return false;
      }

    return true;
  };

  PopulateTypedResultSet(
      Parent,
      DateRange,
      pirset,
      [&](auto&& visitor)
      {
        return List.VisitMappedRange(
            Fn,
            low,
            high,
            (ZRelation_t)Relation,
            IntegerColumnMaps,
            keepRunning,
            visitor);
      });

  return pirset;
}
