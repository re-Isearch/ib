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


#if 0
FC RESULT::GetDisplayEvidence( size_t MaxBytesAdvice, DOCTYPE *DoctypePtr) const
{
  const EVIDENCE_COVERS covers =
    GetEvidenceCovers(0);

  if (covers.empty())
    return FC();

  FC best;
  DOUBLE bestUtility = -MAXFLOAT;

  IDBOBJ *idb = NULL;
  off_t offset = 0;
  bool haveStructure = false;

  //
  // Resolve the record-relative -> global offset once.
  //
  if (DoctypePtr && DoctypePtr->Db)
  {
    idb = DoctypePtr->Db;

    MDTREC mdtrec;

    if (idb->GetMainMdt()->GetEntry(
          GetMdtIndex(), &mdtrec))
    {
      offset =
        mdtrec.GetGlobalFileStart() +
        mdtrec.GetLocalRecordStart();

      haveStructure = true;
    }
  }

  for (const auto& cover : covers)
  {
    //
    // ----------------------------------------------------
    // Candidate 1: the evidence cover itself.
    // ----------------------------------------------------
    //
    {
      const DOUBLE utility =
        DisplayUtility(
          cover,
          cover.extent,
          MaxBytesAdvice,
          false);

      if (utility > bestUtility)
      {
        bestUtility = utility;
        best = cover.extent;
      }
    }

    //
    // ----------------------------------------------------
    // Candidate 2: smallest structural container containing
    // the evidence.
    // ----------------------------------------------------
    //
    if (haveStructure)
    {
      FC global = cover.extent;
      global += offset;

      STRING tag;
      const FC peer =
        idb->GetPeerFc(global, &tag);

      if (!peer.IsEmpty())
      {
        const GPTYPE start =
          peer.GetFieldStart() - offset;

        const GPTYPE end =
          peer.GetFieldEnd() - offset;

        //
        // Sanity: don't escape the RESULT.
        //
        if (end >= start &&
            end <= (RecordEnd - RecordStart))
        {
          const FC localPeer(start, end);

          const DOUBLE utility =
            DisplayUtility(
              cover,
              localPeer,
              MaxBytesAdvice,
              true);

          if (utility > bestUtility)
          {
            bestUtility = utility;
            best = localPeer;
          }
        }
      }
    }
  }

  return best;
}
#elif 0

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

FC RESULT::GetDisplayEvidence(
    size_t MaxBytesAdvice,
    DOCTYPE *DoctypePtr) const
{
  //
  // Legitimate case: relevance-feedback result, etc.
  //
  if (HitTable.IsEmpty())
    return FC();

  const EVIDENCE_COVERS covers =
    GetEvidenceCovers(0);

  IDBOBJ *idb = NULL;
  off_t offset = 0;
  bool haveStructure = false;

  //
  // Resolve record-relative -> global offset once.
  //
  if (DoctypePtr && DoctypePtr->Db)
  {
    idb = DoctypePtr->Db;

    MDTREC mdtrec;

    if (idb->GetMainMdt()->GetEntry(
          GetMdtIndex(), &mdtrec))
    {
      offset =
        mdtrec.GetGlobalFileStart() +
        mdtrec.GetLocalRecordStart();

      haveStructure = true;
    }
  }


  FC best;
  DOUBLE bestUtility = -MAXFLOAT;
  EVIDENCE_COVER bestEvidence = { FC(), 0.0, 0.0 };

  //
  // Common candidate evaluator.
  //
  auto consider =
    [&](const EVIDENCE_COVER& evidence,
        const FC& extent,
        bool structural)
    {
      if (extent.IsEmpty())
        return;

      const DOUBLE utility =
        DisplayUtility(
          evidence,
          extent,
          MaxBytesAdvice,
          structural);

      if (best.IsEmpty() || utility > bestUtility)
      {
        bestUtility = utility;
        best = extent;
        bestEvidence = evidence; 
      }
    };


  //
  // Optionally consider the smallest structural container
  // containing a candidate.
  //
  auto considerStructure =
    [&](const EVIDENCE_COVER& evidence)
    {
      if (!haveStructure)
        return;

      FC global = evidence.extent;
      global += offset;

      STRING tag;

      const FC peer =
        idb->GetPeerFc(global, &tag);

      if (peer.IsEmpty())
        return;

      const GPTYPE start =
        peer.GetFieldStart() - offset;

      const GPTYPE end =
        peer.GetFieldEnd() - offset;

      if (end < start ||
          end > (RecordEnd - RecordStart))
        return;

      consider(
        evidence,
        FC(start, end),
        true);
    };


  //
  // ----------------------------------------------------
  // 1. Evidence-cover candidates.
  // ----------------------------------------------------
  //
  for (const auto& cover : covers)
  {
    consider(
      cover,
      cover.extent,
      false);

    considerStructure(cover);
  }


  //
  // ----------------------------------------------------
  // 2. Graceful degradation:
  //    individual evidence hits.
  //
  // GetEvidenceCovers deliberately does not expose bare K=1
  // lexical covers for a multi-evidence result.  Display,
  // however, must always have something useful to show.
  // ----------------------------------------------------
  //
  UINT possibleEvidence = GetAuxCount();

  if (possibleEvidence == 0)
    possibleEvidence = 1;

  for (const auto& hit : HitTable)
  {
    EVIDENCE_COVER evidence;

    evidence.extent = hit;

    evidence.Energy =
      1.0 / static_cast<DOUBLE>(possibleEvidence);

    evidence.Dispersion =
      ExtentWidth(hit);

    consider(
      evidence,
      hit,
      false);

    considerStructure(evidence);
  }


  //
  // HitTable was nonempty, therefore one of the individual
  // hits should necessarily have supplied a candidate.
  //
#if 1
message_log(LOG_INFO,
    "Display winner %ld-%ld len=%lu E=%g D=%g U=%g",
    (long)best.GetFieldStart(),
    (long)best.GetFieldEnd(),
    (unsigned long)best.GetLength(),
    bestEvidence.Energy,
    bestEvidence.Dispersion,
    bestUtility);
#endif
  return best;
}


