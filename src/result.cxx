/* Copyright (c) 2020-21 Project re-Isearch and its contributors: See CONTRIBUTORS.
It is made available and licensed under the Apache 2.0 license: see LICENSE */
#pragma ident  "@(#)result.cxx"

/************************************************************************
************************************************************************/

/*@@@
File:		result.cxx
Description:	Class RESULT - Search Result
@@@*/

#include <ctype.h>
#include "common.hxx"
#include "result.hxx"
#include "magic.hxx"
#include "lang-codes.hxx"
#include "dtreg.hxx"


#ifdef DEBUG_MEMORY
long __IB_RESULT_allocated_count = 0; // Used to track stray RESULTs
#endif

static RESULT _nulresult;

const RESULT& NulResult = _nulresult;


RESULT::RESULT()
{
  Index       = 0;
  ExtIndex    = 0;
  Score       = 0;
  AuxCount    = 0;
  Category    = 0;
  RecordStart = 0;
  RecordEnd   = 0;
#ifdef DEBUG_MEMORY
  __IB_RESULT_allocated_count++;
#endif
}

RESULT::RESULT(const MDTREC& mdtrec)
{
  Index       = 0;
  ExtIndex    = 0;
  Score       = 0;
  AuxCount    = 0;
  Key         = mdtrec.GetKey() ;
  DocumentType= mdtrec.GetDocumentType();
  Locale      = mdtrec.GetLocale();
  Pathname    = mdtrec.GetPathname();
  origPathname= mdtrec.GetOrigPathname();
  RecordStart = mdtrec.GetLocalRecordStart();
  RecordEnd   = mdtrec.GetLocalRecordEnd();
  Date        = mdtrec.GetDate();
  DateModified= mdtrec.GetDateModified();
  DateCreated = mdtrec.GetDateCreated();
  DateExpires = mdtrec.GetDateExpires();
  Category    = mdtrec.GetCategory();
#ifdef DEBUG_MEMORY
  __IB_RESULT_allocated_count++;
#endif
}


RESULT::RESULT(const RESULT& OtherResult)
{
#ifdef DEBUG_MEMORY
  __IB_RESULT_allocated_count++;
#endif
  *this = OtherResult;
}

RESULT& RESULT::operator=(const RESULT& OtherResult)
{
  Index        = OtherResult.Index;
  ExtIndex     = OtherResult.ExtIndex;
  Key          = OtherResult.Key;
  DocumentType = OtherResult.DocumentType;
  Locale       = OtherResult.Locale;
  Pathname     = OtherResult.Pathname;
  origPathname = OtherResult.origPathname;
  RecordStart  = OtherResult.RecordStart;
  RecordEnd    = OtherResult.RecordEnd;
  Date         = OtherResult.Date;
  DateModified = OtherResult.DateModified;
  DateCreated  = OtherResult.DateCreated;
  DateExpires  = OtherResult.DateExpires;
  Score        = OtherResult.Score;
  AuxCount     = OtherResult.AuxCount;
  Category     = OtherResult.Category;
  HitTable     = OtherResult.HitTable;
// HitTable->SortByFc();
  return *this;
}

STRING RESULT::GetGlobalKey(char Ch) const
{
  INT VirtualIndex = GetVirtualIndex();
  if (VirtualIndex)
    return STRING().form("%d%c%s", VirtualIndex, Ch, Key.c_str());    
  return Key;
}

#if 0
off_t RESULT::GetRecordSize() const
{
  return (RecordEnd - RecordStart + 1);
}
#endif

void RESULT::GetRecordData(STRING *StringBuffer, DOCTYPE *DoctypePtr) const
{
  STRING       fn (GetFullFileName());

  StringBuffer->Clear();
  if (RecordEnd <= RecordStart)
    message_log (LOG_ERROR, "Element Error (%ld-%ld) in '%s'!", RecordStart, RecordEnd, fn.c_str());
  else if (::GetRecordData(fn, StringBuffer, RecordStart, (size_t)(RecordEnd - RecordStart + 1), DoctypePtr) == 0)
    message_log (LOG_ERRNO, "Element %ld-%ld read error in '%s'!", RecordStart, RecordEnd, fn.c_str()); // ERROR
}

void RESULT::Write(FILE *fp) const
{
  putObjID (objRESULT, fp);
  ::Write(Index, fp);
  ::Write(ExtIndex, fp);
  ::Write(Key, fp);
  ::Write(DocumentType, fp);
  ::Write(Locale, fp);
  ::Write(Pathname, fp);
  ::Write(origPathname, fp);
  ::Write(RecordStart, fp);
  ::Write(RecordEnd, fp);
  ::Write(Date, fp);
  ::Write(DateModified, fp);
  ::Write(DateCreated, fp);
  ::Write(DateExpires, fp);
  ::Write(Score, fp);
  ::Write(AuxCount, fp);
  ::Write(Category, fp);
  HitTable.Write(fp);
}

bool RESULT::Read(FILE *fp)
{
  obj_t obj = getObjID(fp);
  if (obj != objRESULT)
    {
      PushBackObjID(obj, fp);
    }
  else
    {
      ::Read(&Index, fp);
      ::Read(&ExtIndex, fp);
      ::Read(&Key, fp);
      ::Read(&DocumentType, fp);
      ::Read(&Locale, fp);
      ::Read(&Pathname, fp);
      ::Read(&origPathname, fp);
      ::Read(&RecordStart, fp);
      ::Read(&RecordEnd, fp);
      ::Read(&Date, fp);
      ::Read(&DateModified, fp);
      ::Read(&DateCreated, fp);
      ::Read(&DateExpires, fp);
      ::Read(&Score, fp);
      ::Read(&AuxCount, fp);
      ::Read(&Category, fp);
      HitTable.Read(fp);
    }
  return obj == objRESULT;
}

STRING RESULT::XMLHitTable() const
{
  STRING XML;
  if (!HitTable.IsEmpty())
    {
      size_t z = 0;
      for (const FC& fc : HitTable)
        {
	  XML << "  <LOC POS=\"" << fc.GetFieldStart() << "\" LEN=\"" << fc.GetLength()
#if 1 
		<< "\"/>\n";
#else
	  // For this we'd need to pass a DoctypePtr...
	  STRING buffer;
	  if (::GetRecordData(GetFullFileName(), &buffer, FieldStart, FieldStart + Length + 1, DoctypePtr) > 0)
	     XML << "\" TERM = \" << buffer << "\";
	  XML << "/>\n";
#endif
	  z++;
        }
      STRING prefix ("<HITS UNITS=\"characters\" NUMBER=\"");
      prefix << z << "\">\n";
      XML.Insert(1, prefix);
      XML << "</HITS>\n";
    }
  return XML;
}

STRING RESULT::JsonHitTable() const
{
#if 1
  STRING JSON;
  if (!HitTable.IsEmpty())
    {
      size_t z = 0;
      JSON << "{\n";
      JSON << "  \"units\": \"characters\",\n";
      JSON << "  \"hits\": [\n";

      for (const FC& fc : HitTable)
        {
          if (z > 0)
            JSON << ",\n"; // Add comma before every item except the first
          JSON << "    {\n";
          JSON << "      \"pos\": " << fc.GetFieldStart() << ",\n";
          JSON << "      \"len\": " << fc.GetLength() << "\n";
          JSON << "    }";
          z++;
        }

      JSON << "\n  ]\n";
      JSON << "  \"number\": " << z << "\n";
      JSON << "}\n";
    }
  return JSON;
#else
  message_log (LOG_FATAL, "RESULT::JsonHitTable() Not yet implemented");
  return NulString;
#endif
}

STRING RESULT::GetXMLHighlightRecordFormat(int pageno, off_t offset) const
{
  STRING XML ("<XML>\n<Body Units=Characters color=#FF00FF Mode=Active version=2>\n <Highlight>\n");
  if (!HitTable.IsEmpty())
    {
      pageno = Key.SearchReverse(':');
      if (pageno) pageno = atoi(((const char *)Key) + pageno);
      if (pageno > 1) pageno--;
      for (const FC& fc : HitTable)
        {
	  const off_t Start = fc.GetFieldStart() - offset;
	  const off_t End   = fc.GetFieldEnd() - offset;
	  XML << "\t<Loc Pg=" << pageno << " pos=" << Start << " len=" << End-Start+1 << " />\n";
        }
    }
  else
    XML << "\t<loc Pg=" << pageno << " pos=0 len=0 />\n";
  XML << "</Highlight></Body></XML>\n";
  return XML;
}

// Get the FC (address range) for the "best" context
// 
// The algorithm is simple: walk through the list of hits and try
// to find one with the minimum distance between any two. This algorithm
// builds upon the sort of HitTable..
//
// There are surely other plausible algorithms, some delivering perhaps even
// better "best" contexts. This one is 1) simple 2) fast 3) works reasonably well.
//
// An alternative we might consider for structured text is looking for the field
// container with the most hits.  This, however, would not work with unfielded text.
// We would need to first analyse the structure to assure that hits are in different
// fields (for example not just in a "body"). At this time I'm not sure it would be
// worth the effort much less if the results would really be "that much" better.

FC RESULT::GetAltContextHit() const
{
  auto current = HitTable.begin();
  const auto  end = HitTable.end();

  if (current == end)
    return FC();

  FC best = *current;
  GPTYPE metric = 200;

  auto next = current;
  ++next;

  while (next != end)
  {
    const GPTYPE start = current->GetFieldStart();
    const GPTYPE finish = next->GetFieldEnd();

    if (finish > start)
    {
      const GPTYPE distance = finish - start;

      if (distance < metric)
      {
        best = *current;
        metric = distance;
      }
    }

    ++current;
    ++next;
  }

  return best;
}


