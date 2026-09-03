/* Copyright (c) 2020-21 Project re-Isearch and its contributors: See CONTRIBUTORS.
It is made available and licensed under the Apache 2.0 license: see LICENSE */
/*@@@
File:		result.hxx
Description:	Class RESULT - Search Result
@@@*/

#ifndef RESULT_HXX
#define RESULT_HXX


#include "defs.hxx"
#include "date.hxx"
#include "lang-codes.hxx"
#include "pathname.hxx"


#if _USE_HITTABLE
# include "fchits.hxx"
#else
#include "fct.hxx"
#endif


#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <vector>


class DOCTYPE;
class MDTREC;
class IDBOBJ;

extern long __IB_RESULT_allocated_count; // Used to track stray RESULTs


struct DISPLAY_MARKER
{
  STRING LexicalStart;
  STRING LexicalEnd;

  STRING EvidenceStart;
  STRING EvidenceEnd;

  bool IsEmpty() const {
  return LexicalStart.IsEmpty()  &&
         LexicalEnd.IsEmpty()    &&
         EvidenceStart.IsEmpty() &&
         EvidenceEnd.IsEmpty();
  }

};

enum DISPLAY_MARKER_STYLE
{
  DisplayMarkerNone = 0,
  DisplayMarkerText,
  DisplayMarkerVT100,
  DisplayMarkerMarkDown
};


struct EVIDENCE_COVER
{
    FC     extent;
    DOUBLE Energy;
    DOUBLE Dispersion;
};

struct COVER_WORK
{
    EVIDENCE_COVER cover;
    UINT           evidence;
};

struct DISPLAY_EVIDENCE
{
  //
  // What evidence actually won.
  //
  EVIDENCE_COVER Cover;

  //
  // What bytes PresentDisplay() should read.
  //
  FC DisplayExtent;

  //
  // Smallest known structural container containing Cover.extent.
  //
  // This may be much larger than DisplayExtent.
  //
  FC     ContainerExtent;
  STRING ContainerName;

  bool HasContainer() const
  {
    return !ContainerName.IsEmpty();
  }
};


typedef std::vector<EVIDENCE_COVER> EVIDENCE_COVERS;


class RESULT {
public:
  RESULT();
  RESULT(const MDTREC& Mdtrec);
  RESULT(const RESULT& OtherResult);

  RESULT& operator=(const RESULT& OtherResult);

  void     SetIndex(const INDEX_ID& newIndex) { Index = newIndex; }
  INDEX_ID GetIndex() const { return Index; }
  void     SetMdtIndex(const INT NewMdtIndex) { Index.SetMdtIndex(NewMdtIndex); }
  INT      GetMdtIndex() const { return Index.GetMdtIndex(); };
  void     SetVirtualIndex(const UCHR newIndex) { Index.SetVirtualIndex(newIndex); }
  INT      GetVirtualIndex() const { return Index.GetVirtualIndex(); }

  void           SetCategory(const _ib_category_t newCategory) { Category = newCategory; }
  _ib_category_t GetCategory() const                           { return Category;        }

  void    SetKey(const STRING& NewKey) { Key = NewKey;           }
  STRING  GetKey() const               { return Key;             }
  STRING& GetKey(STRING *ptr) const    { return *ptr = GetKey(); }
  STRING  GetGlobalKey(char Ch = '@') const;
  void    GetVKey(STRING* StringBuffer) const { *StringBuffer = GetGlobalKey(':'); }

  void        SetDocumentType(const DOCTYPE_ID& NewType) { DocumentType = NewType; }
  const STRING& GetDocumentType (STRING *Ptr) const { return *Ptr = DocumentType.DocumentType(); }
  DOCTYPE_ID  GetDocumentType() const               { return DocumentType; }
  STRING      GetDoctype() const                    { return DocumentType.DocumentType(); }

  LOCALE   GetLocale () const                      { return Locale;         }