#elif 0

FC RESULT::GetDisplayEvidence(
    size_t MaxBytesAdvice,
    DOCTYPE *DoctypePtr) const
{
  if (HitTable.IsEmpty() || RecordEnd < RecordStart)
    return FC();

  FC             best;
  EVIDENCE_COVER bestEvidence;
  DOUBLE         bestUtility = 0.0;
  bool           haveBest    = false;


  //
  // Candidate selection is deliberately cheap:
  //
  //     evidence geometry
  //     +
  //     display byte cost
  //
  // No structural lookup happens here.
  //
  auto consider =
    [&](const EVIDENCE_COVER& evidence,
        const FC& extent,
        bool structured)
    {
      const GPTYPE start = extent.GetFieldStart();
      const GPTYPE end   = extent.GetFieldEnd();

      if (end < start)
        return;

      if (end > (RecordEnd - RecordStart))
        return;

      const DOUBLE utility =
          DisplayUtility(
              evidence,
              extent,
              MaxBytesAdvice,
              structured);

      if (!haveBest ||
          utility > bestUtility ||
          (utility == bestUtility &&
           ExtentWidth(extent) < ExtentWidth(best)))
        {
          best         = extent;
          bestEvidence = evidence;
          bestUtility  = utility;
          haveBest     = true;
        }
    };


  //
  // --------------------------------------------------------
  // 1. Raw evidence candidates.
  // --------------------------------------------------------
  //
  // GetEvidenceCovers() is already ranked by evidence strength
  // and compactness.  DisplayUtility adds the attention/byte
  // budget policy.
  //
  const EVIDENCE_COVERS covers =
      GetEvidenceCovers(0);

  for (const auto& cover : covers)
    consider(
        cover,
        cover.extent,
        false);


  //
  // --------------------------------------------------------
  // 2. Single-hit fallback.
  // --------------------------------------------------------
  //
  // Normally GetEvidenceCovers() gives us something useful.
  // If not, don't invent expensive context: choose the best
  // individual evidence FC.
  //
  if (!haveBest)
    {
      UINT possibleEvidence = GetAuxCount();

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
              false);
        }
    }


  if (!haveBest)
    return FC();


  //
  // --------------------------------------------------------
  // 3. Structural refinement.
  // --------------------------------------------------------
  //
  // Everything above was in-memory geometry.
  //
  // Now -- and only now -- pay for structural discovery.
  //
  if (DoctypePtr == NULL || DoctypePtr->Db == NULL)
    return best;

  IDBOBJ *idb = DoctypePtr->Db;

  MDTREC mdtrec;

  if (!idb->GetMainMdt()->GetEntry(
          GetMdtIndex(),
          &mdtrec))
    return best;

  const GPTYPE offset =
      mdtrec.GetGlobalFileStart() +
      mdtrec.GetLocalRecordStart();


  //
  // Prefer a lexical point inside the winning evidence.
  //
  // SourceId==0 evidence can itself represent a large Date,
  // Numeric, vector, etc. region.  A lexical hit generally gives
  // us the most useful point from which to discover the deepest
  // structural field.
  //
  GPTYPE anchor =
      best.GetFieldStart();

#if _TRACK_TERM_IDENTITY
  for (const auto& hit : HitTable)
    {
      if (hit.GetSourceId() != 0 &&
          best.Contains(hit))
        {
          anchor = hit.GetFieldStart();
          break;
        }
    }