bool RESULT::PresentBestContextHit(STRING *StringBuffer, STRING *Term,
  const STRING& BeforeTerm, const STRING& AfterTerm, DOCTYPE *DoctypePtr, STRING *TagPtr) const
{
  return PresentHit(GetBestContextHit(), StringBuffer, Term, BeforeTerm, AfterTerm, DoctypePtr, TagPtr);
}

// 1 is the first entry
bool RESULT::PresentNthHit(size_t N, STRING *StringBuffer, STRING *Term,
        const STRING& BeforeTerm, const STRING& AfterTerm, DOCTYPE *DoctypePtr, STRING *TagPtr) const
{
  IRESULT::hit_type hit;
  if (StringBuffer)
    StringBuffer->Clear();
  if (Term)
    Term->Clear();
  if (TagPtr)
    TagPtr->Clear();

  if (HitTable.GetEntry(N, &hit))
    {
      const FC& Fc = hit; 
      return PresentHit(Fc, StringBuffer, Term, BeforeTerm, AfterTerm, DoctypePtr, TagPtr);
    }
  return false;
}


bool RESULT::PresentDisplay(const FC& Range, STRING *StringBuffer,
	const DISPLAY_MARKER &Marker, DOCTYPE *DoctypePtr) const
{
  if (StringBuffer == NULL)
    return false;

  StringBuffer->Clear();

  if (Range.IsEmpty())
    return false;

  if (RecordEnd < RecordStart)
    return false;

  const GPTYPE start = Range.GetFieldStart();
  const GPTYPE end   = Range.GetFieldEnd();

  const GPTYPE maxLocal =
      RecordEnd - RecordStart;

  if (end < start || end > maxLocal)
    return false;

  const size_t length =
      end - start + 1;

  if (::GetRecordData( GetFullFileName(), StringBuffer, RecordStart + start,
          length,
          DoctypePtr) == 0)
    return false;

  //
  // Raw display requested.
  //
  if (Marker.IsEmpty())
    return true;
#if 1

  //
  // Build marker boundary events using coordinates relative
  // to the selected display range.
  //
  // We cannot simply insert markers hit-by-hit: evidence FCs
  // may contain lexical FCs, and inserting an outer start marker
  // would shift coordinates of an inner hit not yet processed.
  //
  struct MARK_EVENT
  {
    GPTYPE        Pos;      // zero-based boundary in StringBuffer
    unsigned char Order;
    const STRING *Text;

    MARK_EVENT( GPTYPE pos, unsigned char order, const STRING *text)
      : Pos(pos), Order(order), Text(text)
    {
    }
  };

  //
  // Desired final ordering when several markers share a boundary:
  //
  //   LexicalEnd EvidenceEnd EvidenceStart LexicalStart
  //
  // Thus an exact coincident lexical/nonlexical range becomes:
  //
  //   [ ||text|| ]
  //
  // Since repeated Insert() calls at the same position prepend
  // to what was inserted there previously, events at an equal
  // position are executed in the reverse of this final order.
  //
  enum
  {
    MarkLexicalEnd   = 0,
    MarkEvidenceEnd  = 1,
    MarkEvidenceStart= 2,
    MarkLexicalStart = 3
  };

  std::vector<MARK_EVENT> events;
  events.reserve(HitTable.Size() * 2);

  for (const auto& hit : HitTable)
    {
      //
      // We don't display half of an evidence item.
      //
      if (!Range.Contains(hit))
        continue;

#if _TRACK_TERM_IDENTITY
      const bool lexical = hit.GetSourceId() != 0;
#else
      const bool lexical = true;
#endif

      const GPTYPE hitStart = hit.GetFieldStart() - start;

      const GPTYPE hitEnd = hit.GetFieldEnd() - start + 1;

      if (lexical)
        {
          if (!Marker.LexicalStart.IsEmpty())
            events.push_back( MARK_EVENT( hitStart, MarkLexicalStart, &Marker.LexicalStart));

          if (!Marker.LexicalEnd.IsEmpty())
            events.push_back( MARK_EVENT( hitEnd, MarkLexicalEnd, &Marker.LexicalEnd));
        }
      else
        {
          if (!Marker.EvidenceStart.IsEmpty())
            events.push_back( MARK_EVENT( hitStart, MarkEvidenceStart, &Marker.EvidenceStart));

          if (!Marker.EvidenceEnd.IsEmpty())
            events.push_back( MARK_EVENT( hitEnd, MarkEvidenceEnd, &Marker.EvidenceEnd));
        }
    }

  std::sort(
      events.begin(),
      events.end(),
      [](const MARK_EVENT& a, const MARK_EVENT& b)
      {
        if (a.Pos != b.Pos)
          return a.Pos > b.Pos;

        //
        // Same position: execute reverse final-order because
        // STRING::Insert() puts the new text before the text
        // already inserted at this position.
        //
        return a.Order > b.Order;
      });

  for (const auto& event : events)
    StringBuffer->Insert( event.Pos + 1, *event.Text);



#else
  //
  // HitTable is sorted by FcLess:
  //
  //     FieldEnd ascending
  //     FieldStart descending for equal ends
  //
  // Therefore walking backwards lets us insert markers without
  // invalidating coordinates still to be processed.
  //
  STRING BeforeTerm = Marker.ZZ
  for (auto it = HitTable.rbegin(); it != HitTable.rend(); ++it)
    {
      const auto& hit = *it;

      if (hit.GetFieldEnd() < start)
        break;

      if (hit.GetFieldStart() > end)
        continue;

      //
      // Don't mark partial hits at the display boundary.
      //
      if (!Range.Contains(hit))
        continue;

      const GPTYPE hitStart = hit.GetFieldStart() - start;

      const GPTYPE hitEnd = hit.GetFieldEnd() - start;

      if (!AfterTerm.IsEmpty())
        StringBuffer->Insert( hitEnd + 2, AfterTerm);

      if (!BeforeTerm.IsEmpty())
        StringBuffer->Insert( hitStart + 1, BeforeTerm);
    }
#endif
  return true;
}


STRING RESULT::GetRawRecordData(const FC &fc, IDBOBJ *idb) const
{
  STRING result;
  MDTREC mdtrec;

  if (idb &&  idb->GetMainMdt()->GetEntry (GetMdtIndex(), &mdtrec))
    {
      const off_t offset = mdtrec.GetLocalRecordStart();
      ::GetRecordData(GetFullFileName(), &result, fc.GetFieldStart() + offset, fc.GetLength(),
	 idb->GetDocTypePtr( DocumentType));
    }
  return result;

}

STRING RESULT::GetRawRecordData(const FC &fc, DOCTYPE *doc) const
{
  if (doc) return GetRawRecordData(fc, doc->Db);
  return NulString;
}



/*
  Pass FC for hit
  BeforeTerm, AfterTerm to insert before and after the hit
  DoctypePtr as the pointer to the DOCTYPE class to handle reading (the class in the RESULT)

   DoctypePtr from a RESULT  ResultRecord is (IDB class):
      GetDocTypePtr( ResultRecord.GetDocumentType() )

   Class IDB:
         DOCTYPE *GetDocTypePtr(const DOCTYPE_ID& DocType) const;

  Returns:

   StringBuffer -> Content
   Term         -> The term that the FC hits
   TagPtr       -> The tag/path of the hit

   TRUE/FALSE   -> If OK

*/
static const int PeerMinimumFieldLength = 5;

bool RESULT::PresentHit(const FC& Fc, STRING *StringBuffer, STRING *Term,
        const STRING& BeforeTerm, const STRING& AfterTerm, DOCTYPE *DoctypePtr,
 	STRING *TagPtr) const
{
  if (StringBuffer)
    StringBuffer->Clear();
  if (Term)
    Term->Clear();
  if (TagPtr)
    TagPtr->Clear();

  if (!Fc.IsEmpty())
    {
      const GPTYPE start      = Fc.GetFieldStart();
      const GPTYPE end        = Fc.GetFieldEnd();
      GPTYPE       localStart = start; // (start > 44 ? start - 1 : 0); // was -40
      GPTYPE       localEnd   = end; // was +60 + (end-start)/2;
      GPTYPE       peer_end   = RecordEnd;
 
      bool  skipToFirstWord = true;

#if 1
      if (DoctypePtr && DoctypePtr->Db) {
	IDBOBJ *idb = DoctypePtr->Db;
	MDTREC  mdtrec;
	STRING  Tag;
        if ( idb->GetMainMdt()->GetEntry (GetMdtIndex(), &mdtrec))
	  {
	     off_t offset = mdtrec.GetGlobalFileStart() + mdtrec.GetLocalRecordStart();
             FC     PeerFC = idb->GetPeerFc(FC(Fc)+=offset,&Tag);

	     if (TagPtr) *TagPtr = Tag;

	     GPTYPE peer_start = PeerFC.GetFieldStart() - offset;
	     peer_end   = PeerFC.GetFieldEnd() - offset ;  /***** Add + 1 ? // 2022 ***/;

	     if (peer_end - peer_start < 200) {

		// Min
		if ((peer_end - peer_start) > (end - start + PeerMinimumFieldLength)) {

		  localEnd = peer_end;
		  localStart = peer_start;
		  skipToFirstWord = false;
		}
		// else localEnd =   PeerFC.GetFieldEnd() ; // ???  2022
	     } else {
		if (peer_end < localEnd || ((peer_end - localEnd) < 200))
		  localEnd = peer_end;
		if (peer_start > localStart || (localStart - peer_start < 200)) {
		  localStart = peer_start;
		  skipToFirstWord = false;
		}
	     }
	  }
      }
#endif

      if (RecordEnd < RecordStart)
	{
	  return false;
	}

      if ((localEnd + RecordStart) > RecordEnd)
      {
	localEnd = RecordEnd - RecordStart; // Don't cross boundaries
      }

      if (localEnd <= localStart)
	{
	  return false;
	}

      const size_t Length = localEnd - localStart + 1;


//    if (Length > BUFSIZ) Length = BUFSIZ;
      STRING strPtr;


      if (::GetRecordData(GetFullFileName(), &strPtr, localStart + RecordStart, Length, DoctypePtr) == 0)
	return false;

      REGISTER unsigned char *ptr = (unsigned char *)(strPtr.c_str());
      if (StringBuffer)
	{
	  REGISTER unsigned char *tcp = ptr;
	  if (localStart && skipToFirstWord)
	    {
	      while (!isspace(*tcp))
		tcp++; // Skip to first word..
	    }
	  int i = tcp - ptr;
	  if (i > (int)(start - localStart))
	    {
	      i = 0;
	      tcp = ptr;
	    }
	  else if (localStart && i)
	    {
	      StringBuffer->Cat ("... ");
	      i -= 4; // Offset from "... "
	    }
	  for (; *tcp; tcp++)
	    {
	      if (*tcp == '<' || *tcp == '>' || *tcp == '&')
		{
		  StringBuffer->Cat ('.');
		}
	      else
		{
		  StringBuffer->Cat (*tcp);
		}
	    }

#if 0
cerr << "Build copy" << endl;
        char tmp[1024];
        strncpy(tmp, StringBuffer->c_str() + start - localStart - i, end - start +1);
        tmp[end-start+1] = 0;
cerr << "Term= \"" << tmp << "\"" << endl;
#endif

	  if (AfterTerm.GetLength())
	    StringBuffer->Insert(end - localStart - i + 2, AfterTerm);
	  if (BeforeTerm.GetLength())
	    StringBuffer->Insert(start - localStart - i + 1, BeforeTerm);
	  // Remove multiple empty spaces...
	  StringBuffer->Pack();
	  if (localEnd != peer_end)
	    StringBuffer->Cat ("...");
	}
      if (Term)
	{
	  unsigned char *term = ptr + (start - localStart);
	  term[end - start + 1] = '\0';
	  *Term = term;
	}
      return true;
    }
  return false;
}