  void     SetLocale (const LOCALE& NewLocale)     { Locale = NewLocale;    }
  void     SetLocale (const LANGUAGE& NewLanguage) { Locale = NewLanguage;  }
  void     SetLocale (const CHARSET& NewCharset)   { Locale = NewCharset;   }

  void     SetLanguage (const LANGUAGE& Language)  { Locale.SetLanguage(Language); }
  void     SetCharset  (const CHARSET& Charset)    { Locale.SetCharset(Charset);   }

  const char *GetLanguageCode () const             { return Locale.GetLanguageCode();  }
  const char *GetCharsetCode () const              { return Locale.GetCharsetCode();   }
  const char *GetLanguageName () const             { return Locale.GetLanguageName();  }
  const char *GetCharsetName () const              { return Locale.GetCharsetName();   }
  BYTE        GetCharsetId() const                 { return Locale.GetCharsetId();     }

#if 1
  void     SetPath(const STRING& newPath)     { Pathname.SetPath(newPath);         }
  STRING   GetPath() const                    { return Pathname.GetPath();         }
  STRING&  GetPathName(STRING *ptr) const     { return *ptr = GetPath();           }
  void     SetFileName(const STRING& newName) { Pathname.SetFileName(newName);     }
  STRING   GetFileName() const                { return Pathname.GetFileName();     }
  STRING&  GetFileName(STRING *ptr) const     { return *ptr = GetFileName();       }
#endif

  STRING   GetFullFileName() const            { return Pathname.GetFullFileName(); }
  STRING&  GetFullFileName(STRING *ptr) const { return *ptr = GetFullFileName();   }

  PATHNAME GetPathname() const               { return Pathname;     }
  PATHNAME GetOrigPathname() const           { return origPathname; }

  void     SetPathname(const PATHNAME& newPathname)     { Pathname = newPathname; }
  void     SetOrigPathname(const PATHNAME& newPathname) { origPathname = newPathname; }

  void   SetRecordStart(const UINT4 NewRecordStart) { RecordStart = NewRecordStart; }
  UINT4  GetRecordStart() const                     { return RecordStart;           }
  void   SetRecordEnd(const UINT4 NewRecordEnd )    { RecordEnd = NewRecordEnd;     }
  UINT4  GetRecordEnd() const                       { return RecordEnd;             }
  off_t  GetLength () const                         { return RecordEnd-RecordStart; }
  off_t  GetRecordSize() const                      { return GetLength() + 1;       }

  STRING GetRawRecordData(const FC &fc, IDBOBJ *idb) const;
  STRING GetRawRecordData(const FC &fc, DOCTYPE *idb) const;


  void        SetExtIndex(const _index_id_t newVal) { ExtIndex = newVal;            }
  _index_id_t GetExtIndex() const                   { return ExtIndex;              }

  void   SetScore(const DOUBLE NewScore) { Score = NewScore; }
  DOUBLE GetScore() const                { return Score;     }

  void       SetDate(const SRCH_DATE& NewDate) { Date = NewDate; }
  SRCH_DATE  GetDate() const                   { return Date;    }

  void       SetDateModified(const SRCH_DATE& NewDate) { DateModified = NewDate; }
  SRCH_DATE  GetDateModified() const                   { return DateModified;    }

  void       SetDateCreated(const SRCH_DATE& NewDate) { DateCreated = NewDate; }
  SRCH_DATE  GetDateCreated() const                   { return DateCreated;    }

  void       SetDateExpires(const SRCH_DATE& NewDate) { DateExpires = NewDate; }
  SRCH_DATE  GetDateExpires() const                   { return DateExpires;  }

  void       SetAuxCount(UINT newCount)               { AuxCount = newCount; }
  UINT       GetAuxCount() const                      { return AuxCount;     }