#endif


  STRING deepestField;

  //
  // The ONE generic PEER lookup.
  //
  // Keep its returned FC: it may already be the structural
  // container we need.
  //
  const FC anchorPeer =
      idb->GetPeerFc(
          anchor + offset,
          &deepestField);

  if (deepestField.IsEmpty())
    return best;


  DFDT *dfdt = idb->GetDfdt();

  if (dfdt == NULL)
    return best;


  const FIELD_PATH path =
      dfdt->GetFieldPath(deepestField);

  //
  // [TEXT], [BODY], etc. -- only a root/common field.
  //
  // Nothing structurally useful to add.
  //
  if (path.size() <= 1)
    return best;


  FC globalBest(best);
  globalBest += offset;

  FC     globalPeer;
  STRING peerField;
  bool   havePeer = false;


  //
  // Cheapest case:
  //
  // the deepest field discovered by the generic point lookup
  // already contains all of the selected evidence.
  //
  // Don't search its field index again.
  //
  if (anchorPeer.Contains(globalBest))
    {
      globalPeer = anchorPeer;
      peerField  = deepestField;
      havePeer   = true;
    }
  else
    {
      //
      // The deepest field did not contain the complete evidence.
      //
      // We already know that, so don't probe it again.
      //
      // Remove it from the path and search only its ancestors.
      // The final path member remains the root and is skipped by
      // the path-constrained GetPeerFc().
      //
      //
      // Example:
      //
      //   original:
      //     LINE SPEECH SCENE ACT PLAY
      //
      //   ancestors:
      //          SPEECH SCENE ACT PLAY
      //
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


  if (!havePeer)
    return best;


  //
  // Convert the structural candidate back to record-relative
  // coordinates.
  //
  FC peer(globalPeer);
  peer -= offset;


  //
  // Defensive sanity checks.
  //
  if (!peer.Contains(best))
    return best;

  if (peer.GetFieldEnd() < peer.GetFieldStart())
    return best;

  if (peer.GetFieldEnd() >
      (RecordEnd - RecordStart))
    return best;


  //
  // Same evidence, one structural alternative.
  //
  // MaxBytesAdvice remains soft policy inside DisplayUtility().
  //
  consider(
      bestEvidence,
      peer,
      true);


  return best;
}

#else