#if 1
bool RESULT::XMLPresentBestContextHit(STRING *StringBuffer, const STRING& Tag,
   STRING *Term, DOCTYPE *DoctypePtr) const
{
  return XMLPresentHit(GetBestContextHit(), StringBuffer, Tag, Term, DoctypePtr);
}
#endif

#if 1
bool RESULT::XMLPresentHit(const FC& Fc, STRING *StringBuffer, const STRING& Tag,
   STRING *Term, DOCTYPE *DoctypePtr) const
{
      STRING  hitTag;
      const GPTYPE start = Fc.GetFieldStart();
      const GPTYPE end   = Fc.GetFieldEnd();
      GPTYPE       localStart = (start > 44 ? start - 40 : 0);
      GPTYPE       localEnd   = end + 60 + (end-start)/2;
      GPTYPE       peer_end = RecordEnd;
      bool  skipToFirstWord = true;

      if (StringBuffer) StringBuffer->Clear();

      if (DoctypePtr && DoctypePtr->Db) {
        IDBOBJ *idb = DoctypePtr->Db;
        MDTREC  mdtrec;
        if ( idb->GetMainMdt()->GetEntry (GetMdtIndex(), &mdtrec))
          {
             off_t offset = mdtrec.GetGlobalFileStart() + mdtrec.GetLocalRecordStart();
             FC     PeerFC = idb->GetPeerFc(FC(Fc)+=offset,&hitTag);

             GPTYPE peer_start = PeerFC.GetFieldStart() - offset;

             peer_end   = PeerFC.GetFieldEnd() - offset ; /* 2022 add +1 ?? */;

             if (peer_end - peer_start < 200) {
		// Min
		if ((peer_end - peer_start) > (end - start + PeerMinimumFieldLength)) {
                  localEnd = peer_end;
                  localStart = peer_start;
                  skipToFirstWord = false;
		}
             } else {
                if (peer_end < localEnd || ((peer_end - localEnd) < 200))
                  localEnd = peer_end;
                if (peer_start > localStart || (localStart - peer_start < 200)) {
                  localStart = peer_start;
                  skipToFirstWord = false;
                }
             }
          }
      } // Have a Doctype Pointer 

      if (localEnd > RecordEnd) localEnd = RecordEnd - RecordStart;

      if (localStart > localEnd)
	{
	  message_log (LOG_PANIC, "Start after End in RESULT::XMLPresentNthHit()");
StringBuffer->form("ERROR (%ld,%ld) not inside Record (%ld,%ld)",  start, end, RecordStart, RecordEnd);
	  return false;
	}

      const size_t Length = localEnd - localStart + 1;

//    if (Length > BUFSIZ) Length = BUFSIZ;
      STRING strPtr;
      if (::GetRecordData(GetFullFileName(), &strPtr, localStart + RecordStart, Length, DoctypePtr) == 0)
	return false;
      REGISTER unsigned char *ptr = (unsigned char *)(strPtr.c_str());
      if (StringBuffer)
	{
	  STRING Context;
	  unsigned char *tcp = ptr;

	  if (localStart && skipToFirstWord)
	    {
	      while (!isspace(*tcp) && *tcp)
		tcp++; // Skip to first word..
	    }
	  int i = tcp - ptr;

          if (i > (int)(start - localStart))
	    {
	      Context.Cat ("...");
	      i = -3;
	      tcp = ptr;
	    }
	  else if (localStart && i)
	    {
	      Context.Cat ("... ");
	      i -= 4; // Offset from "... "
	    }
	  Context.Cat(tcp);
	  Context.Insert(end - localStart - i + 2, "\002");
	  Context.Insert(start - localStart - i + 1, "\001");
	  // Remove multiple empty spaces...
	  Context.Pack();
#if 0
	  if (localEnd != RecordEnd) Context.Cat ("...");
#else
	  if (localEnd != peer_end) Context.Cat ("...");
#endif
	  const int Context_len = (int) Context.Length();
	  const BYTE   charsetId =  Locale.GetCharsetId();
	  CHARSET charset (charsetId);
	  for (i=1; i <= Context_len; i++)
	    {
	      char buf[14]; // max 4294967295 although most is only short (UCS-2)
	      unsigned int wchar;
	      unsigned char ch;

	      if ((ch = Context.GetUChr(i)) == '\001')
		{
		  *StringBuffer << "<" << Tag;
		  if (hitTag.GetLength())
		    *StringBuffer << " CONTAINER_NAME=\"" << hitTag << "\"";
		  *StringBuffer << ">";
		}
	      else if (ch == '\002')
		{
		  *StringBuffer << "</" << Tag << ">";
		}
	      else if ((wchar = (unsigned)charset.UCS(ch)) < 0x7f && wchar > 0x1f)
		{
		  if (wchar == '<')
		    StringBuffer->Cat("&lt;");
		  else if (wchar == '>')
		    StringBuffer->Cat("&gt;");
		  else if (wchar == '&')
		    StringBuffer->Cat("&amp;");
		  else
		    StringBuffer->Cat(ch);
		}
	      else if (wchar == 160)
		{
		  StringBuffer->Cat("&nbsp;");
		}
	      else if (wchar == 173)
		{
		  StringBuffer->Cat("&shy;");
		}
	      else
		{
		  // Non-ASCII character -- Map to UCS
		  const int length = std::snprintf( buf, sizeof buf, "&#%lu;", static_cast<unsigned long>(wchar));
		  if (length >= 0) StringBuffer-> Cat (buf);
		}
	    }
	}
      if (Term)
	{
	  unsigned char *term = ptr + (start - localStart);
	  term[end - start + 1] = '\0';
	  *Term = term;
	}
   return true;
}
#endif