  size_t     GetHitTotal() const        { return HitTable.GetTotalEntries(); }
#if _USE_HITTABLE
  void   SetHitTable(const HITTABLE& NewHitTable)    { (HitTable = NewHitTable).SortByFc(); }
#else
  int        GetRefcount_() const       { return HitTable.Refcount_(); }
  void   SetHitTable(const FCT& NewHitTable)    { (HitTable = NewHitTable).SortByFc(); }
//void   SetHitTable(const FCLIST& NewHitTable) { HitTable = NewHitTable; }
#endif


#if _USE_HITTABLE
  HITTABLE GetHitTable() const                       { return HitTable; }
#else
  FCT      GetHitTable() const                       { return HitTable; }
#endif

  // Dump a XML hit table
  STRING XMLHitTable() const;
  
  // Dump a Json hit table;
  STRING JsonHitTable() const;

  // Get the data
  void   GetRecordData(STRING *StringBuffer, DOCTYPE *DoctypePtr = NULL) const;

  static const DISPLAY_MARKER& GetDisplayMarkers(const STRING& Style);
  static const DISPLAY_MARKER& GetDisplayMarkers(DISPLAY_MARKER_STYLE Style);

  bool PresentBestDisplayEvidence(size_t MaxBytesAdvice, const DISPLAY_MARKER &Marker,
    STRING *StringBuffer, DOCTYPE *DoctypePtr = NULL, STRING *TagPtr = NULL) const;

  bool PresentDisplay( const FC& Range, STRING *StringBuffer, DOCTYPE *DoctypePtr) const {
    return PresentDisplay( Range, StringBuffer, DisplayMarkerVT100, DoctypePtr);
  }

  bool PresentDisplay( const FC& Range, STRING *StringBuffer,
	DISPLAY_MARKER_STYLE Style = DisplayMarkerText, DOCTYPE *DoctypePtr = NULL) const {
    return PresentDisplay( Range, StringBuffer, GetDisplayMarkers(Style), DoctypePtr);
  }
  // Display the hit with evidence wrapped 
  bool PresentDisplay(const FC& Range, STRING *StringBuffer, const DISPLAY_MARKER& Marker,
	DOCTYPE *DoctypePtr = NULL) const;


  // TODO:   REPLACE: We no longer want to have Before, After but
  // have now DISPLAY_MARKER  which includes features to distinguish between
  bool PresentHit(const FC& Fc, STRING *StringBuffer, STRING *Term,
        const STRING& BeforeTerm, const STRING& AfterTerm, DOCTYPE *DoctypePtr = NULL,
	STRING *Tag = NULL) const;

  bool XMLPresentHit(const FC& Fc, STRING *StringBuffer, const STRING& Tag,
	STRING *Term, DOCTYPE *DoctypePtr) const;

  // Context..
  EVIDENCE_COVERS GetEvidenceCovers(size_t Max) const;
  FC GetBestContextHit() const;



  DISPLAY_EVIDENCE GetDisplayEvidence(size_t MaxBytesAdvice, DOCTYPE *DoctypePtr) const;
  DISPLAY_EVIDENCE GetDisplayEvidence(DOCTYPE *DoctypePtr) const {
    return GetDisplayEvidence( (size_t)0, DoctypePtr);
  }

#if 0
  FC GetDisplayEvidence(DOCTYPE *DoctypePtr) const {
   return GetDisplayEvidence(0, DoctypePtr);
  }
  FC GetDisplayEvidence(size_t MaxBytesAdvice, DOCTYPE *DoctypePtr = NULL) const;
#endif

  bool PresentBestDisplayEvidence( size_t MaxBytesAdvice, STRING *StringBuffer,
    STRING *Term = NULL, const STRING& BeforeTerm = NulString, const STRING& AfterTerm = NulString,
    DOCTYPE *DoctypePtr = NULL, STRING *TagPtr = NULL) const;