FC RESULT::GetDisplayEvidence( size_t MaxBytesAdvice, DOCTYPE *DoctypePtr) const
{
  if (HitTable.IsEmpty() || RecordEnd < RecordStart)
    return FC();

  const EVIDENCE_COVERS covers =
      GetEvidenceCovers(0);

  if (covers.empty())
    return FC();


  //
  // ========================================================
  // UNBOUNDED DISPLAY
  // ========================================================
  //
  // MaxBytesAdvice == 0 means:
  //
  //     show the strongest evidence we have
  //
  // No attention-cost model, no partial/single fallback merely
  // to make the result shorter.
  //
  // This is useful for agents, diagnostics, query explanation,
  // etc.
  //
  if (MaxBytesAdvice == 0)
    return covers[0].extent;


  //
  // ========================================================
  // BOUNDED DISPLAY
  // ========================================================
  //
  // Now attention has a cost.
  //
  // Full evidence, partial evidence and individual hits are all
  // allowed to compete.
  //
  FC             best;
  EVIDENCE_COVER bestEvidence;
  DOUBLE         bestUtility = 0.0;
  bool           haveBest    = false;
  bool           bestIsSingle = false;


  auto consider =
    [&](const EVIDENCE_COVER& evidence,
        const FC& extent,
        bool structured,
        bool single)
    {
      if (extent.IsEmpty())
        return;

      const GPTYPE start = extent.GetFieldStart();
      const GPTYPE end   = extent.GetFieldEnd();

      if (end < start)
        return;

      if (end > (RecordEnd - RecordStart))
        return;

      const DOUBLE utility =
          DisplayUtility(
              evidence,
              extent,
              MaxBytesAdvice,
              structured);

      if (!haveBest ||
          utility > bestUtility ||
          (utility == bestUtility &&
           ExtentWidth(extent) < ExtentWidth(best)))
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
          false,
          false);
    }


  //
  // --------------------------------------------------------
  // 2. Individual evidence.
  // --------------------------------------------------------
  //
  // Singles are DISPLAY alternatives only in bounded mode.
  //
  // Retrieval semantics remain untouched.
  //
  UINT possibleEvidence = GetAuxCount();

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
          false,
          true);
    }


  if (!haveBest)
    return FC();


  //
  // --------------------------------------------------------
  // 3. One structural refinement.
  // --------------------------------------------------------
  //
  // No structure exists, or caller did not supply a database:
  // we're done with evidence selection.
  //
  if (DoctypePtr && DoctypePtr->Db)
    {
      IDBOBJ *idb = DoctypePtr->Db;
      MDTREC mdtrec;

      if (idb->GetMainMdt()->GetEntry(
              GetMdtIndex(),
              &mdtrec))
        {
          const GPTYPE offset =
              mdtrec.GetGlobalFileStart() +
              mdtrec.GetLocalRecordStart();

          //
          // Prefer a lexical anchor inside the selected evidence.
          //
          GPTYPE anchor =
              best.GetFieldStart();

#if _TRACK_TERM_IDENTITY
          for (const auto& hit : HitTable)
            {
              if (hit.GetSourceId() != 0 &&
                  best.Contains(hit))
                {
                  anchor = hit.GetFieldStart();
                  break;
                }
            }
#endif

          STRING deepestField;

          //
          // The ONE generic PEER.
          //
          const FC anchorPeer =
              idb->GetPeerFc(
                  anchor + offset,
                  &deepestField);

          if (!deepestField.IsEmpty())
            {
              DFDT *dfdt = idb->GetDfdt();

              if (dfdt)
                {
                  const FIELD_PATH path =
                      dfdt->GetFieldPath(deepestField);

                  //
                  // A one-element path is effectively flat:
                  //
                  //     [TEXT]
                  //
                  if (path.size() > 1)
                    {
                      FC globalBest(best);
                      globalBest += offset;

                      FC     globalPeer;
                      STRING peerField;
                      bool   havePeer = false;

                      //
                      // The generic point lookup may already
                      // have found exactly the container we need.
                      //
                      if (anchorPeer.Contains(globalBest))
                        {
                          globalPeer = anchorPeer;
                          peerField  = deepestField;
                          havePeer   = true;
                        }
                      else if (path.size() > 2)
                        {
                          //
                          // Deepest field already failed to
                          // contain the complete evidence.
                          //
                          // Search ancestors only.
                          //
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

                      if (havePeer)
                        {
                          FC peer(globalPeer);
                          peer -= offset;

                          if (peer.Contains(best) &&
                              peer.GetFieldStart() <=
                                peer.GetFieldEnd() &&
                              peer.GetFieldEnd() <=
                                (RecordEnd - RecordStart))
                            {
                              const size_t peerBytes =
                                  static_cast<size_t>(
                                      ExtentWidth(peer));

                              //
                              // Structure is optional.
                              //
                              // It may consume unused attention
                              // budget, but it must not be the
                              // reason an otherwise compact
                              // display blows through that budget.
                              //
                              if (peerBytes <= MaxBytesAdvice)
                                {
                                  consider(
                                      bestEvidence,
                                      peer,
                                      true,
                                      false);
                                }
                            }
                        }
                    }
                }
            }
        }
    }


  //
  // --------------------------------------------------------
  // 4. Cheap context for a bare single-hit winner.
  // --------------------------------------------------------
  //
  // If the bounded model deliberately sacrificed evidence
  // coverage to avoid a huge span, don't leave the human with
  // just one naked word.
  //
  // Structural refinement above gets first refusal.  We only
  // expand when the winning result is still the original single
  // evidence FC.
  //
  if (bestIsSingle &&
      ExtentWidth(best) < MaxBytesAdvice)
    {
      GPTYPE start = best.GetFieldStart();
      GPTYPE end   = best.GetFieldEnd();

      const GPTYPE width =
          end - start + 1;

      GPTYPE extra =
          static_cast<GPTYPE>(MaxBytesAdvice) - width;

      GPTYPE left  = extra / 2;
      GPTYPE right = extra - left;

      //
      // Left record boundary.
      //
      if (left > start)
        {
          right += left - start;
          start = 0;
        }
      else
        {
          start -= left;
        }

      //
      // Right record boundary.
      //
      const GPTYPE recordEnd =
          RecordEnd - RecordStart;

      if (end + right > recordEnd)
        {
          const GPTYPE overflow =
              end + right - recordEnd;

          end = recordEnd;

          if (overflow <= start)
            start -= overflow;
          else
            start = 0;
        }
      else
        {
          end += right;
        }

      best = FC(start, end);
    }


  return best;
}



#endif



bool RESULT::PresentBestDisplayEvidence(
    size_t MaxBytesAdvice,
    STRING *StringBuffer,
    STRING *Term,
    const STRING& BeforeTerm,
    const STRING& AfterTerm,
    DOCTYPE *DoctypePtr,
    STRING *TagPtr) const
{
  const FC fc = GetDisplayEvidence(MaxBytesAdvice, DoctypePtr);
  return PresentDisplay(fc, StringBuffer, DoctypePtr);
}


struct DISPLAY_EVIDENCE
{
    FC     extent;
    DOUBLE Utility;
    DOUBLE Energy;
    DOUBLE Dispersion;
    bool   Partial;
};

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