bool RESULT::XMLPresentNthHit(size_t N, STRING *StringBuffer, const STRING& Tag,
   STRING *Term, DOCTYPE *DoctypePtr) const
{
  IRESULT::hit_type Fc;

  if (StringBuffer)
    StringBuffer->Clear();
  if (Term)
    Term->Clear();
  if (HitTable.GetEntry(N, &Fc))
    {
#if 1
      STRING  hitTag;

      if (XMLPresentHit(Fc,StringBuffer, Tag, Term, DoctypePtr) == false)
	return false;
#else
      const GPTYPE start = Fc.GetFieldStart();
      const GPTYPE end   = Fc.GetFieldEnd();
      GPTYPE       localStart = (start > 44 ? start - 40 : 0);
      GPTYPE       localEnd   = end + 60 + (end-start)/2;
      GPTYPE       peer_end = RecordEnd;
      bool  skipToFirstWord = true;

      if (DoctypePtr && DoctypePtr->Db) {
        IDBOBJ *idb = DoctypePtr->Db;
        MDTREC  mdtrec;
        if ( idb->GetMainMdt()->GetEntry (GetMdtIndex(), &mdtrec))
          {
             off_t offset = mdtrec.GetGlobalFileStart() + mdtrec.GetLocalRecordStart();
             FC     PeerFC = idb->GetPeerFc(FC(Fc)+=offset,&hitTag);

             GPTYPE peer_start = PeerFC.GetFieldStart() - offset;

             peer_end   = PeerFC.GetFieldEnd() - offset ; /* 2022 add +1 ?? */;

             if (peer_end - peer_start < 200) {
		// Min
		if ((peer_end - peer_start) > (end - start + PeerMinimumFieldLength)) {
                  localEnd = peer_end;
                  localStart = peer_start;
                  skipToFirstWord = false;
		}
             } else {
                if (peer_end < localEnd || ((peer_end - localEnd) < 200))
                  localEnd = peer_end;
                if (peer_start > localStart || (localStart - peer_start < 200)) {
                  localStart = peer_start;
                  skipToFirstWord = false;
                }
             }
          }
      }

      if (localEnd > RecordEnd) localEnd = RecordEnd - RecordStart;

      if (localStart > localEnd)
	{
	  message_log (LOG_PANIC, "Start after End in RESULT::XMLPresentNthHit()");
StringBuffer->form("ERROR (%ld,%ld) not inside Record (%ld,%ld)",  start, end, RecordStart, RecordEnd);
	  return false;
	}

      const size_t Length = localEnd - localStart + 1;

//    if (Length > BUFSIZ) Length = BUFSIZ;
      STRING strPtr;
      if (::GetRecordData(GetFullFileName(), &strPtr, localStart + RecordStart, Length, DoctypePtr) == 0)
	return false;
      REGISTER unsigned char *ptr = (unsigned char *)(strPtr.c_str());
      if (StringBuffer)
	{
	  STRING Context;
	  unsigned char *tcp = ptr;

	  if (localStart && skipToFirstWord)
	    {
	      while (!isspace(*tcp) && *tcp)
		tcp++; // Skip to first word..
	    }
	  int i = tcp - ptr;

          if (i > (int)(start - localStart))
	    {
	      Context.Cat ("...");
	      i = -3;
	      tcp = ptr;
	    }
	  else if (localStart && i)
	    {
	      Context.Cat ("... ");
	      i -= 4; // Offset from "... "
	    }
	  Context.Cat(tcp);
	  Context.Insert(end - localStart - i + 2, "\002");
	  Context.Insert(start - localStart - i + 1, "\001");
	  // Remove multiple empty spaces...
	  Context.Pack();
#if 0
	  if (localEnd != RecordEnd) Context.Cat ("...");
#else
	  if (localEnd != peer_end) Context.Cat ("...");
#endif
	  const int Context_len = (int) Context.Length();
	  const BYTE   charsetId =  Locale.GetCharsetId();
	  CHARSET charset (charsetId);
	  for (i=1; i <= Context_len; i++)
	    {
	      char buf[14]; // max 4294967295 although most is only short (UCS-2)
	      unsigned int wchar;
	      unsigned char ch;

	      if ((ch = Context.GetUChr(i)) == '\001')
		{
		  *StringBuffer << "<" << Tag;
		  if (hitTag.GetLength())
		    *StringBuffer << " CONTAINER_NAME=\"" << hitTag << "\"";
		  *StringBuffer << ">";
		}
	      else if (ch == '\002')
		{
		  *StringBuffer << "</" << Tag << ">";
		}
	      else if ((wchar = (unsigned)charset.UCS(ch)) < 0x7f && wchar > 0x1f)
		{
		  if (wchar == '<')
		    StringBuffer->Cat("&lt;");
		  else if (wchar == '>')
		    StringBuffer->Cat("&gt;");
		  else if (wchar == '&')
		    StringBuffer->Cat("&amp;");
		  else
		    StringBuffer->Cat(ch);
		}
	      else if (wchar == 160)
		{
		  StringBuffer->Cat("&nbsp;");
		}
	      else if (wchar == 173)
		{
		  StringBuffer->Cat("&shy;");
		}
	      else
		{
		  // Non-ASCII character -- Map to UCS
                  const int length = std::snprintf( buf, sizeof buf, "&#%lu;", static_cast<unsigned long>(wchar));
		  if (length) StringBuffer-> Cat (buf);
		}
	    }
	}
      if (Term)
	{
	  unsigned char *term = ptr + (start - localStart);
	  term[end - start + 1] = '\0';
	  *Term = term;
	}
      return true;
#endif
    }
  return false;
}

void RESULT::GetHighlightedRecord(const STRING& BeforeTerm, const STRING& AfterTerm,
	STRING *StringBuffer, DOCTYPE *DoctypePtr) const
{
  GetRecordData(StringBuffer, DoctypePtr);

  GPTYPE End = StringBuffer->GetLength() + 1;
  GPTYPE Start = End;

  // process terms backwards
  for (const FC& Fc : HitTable)
    {
      // @@@ Highlight Bugfix workaround (edz@nonmonotonic.com)
      // see also fct.cxx
      if ( Fc.GetFieldEnd() < End && Fc.GetFieldStart() < Start)
	{
	  End = Fc.GetFieldEnd();
	  Start = Fc.GetFieldStart();
	  StringBuffer->Insert(End + 2, AfterTerm);
	  StringBuffer->Insert(Start + 1, BeforeTerm);
	}
    }
}


// Get the Range of the record with bits highlighted..
void RESULT::GetHighlighted(const STRING& BeforeTerm, const STRING& AfterTerm, FC Range,
	STRING *StringBuffer, DOCTYPE *DoctypePtr) const
{
  StringBuffer->Clear();
  const GPTYPE rs = RecordStart + Range.GetFieldStart();
  const GPTYPE re = Range.GetFieldEnd();

  // Make sure that the range is consistant..
  if (rs > re || re > RecordEnd || rs < RecordStart)
    return; // Error

  const size_t size = re - rs + 1;

  if (::GetRecordData( GetFullFileName(), StringBuffer, rs, size, DoctypePtr) == 0)
    return; // Error

  // Now have the data...
  GPTYPE End = size + 1;
  GPTYPE Start = End;
  // process terms backwards
  for (const FC &Fc : HitTable)
    {
      const GPTYPE end = Fc.GetFieldEnd();
      const GPTYPE start = Fc.GetFieldStart();
      // @@@ Highlight Bugfix workaround (edz@nonmonotonic.com)
      // see also fct.cxx
      if ( end < End && start < Start)
	{
	  End = end;
	  Start = start;
	  StringBuffer->Insert(End + 2, AfterTerm);
	  StringBuffer->Insert(Start + 1, BeforeTerm);
        }
    }
}



RESULT::~RESULT()
{
#ifdef DEBUG_MEMORY
  if (--__IB_RESULT_allocated_count < 0)
    message_log (LOG_PANIC, "RESULT global allocated count %ld < 0!", (long)__IB_RESULT_allocated_count);
#endif
}


static GPTYPE EvidenceGap(const FC& a, const FC& b)
{
    if (a.GetFieldEnd() < b.GetFieldStart())
        return b.GetFieldStart() - a.GetFieldEnd();

    if (b.GetFieldEnd() < a.GetFieldStart())
        return a.GetFieldStart() - b.GetFieldEnd();

    return 0;
}


static bool Contains(const FC& outer, const FC& inner)
{
    return outer.GetFieldStart() <= inner.GetFieldStart() &&
           outer.GetFieldEnd()   >= inner.GetFieldEnd();
}

static bool Dominates(
    const EVIDENCE_COVER& a,
    const EVIDENCE_COVER& b)
{
    return Contains(b.extent, a.extent) &&
           a.Energy     >= b.Energy &&
           a.Dispersion <= b.Dispersion;
}

struct EVIDENCE_COVER_PARAMS
{
    double EvidenceWeight   = 1.0;
    double DispersionWeight = 1.0;
    double DispersionDecay  = 32.0;
};


#if 0
double EVIDENCE_COVER::Rank(const EVIDENCE_COVER_PARAMS& p) const
{
    const double compactness =
        1.0 / (1.0 + Dispersion / p.DispersionDecay);

    return pow(Energy, p.EvidenceWeight) *
           pow(compactness, p.DispersionWeight);
}


static COVER_WORK AddOpaqueEvidence(
    const COVER_WORK& lexical,
    const HIT& opaque,
    UINT totalEvidence)
{
    COVER_WORK result = lexical;

    const GPTYPE gap =
        EvidenceGap(lexical.cover.extent, opaque);

    const GPTYPE start =
        std::min(lexical.cover.extent.GetFieldStart(),
                 opaque.GetFieldStart());

    const GPTYPE end =
        std::max(lexical.cover.extent.GetFieldEnd(),
                 opaque.GetFieldEnd());

    result.cover.extent = FC(start, end);

    //
    // Existing dispersion represents (N-1) relationships.
    // Adding another independent evidence item adds one more.
    //
    const DOUBLE accumulated =
        lexical.cover.Dispersion *
        (lexical.evidence > 1
            ? lexical.evidence - 1
            : 0);

    ++result.evidence;

    result.cover.Dispersion =
        result.evidence > 1
            ? (accumulated + gap) /
              (DOUBLE)(result.evidence - 1)
            : 0.0;

    result.cover.Energy =
        totalEvidence
            ? (DOUBLE)result.evidence /
              (DOUBLE)totalEvidence
            : 1.0;

    return result;
}
#endif



using HIT = IRESULT::hit_type;

//
// First experimental parameters.
//
// Later these can be moved into the ranking/profile configuration.
//
static const DOUBLE EvidenceWeight   = 1.5; // was 1.0;
static const DOUBLE DispersionWeight = 1.0;
static const DOUBLE DispersionDecay  = 32.0;


struct COVER_CANDIDATE
{
  EVIDENCE_COVER cover;
  UINT           evidence;
};


static FCSOURCE HitSource(const HIT& hit)
{
#if _TRACK_TERM_IDENTITY
  return hit.GetSourceId();
#else
  return 0;
#endif
}


static bool HitByStart(const HIT& a, const HIT& b)
{
  if (a.GetFieldStart() != b.GetFieldStart())
    return a.GetFieldStart() < b.GetFieldStart();

  return a.GetFieldEnd() < b.GetFieldEnd();
}


static FC CoverExtent(const FC& a, const FC& b)
{
  return FC(
    std::min(a.GetFieldStart(), b.GetFieldStart()),
    std::max(a.GetFieldEnd(),   b.GetFieldEnd()));
}


static DOUBLE ExtentWidth(const FC& fc)
{
  const GPTYPE start = fc.GetFieldStart();
  const GPTYPE end   = fc.GetFieldEnd();

  return end >= start
       ? static_cast<DOUBLE>(end - start + 1)
       : 0.0;
}