  bool PresentBestDisplayEvidence(STRING *StringBuffer, STRING *Term = NULL,
	const STRING& BeforeTerm = NulString, const STRING& AfterTerm = NulString,
	DOCTYPE *DoctypePtr = NULL, STRING *TagPtr = NULL) const {
    return PresentBestDisplayEvidence(130, StringBuffer, Term, BeforeTerm, AfterTerm,
	DoctypePtr, TagPtr);
  }

  bool PresentBestContextHit(STRING *StringBuffer, STRING *Term,
	const STRING& BeforeTerm = NulString, const STRING& AfterTerm = NulString,
	DOCTYPE *DoctypePtr = NULL, STRING *FieldNamePtr=NULL) const;

  // Get the Context of the Nth hit
  bool PresentNthHit(size_t N, STRING *StringBuffer, STRING *Term,
        const STRING& BeforeTerm, const STRING& AfterTerm, DOCTYPE *DoctypePtr = NULL, STRING *TagPtr = NULL) const;
  // Get the Context of the first hit
  bool PresentFirstHit(STRING *StringBuffer, STRING *Term = NULL, DOCTYPE *DoctypePtr = NULL,
	STRING *TagPtr = NULL) const {
    return PresentNthHit(1, StringBuffer, Term,  NulString,  NulString, DoctypePtr, TagPtr);
  }
  bool PresentFirstHit(STRING *StringBuffer, STRING *Term,
	const STRING& BeforeTerm, const STRING& AfterTerm, DOCTYPE *DoctypePtr = NULL, STRING *TagPtr = NULL) const {
    return PresentNthHit(1, StringBuffer, Term, BeforeTerm, AfterTerm, DoctypePtr, TagPtr);
  }
  // XML versions of above

  bool XMLPresentBestContextHit(STRING *StringBuffer, const STRING& Tag,
	STRING *Term = NULL, DOCTYPE *DoctypePtr = NULL) const;
  bool XMLPresentNthHit(size_t N, STRING *StringBuffer, const STRING& Tag,
        STRING *Term = NULL, DOCTYPE *DoctypePtr = NULL) const;
  bool XMLPresentFirstHit(STRING *StringBuffer, const STRING& Tag,
	STRING *Term = NULL, DOCTYPE *DoctypePtr = NULL) const {
    return XMLPresentNthHit(1, StringBuffer, Tag, Term, DoctypePtr);
  }

  STRING GetXMLHighlightRecordFormat(int pageno = 0, off_t offset=0) const;

  // Record with highlighted "hits"
  void GetHighlightedRecord(const STRING& BeforeTerm, const STRING& AfterTerm,
	STRING *StringBuffer, DOCTYPE *DoctypePtr = NULL) const;
  void GetHighlighted(const STRING& BeforeTerm,
	const STRING& AfterTerm, FC Range, STRING *StringBuffer, DOCTYPE *DoctypePtr = NULL) const;

  void Write(FILE *fp) const;
  bool Read(FILE *fp);

  ~RESULT();
private:
  FC             GetAltContextHit() const;
  INDEX_ID       Index;
  _index_id_t    ExtIndex; // Other order
  STRING         Key;
  DOCTYPE_ID     DocumentType;
  PATHNAME       Pathname;
  PATHNAME       origPathname;
  UINT4          RecordStart;
  UINT4          RecordEnd;
  SRCH_DATE      Date;
  SRCH_DATE      DateModified;
  SRCH_DATE      DateCreated;
  SRCH_DATE      DateExpires;
  LOCALE         Locale;
  DOUBLE         Score;
  UINT           AuxCount;
  _ib_category_t Category;
#if _USE_HITTABLE
  HITTABLE       HitTable;
#else
  FCT            HitTable;
#endif
};


extern const RESULT& NulResult;

typedef RESULT* PRESULT;

// Common Functions
inline void Write(const RESULT& Result, PFILE Fp)
{
  Result.Write(Fp);
}

inline bool Read(PRESULT ResultPtr, PFILE Fp)
{
  return ResultPtr->Read(Fp);
}


#endif
