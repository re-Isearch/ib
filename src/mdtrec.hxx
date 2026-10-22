/*
Copyright (c) 2020-21 Project re-Isearch and its contributors: See CONTRIBUTORS.
It is made available and licensed under the Apache 2.0 license: see LICENSE
*/
/************************************************************************
************************************************************************/

/*@@@
File:		mdtrec.hxx
Version:	1.00
Description:	Class MDTREC - Multiple Document Table Record
@@@*/

#ifndef MDTREC_HXX
#define MDTREC_HXX

#include <limits>

#include "date.hxx"
#include "pathname.hxx"
#include "lang-codes.hxx"
#include "mdthashtable.hxx"

class MDT;

class MDTREC {
friend class MDT;
friend class KEYREC; 
public:
  MDTREC(MDT *Mdt= NULL);
  MDTREC(const MDTREC& OtherMdtRec);
  MDTREC& operator=(const MDTREC& OtherMdtRec);

  void           SetCategory(const _ib_category_t newCategory) { Category = newCategory; }
  _ib_category_t GetCategory() const                           { return Category;        }

  void           SetPriority(const _ib_priority_t newPriority) { Priority = newPriority; }
  _ib_priority_t GetPriority() const                           { return Priority;        }

  bool    SetKey(const STRING& NewKey);
  STRING  GetKey() const { return STRING().Assign (Key, DocumentKeySize);}
  STRING& GetKey(PSTRING Buffer) const { return Buffer->Assign (Key, DocumentKeySize);}

  void    SetDocumentType(const DOCTYPE_ID &NewDoctypeId);
  void    SetDocumentType(const STRING& NewDocumentType);
  DOCTYPE_ID GetDocumentType() const {
    DOCTYPE_ID Doctype;
    Doctype.Name.Assign (DocumentType, DocumentTypeSize);
    Doctype.Id = Property & 0x7F;
    return Doctype;
  }
  INT GetDocumentType(STRING *Buffer) const {
    if (Buffer) Buffer->Assign (DocumentType, DocumentTypeSize);
    return Property & 0x7F;
  }

  void    SetPath(const STRING& NewPath);
  STRING  GetPath() const;
  STRING& GetPath(STRING *Buffer) const {
    return *Buffer = GetPath();
  }

  void    SetFileName(const STRING& NewFileName);
  STRING  GetFileName() const;

  STRING& GetFileName(STRING *Buffer) const {
    return *Buffer = GetFileName();
  }

  void    SetFullFileName(const STRING& NewFullPath);
  STRING  GetFullFileName() const {
    return GetPath() + GetFileName();
  }

  void    SetOrigPath(const STRING& NewPathName);
  STRING  GetOrigPath() const;
  void    SetOrigFileName(const STRING& NewFileName);
  STRING  GetOrigFileName() const;

  void    SetOrigFullFileName(const STRING& NewFullPath);
  STRING  GetOrigFullFileName() const {
    return GetOrigPath() + GetOrigFileName();
  }

  STRING& GetFullFileName(STRING *Buffer) const {
    return *Buffer = GetFullFileName();
  }

  PATHNAME GetPathname() const;
  PATHNAME GetOrigPathname() const;

  void SetPathname (const PATHNAME& Pathname);
  void SetOrigPathname (const PATHNAME& Pathname);

  void   SetGlobalFileStart(const GPTYPE NewStart) { GlobalFileStart = NewStart; }
  GPTYPE GetGlobalFileStart() const { return GlobalFileStart; };

  // For index import
  MDTREC& operator+=(const GPTYPE Offset) { IncrementStart(Offset); return *this;}
  void    IncrementStart(GPTYPE x)  { GlobalFileStart += x;}

#if 0
  void   SetGlobalFileEnd(const GPTYPE NewEnd) { GlobalFileEnd = NewEnd; }
  GPTYPE GetGlobalFileEnd() const { return GlobalFileEnd; };
#endif