//
// For this first trial:
//
//   Energy     = how much independent evidence participates.
//
//   Dispersion = how much address space is required per
//                participating evidence item.
//
// Using extent width here is intentional.  A datatype/vector
// hit covering an entire paragraph should be less specific than
// one covering a short field, even when lexical evidence lies
// entirely inside it.
//
static COVER_CANDIDATE MakeCover(
    const FC& extent,
    UINT evidence,
    UINT possibleEvidence)
{
  COVER_CANDIDATE candidate;

  candidate.cover.extent = extent;
  candidate.evidence     = evidence;

  candidate.cover.Energy =
    possibleEvidence
      ? static_cast<DOUBLE>(evidence) /
        static_cast<DOUBLE>(possibleEvidence)
      : 1.0;

  candidate.cover.Dispersion =
    evidence
      ? ExtentWidth(extent) /
        static_cast<DOUBLE>(evidence)
      : ExtentWidth(extent);

  return candidate;
}


static DOUBLE CoverRank(const EVIDENCE_COVER& cover)
{
  const DOUBLE decay =
    DispersionDecay > 0.0 ? DispersionDecay : 1.0;

  const DOUBLE compactness =
    1.0 / (1.0 + cover.Dispersion / decay);

  return
    std::pow(cover.Energy, EvidenceWeight) *
    std::pow(compactness, DispersionWeight);
}



//
// Find all minimal windows containing K distinct lexical SourceIds.
//
// Hits must be ordered by FieldStart.
//
static void FindLexicalCovers(
    const std::vector<HIT>& hits,
    UINT K,
    UINT possibleEvidence,
    std::vector<COVER_CANDIDATE> *covers)
{
  if (covers == NULL || hits.empty() || K == 0)
    return;

  std::map<FCSOURCE, UINT> counts;

  //
  // Needed because FCs are intervals: the last hit by start GP
  // does not necessarily have the greatest FieldEnd.
  //
  std::multiset<GPTYPE> ends;

  size_t left = 0;
  UINT distinct = 0;

  for (size_t right = 0; right < hits.size(); ++right)
  {
    const FCSOURCE source = HitSource(hits[right]);

    if (source == 0)
      continue; // Should not be in lexical vector anyway.

    if (++counts[source] == 1)
      ++distinct;

    ends.insert(hits[right].GetFieldEnd());

    //
    // Too many distinct sources.  Move the left boundary.
    //
    while (distinct > K && left <= right)
    {
      const FCSOURCE leftSource = HitSource(hits[left]);

      auto e = ends.find(hits[left].GetFieldEnd());
      if (e != ends.end())
        ends.erase(e);

      auto c = counts.find(leftSource);

      if (c != counts.end())
      {
        if (--c->second == 0)
        {
          counts.erase(c);
          --distinct;
        }
      }

      ++left;
    }

    //
    // Remove redundant occurrences at the left boundary.
    //
    while (distinct == K && left <= right)
    {
      const FCSOURCE leftSource = HitSource(hits[left]);

      auto c = counts.find(leftSource);

      if (c == counts.end() || c->second <= 1)
        break;

      auto e = ends.find(hits[left].GetFieldEnd());
      if (e != ends.end())
        ends.erase(e);

      --c->second;
      ++left;
    }

    if (distinct != K || ends.empty())
      continue;

    //
    // The right boundary must itself contribute something.
    // Otherwise this is not a minimal K-source window.
    //
    auto r = counts.find(source);

    if (r == counts.end() || r->second != 1)
      continue;

    const GPTYPE start = hits[left].GetFieldStart();
    const GPTYPE end   = *ends.rbegin();

    covers->push_back(
      MakeCover(FC(start, end), K, possibleEvidence));
  }
}


EVIDENCE_COVERS RESULT::GetEvidenceCovers(size_t Max) const
{
  EVIDENCE_COVERS result;

  if (HitTable.IsEmpty())
    return result;

  std::vector<HIT> lexical;
  std::vector<HIT> opaque;

  lexical.reserve(HitTable.GetTotalEntries());
  opaque.reserve(HitTable.GetTotalEntries());

  std::set<FCSOURCE> lexicalSourceSet;

  for (const auto& hit : HitTable)
  {
    const FCSOURCE source = HitSource(hit);

    if (source != 0)
    {
      lexical.push_back(hit);
      lexicalSourceSet.insert(source);
    }
    else
    {
      //
      // A real FC hit without lexical provenance:
      // Date, Numeric, HNSW, etc.
      //
      opaque.push_back(hit);
    }
  }

  std::sort(lexical.begin(), lexical.end(), HitByStart);
  std::sort(opaque.begin(),  opaque.end(),  HitByStart);

  const UINT lexicalSources =
    static_cast<UINT>(lexicalSourceSet.size());

  //
  // AuxCount is our best statement of how much independent
  // query evidence is represented by this RESULT.
  //
  UINT possibleEvidence = GetAuxCount();

  if (possibleEvidence < lexicalSources)
    possibleEvidence = lexicalSources;

  //
  // If anonymous FC evidence exists but AuxCount does not
  // reflect it, allow one anonymous evidence dimension.
  //
  if (!opaque.empty() && possibleEvidence <= lexicalSources)
    possibleEvidence = lexicalSources + 1;

  if (possibleEvidence == 0)
    possibleEvidence = 1;


  std::vector<COVER_CANDIDATE> candidates;


  //
  // --------------------------------------------------------
  // 1. Lexical evidence covers.
  // --------------------------------------------------------
  //
  // K=1 is useful internally because it can combine with an
  // anonymous Date/HNSW/etc. FC.  We normally don't retain
  // bare one-term contexts for a multi-evidence query.
  //
  for (UINT K = 1; K <= lexicalSources; ++K)
  {
    std::vector<COVER_CANDIDATE> lexicalCovers;

    FindLexicalCovers(
      lexical,
      K,
      possibleEvidence,
      &lexicalCovers);

    for (const auto& lexicalCover : lexicalCovers)
    {
      //
      // Keep lexical-only candidates when they contain at
      // least two distinct lexical sources, or this really
      // is a one-source result.
      //
      if (K >= 2 || possibleEvidence == 1)
        candidates.push_back(lexicalCover);

      //
      // ----------------------------------------------------
      // 2. Combine with anonymous/nonlexical FC evidence.
      // ----------------------------------------------------
      //
      // At present all SourceId==0 hits are anonymous.
      // We therefore treat each FC as an alternative for
      // ONE anonymous evidence dimension, not as mutually
      // independent evidence.
      //
      for (const auto& anonymousHit : opaque)
      {
        const FC extent =
          CoverExtent(lexicalCover.cover.extent,
                      anonymousHit);

        candidates.push_back(
          MakeCover(
            extent,
            lexicalCover.evidence + 1,
            possibleEvidence));
      }
    }
  }


  //
  // Anonymous-only covers.
  //
  // Important for pure Date/Numeric/HNSW searches.
  //
  for (const auto& hit : opaque)
  {
    candidates.push_back(
      MakeCover(hit, 1, possibleEvidence));
  }


  if (candidates.empty())
    return result;


  //
  // --------------------------------------------------------
  // 3. Collapse identical extents.
  // --------------------------------------------------------
  //
  // If the same extent was discovered with different amounts
  // of evidence, keep the one carrying the most evidence.
  //
  std::sort(
    candidates.begin(),
    candidates.end(),
    [](const COVER_CANDIDATE& a,
       const COVER_CANDIDATE& b)
    {
      const GPTYPE as = a.cover.extent.GetFieldStart();
      const GPTYPE bs = b.cover.extent.GetFieldStart();

      if (as != bs)
        return as < bs;

      const GPTYPE ae = a.cover.extent.GetFieldEnd();
      const GPTYPE be = b.cover.extent.GetFieldEnd();

      if (ae != be)
        return ae < be;

      return a.evidence > b.evidence;
    });


  std::vector<COVER_CANDIDATE> unique;
  unique.reserve(candidates.size());

  for (const auto& candidate : candidates)
  {
    if (!unique.empty() &&
        unique.back().cover.extent.GetFieldStart() ==
          candidate.cover.extent.GetFieldStart() &&
        unique.back().cover.extent.GetFieldEnd() ==
          candidate.cover.extent.GetFieldEnd())
    {
      //
      // Sorted by evidence descending, therefore the first
      // one for this FC is the strongest.
      //
      continue;
    }

    unique.push_back(candidate);
  }


  //
  // --------------------------------------------------------
  // 4. Rank the covers.
  // --------------------------------------------------------
  //
  std::sort(
    unique.begin(),
    unique.end(),
    [](const COVER_CANDIDATE& a,
       const COVER_CANDIDATE& b)
    {
      const DOUBLE ar = CoverRank(a.cover);
      const DOUBLE br = CoverRank(b.cover);

      if (ar != br)
        return ar > br;

      if (a.cover.Energy != b.cover.Energy)
        return a.cover.Energy > b.cover.Energy;

      if (a.cover.Dispersion != b.cover.Dispersion)
        return a.cover.Dispersion < b.cover.Dispersion;

      return a.cover.extent.GetFieldStart() <
             b.cover.extent.GetFieldStart();
    });


  const size_t count =
    Max && Max < unique.size()
      ? Max
      : unique.size();

  result.reserve(count);

  for (size_t i = 0; i < count; ++i)
    result.push_back(unique[i].cover);

  return result;
}


FC RESULT::GetBestContextHit() const
{
  const EVIDENCE_COVERS covers =
    GetEvidenceCovers(1);

  if (!covers.empty())
    return covers[0].extent;

  return FC();
}


// Hyper-paramters for Display
static const DOUBLE DisplayStructureBonus  = 0.05;
static const DOUBLE DisplayOverageWeight   = 0.25;
static const DOUBLE DisplayOverageExponent = 2.0;
static const DOUBLE DisplayHardLimitFactor = 4.0;


// MaxBytesAdvice expresses the expected human attention budget; the cost function
// allows evidence to spend beyond that budget when the marginal value justifies it.

static DOUBLE DisplayUtility(
    const EVIDENCE_COVER& evidence,
    const FC& displayExtent,
    size_t MaxBytesAdvice,
    bool structural)
{
  const DOUBLE bytes = ExtentWidth(displayExtent);

  if (bytes <= 0.0)
    return -MAXFLOAT;

  //
  // Guardrail only.  Normal selection should be determined
  // by the soft cost below.
  //
  if (MaxBytesAdvice &&
      bytes > MaxBytesAdvice * DisplayHardLimitFactor)
    return -MAXFLOAT;

  DOUBLE value = CoverRank(evidence);

  //
  // A complete structural unit is slightly preferable to an
  // arbitrary positional cut when its attention cost is reasonable.
  //
  if (structural)
    value *= (1.0 + DisplayStructureBonus);

  DOUBLE over = 0.0;

  if (MaxBytesAdvice && bytes > MaxBytesAdvice)
    over =
      (bytes - static_cast<DOUBLE>(MaxBytesAdvice)) /
      static_cast<DOUBLE>(MaxBytesAdvice);

  const DOUBLE cost =
    DisplayOverageWeight *
    std::pow(over, DisplayOverageExponent);

  return value - cost;
}


static DOUBLE DisplayEvidenceValue(
    const EVIDENCE_COVER& evidence)
{
  const DOUBLE decay =
    DispersionDecay > 0.0 ? DispersionDecay : 1.0;

  const DOUBLE compactness =
    1.0 / (1.0 + evidence.Dispersion / decay);

  return
    std::pow(evidence.Energy, 1.5) *
    std::pow(compactness, DispersionWeight);
}


#if 0

FC RESULT::GetDisplayEvidence(size_t MaxBytesAdvice, DOCTYPE *DoctypePtr) const
{
//    cerr << "GetDisplayEvidence(MAX): " << MaxBytesAdvice << " doctype=" << (void *)DoctypePtr << endl ;
 
  // DISPLAY_EVIDENCE result; 

  if (HitTable.IsEmpty() || RecordEnd < RecordStart)
    return FC();

  const GPTYPE recordEnd = RecordEnd - RecordStart;

  const EVIDENCE_COVERS covers = GetEvidenceCovers(0);
 
  //
  // ========================================================
  // UNBOUNDED DISPLAY
  // ========================================================
  //
  // Max == 0 means:
  //
  //     preserve the strongest/full evidence.
  //
  // No attention-cost model, no shortening, no presentation
  // expansion.  This is the useful agent / diagnostic form.
  //
  if (MaxBytesAdvice == 0)
    {
      if (!covers.empty())
        return covers[0].extent;

      //
      // Defensive fallback.
      //
      for (const auto& hit : HitTable)
        return hit;

      return FC();
    }


  //
  // ========================================================
  // BOUNDED DISPLAY
  // ========================================================
  //
  // Attention now has a cost.
  //
  // Full/partial covers and individual evidence may compete.
  //
  FC             best;
  EVIDENCE_COVER bestEvidence;

  DOUBLE bestUtility = 0.0;

  bool haveBest         = false;
  bool bestIsSingle     = false;
  bool bestHasStructure = false;


  //
  // --------------------------------------------------------
  // Evidence candidate comparator.
  // --------------------------------------------------------
  //
  // This chooses WHAT evidence is worth showing.
  //
  // Structure does not compete here.  Structure is considered
  // later as a presentation envelope around the winner.
  //
  auto consider =
    [&](const EVIDENCE_COVER& evidence,
        const FC& extent,
        bool single)
    {
      const GPTYPE start =
          extent.GetFieldStart();

      const GPTYPE end =
          extent.GetFieldEnd();

      if (end < start || end > recordEnd)
        return;

      const DOUBLE utility =
          DisplayUtility(
              evidence,
              extent,
              MaxBytesAdvice,
              false);

      bool better = false;

      if (!haveBest)
        {
          better = true;
        }
      else if (utility > bestUtility)
        {
          better = true;
        }
      else if (utility == bestUtility)
        {
          //
          // Deterministic tie breaking:
          //
          //     more evidence
          //     less dispersion
          //     shorter extent
          //     earlier occurrence
          //
          if (evidence.Energy > bestEvidence.Energy)
            {
              better = true;
            }
          else if (evidence.Energy == bestEvidence.Energy)
            {
              if (evidence.Dispersion <
                  bestEvidence.Dispersion)
                {
                  better = true;
                }
              else if (evidence.Dispersion ==
                       bestEvidence.Dispersion)
                {
                  const DOUBLE width =
                      ExtentWidth(extent);

                  const DOUBLE bestWidth =
                      ExtentWidth(best);

                  if (width < bestWidth)
                    {
                      better = true;
                    }
                  else if (width == bestWidth &&
                           extent.GetFieldStart() <
                           best.GetFieldStart())
                    {
                      better = true;
                    }
                }
            }
        }

      if (better)
        {
          best          = extent;
          bestEvidence  = evidence;
          bestUtility   = utility;
          bestIsSingle  = single;
          haveBest      = true;
        }
    };


  //
  // --------------------------------------------------------
  // 1. Full and partial evidence covers.
  // --------------------------------------------------------
  //
  for (const auto& cover : covers)
    {
      consider(
          cover,
          cover.extent,
          false);
    }


  //
  // --------------------------------------------------------
  // 2. Individual evidence candidates.
  // --------------------------------------------------------
  //
  // Singles are legitimate display alternatives in bounded
  // mode.  This is what lets:
  //
  //     whores ---------------- money
  //
  // or:
  //
  //     tools ----------------- quantum
  //
  // collapse to useful local evidence rather than forcing the
  // human to consume the entire span.
  //
  // Keep possibleEvidence consistent with GetEvidenceCovers().
  //
  std::set<FCSOURCE> lexicalSourceSet;
  bool haveOpaque = false;

  for (const auto& hit : HitTable)
    {
      const FCSOURCE source =
          HitSource(hit);

      if (source != 0)
        lexicalSourceSet.insert(source);
      else
        haveOpaque = true;
    }

  const UINT lexicalSources =
      static_cast<UINT>(
          lexicalSourceSet.size());

  UINT possibleEvidence =
      GetAuxCount();

  if (possibleEvidence < lexicalSources)
    possibleEvidence = lexicalSources;

  if (haveOpaque &&
      possibleEvidence <= lexicalSources)
    {
      possibleEvidence =
          lexicalSources + 1;
    }

  if (possibleEvidence == 0)
    possibleEvidence = 1;


  for (const auto& hit : HitTable)
    {
      const COVER_CANDIDATE candidate =
          MakeCover(
              hit,
              1,
              possibleEvidence);

      consider(
          candidate.cover,
          hit,
          true);
    }


  if (!haveBest)
    return FC();


  //
  // --------------------------------------------------------
  // 3. Natural structural presentation envelope.
  // --------------------------------------------------------
  //
  // Evidence selection is now finished.
  //
  // Spend at most:
  //
  //     ONE generic PEER discovery
  //
  // followed, only if required, by a bounded ancestor-path
  // search.
  //
  // Important:
  //
  // The field discovered by the generic PEER is useful even if
  // its FIELD_PATH contains only itself:
  //
  //     [ LINE ]
  //
  // Path depth only determines whether we can search upward.
  //
  if (DoctypePtr && DoctypePtr->Db)
    {
      IDBOBJ *idb =
          DoctypePtr->Db;

      MDTREC mdtrec;

      if (idb->GetMainMdt()->GetEntry(
              GetMdtIndex(),
              &mdtrec))
        {
          const GPTYPE offset =
              mdtrec.GetGlobalFileStart() +
              mdtrec.GetLocalRecordStart();


          //
          // Prefer a lexical point inside the winning evidence.
          //
          // SourceId==0 evidence may itself be a broad Date,
          // Numeric, HNSW, etc. interval.
          //
          GPTYPE anchor =
              best.GetFieldStart();

#if _TRACK_TERM_IDENTITY
          for (const auto& hit : HitTable)
            {
              if (hit.GetSourceId() != 0 &&
                  best.Contains(hit))
                {
                  anchor =
                      hit.GetFieldStart();

                  break;
                }
            }
#endif


          STRING deepestField;

          //
          // The ONE generic PEER operation.
          //
          // Keep the returned FC.  It may already be the natural
          // presentation envelope.
          //

          const FC anchorPeer = idb->GetPeerFc(anchor + offset, &deepestField);

          if (!deepestField.IsEmpty())
            {
              FC globalBest(best);
              globalBest += offset;

#if 0
  cerr << "STRUCT:"
         << " best(local)="
         << best.GetFieldStart() << "-"
         << best.GetFieldEnd()
         << " anchor(local)=" << anchor
         << " offset=" << offset
         << " best(global)="
         << globalBest.GetFieldStart() << "-"
         << globalBest.GetFieldEnd()
         << endl;
#endif

              FC     globalPeer;
              STRING peerField;

              bool havePeer = false;


              //
              // ------------------------------------------------
              // 3a. Deepest field itself.
              // ------------------------------------------------
              //
              // This is valid structure regardless of path depth.
              //
              // Example:
              //
              //     deepestField = LINE
              //     path         = [ LINE ]
              //
              // If LINE contains the complete selected evidence,
              // it is exactly the envelope we want.
              //
              if (anchorPeer.Contains(globalBest))
                {
                  globalPeer =
                      anchorPeer;

                  peerField =
                      deepestField;

                  havePeer = true;
                }
              else
                {
                  //
                  // --------------------------------------------
                  // 3b. Search ancestors only.
                  // --------------------------------------------
                  //
                  // The deepest field has already been disproved:
                  //
                  //     anchorPeer !Contains(globalBest)
                  //
                  // Don't search that field again.
                  //
                  DFDT *dfdt =
                      idb->GetDfdt();

                  if (dfdt)
                    {
                      const FIELD_PATH path =
                          dfdt->GetFieldPath(
                              deepestField);

                      //
                      // Need at least:
                      //
                      //     deepest
                      //     parent
                      //     root
                      //
                      // because:
                      //
                      //   deepest is already disproved
                      //   root is deliberately excluded
                      //
                      if (path.size() > 2)
                        {
                          const FIELD_PATH ancestors(
                              path.begin() + 1,
                              path.end());

                          globalPeer =
                              idb->GetPeerFc(
                                  globalBest,
                                  ancestors,
                                  &peerField);

                          havePeer =
                              !peerField.IsEmpty();
                        }
                    }
                }


              //
              // ------------------------------------------------
              // 3c. Natural structure gets first refusal.
              // ------------------------------------------------
              //
              if (havePeer)
                {
                  FC peer(globalPeer);
                  peer -= offset;

                  if (peer.Contains(best) &&
                      peer.GetFieldStart() <=
                          peer.GetFieldEnd() &&
                      peer.GetFieldEnd() <=
                          recordEnd)
                    {
                      const size_t peerBytes = static_cast<size_t>( ExtentWidth(peer));

                      //
                      // Structure is presentation, not evidence.
                      //
                      // If the complete natural container fits
                      // inside the caller's attention budget,
                      // use it outright.
                      //
                      // No StructureBonus.
                      // No utility contest against its contents.
                      //
                      if (peerBytes <= MaxBytesAdvice)
                        {
                          best = peer;
			  // result.DisplayExtent = peer;
                          bestHasStructure = true;
                        }
                    }
                }
            }
        }
    }


  //
  // --------------------------------------------------------
  // 4. Bounded flat-context fallback.
  // --------------------------------------------------------
  //
  // If bounded selection deliberately chose a single evidence
  // item and no useful natural structural envelope fit, give
  // the human some nearby context.
  //
  // This is deliberately the LAST resort because arbitrary byte
  // clipping may cut XML, JSON, markup, etc.
  //
  // Shakespeare should normally be handled by LINE/SPEECH above.
  //
  // Flat WIKI TEXT will normally arrive here.
  //
#if 1
  if (!bestHasStructure && ExtentWidth(best) < MaxBytesAdvice)
#else
  if (bestIsSingle && !bestHasStructure)
#endif
    {
      GPTYPE start =
          best.GetFieldStart();

      GPTYPE end =
          best.GetFieldEnd();

      const GPTYPE width =
          end - start + 1;

      const GPTYPE budget =
          static_cast<GPTYPE>(
              MaxBytesAdvice);

      if (budget > width)
        {
          GPTYPE extra =
              budget - width;

          GPTYPE left =
              extra / 2;

          GPTYPE right =
              extra - left;


          //
          // Grow left.
          //
          if (left > start)
            {
              const GPTYPE missing =
                  left - start;

              start = 0;
              right += missing;
            }
          else
            {
              start -= left;
            }


          //
          // Grow right.
          //
          const GPTYPE availableRight =
              recordEnd - end;

          if (right <= availableRight)
            {
              end += right;
            }
          else
            {
              const GPTYPE missing =
                  right - availableRight;

              end = recordEnd;

              //
              // Recover unused budget on the left if possible.
              //
              if (missing <= start)
                start -= missing;
              else
                start = 0;
            }


          best =
              FC(start, end);
        }
    }


  return best;
}
#endif