  // Absolute source-file start, with a bounded end-start span. Keep the
  // existing inclusive-end API; moving the start preserves the span.
  off_t GetLength() const { return HasValidRecordRange() ? off_t(LocalRecordSpan) : -1; }
  bool HasValidRecordRange() const {
    return RangeValid == 1 && LocalRecordSpan <= MAX_GPTYPE - LocalRecordStart;
  }
  UINT4 GetRecordSpan() const { return LocalRecordSpan; }
  static constexpr UINT4 MaxRecordSpan() { return std::numeric_limits<UINT4>::max(); }
  void SetLocalRecordStart(const GPTYPE NewStart) {
    LocalRecordStart = NewStart;
    if (LocalRecordSpan > MAX_GPTYPE - NewStart) RangeValid = 0;
  }
  GPTYPE GetLocalRecordStart() const { return LocalRecordStart; }

  bool SetLocalRecordEnd(const GPTYPE NewEnd) {
    RangeValid = NewEnd >= LocalRecordStart && NewEnd - LocalRecordStart <= MaxRecordSpan();
    if (RangeValid) LocalRecordSpan = UINT4(NewEnd - LocalRecordStart);
    return RangeValid == 1;
  }
  bool SetLocalRecordRange(GPTYPE start, GPTYPE end) {
    LocalRecordStart = start;
    return SetLocalRecordEnd(end);
  }
  GPTYPE GetLocalRecordEnd() const { return LocalRecordStart + LocalRecordSpan; }

  void    SetLocale(const LOCALE& NewLocale)   { Locale = NewLocale; }
  LOCALE  GetLocale() const                    { return Locale;      }

  // We store the dates as a pair of 4-byte ints
  // Generic Date
  void SetDate(const SRCH_DATE& NewDate)         { Date = NewDate;         }
  SRCH_DATE GetDate() const                      { return Date;            }
  // Modified Date
  void SetDateModified(const SRCH_DATE& NewDate) {
    if ((DateModified = NewDate).Ok() && Date.IsNotSet())
      Date = DateModified;
  }
  SRCH_DATE GetDateModified() const              { return DateModified;    }
  // Date Created
  void SetDateCreated(const SRCH_DATE& NewDate)  { 
    if ((DateCreated = NewDate).Ok() && Date.IsNotSet())
      Date = DateCreated;
  }
  SRCH_DATE GetDateCreated() const               { return DateCreated;     }

  // Date Expires
  void SetDateExpires(const SRCH_DATE& NewDate)  { DateExpires = NewDate;  }
  SRCH_DATE GetDateExpires() const               { return DateExpires;     }

  // Time To Live
  int  TTL() const;
  int  TTL(const SRCH_DATE& Now) const;

  void SetDeleted(const bool Flag);
  bool GetDeleted() const;

  void FlipBytes();

  // In the structure read/write..
  bool Write(FILE *fp, size_t Index = 0) const;
  bool Read(FILE *fp, size_t Index = 0);
  SRCH_DATE GetDate(FILE *fp, size_t Index = 0) const;


  // Is the MDT Index deleted?
  bool IsDeleted(FILE *fp, size_t Index);


  STRING        Dump() const;

  friend ostream& operator<<(ostream& os, const MDTREC& mdtrec);

  void         SetHashTable (MDTHASHTABLE  *newHashTable) {
    HashTable = newHashTable;
  }

  ~MDTREC();
private:
  SRCH_DATE      Date; // Date of Record (Must be first element!)
/* This is an object we write so the types need to be well defined */

  // We need to be long enough to support 58-bit encoding of 32-byte hashes
  // 32 * log(256) / log(58) + 1  -> (1.38 * 32 = 44 
  // -> 47
  CHR            Key[DocumentKeySize];
  CHR            DocumentType[DocumentTypeSize];

  GPTYPE         pathNameID;
  GPTYPE         fileNameID;

  GPTYPE         origpathNameID;
  GPTYPE         origfileNameID;

  MDTHASHTABLE  *HashTable;
  GPTYPE         GlobalFileStart;
  GPTYPE         LocalRecordStart;
  UINT4          LocalRecordSpan;
  LOCALE         Locale;
  _ib_category_t Category;
  _ib_priority_t Priority;
  BYTE           Property;
  BYTE           RangeValid;

  SRCH_DATE      DateModified;
  SRCH_DATE      DateCreated;
  SRCH_DATE      DateExpires;

};


typedef MDTREC* PMDTREC;

// Common Functions
inline void Write(const MDTREC& Mdtrec, PFILE Fp)
{
  Mdtrec.Write(Fp);
}


inline bool Read(PMDTREC MdtrecPtr, PFILE Fp)
{
  return MdtrecPtr->Read(Fp);
}


#endif