bool RESULT::PresentBestDisplayEvidence(size_t MaxBytesAdvice,
    const DISPLAY_MARKER& Marker, STRING *StringBuffer, DOCTYPE *DoctypePtr, STRING *TagPtr) const
{ 
  const DISPLAY_EVIDENCE evidence = GetDisplayEvidence( MaxBytesAdvice, DoctypePtr);
  bool result = PresentDisplay(evidence.DisplayExtent, StringBuffer, Marker, DoctypePtr);
  if (result) {
    if (StringBuffer && !TagPtr) {
      STRING s;
       s << "[" << evidence.ContainerName << "] " << *StringBuffer;
       *StringBuffer = s;
    } else if (TagPtr) *TagPtr = evidence.ContainerName;
  }
  return result;
} 



// OBSOLETE (to be removed)
bool RESULT::PresentBestDisplayEvidence(
    size_t MaxBytesAdvice,
    STRING *StringBuffer,
    STRING *Term,
    const STRING& BeforeTerm,
    const STRING& AfterTerm,
    DOCTYPE *DoctypePtr,
    STRING *TagPtr) const
{
  const DISPLAY_EVIDENCE evidence = GetDisplayEvidence( MaxBytesAdvice, DoctypePtr);
  bool result = PresentDisplay(evidence.DisplayExtent, StringBuffer, DoctypePtr);
  if (result && StringBuffer) {
     STRING s;
     s << "[" << evidence.ContainerName << "] " << *StringBuffer;
     *StringBuffer = s;
  }
 
  return result;
}


static const DISPLAY_MARKER DisplayMarkersNone =
{
  "", "", "", ""
};

static const DISPLAY_MARKER DisplayMarkersText =
{
  "||", "||",
  "[",  "]"
};

static const DISPLAY_MARKER DisplayMarkersVT100 =
{
  "\033[1m",  "\033[22m",   // bold on/off
  "\033[4m",  "\033[24m"    // underline on/off
};


static const DISPLAY_MARKER DisplayMarkersMarkDown =
{
  "**",  "**",   // bold on/off
  "_",  "_"      // underline on/off
};



const DISPLAY_MARKER& RESULT::GetDisplayMarkers(DISPLAY_MARKER_STYLE Style)
{
  switch (Style) {
    default:
    case DisplayMarkerNone: return DisplayMarkersNone; 
    case DisplayMarkerText: return DisplayMarkersText;
    case DisplayMarkerVT100:return  DisplayMarkersVT100;
    case DisplayMarkerMarkDown: return DisplayMarkersMarkDown;
   }
  // NOT REACHED 
}

const DISPLAY_MARKER& GetDisplayMarkers(const STRING& Style)
{
  if (Style.IsNumber())
    return GetDisplayMarkers( (DISPLAY_MARKER_STYLE)(Style.GetInt()));
  if (Style.IsEmpty() || (Style ^= "None")) 
    return GetDisplayMarkers( DisplayMarkerNone );
  if (Style ^= "Text") return GetDisplayMarkers( DisplayMarkerText );
  if (Style ^= "VT100") return GetDisplayMarkers( DisplayMarkerVT100 );
  if ((Style ^= "MarkDown") || (Style ^= "MD"))
    return GetDisplayMarkers( DisplayMarkerMarkDown );

  // Default
  return GetDisplayMarkers(DisplayMarkerNone);
}





DISPLAY_EVIDENCE RESULT::GetDisplayEvidence(size_t MaxBytesAdvice, DOCTYPE *DoctypePtr) const
{
  DISPLAY_EVIDENCE result;

  if (HitTable.IsEmpty() || RecordEnd < RecordStart)
    return result;

  const GPTYPE recordEnd =
      RecordEnd - RecordStart;

  const EVIDENCE_COVERS covers =
      GetEvidenceCovers(0);


  //
  // --------------------------------------------------------
  // Possible evidence dimensions.
  // --------------------------------------------------------
  //
  // Keep this compatible with GetEvidenceCovers().
  //
  auto getPossibleEvidence =
    [&]() -> UINT
    {
      std::set<FCSOURCE> lexicalSourceSet;
      bool haveOpaque = false;

      for (const auto& hit : HitTable)
        {
          const FCSOURCE source =
              HitSource(hit);

          if (source != 0)
            lexicalSourceSet.insert(source);
          else
            haveOpaque = true;
        }

      const UINT lexicalSources =
          static_cast<UINT>(
              lexicalSourceSet.size());

      UINT possibleEvidence =
          GetAuxCount();

      if (possibleEvidence < lexicalSources)
        possibleEvidence = lexicalSources;

      if (haveOpaque &&
          possibleEvidence <= lexicalSources)
        {
          possibleEvidence =
              lexicalSources + 1;
        }

      if (possibleEvidence == 0)
        possibleEvidence = 1;

      return possibleEvidence;
    };


  //
  // --------------------------------------------------------
  // Bounded byte envelope.
  // --------------------------------------------------------
  //
  // Expands Evidence up to MaxBytesAdvice while remaining
  // inside Lower..Upper.
  //
  // Lower/Upper may be:
  //
  //     the record
  //
  // or, better:
  //
  //     a known structural container which was too large
  //     to display in its entirety.
  //
  auto makeBoundedExtent =
    [&](const FC& Evidence,
        GPTYPE Lower,
        GPTYPE Upper) -> FC
    {
      const GPTYPE start =
          Evidence.GetFieldStart();

      const GPTYPE end =
          Evidence.GetFieldEnd();

      if (end < start)
        return Evidence;

      if (start < Lower || end > Upper)
        return Evidence;

      const GPTYPE width =
          end - start + 1;

      const GPTYPE budget =
          static_cast<GPTYPE>(
              MaxBytesAdvice);

      if (budget == 0 || budget <= width)
        return Evidence;

      GPTYPE extra =
          budget - width;

      const GPTYPE leftAvailable =
          start - Lower;

      const GPTYPE rightAvailable =
          Upper - end;

      GPTYPE left =
          extra / 2;

      if (left > leftAvailable)
        left = leftAvailable;

      extra -= left;

      GPTYPE right =
          extra;

      if (right > rightAvailable)
        right = rightAvailable;

      extra -= right;

      //
      // If the right edge prevented us from using the budget,
      // recover what we can on the left.
      //
      if (extra != 0)
        {
          const GPTYPE moreLeft =
              leftAvailable - left;

          const GPTYPE take =
              extra < moreLeft ?
                  extra :
                  moreLeft;

          left += take;
          extra -= take;
        }

      //
      // Defensive symmetry: normally unnecessary because right
      // got all remaining bytes above, but retain it in case this
      // code changes.
      //
      if (extra != 0)
        {
          const GPTYPE moreRight =
              rightAvailable - right;

          const GPTYPE take =
              extra < moreRight ?
                  extra :
                  moreRight;

          right += take;
        }

      return FC(
          start - left,
          end + right);
    };


  //
  // ========================================================
  // 1. CHOOSE THE EVIDENCE
  // ========================================================
  //
  // This phase answers:
  //
  //     "What matters in this result?"
  //
  // Structure does not compete here.
  //


  //
  // --------------------------------------------------------
  // Unbounded mode.
  // --------------------------------------------------------
  //
  // Max == 0 means no attention-cost model.
  //
  // Preserve the strongest evidence cover.
  //
  // We will STILL discover structural identity below because
  // ContainerName/ContainerExtent are useful intelligence even
  // when the caller does not want presentation expansion.
  //
  if (MaxBytesAdvice == 0)
    {
      if (!covers.empty())
        {
          result.Cover =
              covers[0];
        }
      else
        {
          //
          // Defensive single-hit fallback.
          //
          const UINT possibleEvidence =
              getPossibleEvidence();

          for (const auto& hit : HitTable)
            {
              result.Cover =
                  MakeCover(
                      hit,
                      1,
                      possibleEvidence).cover;

              break;
            }
        }
    }


  //
  // --------------------------------------------------------
  // Bounded mode.
  // --------------------------------------------------------
  //
  // Full/partial covers and individual evidence candidates
  // compete against attention cost.
  //
  else
    {
      EVIDENCE_COVER bestEvidence;

      DOUBLE bestUtility = 0.0;
      bool   haveBest    = false;

      auto consider =
        [&](const EVIDENCE_COVER& evidence,
            const FC& extent)
        {
          const GPTYPE start =
              extent.GetFieldStart();

          const GPTYPE end =
              extent.GetFieldEnd();

          if (end < start || end > recordEnd)
            return;

          //
          // Structure is NOT part of this score.
          //
          const DOUBLE utility =
              DisplayUtility(
                  evidence,
                  extent,
                  MaxBytesAdvice,
                  false);

          bool better = false;

          if (!haveBest)
            {
              better = true;
            }
          else if (utility > bestUtility)
            {
              better = true;
            }
          else if (utility == bestUtility)
            {
              //
              // Deterministic tie breaking:
              //
              //     more evidence
              //     less dispersion
              //     shorter extent
              //     earlier occurrence
              //
              if (evidence.Energy >
                  bestEvidence.Energy)
                {
                  better = true;
                }
              else if (evidence.Energy ==
                       bestEvidence.Energy)
                {
                  if (evidence.Dispersion <
                      bestEvidence.Dispersion)
                    {
                      better = true;
                    }
                  else if (evidence.Dispersion ==
                           bestEvidence.Dispersion)
                    {
                      const DOUBLE width =
                          ExtentWidth(extent);

                      const DOUBLE bestWidth =
                          ExtentWidth(
                              bestEvidence.extent);

                      if (width < bestWidth)
                        {
                          better = true;
                        }
                      else if (
                          width == bestWidth &&
                          extent.GetFieldStart() <
                          bestEvidence.extent.GetFieldStart())
                        {
                          better = true;
                        }
                    }
                }
            }

          if (better)
            {
              bestEvidence =
                  evidence;

              bestUtility =
                  utility;

              haveBest =
                  true;
            }
        };


      //
      // Existing full and partial evidence covers.
      //
      for (const auto& cover : covers)
        {
          consider(
              cover,
              cover.extent);
        }


      //
      // Singles are also legitimate display evidence.
      //
      const UINT possibleEvidence =
          getPossibleEvidence();

      for (const auto& hit : HitTable)
        {
          const COVER_CANDIDATE candidate =
              MakeCover(
                  hit,
                  1,
                  possibleEvidence);

          consider(
              candidate.cover,
              hit);
        }


      if (!haveBest)
        return result;

      result.Cover =
          bestEvidence;
    }


  //
  // We now have the winning evidence.
  //
  const FC evidenceExtent =
      result.Cover.extent;

  if (evidenceExtent.GetFieldEnd() <
      evidenceExtent.GetFieldStart())
    return result;


  //
  // Until presentation refinement says otherwise, display
  // exactly the winning evidence.
  //
  result.DisplayExtent =
      evidenceExtent;


  //
  // ========================================================
  // 2. DISCOVER STRUCTURAL IDENTITY
  // ========================================================
  //
  // This phase answers:
  //
  //     "Where does this evidence live?"
  //
  // Container information is retained whether or not the
  // container fits the display budget.
  //
  // Examples:
  //
  //     LINE
  //     PLAY\ACT\SCENE\SPEECH
  //     event/date
  //     TEXT
  //
  // Fully unfielded:
  //
  //     ContainerName   = ""
  //     ContainerExtent = empty FC
  //
  if (DoctypePtr && DoctypePtr->Db)
    {
      IDBOBJ *idb =
          DoctypePtr->Db;

      MDTREC mdtrec;

      if (idb->GetMainMdt() &&
          idb->GetMainMdt()->GetEntry(
              GetMdtIndex(),
              &mdtrec))
        {
          const GPTYPE offset =
              mdtrec.GetGlobalFileStart() +
              mdtrec.GetLocalRecordStart();


          //
          // Prefer a lexical anchor inside the winning evidence.
          //
          // SourceId==0 evidence can represent a broad Date,
          // Numeric, HNSW, etc. region, so a lexical point is
          // normally the better structural probe.
          //
          GPTYPE anchor =
              evidenceExtent.GetFieldStart();

#if _TRACK_TERM_IDENTITY
          for (const auto& hit : HitTable)
            {
              if (hit.GetSourceId() != 0 &&
                  evidenceExtent.Contains(hit))
                {
                  anchor =
                      hit.GetFieldStart();

                  break;
                }
            }
#endif


          const GPTYPE anchorGp =
              anchor + offset;

          STRING deepestField;

          //
          // Use an explicit point FC.
          //
          // This also avoids depending on the historical
          // GPTYPE GetPeerFc() implementation.
          //
          const FC anchorPeer =
              idb->GetPeerFc(
                  FC(anchorGp, anchorGp),
                  &deepestField);


          if (!deepestField.IsEmpty())
            {
              FC globalEvidence(
                  evidenceExtent);

              globalEvidence +=
                  offset;


              FC     globalContainer;
              STRING containerName;

              bool haveContainer =
                  false;


              //
              // ------------------------------------------------
              // 2a. Deepest field already contains all evidence.
              // ------------------------------------------------
              //
              // Valid even when FIELD_PATH contains only:
              //
              //     [ LINE ]
              //
              if (anchorPeer.Contains(
                      globalEvidence))
                {
                  globalContainer =
                      anchorPeer;

                  containerName =
                      deepestField;

                  haveContainer =
                      true;
                }


              //
              // ------------------------------------------------
              // 2b. Evidence crosses the deepest field.
              // ------------------------------------------------
              //
              // Search only exact ancestors.
              //
              else
                {
                  DFDT *dfdt =
                      idb->GetDfdt();

                  if (dfdt)
                    {
                      const FIELD_PATH path =
                          dfdt->GetFieldPath(
                              deepestField);

                      //
                      // path[0] is the deepest field and has
                      // already been disproved.
                      //
                      // The final path member is the common/root
                      // container and is deliberately not used by
                      // the path PEER lookup.
                      //
                      if (path.size() > 2)
                        {
                          const FIELD_PATH ancestors(
                              path.begin() + 1,
                              path.end());

                          globalContainer =
                              idb->GetPeerFc(
                                  globalEvidence,
                                  ancestors,
                                  &containerName);

                          haveContainer =
                              !containerName.IsEmpty();
                        }
                    }
                }


              //
              // ------------------------------------------------
              // 2c. Store structural identity independently of
              //     presentation size.
              // ------------------------------------------------
              //
              if (haveContainer)
                {
                  FC localContainer(
                      globalContainer);

                  localContainer -=
                      offset;

                  //
                  // Defensive checks.
                  //
                  if (localContainer.Contains(
                          evidenceExtent) &&
                      localContainer.GetFieldStart() <=
                          localContainer.GetFieldEnd() &&
                      localContainer.GetFieldEnd() <=
                          recordEnd)
                    {
                      result.ContainerExtent =
                          localContainer;

                      result.ContainerName =
                          containerName;
                    }
                }
            }
        }
    }


  //
  // ========================================================
  // 3. CHOOSE THE PRESENTATION ENVELOPE
  // ========================================================
  //
  // Container identity is now known and retained.
  //
  // Max == 0:
  //
  //     don't expand display bytes
  //     but still return ContainerName/ContainerExtent
  //
  if (MaxBytesAdvice == 0)
    return result;


  //
  // --------------------------------------------------------
  // Natural structure gets first refusal.
  // --------------------------------------------------------
  //
  // If the entire structural container fits inside the caller's
  // attention budget, use it as the display extent.
  //
  if (result.HasContainer())
    {
      const size_t containerBytes =
          static_cast<size_t>(
              ExtentWidth(
                  result.ContainerExtent));

      if (containerBytes <=
          MaxBytesAdvice)
        {
          result.DisplayExtent =
              result.ContainerExtent;

          return result;
        }
    }


  //
  // --------------------------------------------------------
  // Container is absent or too large.
  // --------------------------------------------------------
  //
  // Spend the remaining attention budget on nearby context.
  //
  // But if we KNOW a structural container, stay inside it.
  //
  // This is useful for something like:
  //
  //     SPEECH is 400 bytes
  //     MaxBytesAdvice is 130
  //
  // We cannot show the complete SPEECH, but there is no reason
  // for an arbitrary 130-byte excerpt to spill into the next
  // SPEECH.
  //
  GPTYPE contextStart = 0;
  GPTYPE contextEnd   = recordEnd;

  if (result.HasContainer() &&
      result.ContainerExtent.Contains(
          evidenceExtent))
    {
      contextStart =
          result.ContainerExtent.GetFieldStart();

      contextEnd =
          result.ContainerExtent.GetFieldEnd();
    }


  result.DisplayExtent =
      makeBoundedExtent(
          evidenceExtent,
          contextStart,
          contextEnd);


  return result;
}
