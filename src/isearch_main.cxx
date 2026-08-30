/* Copyright (c) 2020-21 Project re-Isearch and its contributors: See CONTRIBUTORS.
It is made available and licensed under the Apache 2.0 license: see LICENSE */
/*-@@@
File:		Isearch.cxx
Description:	Command-line search utility
@@@*/
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <iostream>
#include <iomanip>
#include "common.hxx"
#include "vidb.hxx"
#include "squery.hxx"
#include "rset.hxx"
#include "dtreg.hxx"
#include "process.hxx"
#include "infix2rpn.hxx"
#ifdef HAVE_LOCALE
#include <locale.h>
#endif

#include "jsonstring.hxx"

#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
  
using std::cout;
using std::endl;


static const int _isearch_main_version = 3;

#ifndef _WIN32
# define SHOW_RUSAGE 1

static unsigned long MaxResidentKiB(const struct rusage& usage)
{               
#if defined(__APPLE__)
  return static_cast<unsigned long>(usage.ru_maxrss / 1024);
#else       
  return static_cast<unsigned long>(usage.ru_maxrss);
#endif      
}    


#else
# include <io.h>
#endif

#if SHOW_RUSAGE
#include <sys/resource.h> 
#endif


#if 0 /* Obsolete */
static void dumpXMLHitTable(FCLIST *HitTable)
{
  const INT z = HitTable->GetTotalEntries();
  if (z)
    {
      FC Fc;
      cout << "<HITS UNITS=\"characters\" NUMBER=\"" << z << "\">" << endl;
      for (INT i=1; i <=z ; i++)
	{
	  if (HitTable->GetEntry(i, &Fc))
	    {
	      size_t Start = Fc.GetFieldStart();
	      size_t End   = Fc.GetFieldEnd();
	      cout << "  <LOC POS=\"" << Start 
		<< "\" LEN=\"" << End-Start+1 << "\"/>" << endl;
	    }
	}
      cout << "</HITS>" << endl;
    }
}


static void dumpXMLHitTable(FCLIST *HitTable, VIDB *vidb, const RESULT& Result)
{
  const INT z = HitTable->GetTotalEntries();

  IDBOBJ *idb = vidb->GetIDB( Result.GetVirtualIndex() );
  MDTREC mdtrec;


  if ( idb->GetMainMdt()->GetEntry (Result.GetMdtIndex(), &mdtrec) == false)
    {
      cerr << "Can't resolve record!" << endl;
      return;
    } 
  int offset = mdtrec.GetGlobalFileStart() + mdtrec.GetLocalRecordStart();

  if (z)
    {
      FC Fc;
      STRING Tag;
      STRING lastTag;
      FC     lastPeerFC;
      bool firstTime = true;
      cout << "<HITS UNITS=\"characters\" NUMBER=\"" << z << "\">" << endl;
      for (INT i=1; i <=z ; i++)
        {
          if (HitTable->GetEntry(i, &Fc))
            {
              GPTYPE Start = Fc.GetFieldStart();
              GPTYPE End   = Fc.GetFieldEnd();
	      FC     PeerFC = idb->GetPeerFc(FC(Fc)+=offset,&Tag);

	      if (! (PeerFC == lastPeerFC))
		{
		  if (! firstTime)
		    cout << ( lastTag.GetLength() ? "  </CONTAINER>" : "  </FULLTEXT>" ) << endl;
		  else
		    firstTime = false;
		  if (Tag.GetLength())
		    {
			FIELDTYPE ft = idb->GetFieldType(Tag);
			STRING    value;

			cout << "  <CONTAINER NAME=\"" << Tag
				<< "\" TYPE=\"" << ft.c_str()
				<< "\" FC=\"(" << PeerFC.GetFieldStart() << ","
				<< PeerFC.GetFieldEnd() << ")";
			if (!ft.IsText() && idb->GetFieldData(PeerFC, Tag, &value)) 
			  cout << "\" VALUE=\"" << value;
			cout << "\">" << endl;
		    }
		  else
		    cout << "  <FULLTEXT>" << endl;
		  lastPeerFC = PeerFC;
		  lastTag    = Tag;
		}
              cout << "    <LOC POS=\"" << Start
                << "\" LEN=\"" << End-Start+1 << "\"/>" << endl;
            }
        }
      if (lastTag.GetLength())
	cout << "  </CONTAINER>" << endl;
      cout << "</HITS>" << endl;
    }
}


/*

XML
  
<folders>
    <folder id="123" private="0" archived="0" order="1">Shopping</folder>
</folders>


is JSON

{
    "folders": {
        "folder":{
        "@": {
            "id": "123",
            "private": "0",
            "archived": "0",
            "order": "1"
            },
        "#": "Shopping"
        }
    }
}

*/


static void dumpJsonHitTable(FCLIST *HitTable, VIDB *vidb, const RESULT& Result)
{
  const INT z = HitTable->GetTotalEntries();

  IDBOBJ *idb = vidb->GetIDB( Result.GetVirtualIndex() );
  MDTREC mdtrec;


  if ( idb->GetMainMdt()->GetEntry (Result.GetMdtIndex(), &mdtrec) == false)
    {
      cerr << "Can't resolve record!" << endl;
      return;
    } 
  int offset = mdtrec.GetGlobalFileStart() + mdtrec.GetLocalRecordStart();

  if (z)
    {
      FC Fc;
      STRING Tag;
      STRING lastTag;
      FC     lastPeerFC;
      bool firstTime = true;
      cout << "\
\tHITS:{\n\
\t@: {\n\
\t\t\"UNITS\": \"characters\"i,\n\
\t\t\"NUMBER=\"" << z << "\n\
\t\t},\n\
\t#: \"HIT\": {" << endl;
      for (INT i=1; i <=z ; i++)
        {
          if (HitTable->GetEntry(i, &Fc))
            {
              GPTYPE Start = Fc.GetFieldStart();
              GPTYPE End   = Fc.GetFieldEnd();
	      FC     PeerFC = idb->GetPeerFc(FC(Fc)+=offset,&Tag);

	      if (! (PeerFC == lastPeerFC))
		{
		  if (! firstTime)
		    cout << "  }" << endl; // End Tag
		  else
		    firstTime = false;
		  if (Tag.GetLength())
		    {
			FIELDTYPE ft = idb->GetFieldType(Tag);
			STRING    value;

			cout << "  <CONTAINER NAME=\"" << Tag
				<< "\" TYPE=\"" << ft.c_str()
				<< "\" FC=\"(" << PeerFC.GetFieldStart() << ","
				<< PeerFC.GetFieldEnd() << ")";
			if (!ft.IsText() && idb->GetFieldData(PeerFC, Tag, &value)) 
			  cout << "\" VALUE=\"" << value;
			cout << "\">" << endl;
		    }
		  else
		    cout << "  <FULLTEXT>" << endl;
		  lastPeerFC = PeerFC;
		  lastTag    = Tag;
		}
              cout << "    <LOC POS=\"" << Start
                << "\" LEN=\"" << End-Start+1 << "\"/>" << endl;
            }
        }
      if (lastTag.GetLength())
	cout << "  </CONTAINER>" << endl;
      cout << "</HITS>" << endl;
    }
}

#endif /* Obsolete */


static inline void IniUsage(std::ostream& out = std::cout)
{
  VIDB  db;
  STRING code = db.Description();
  if (!code.IsEmpty())
    {
      out << code << endl;
    }
}


#if 0
PRSET LoadResultSet(SQUERY *QueryPtr, const STRING& LoadTable)
{
  PRSET prset = NULL;
  if (!LoadTable.IsEmpty())
    {
      // Load Session
      PFILE Fp = fopen(LoadTable, "rb");
      if (Fp)
        {
	  prset = new RSET();
          BYTE ranked;
          STRING tmp;
          prset->Read (Fp);
          Read(&ranked, Fp);
          DBName.Read (Fp);
          tmp.Read (Fp);
          QueryPtr->Read (Fp);
          fclose (Fp);
        }
    }
  return prset;
}
#endif



static void HelpUsageJSON(const char *progname);
static void HelpUsageText( const char *progname, std::ostream& out = std::cout);

static void HelpUsage(const char *progname)
{
#if 1
  if (isatty(fileno(stdout)))
    HelpUsageText(RemovePath(progname));
  else
    HelpUsageJSON(RemovePath(progname));
#else
  const char *prog = (progname && *progname ? progname : "Isearch");
  cout << prog << " -d db [options] term..." << endl <<
	"options:" << endl << 
        "  -d (X)           // Search database with root name (X)." << endl <<
	"  -fuel nn         // Space fuel where nn in %." << endl <<
	"  -cd (X)          // Change working directory to (X)." << endl <<
	"  -id (X)          // Request document(s) with docid (X)." << endl <<
	"  -D (X)           // Load Result Set from file (X)." << endl <<
	"  -p (X)           // Present element set (X) as the identifier with results." << endl <<
	"  -P (X|X/Y)       // Present Ancestor (X) content for hits (may be recurringly specified)." << endl <<
	"                   // where X may be as (X)/(Y) where (X) is an ancestor and" << endl <<
        "                   // (Y) is a descendant of that ancestor." << endl << 
	"  -c               // Sort results Chronologically." << endl <<
	"  -cr              // Sort result from oldest to newest." << endl <<
	"  -s               // Sort results by Score (Relevant Ranked)." << endl <<
	"  -sc              // Sort results by Score modified by Category." << endl <<
	"  -smag (NN.NN)    // Sort results by Score/Category with Magnetism factor NN.NN" << endl <<
	"  -scat            // Sort results by Category." << endl <<
	"  -snews           // Sort results by News Rank." << endl <<
	"  -h               // Sort results by different matches (see -joint)." << endl <<
	"  -k               // Sort results by Key." << endl <<
	"  -n               // Don't sort (By indexing order)." << endl <<
        "  -AF_norm         // AF Normalization." << endl <<
        "  -bytes_norm      // Bytes Normalization." << endl <<
	"  -euclidean_norm  // Euclidean Normalization." << endl << 
//	"  -metric_norm     // (Cosine) Metric Normalization." << endl << 
        "  -L1_norm         // Cosine L1 Normalization." << endl <<
        "  -L2_norm         // Cosine L2 Normalization." << endl <<
        "  -log_norm        // Log Normalization." << endl << 
        "  -max_norm        // Max. normalization. " << endl <<
	"  -no_norm         // Don't calculate scores or normalize." << endl <<
	"  -sort B[entley]|S[edgewick]|D[ualPivot]|T[im]|N[ative] // Which variation of QuickSort to use" << endl <<
	"  -show            // Show 'best' hit neighborhood." << endl <<
//	"  -showall         // Show all hits neighborhoods." << endl <<
	"  -summary         // Show summary/description." << endl <<
	"  -XML             // Present Results in XML-like structure." << endl <<
	"  -Json            // Present Result in Json structure." << endl <<
	"  -H[TML]          // Use HTML Record Presentation." << endl <<
	"  -q[uiet]         // Print results and exit immediately." << endl <<
	"  -t[erse]         // Print terse results." << endl <<
	"  -tab             // Use Terse tab format." << endl <<
	"  -scan field      // scan service." << endl <<
	"  -shell           // Interactive mode." << endl <<
	"  -rpn             // Interpret as an RPN query." << endl <<
	"  -infix           // Interpret as an InFix-Notation query." << endl <<
	"                   //   Additional Unary Ops:  ! for NOT, field/  for WITHIN:field" << endl <<
	"  -words           // Interpret as words." << endl <<
	"  -and             // Interpret as intersection of words." << endl <<
	"  -smart field     // Fielded Smart search (fallback to OR). Field -- means generic smart." << endl << 
 	"  -regular         // Regular Query (fields, weights etc. but no operators)." << endl <<
	"  -syn             // Do synonym expansion." << endl <<
	"  -priority (NN.NN)// Over-ride priority factor with NN.NN" << endl <<
	"  -scale (NN)      // Normalize score to 0-(NN) (scale)." << endl <<
	"  -top (NN)        // Max. NN hits" << endl <<
        "  -negative        // Show results with negative scores." << endl <<
	"  -positive        // Show only results with positive scores." << endl <<
	"  -clip (NN)       // Clip at NN." << endl <<
	"  -common (NN)     // Set common words threshold at NN." << endl <<
	"  -reduce          // Reduce result set (MiniMax of different matches)." << endl <<
	"  -reduce0         // Same as -h -reduce" << endl << 
	"  -drop_h (NN)     // Drop all results with less than NN different matches." << endl <<
	"  -drop_a (NN)     // Drop all results with absolute score less than NN." << endl << 
	"  -drop_s (NN)     // Drop all results with scaled score less than NN." << endl <<
	"  -prefix (X)      // Add prefix (X) to matched terms in document." << endl <<
	"  -suffix (X)      // Add suffix (X) to matched terms in document." << endl <<
	"  -headline (X)    // Use alternative headline display using element (X)." << endl << 
	"  -filename        // Filenames ONLY." << endl << 
	"  -filesystem      // Same as -q -filename -byterange" << endl <<
	"  -hits            // Show total matches per record." << endl <<
	"  -joint           // Show joint total (different matches) per record." << endl <<
        "  -score           // Show unnormalized scores." << endl <<
	"  -date            // Print date." << endl <<
	"  -datemodified    // Print date modified." << endl <<
	"  -key             // Print Record Key." << endl <<
	"  -doctype         // Print Record doctype." << endl <<
	"  -byterange       // Print the byte range of each document within" << endl <<
	"                   // the file that contains it." << endl <<
	"  -range (X)[-(Y)] // Show the (X)th to (Y)th result in set" << endl <<
	"  -daterange (X)   // Limit search to records in the range (X)" << endl <<
	"                   // Specified as YYYY[MM[DD]][-YYYY[MM[DD]]] or" << endl <<
	"                   // YYYY[MM[DD]]/[[YYYY]MM]DD" << endl << 
	"                   // Example: 2005 would return all records of the year 2005" << endl <<
	"                   // Note: The date here is the 'date of the record'" << endl <<
        "                   // in contrast to ordinary date fields." << endl << 
	"                   // Note2: -daterange limits ALL searches to the range. The unary" << endl <<
	"                   // operator WITHIN:<daterange> applies to a search set." << endl <<
	"  -startdoc (NN)   // Display result set starting with the (NN)th" << endl <<
	"                   // document in the list." << endl <<
	"  -enddoc (NN)     // Display result set ending with the (NN)th document" << endl <<
	"                   // in the list." << endl <<
	"  -o (X)]          // Document type specific option." << endl <<
	"  -table (X)       // Save Result Set into file (X)" << endl <<
	"  -level (NN)      // Set message level to NN (0-255)." << endl <<
        "  -debug           // Load of debug messages." << endl <<
	"  -bench           // Show rusage" << endl <<
	"  -pager (EXE)     // Use program EXE to page results (e.g. more)." << endl <<
        "  -more            // Same as -pager /bin/more" << endl <<
	"  -copyright       // Display the copyright statement" << endl <<
	"  -help            // Display the help" << endl <<
	"  -qhelp[=json]    // Display the query operators available" << endl <<
	"  (...)            // Terms (X), (Y), .. or a query sentence." << endl <<
	"                   // If -rpn was specified then the sentence is expected to be in RPN." << endl <<
        "                   // If -infix then the conventional in-fix notation is expected." << endl << 
        "                   // If -words or -regular then OR'd.  Default is Smart search." << endl <<  
        endl <<
	"                   // Fielded Search: [[fieldname][relation]]searchterm[*][:n]" << endl <<
	"                   // Relations are <,>,>=,<=,<> whose semantics of depends upon the" << endl <<
        "                   // field datatype." << endl <<  
	endl <<
	"                   // In addition one may prefix with fieldname/ for fielded text searching." << endl <<
	"                   // Append * for right truncation." << endl <<
	"                   // Prepend * for left truncation." << endl <<
        "                   // Use combination of * and ? for glob matching." << endl <<
        "                   // Append ~ for fuzzy (Ratcliff) search." << endl <<
        "                   // Append # for phonetic (soundex) search." << endl <<
	"                   // Append = for exact (case dependent) search." << endl <<
	"                   // Append > for exact right truncated search (=*)." << endl <<
	"                   // Append . for \"exact-term\" (e.g. \"auto\" won't match \"auto-mobile\")" << endl << 
	"                   // Append $ for freeform encoded address (sparse vector)." << endl <<
	"                   // Append @ for embeddings address (dense vector)." << endl <<
	"                   // Append :n for term weighting (default=1)." << endl <<
	"                   //   (Use negative values to lower rank.)" << endl <<
	"                   // Use \"literal phrase\" for literal search" << endl <<
	endl <<
	"                   // Note: the term prefix RECT (as in RECT{N,W,S,E}) is reserved" << endl <<
        "                   // for bounding box searches in pre-defined numeric fields (quadrants)." << endl <<
	endl <<
	"                   // Note: dates can be specified in many different formats" << endl <<
	"                   // and the software tries to be \"smart\". Date ranges too" << endl <<
	"                   // can be specified using various formats including ISO 8601." << endl <<
	endl <<
	"                   // Binary Operators:" << endl <<
	"                   //   OR    := Union, the set of all elements in either of two sets" << endl << 
	"                   //   AND   := Intersection, the set of all elements in both sets" << endl << 
	"                   //   ANDNOT:= Elements in the one set but NOT in the other" << endl << 
	"                   //   XOR   := Exclusive Union, elements in either but not both" << endl <<
        "                   //   ADJ   := Matching terms are adjacent to one another" << endl <<
	"                   //   NEAR[:num] := matching terms in the sets are within  elements" << endl <<
        "                   //   BEFORE[:num] and AFTER[:num] := As NEAR but in specific order" << endl << 
	"                   //     num as integer is characters. As fraction of 1 its % of doc" << endl <<
	"                   //   DIST[>,>=,<,<=]num := distance between words in source measured in bytes." << endl <<
        "                   //   PEER  := Elements in the same (unnamed) final tree leaf node" << endl <<
	"                   //   PEERa := like PEER but after" << endl <<
	"                   //   PEERb := like PEER but ordered after" << endl <<
	"                   //   XPEER := Not in the same container" << endl <<
        "                   //   AND:field := Elements in the same node instance of field" << endl <<
	"                   //   BEFORE:field := like AND:field but before" << endl <<
	"                   //   AFTER:field := like AND:field but after" << endl << 
	"                   //   NEIGHBOR := A character prox variant (x%)" << endl <<
        "                   //   FOLLOWS, PRECEDES := Within some ordered elements of one another" << endl <<
	"                   //   FAR   := Elements a \"good distance\" away from each other" << endl <<
	"                   //   NEAR  := Elements \"near\" one another." << endl <<
	endl <<
	"                   // Unary Operators:" << endl <<
	"                   //   NOT               := Set compliment" << endl <<
	"                   //   WITHIN[:field]    := Records with elements within the specified field" << endl <<
	"                   //      RPN queries \"term WITHIN:field\" and \"field/term\" are equivalent." << endl <<
	"                   //      (for performance the query \"field/term\" is prefered to \"term WITHIN:field\")"
<< endl <<
        "                   //   WITHIN[:daterange] := Only records with record dates within the range" << endl <<
        "                   //   WITHKEY:pattern    := Only records whose key match pattern" << endl <<
        "                   //   SIBLING            := only hits in the same container" << endl <<
//	"                   //   INSIDE[:field]     := Hits are limited to those in the specified field" << endl <<
	"                   //   XWITHIN[:field]    := Absolutely NOT in the specified field" << endl << endl << 
        "                   //   FILE:pattern       := Records whose local file path match pattern" << endl <<
#if 0
        "                   //   EXTENSION:pattern := Records whose file extension match pattern" << endl <<
        "                   //   DOCTYPE:pattern   := Records whose doctype match pattern" << endl <<
        "                   //     (where pattern is a glob/wildcard expression)" << endl <<
#endif

        "                   //   REDUCE[:nnn]   := Reduce set to those records with nnn matching terms" << endl <<
        "                   //   FOCUS[:nnn]    := Prefer records with stronger joint-term evidence" << endl <<
	"                   //   NOTE: REDUCE:metric is a special kind of unary operator that trims the result" << endl <<
        "                   //   to metric cutoff regarding the number of different terms (see -h and -joint)." << endl
<<
        "                   //   Reduce MUST be specified with a positive metric and 0 (Zero) is a special case" << endl
 <<
        "                   //   designating the max. number of different terms found in the set." << endl << endl <<
	"                   //   HITCOUNT:nnn := Trim set to contain only records with min. nnn hits." << endl <<
	"                   //   HITCOUNT[>,>=,<,<=]num := as above. Example: HITCOUNT>10 means to include only" << endl <<
	"                   //       those records with MORE than 10 hits." << endl <<
	"                   //   TRIM:nnn    := Truncate set to max. nnn elements" << endl <<
	"                   //   BOOST:nnn   := Boost score by nnn (as weight)" << endl <<
	"                   //   SORTBY:<ByWhat> := Sort the set \"ByWhat\" (reserved names: Date, Score, Hits, etc. )" << endl <<
	"                   //   Must be specified with -n to have effect." << endl << endl;

      cout <<
	"Examples: " << prog << " -d POETRY truth 'beaut*' urn:2" << endl <<
	"          " << prog << " -d FTP -headline L C++" << endl <<
	"          " << prog << " -d WEBPAGES title/library" << endl <<
	"          " << prog << " -d WEBPAGE  -rpn library WITHIN:title" << endl <<
	"          " << prog << " -d WEBPAGES library WITHIN:title" << endl <<
	"          " << prog << " -d STORIES -rpn title/cat title/dog OR title/mouse OR" << endl <<
	"          " << prog << " -d STORIES -infix title/(cat or dog or mouse)" << endl <<
	"          " << prog << " -d POETRY -rpn speaker/hamlet line/love AND:scene" << endl <<
	"          " << prog << " -d POETRY -infix (speaker/hamlet and:scene line/love)" << endl <<
	"          " << prog << " -d POETRY -infix act/(speaker/hamlet and:scene line/love)" << endl <<
	"          " << prog << " -d MAIL -H -infix from/edz AND 'subject/\"Isearch Doctypes\"'" << endl <<
	"          " << prog << " -d KFILM -XML -show -rpn microsoft NT AND windows NOT" << endl <<
	"          " << prog << " -d BILLS -rpn vendor/BSn price<100 AND" << endl << 
	"          " << prog << " -d NEWS  -rpn unix WITHIN:2006" << endl <<
	"          " << prog << " -d SHAKESPEARE -P SPEECH/SPEAKER -rpn out spot PEER" << endl <<
        "          " << prog << " -d SHAKESPEARE  -P speech/speaker -P line/line  war court AND SIBLING
	"Note: \"Built-in\" Elements for -p and -headline: F for Full, B for Brief and S for Short." << endl <<
        "Additional \"Special\" elements: R for Raw, H for Highlight/Hits; and if they exist," << endl <<
        "L for location/redirect and M for metadata." << endl << endl <<
	"In the response one can select not just the record but also all elements of a specific field as" << endl <<
	"well as specific contents where the hit occurs (similar to -P). Example:  1,speech/speech" << endl <<
        "to select the contents of a speech where the hit(s) in record #1 occurs." << endl << 
	"Shorthand for element/element is @element, so 1,@line or even 1@line is really 1,line/line" << endl <<
	"By contrast 1,speech returns ALL the speeches in record #1." << endl <<
	"The above 'special' elements may also be specified, e.g. 1,H" << endl << endl;
//    PrintDoctypeList();
#endif
}

extern "C" {
  int _Isearch_main(int argc, char **argv);
};


int _Isearch_main (int argc, char **argv)
{
  const char *argv0 = argv[0];
#ifdef __GNUG__
  // cout is a performance disaster in libg++ unless we do this.
  ios::sync_with_stdio (0);
#endif
  if (argc < 2)
    {
      cout << endl << 
#ifdef VECTOR_INDEX
	"CoreQuarry Search v."
#else
	"IB Search v." 
#endif
	<<  _isearch_main_version << "." << SRCH_DATE(__DATE__).ISOdate() << "."
	      <<  __IB_Version << " (" << __HostPlatform <<
#ifdef _OPENMP
	" MultiThreaded" << 
#endif
 	")" << endl
      ; //  << __CopyrightData  << endl << endl;
      HelpUsage(  RemovePath(argv[0]).c_str() );
      return 0;
    }
  STRLIST DocTypeOptions;
  STRING Flag;
  STRING DBName;
  STRING RecordID;
  STRING ElementSet;
  // STRING AncestorElementSet;
  STRLIST AncestorElementList;
  STRING TermPrefix, TermSuffix;
  STRING Before ("\033[7m");
  STRING After ("\033[m");
  DATERANGE DateRange;
  bool have_date = false;
  bool ScanSearch = false;
  STRING      ScanSearchField;
  INT    startDoc = 0, endDoc = 0;
  size_t StartDoc = 0, EndDoc = 0;
  STRING ResTable;
  STRING LoadTable;
  enum SortBy Sort = ByScore; 
  enum NormalizationMethods Method = E2Normalization;
  DOUBLE MagFactor = 0.0;
  DOUBLE PriorityFactor = 1.192092896E-07F;
  size_t Clip = 0;
  DOUBLE DropAbsolute = 0;
  INT    DropScaled = 0;
  INT    DropHits = 0;
  bool Reduce = false;
  bool DebugFlag = false;
  bool ShowRusage = false;
  bool ShowRating = false;
  bool QuitFlag = false;
  bool VerboseFlag = false;
  bool ShellFlag = false;
  bool ByteRangeFlag = false;
  bool KeyFlag = false;
  bool DoctypeFlag = false;
  bool DateFlag = false;
  bool DateModifiedFlag = false;
  bool RpnQuery = false;
  bool InfixQuery = false;
  bool SmartQuery = true;
  STRING SmartField;
  bool WordsQuery = false;
  bool AndWordsQuery = false;
  bool PlainQuery = false;
  bool ExpandSynonyms = false;
  bool PresentHtml = false;
  INT  TopK = 300;
  INT Fuel = 0;
  bool ShowHits = false;
  bool ShowAux = false;
  bool ShowHeadline = false;
  bool FilenameOnly = false;
  bool Negative_scores = true;
  STRING Headline;
  bool Terse = false;
  bool TabFormat = false;
  bool ShowAbsoluteScore = false;
  INT x = 0;
  INT LastUsed = 0;
  bool ShowXML = false;
  bool ShowJson = false;
  bool ShowSummary = false;
  bool ShowHitContent = false;
  bool ShowAllHitContents = false;
  ElementSet = BRIEF_MAGIC;
  INT first = 1;
  INT last =  0;
  INT base_score = 100; 
  off_t common_words = 0;
  STRING string, tstring;
  const char *Pager = NULL;

  if (argv0 == NULL) argv0 = "Isearch";

#define DEF_LOG (LOG_PANIC|LOG_FATAL|LOG_ERROR|LOG_ERRNO|LOG_WARN|LOG_NOTICE)
  log_init(DEF_LOG, argv0, stdout);

  while (++x < argc)
    {
      if (argv[x][0] == '-')
	{
	  // process --opt as -opt
	  Flag = argv[x][1] == '-' ? argv[x] + 1 : argv[x];

          if (Flag.Equals ("-api"))
            {
              cout << "API " << __IB_Version << "/" << (DoctypeDefVersion & 0xFFF)
                << "  built with: " << __CompilerUsed  << " (" <<  __HostPlatform << ")" << endl;
              LastUsed = x;
            }
	  else if (Flag.CaseEquals ("-XML"))
	    {
	      ShowXML = true;
	      PresentHtml = true; // Latter PresentXML
	      LastUsed = x;
	    }
          else if (Flag.CaseEquals ("-JSON"))
            {
	      message_log (LOG_FATAL, "Json not yet supported");
              ShowJson = true;
              PresentHtml = true; // Latter PresentXML
              LastUsed = x;
            }
          else if (Flag.Equals ("-summary"))
            {
              ShowSummary = true;
              LastUsed = x;
            }
	  else if (Flag.Equals ("-show"))
	    {
	      ShowHitContent = true;
	      LastUsed = x;
	    }
	  else if (Flag.Equals ("-showall"))
	    {
	      ShowAllHitContents = true;
	      LastUsed = x;
	    }
	  else if (Flag.Equals ("-HTML") || Flag.Equals ("-H"))
	    {
	      PresentHtml = true;
	      LastUsed = x;
	    }
	  else if (Flag.Equals ("-o"))
	    {
	      if (++x >= argc)
		{
		  message_log (LOG_FATAL, "Usage: No option specified after -o.");
		  return 0;
		}
	      STRING S;
	      S = argv[x];
	      DocTypeOptions.AddEntry (S);
	      LastUsed = x;
	    }
	  else if (Flag.Equals ("-scan"))
	   {
              if (++x >= argc)
                {
                  message_log (LOG_FATAL, "Usage: No field/path specified after -scan.");
                  return 0;
                }
	      ScanSearchField = argv[x];
	      ScanSearch = true;
	      LastUsed = x;
	   }
	  else if (Flag.Equals ("-scale"))
	   {
              if (++x >= argc || (base_score = atoi(argv[x])) == 0)
                {
                  message_log (LOG_FATAL, "Usage: No number specified after -scale.");
                  return 0;
                }
	      LastUsed = x;
	   }
	  else if (Flag.Equals ("-fuel"))
	   {
	     if (++x >= argc)
                {
                  message_log (LOG_FATAL, "Usage: space fuel specified after %s.", Flag.c_str());
                  return 0;
                }
              Fuel = atoi(argv[x]);
              LastUsed = x;
	   }
	  else if (Flag.Equals ("-id"))
	   {
              if (++x >= argc)
                {
                  message_log (LOG_FATAL, "Usage: No Record_ID specified after %s.", Flag.c_str());
                  return 0;
                }
              RecordID = argv[x];
              LastUsed = x;
	   }
	  else if (Flag.Equals ("-d"))
	    {
	      if (++x >= argc)
		{
		  message_log (LOG_FATAL, "Usage: No database name specified after -d.");
		  return 0;
		}
	      DBName = argv[x];
	      LastUsed = x;
	    }
          else if (Flag.Equals ("-n"))
            {
              Sort = Unsorted;
              LastUsed = x;
            }
	  else if (Flag.Equals ("-k"))
	    {
	      Sort = ByKey;
	      LastUsed = x;
	    }
          else if (Flag.Equals ("-s"))
            {
              Sort = ByScore;
              LastUsed = x;
            }
	  else if (Flag.Equals ("-scat"))
	    {
	      Sort = ByCategory;
	      LastUsed = x;
	    }
	  else if (Flag.Equals("-snews"))
	    {
	      Sort = ByNewsrank;
	      LastUsed= x;
	    }
          else if (Flag.Equals ("-smag"))
            {
              Sort = Unsorted;
              if (++x >= argc)
                {
                  message_log (LOG_FATAL, "Usage: No factor specified after %s.", Flag.c_str());
                  return 0;
                }
              MagFactor = atof(argv[x]);
              LastUsed = x;
            }
          else if (Flag.Equals ("-priority"))
            {
              Sort = Unsorted;
              if (++x >= argc)
                {
                  message_log (LOG_FATAL, "Usage: No factor specified after %s.", Flag.c_str());
                  return 0;
                }
              PriorityFactor = atof(argv[x]);
              LastUsed = x;
            }
	  else if (Flag.Equals ("-sc"))
	    {
	      Sort = ByAdjScore;
	      LastUsed = x;
	    }
	  else if (Flag.Equals("-help"))
	    {
	      HelpUsage(RemovePath(argv[0]).c_str());
	      LastUsed = x;
	    }
	  else if (Flag.Equals("-help=json"))
	    {
	      HelpUsageJSON( RemovePath(argv[0]).c_str());
	      LastUsed = x;
	    }
	  else if (Flag.Equals("-help=txt"))
	    {
	      HelpUsageText(RemovePath(argv[0]).c_str() );
	      LastUsed = x;
	    }

	  else if (Flag.Equals("-qhelp=json"))
	    {
	      SQUERY::WriteOperatorHelpJSON(std::cout);
	      LastUsed = x;
	    }
          else if (Flag.Equals("-qhelp=txt"))
            {               
              SQUERY::WriteOperatorHelp(std::cout);
              LastUsed = x; 
            }
	  else if (Flag.Equals("-qhelp"))
	    {
	      if (isatty(fileno(stdout)))
		SQUERY::WriteOperatorHelp(std::cout);
	      else
		SQUERY::WriteOperatorHelpJSON(std::cout);
	      LastUsed = x;
	    }
	  else if (Flag.Equals("-ohelp"))
	    {
	     IniUsage(std::cerr);
	     // if (x + 1 == argc) return 0;
	     LastUsed = x;
	    }
	  else if (Flag.Equals("-h"))
	    {
	      Sort = ByAuxCount;
	      LastUsed = x;
	    }
	  else if (Flag.Equals ("-c"))
	    {
	      Sort = ByDate;
              LastUsed = x;
	    }
	  else if (Flag.Equals ("-cr"))
	    {
	      Sort = ByReverseDate;
	      LastUsed = x;
	    }
	  else if (Flag.Equals("-cosine_norm") || Flag.Equals("-L2_norm") || Flag.Equals("-norm=L2"))
	    {
	      Method = NormalizationL2;
	      LastUsed = x;
	    }
	  else if (Flag.Equals("-S2_norm") || Flag.Equals("-norm=S2"))
	    {
	      Method = NormalizationS2;
	      LastUsed = x;
	    }
          else if (Flag.Equals("-BM25_norm") || Flag.Equals("-norm=BM25") ||
		Flag.Equals("-norm=bm25"))
            {
              Method = NormalizationBM25;
              LastUsed = x;
            }
	  else if (Flag.Equals("-E2_norm") || Flag.Equals("-norm=E2"))
	    {
	      Method = E2Normalization ;
	      LastUsed = x;
	    }
	  else if (Flag.Equals("-L1_norm") || Flag.Equals("-norm=L1"))
	   {
	      Method = NormalizationL1;
	      LastUsed = x;
	   }
          else if (Flag.Equals("-AF_norm") || Flag.Equals("-norm=AF"))
           {
              Method = NormalizationAF; 
              LastUsed = x; 
           }
	  else if (Flag.Equals("-euclidean_norm") || Flag.Equals("-metric_norm") 
		|| Flag.Equals("-norm=E1"))
	    {
	      Method = EuclideanNormalization;
	      LastUsed = x;
	    }
          else if (Flag.Equals("-max_norm") || Flag.Equals("-norm=max"))
            {
              Method = MaxNormalization;
              LastUsed = x;
            }
          else if (Flag.Equals("-log_norm") || Flag.Equals("-norm=log"))
            {
              Method = LogNormalization;
              LastUsed = x;
            }
          else if (Flag.Equals("-bytes_norm") || Flag.Equals("-norm=bytes"))
            {
              Method = BytesNormalization;
              LastUsed = x;
            }
          else if (Flag.Equals("-no_norm"))
            {
              Method = NoNormalization;
	      if ( Sort == ByScore )
		 Sort = Unsorted;
              LastUsed = x;
            }
	  else if (Flag.Equals ("-p"))
	    {
	      if (++x >= argc)
		{
		  message_log (LOG_FATAL, "Usage: No element set specified after -p.");
		  return 0;
		}
	      ElementSet = argv[x];
	      LastUsed = x;
	    }
	  else if (Flag.Equals ("-P"))
	    {
	      if (++x >= argc)
		{
		  message_log (LOG_FATAL, "Usage: No element set specified after -P.");
		  return 0;
		}
	      if (!AncestorElementList.ContainsCase( argv[x] ))
		AncestorElementList.AddEntry(argv[x]);
	      LastUsed = x;
	    }
          else if (Flag.Equals ("-max") || (Flag.Equals("-top")))
            {
              if (++x >= argc)
                {
                  message_log (LOG_FATAL, "Usage: No number specified after -top.");
                  return 0;
                }
              TopK = atoi(argv[x]);
              LastUsed = x;
            }
          else if (Flag.Equals ("-negative"))
            { 
              Negative_scores = true;
              LastUsed = x;
            }
          else if (Flag.Equals ("-positive"))
            { 
              Negative_scores = false;
              LastUsed = x;
            }
	  
	  else if (Flag.Equals ("-reduce"))
	    {
	      Reduce = true;
              LastUsed = x;
	    }
	  else if (Flag.Equals("-reduce0"))
	    {
	      Reduce = true;
	      Sort = ByAuxCount;
	      LastUsed = x;
	    }
	  else if (Flag.Equals ("-drop_h"))
            {
              if (++x >= argc)
                {
                  message_log (LOG_FATAL, "Usage: No number specified after %s.", Flag.c_str());
                  return 0;
                }
              DropHits = (INT)atof(argv[x]);
              LastUsed = x;
            }
          else if (Flag.Equals ("-drop_a"))
            {
              if (++x >= argc)
                {
                  message_log (LOG_FATAL, "Usage: No number specified after %s.", Flag.c_str());
                  return 0;
                }
              DropAbsolute = atof(argv[x]);
              LastUsed = x;
            }
	  else if (Flag.Equals ("-drop_s"))
            {
              if (++x >= argc)
                {
                  message_log (LOG_FATAL, "Usage: No number specified after %s", Flag.c_str());
                  return 0;
                }
              DropScaled = atoi(argv[x]);
              LastUsed = x;
            }

          else if (Flag.Equals ("-clip"))
            {
              if (++x >= argc)
                {
                  message_log (LOG_FATAL, "Usage: No number specified after -clip.");
                  return 0;
                }
              Clip = (size_t)atol(argv[x]);
              LastUsed = x;
            }
	  else if (Flag.Equals ("-common"))
            {
              if (++x >= argc)
                {
                  message_log (LOG_FATAL, "Usage: No number specified after %s.", Flag.c_str());
                  return 0;
                }
              common_words = atol(argv[x]);
              LastUsed = x;
            }
	  else if (Flag.Equals ("-prefix"))
	    {
	      if (++x >= argc)
		{
		  message_log (LOG_FATAL, "Usage: No prefix specified after -prefix.");
		  return 0;
		}
	      Before = TermPrefix = argv[x];
	      LastUsed = x;
	    }
	  else if (Flag.Equals ("-suffix"))
	    {
	      if (++x >= argc)
		{
		  message_log (LOG_FATAL, "Usage: No suffix specified after -suffix.");
		  return 0;
		}
	      After = TermSuffix = argv[x];
	      LastUsed = x;
	    }
	  else if (Flag.Equals ("-range"))
	    {
             if (++x >= argc)
                {
                  message_log (LOG_FATAL, "Usage: No value specified after -range.");
                  return 0;
                }
	      if (strncasecmp(argv[x], "all", strlen(argv[x])) == 0) // 
		{
		  first = last = 0;
		}
	      else if (sscanf(argv[x], "%d-%d", &first, &last) == 0)
		{
		  message_log (LOG_FATAL, "Usage: Bad range specified.");
		  return 0;
		}
	      if (last == 0) TopK = -1;
	      else if (last > TopK) TopK = last; 
              LastUsed = x;
	    }
	  else if (Flag.Equals("-syn"))
	    {
	      ExpandSynonyms = true;
	      LastUsed = x;
	    }
	  else if (Flag.Equals ("-hits"))
	   {
	      ShowHits = true;
	      LastUsed = x;
	   }
          else if (Flag.Equals ("-joint"))
           {
              ShowAux = true;
              LastUsed = x;
           }
	  else if (Flag.Equals ("-t") || Flag.Equals("-terse"))
	   {
	     if (FilenameOnly)
	       message_log (LOG_WARN, "%s: headline (\"B\") over-ride for filename", Flag.c_str());
	     Headline = "B";
	     ShowHeadline = true;
	     FilenameOnly = false;
	     Terse = true;
	     LastUsed = x ;
	   }
	  else if (Flag.Equals("-filename"))
	   {
	      if (ShowHeadline)
		message_log (LOG_WARN, "%s: filename over-ride for headline", Flag.c_str());
	      FilenameOnly = true;
	      ShowHeadline = false;
	      ElementSet = NulString;
	      LastUsed = x;
	   }
	  else if (Flag.Equals("-tab"))
	   {
	     if (Headline.IsEmpty())
		Headline = "B";
             ShowHeadline = true;
             // Terse = true;
             LastUsed = x ;
	     TabFormat = true;
	   }
          else if (Flag.Equals ("-headline"))
           {
	     if (++x >= argc)
                {
                  message_log (LOG_FATAL, "Usage: No value specified after %s.", Flag.c_str());
                  return 0;
                }
	      Headline = argv[x];
              ShowHeadline = true;
              LastUsed = x;
           }
          else if (Flag.Equals ("-score"))
           {
              ShowAbsoluteScore = true;
              LastUsed = x;
           }
	  else if (Flag.Equals("-rating"))
	   {
	      ShowRating = true;
	      base_score = 5;
  	      LastUsed = x;
	   }
          else if (Flag.Equals ("-daterange"))
            {
             if (++x >= argc)
                {
                  message_log (LOG_FATAL, "Usage: No value specified after -daterange.");
                  return 0;
                }
	      DateRange = argv[x];
              LastUsed = x;
            }
	  else if (Flag.Equals ("-startdoc"))
	    {
	      if (++x >= argc)
		{
		  message_log (LOG_FATAL, "Usage: No value specified after -startdoc.");
		  return 0;
		}
	      startDoc = atoi(argv[x]);
	      if (startDoc > TopK) TopK = startDoc + 300;
	      LastUsed = x;
	    }
	  else if (Flag.Equals ("-enddoc"))
	    {
	      if (++x >= argc)
		{
		  message_log (LOG_FATAL, "Usage: No value specified after %s.", Flag.c_str());
		  return 0;
		}
	      endDoc = atoi(argv[x]);
	      if (endDoc > TopK) endDoc = TopK;
	      LastUsed = x;
	    }
	  else if (Flag.Equals ("-table") || Flag.Equals("-save"))
	    {
              if (++x >= argc)
                {
                  message_log (LOG_FATAL, "Usage: No value specified after %s.", Flag.c_str());
                  return 0; 
                }
              ResTable = argv[x];
              LastUsed = x; 
	    }
          else if (Flag.Equals ("-D") || Flag.Equals("-load"))
            {
              if (++x >= argc)
                {
                  message_log (LOG_FATAL, "Usage: No value specified after %s.", Flag.c_str());
                  return 0;
                }
              LoadTable = argv[x];
              LastUsed = x;
            }
	  else if (Flag.Equals ("-v") || Flag.Equals("-verbose"))
	    {
	      QuitFlag = false;
	      VerboseFlag = true;
	      Terse = false;
	      LastUsed = x;
	    }
	  else if (Flag.Equals ("-q") || Flag.Equals("-quiet"))
	    {
	      QuitFlag = true;
	      LastUsed = x;
	    }
	  else if (Flag.Equals("-shell"))
	    {
	      ShellFlag = true;
	      LastUsed = x;
	    }
	  else if (Flag.Equals ("-byterange"))
	    {
	      ByteRangeFlag = true;
	      LastUsed = x;
	    }
	  else if (Flag.Equals ("-filesystem"))
	    {
	      FilenameOnly = true;
              ShowHeadline = false;
	      Headline = NulString;
              ElementSet = NulString;
	      ByteRangeFlag = true;
	      QuitFlag = true;
	      TabFormat = true;
              LastUsed = x;
	    }
	  else if (Flag.Equals ("-key"))
	    {
	      KeyFlag = true;
	      LastUsed = x;
	    }
	  else if (Flag.Equals("-doctype"))
	    {
	      DoctypeFlag = true;
	      LastUsed = x;
	    }
	  else if (Flag.Equals ("-date"))
	    {
	      DateFlag = true;
	      LastUsed = x;
	    }
	  else if (Flag.Equals("-datemodified"))
	    {
	      DateModifiedFlag = true;
	      LastUsed = x;
	    }
	  else if (Flag.Equals ("-rpn"))
	    {
	      RpnQuery = true;
	      LastUsed = x;
	      SmartQuery = false;
	    }
	  else if (Flag.Equals ("-infix"))
	    {
	      InfixQuery = true;
	      LastUsed = x;
	      SmartQuery = false;
	    }
	  else if (Flag.Equals ("-words"))
	    {
	      WordsQuery = true;
	      LastUsed = x;
	    }

	  else if (Flag.Equals ("-and"))
            {
              AndWordsQuery = true;
              LastUsed = x;
            }
	  else if (Flag.Equals ("-regular"))
	    {
	      PlainQuery = true;
	      if (SmartQuery) SmartQuery = false;
	      LastUsed = x;
	    }
          else if (Flag.Equals ("-smart"))
            {
              SmartQuery = true;
              if (++x >= argc)
                { 
                  message_log (LOG_FATAL, "Usage: No field specified after -smart.");
                  return 2;
                }
	      SmartField = argv[x];
	      LastUsed = x;
            }
          else if (Flag.Equals ("-level"))
            {
              if (++x >= argc)
                {
                  message_log (LOG_FATAL, "Usage: No level specified after -level.");
                  return 2;
                }
              log_init((int)(strtol (argv[x], NULL, 10) & 0xFF));
              LastUsed = x;
            }

          else if (Flag.Equals ("-sort") || Flag.Equals("-qsort"))
            {
              if (++x >= argc)
                {
                  message_log (LOG_FATAL, "Usage: No algorithm name specified after -%d.", Flag.c_str());
                  return 2;
                }
              switch (*(argv[x]))
                {
                  case 'b': case 'B': _IB_Qsort =  BentleyQsort; break;
                  case 's': case 'S': _IB_Qsort =  SedgewickQsort; break;
		  case 'd': case 'D': _IB_Qsort =  DualPivotQsort; break;
		  // case 't': case 'T': _IB_Qsort =  TimQsort; break;
		  case 'n': case 'N': default:  _IB_Qsort =  qsort; break; // Default libc
                }
              LastUsed = x;
            }

          else if (Flag.Equals ("-cd") || Flag.Equals("-chdir") || Flag.Equals("-cwd"))
           {
            if (++x >= argc)
                {
                  message_log (LOG_FATAL, "Usage: No directory specified after %s.", Flag.c_str());
                  return 2;
                }
             const char *path = argv[x];
             if (! DirectoryExists(path))
               {
                  message_log (LOG_ERROR, "Usage: directory specified in %s \"%s\" does not exist or is not accessible.",
                        Flag.c_str(), path);
               }
            else
              if (chdir (path) == -1)
		message_log (LOG_ERRNO|LOG_WARN, "Could not change directory to '%s'", path);
             LastUsed = x;
	   }
	  else if (Flag.Equals ("-more"))
	    {
	      Pager = "more";
	      LastUsed = x;
	    }
	  else if (Flag.Equals ("-pager"))
            {
              if (++x >= argc)
                {
                  message_log (LOG_FATAL, "Usage: No program specified after %s.", Flag.c_str());
                  return 2;
                }
              Pager = argv[x];
              LastUsed = x;
            }
	  else if (Flag.Equals ("-copyright"))
	    {
	      std::cout << __CopyrightData  << endl << endl;
	      LastUsed =x;
	    }
	  else if (Flag.Equals ("-bench"))
	    {
              ShowRusage = true;
              LastUsed =x;
	    }
	  else if (Flag.Equals ("-debug"))
	    {
	      DebugFlag = true;
	      __Register_IB_Application(argv0, stdout, DebugFlag);
	      LastUsed = x;
	    }
	}
    }		/* while() */

  __Register_IB_Application(argv0, stdout, DebugFlag);

  clock_t start = clock();

  PRSET prset = NULL;

  SQUERY squery;
  UINT4 matches = 0;
  if (!LoadTable.IsEmpty()) 
    {
      // Load Session
      prset = new RSET(); 
      PFILE Fp = fopen(LoadTable, "rb");
      if (Fp)
	{
	  BYTE ranked;
	  STRING tmp;
	  prset->Read (Fp);
	  Read(&ranked, Fp);
	  DBName.Read (Fp);
	  tmp.Read (Fp);
	  squery.Read (Fp);
	  Read(&matches, Fp);
	  fclose (Fp);
	}
    }

  if (DBName.IsEmpty())
    {
      DBName = __IB_DefaultDbName;
//              cout << "ERROR: No database name specified!" << endl;
      //              return 0;
    }

  if ((RpnQuery) && (InfixQuery))
    {
      message_log (LOG_FATAL, "Usage: The -rpn and -infix options can not be used together.");
      exit (1);
    }
   if ((AndWordsQuery || WordsQuery) && (RpnQuery || InfixQuery))
    {
      message_log (LOG_FATAL, "Usage: -and/-words and -rpn/-infix options can not be used together.");
      exit (1);
    }

  if (ExpandSynonyms)
    squery.OpenThesaurus( DBName );

  x = LastUsed + 1;
  INT NumWords = 0;
  if (x >= argc && LoadTable.IsEmpty() && RecordID.IsEmpty())
    {
      if (!ShellFlag && !ScanSearch)
        return 0;
    }
  else
    NumWords = argc - x;

  INT z = x;
//      STRING WordList[NumWords];
  PSTRING WordList = NULL;

  if (NumWords)
    WordList = new STRING[NumWords];
  for (z = 0; z < NumWords; z++)
    {
      WordList[z] = argv[z + x];
    }

  STRING DBPathName, DBFileName;
  RESULT result;
  INT t, n;

#define USE_VIDB 1
#if USE_VIDB
  PVIDB pdb = new VIDB (DBName, DocTypeOptions);
#else
  PIDB pdb = new IDB (DBName, DocTypeOptions);
#endif
  if (pdb == NULL)
    {
      return -1;
    }

  
  if (Fuel)
    pdb->SetDbSearchFuel (Fuel) ;

  if (ShowXML)
    {
      cout << "<?xml version='1.0' ";
      STRING Charset;
      if (GetGlobalCharset (&Charset))
        cout << "encoding='" << Charset << "' ";
      cout << "standalone='yes'?>" << endl;
    }
  else if (ShowJson)
    {
      cout << "{\n@:{\n";
      STRING Charset;
      if (GetGlobalCharset (&Charset))
        cout << " \"encoding\":'" << Charset << "',\n";
      cout <<   " \"standalone\": 1\n}" << endl;
    }       

  INT Locks = pdb->GetLocks ();
  if (Locks)
    {
      if (Locks & L_READ)
	{
	  if (ShowXML)
	    cout << "<ERROR>";
	  else if (ShowJson)
	    cout << "\"ERROR\": ";
	  cout << "Database \"" << DBName << "\" temp. not available. Try again in a few minutes..";
	  if (ShowXML)
	    cout << "</ERROR>";
	  cout << endl;
	  delete pdb;
	  if (WordList) delete[]WordList;
	  return 0;
	}
      else
	{
	  cout << "Warning: " << DBName << " is being updated.." << endl;
	}
    }

  if (!pdb->Ok ())
    {
      if (ShowXML)       cout << "<ERROR>";
      else if (ShowJson) cout << "\"ERROR\": ";
      cout << "The specified database \"" << DBName << "\" is not compatible with this version (or undefined). ";
      if (pdb->GetIDBCount() == 1)
	{
	  switch (pdb->GetIDB(1)->GetErrorCode()) {
	    case -64: cout << "Use 32-bit binaries."; break;
	    case -32: cout << "Use 64-bit binaries."; break;
	    default: cout << "Re-index!";
	}
      }
      if (ShowXML)
        cout << "</ERROR>";
      cout << endl;
      delete pdb;
      if (WordList) delete[]WordList;
      return 0;
    }


  INT Total = pdb->GetTotalRecords () /* - pdb->GetTotalDocumentsDeleted () */;
  if (Total <= 0)
    {
      if (ShowXML)
	cout << "<ERROR>";
      cout << "Database \"" << DBName << "\" not found or empty ("
	<<  pdb->GetTotalRecords () << "." << pdb->GetTotalDocumentsDeleted ()
	<< ").";
      if (ShowXML)
	cout << "</ERROR>";
      cout << endl;
      if (pdb)
	delete pdb;		// @@@ edz: Need to destroy pdb!
      if (WordList) delete[]WordList;
      return 0;
    }

  if (common_words > 0)
    pdb->SetCommonWordsThreshold(common_words);

  if (DebugFlag)
    {
      pdb->DebugModeOn ();
    }

  STRING RecordSyntax = PresentHtml ? HtmlRecordSyntax : SutrsRecordSyntax;

  if (!RecordID.IsEmpty())
    {
      RESULT RsRecord;
      if (pdb->KeyLookup (RecordID, &RsRecord))
	{
	  pdb->BeginRsetPresent (RecordSyntax);
	  cout << pdb->DocPresent (RsRecord, ElementSet, RecordSyntax) << endl;
	  pdb->EndRsetPresent (RecordSyntax);
	  return 0;
	}
      else
	message_log (LOG_WARN, "Could not locate record id '%s'", RecordID.c_str());
      return -1;
    }
  else if (!Terse && !ShowXML && !ShowJson && !TabFormat && VerboseFlag)
    cout << "Isearch " << __IB_Version << endl;


  if (Clip)
    pdb->SetDbSearchCutoff(Clip);
  if (PriorityFactor != 1.192092896E-07F)
    pdb->SetPriorityFactor(PriorityFactor);

  // #################################################################################
  // Here is where we start the search

  if (DateRange.Defined())
    {
      pdb->SetDateRange(DateRange);
      have_date = true;
    }

  clock_t s_creat = clock();
  STRING QueryString;

  for (z = 0; z < NumWords; z++)
    {
      if (z != 0)
	{
	  QueryString.Cat (' ');
	}
      QueryString.Cat (WordList[z]);
    }
  if (WordList) delete[]WordList;

  if (ShowXML)
    cout << "<!-- ";

  int nothing = 0;

again:
  if (!Terse && !TabFormat && VerboseFlag)
    cout << "Searching database " << DBName
#ifdef HAVE_LOCALE
    << " (\"" << setlocale (LC_CTYPE, NULL) << "\" locale)"
#endif
    << " of " << pdb->DateLastModified()
    << ": (" << Total << " Records)" << endl;

 STRING originalQueryString( QueryString );

 if (!QueryString.IsEmpty()) {
  if (InfixQuery)
    {
      if (!squery.SetInfixTerm (QueryString))
	{
	  cout << "InFix ERROR: " << squery.LastErrorMessage() << endl;
	  return 0;
	}
    }
  else if (RpnQuery)
    {
      if (!squery.SetRpnTerm (QueryString))
	{
	  cout << "RPN ERROR: " << squery.LastErrorMessage() << endl;
	  return 0;
	}
    }
  else if (QueryString.GetLength())
    {
      if (AndWordsQuery)
        squery.SetWords (QueryString, 1, OperatorAnd);
      else if (WordsQuery)
	squery.SetWords (QueryString);
      else
	squery.SetTerm (QueryString);
    }
  }

  size_t termCount = squery.GetRpnTerm (&QueryString);

  SCANLIST List;
  clock_t s_search = clock();

  if (prset == NULL && (termCount != 0 || ScanSearch))
    {
      if (ExpandSynonyms)
	{
	  squery.ExpandQuery();

	  STRING S;
	  termCount = squery.GetRpnTerm (&S);
	  message_log (LOG_DEBUG, "Expand Query = %s", S.c_str());
	}
      if (Clip == 0 && Reduce && termCount == 1) 
	{
	  pdb->SetDbSearchCutoff(TopK);
	}
      size_t match = 0;
      if (ScanSearch)
	{
	  List = pdb->ScanSearch(squery, ScanSearchField);
	} 
      else if (SmartQuery && !(AndWordsQuery || WordsQuery))
        {
	  if (SmartField == "--" ) SmartField.Clear();
  	  prset = pdb->VSearchSmart(originalQueryString, SmartField, Sort, TopK, &match, Method, &squery);
	  termCount = squery.GetRpnTerm (&QueryString);
	  message_log (LOG_DEBUG, "Query: %s", QueryString.c_str());
        }
       else
        {
	  prset = pdb->VSearch (squery, Sort, TopK, &match, Method);
        }
      matches = match;
      if (prset == NULL && !ScanSearch)
        message_log (LOG_ERROR, "Search ERROR: %s", (const char *)( pdb->ErrorMessage()) );
  }

  clock_t s_end = clock();


  if (prset)
    {
      if (Reduce)
        prset->Reduce (termCount);
      if (DropHits)
	prset->DropByTerms (DropHits);
      if (DropAbsolute)
        prset->DropByScore(DropAbsolute);
      else if (DropScaled)
        prset->DropByScore(DropScaled, base_score);
      if (MagFactor)
	prset->SortByCategoryMagnetism (MagFactor);
      else
	prset->SortBy(Sort);
    }


#ifndef _WIN32
  if (!Terse)
  {
    const double factor = 1000.0/CLOCKS_PER_SEC;
    const double cpu = s_end > s_search ? (s_end - s_search)*factor : 0.0; 
    const double cpu_total = s_end > start ? (s_end - start)*factor : 0.0;
    const double cpu_start = s_creat > start ? (s_creat - start)*factor : 0.0;
    char line[BUFSIZ];
    // Need to check since BSD reports funny values near 0
    if (termCount > 1)
      {
/*  The value of CLOCKS_PER_SEC is required to be 1 million on all
    XSI-conformant systems. */
	snprintf(line, BUFSIZ,
        // XSI 
	"Process time: %.0f ms. (%.0f) Search: %.0f ms. (%.1f ms/term)"
	// "Process time: %.1f ms. (%.1f) Search: %.1f ms. (%.2f ms/term)"
	, cpu_total, cpu_start, cpu, cpu/termCount);
      }
    else // 1 term 
      {
	snprintf(line, BUFSIZ,
	// XSI
	"Process time: %.0f ms. (%.0f) Search: %.0f ms."
	// "Process time: %.1f ms. (%.1f) Search: %.1f ms."
	,cpu_total, cpu_start, cpu);

      }
    if (TabFormat)
      cout << "# ";
    cout << line;
  }
#endif

  if (!Terse && have_date)
    {
       cout << endl << "Date Range: " << DateRange;
    }

  if (ScanSearch) 
    {
      cout << endl << "Words: " ;
      List.Dump();
      List.Clear();
      cout << endl;
    }

  if (ShowXML)
   cout << "-->";
  cout << endl;

  pdb->BeginRsetPresent (RecordSyntax);

  if (prset == NULL)
    {
      if (!QueryString.IsEmpty() && !ScanSearch)
	{
	  cout << "ERROR: "<< pdb->ErrorMessage () << " [ " << QueryString <<  " ]" << endl;
	  delete pdb;
	  exit (-1);
	}
      prset = new RSET ();
    }
  if (!ResTable.IsEmpty())
    {
      // Save Results
      PFILE Fp = fopen(ResTable, "wb");
      if (Fp)
	{
	  // Save Session
	  prset->Write (Fp);
	  Write((BYTE)false, Fp);
	  DBName.Write (Fp);
	  QueryString.Write (Fp);
	  squery.Write (Fp);
	  ::Write((UINT4)matches, Fp);
	  fclose (Fp);
	}
    }

  n = prset->GetTotalEntries ();
  // Display only documents in -startdoc/-enddoc range
  if (startDoc || endDoc)
    {
      if ((EndDoc = (endDoc < 0) ? n - endDoc : endDoc) == 0)
	EndDoc = n;
      if ((StartDoc = (startDoc < 0) ? n - startDoc : startDoc) == 0)
	StartDoc = 1;
      PRSET OldPrset = prset;
      prset = new RSET ();
      for (size_t i = StartDoc; i <= EndDoc && i <= n; i++)
	{
	  if (OldPrset->GetEntry (i, &result))
	    prset->AddEntry (result);
	}
      delete OldPrset;
      n = prset->GetTotalEntries ();
    }

  CHR Selection[BUFSIZ];
  CHR s[256];
  INT FileNum;
  STRING BriefString;
  STRING Element, TempElementSet;
  STRING GlobalDoctype;

  pdb->GetGlobalDocType (&GlobalDoctype);
  if (last == 0)
    last = n;
  if (matches == 0)
    n = 0;
  else
    n = last - first + 1;
  if (!Terse && !ShowXML && !TabFormat)
    {
      if (VerboseFlag) {
        if (!GlobalDoctype.IsEmpty()) cout << "Global Doctype: " << GlobalDoctype << endl;
        if (!(string = pdb->GetTitle ()).IsEmpty()) cout << "Title: " << string << endl;
        if (!(string = pdb->GetMaintainer ()).IsEmpty()) cout << "Maintainer: " << string << endl;
        if (!(string = pdb->GetComments ()).IsEmpty()) cout << "Comments: " << string << endl;
      }
      if (!QueryString.IsEmpty())
	{
	  cout << "Query: " << QueryString << endl;
	  if ( base_score != 100) {
	    if (ShowRating) cout << "Rating Scale 1-" << base_score << " stars" << endl;
	    else cout << "Score Scale: 1-" << base_score << endl; 
	  }

	  if (VerboseFlag) cout << "Number of Term-Hits: " << (INT)(prset->GetHitTotal()) << endl;
	  if (matches) cout << endl << matches << " record" << ((matches != 1) ? "s" : "")
		<< " of " << Total << " matched your query, ";
	  if (!Negative_scores) cout << "max. ";
	  cout << n;
	  if (!Negative_scores) cout << " non-negative scored";
	  cout << " record" << ((n != 1) ? "s" : "")  << " displayed." << endl;
	  if (!ElementSet.IsEmpty() && ElementSet!="B")
	    cout << "HeadlineElement:= " << ElementSet << endl;

	  cout << endl;
	}
    }

    static const char *Rating[] = {
      "☆☆☆☆☆",
      "★☆☆☆☆",
      "★★☆☆☆",
      "★★★☆☆",
      "★★★★☆",
      "★★★★★"
    };


  // Process Loop
  do {
      if (TabFormat && !Terse)
	{
          cout << "# " << n << "\t" << first << "-" << last << "\t" << matches  << "\t" << prset->GetHitTotal() << endl;
	}
      else if (!Terse && !ShowXML && n != 0)
	{
	  cout << "       ";
	  if (ShowAbsoluteScore)
	  cout << "Absolute ";
	  cout << (ShowRating ? "Rating" : "Score");
	  if (ShowHits)
	    cout << "  Hits  ";
	  if (ShowAux)
	    cout << "  Joint ";
	  if (FilenameOnly)
	    cout << "  Filename" << endl;
	  else if (ShowHeadline)
	    cout << "  Headline" << endl;
	  else
	    cout << "  File" << endl;
	}
      else if (ShowXML)
	{
	  cout << "<RESULTS NUMBER=\"" << n << "\" HITS=\""
		<< (INT)(prset->GetHitTotal()) << "\" MATCHES=\"" << matches;
	  if (n != last)
	    cout << "\" FIRST=\"" << first << "\" LAST=\"" << last;
	  cout << "\">" << endl; 
	  if (!(string = pdb->GetTitle ()).IsEmpty())
	    cout << "<TITLE>" << string << "</TITLE>" << endl;
	  STRING Name, Address;
	  pdb->GetMaintainer (&Name, &Address);
	  if (Name.GetLength() || Address.GetLength())
	    {
	      cout << "<MAINTAINER";
	      if (Name.GetLength()) cout << " NAME='" << Name << "'";
	      if (Address.GetLength()) cout << " ADDRESS='" << Address << "'";
	      cout << "/>" << endl;
	      if (!(string = pdb->GetComments ()).IsEmpty())
		cout << "<COMMENTS>" << string << "</COMMENTS>" << endl;
	    }
	  cout << "<QUERY>" << QueryString << "</QUERY>" << endl;
	}
      for (t = first; t <= last; t++)
	{
	  if (!prset->GetEntry (t, &result))
	    continue;
          const DOUBLE score = result.GetScore ();
	  if (!Negative_scores && score < 0.0)
	    continue;

	  if (ShowXML)
	    {
	      LOCALE Locale (result.GetLocale());
	      cout << "<RESULT ID=\"" << t << "\" CHARSET=\"" << Locale.GetCharsetName()
		<< "\" LANGUAGE=\"" << Locale.GetLanguageCode() << "\">" << endl
		<< "<SCORE ABSOLUTE=\"" << score << "\" SCALE=\"" << base_score  << "\">"
		<< prset->GetScaledScore (score, base_score)
		<< "</SCORE>" << endl;
	    }
	  else if (TabFormat)
	    {
	      cout << t << ".\t";
              if (ShowAbsoluteScore)
                cout << score << "\t";
	      int N = prset->GetScaledScore (score, base_score);
	      if (ShowRating) cout << Rating[N];
	      else cout << N;
	      cout << "\t";
              if (ShowHits)
                cout << result.GetHitTotal() << "\t";
	      if (ShowAux)
		cout << result.GetAuxCount() << "\t";
	    }
	  else
	    {
	      cout << setw (4) << t << ".";
	      if (ShowAbsoluteScore)
		cout << "  " << setw(8) << score;

	      int N = prset->GetScaledScore (score, base_score); 
	      cout << "  ";
	      if (ShowRating) cout << Rating[N];
	      else cout << setw ( (int)(log10((double)base_score)+0.5) + 2) << N;
	      if (ShowHits)
                cout << " " << setw(4) << result.GetHitTotal() << "   ";
	      if (ShowAux)
		cout << " " << setw(4) << result.GetAuxCount() << "   ";
	      cout << "  ";

            }
	  if (ShowXML)
	    {
	      cout << "<FILE PATH=\"" << result.GetPath() << "\" NAME=\""
		<< result.GetFileName() << 
		"\" START=\"" << result.GetRecordStart () <<
		"\" END=\"" << result.GetRecordEnd () << "\"/>" << endl;
	    }
	  else
	    {
	      if (ShowHeadline)
		{
		  if (Headline == "H")
		    pdb->HighlightedRecord(result, Before, After, &BriefString);
		  else if (Headline == "B")
		    pdb->Headline(result, RecordSyntax, &BriefString);
		  else
		    pdb->Present (result, Headline, RecordSyntax, &BriefString);
		  cout << BriefString;
		}
	      else
		{
		  cout << result.GetFullFileName();
		  if (ByteRangeFlag)
		    cout << "\t[ " << result.GetRecordStart () <<
			" - " << result.GetRecordEnd () << " ]";
		}
	      cout << endl;
	    }
	  if (ShowXML || DoctypeFlag)
	    {
	      result.GetDocumentType (&string);
	      if (string.IsEmpty())
		string = GlobalDoctype;
	      if (!string.IsEmpty())
		{
		  if (ShowXML) cout << "<DOCTYPE>";
		  cout << string;
		  if (ShowXML) cout << "</DOCTYPE>" << endl;
		  else cout << ": ";
		}
	    }
          if (RecordSyntax == HtmlRecordSyntax)
            {
              if (pdb->URL (result, &string, false))
		{
		  if (ShowXML)
		    cout << "<URL>" << string << "</URL>" << endl;
		  else
		    cout << string << endl;
		}
            }
	  if (ShowXML)
	    {
	      result.GetKey (&string);
	      cout << "<KEY>" << string << "</KEY>" << endl;
	    }
	  else if (KeyFlag)
	    {
	      result.GetKey (&string);
	      cout << "              {" << string << "}" << endl;
	    }
	  if (ShowXML)
	    {
	      SRCH_DATE TheDate = result.GetDate ();
	      if (TheDate.IsValidDate())
		{
		  TheDate.ISO(&string);
		  cout << "<DATE>" << string << "</DATE>" << endl;
		}
	    }
	  else
	    {if (DateFlag) {
	      SRCH_DATE TheDate (result.GetDate ());
	      if (TheDate.IsValidDate())
		cout << "              Record Date: " << TheDate.RFCdate() << endl;
	     }
	     if (DateModifiedFlag) {
		SRCH_DATE TheDate (result.GetDateModified ());
		if (TheDate.IsValidDate())
		  cout << "              Record DateModified: " << TheDate.RFCdate() << endl;

	     }
	    }
	  if (ShowSummary)
	    {
	      pdb->Summary(result, (PresentHtml || ShowXML ) ? HtmlRecordSyntax : SutrsRecordSyntax, &string);
	      if (string.GetLength())
		{
		  if (ShowXML)
		    cout << "<DESCRIPTION>";
		  cout << string;
		  if (ShowXML)
		    cout << "</DESCRIPTION>";
		  cout << endl;
		}
	    }
	  if (!ShowHeadline || (ElementSet != "B")) {
	    TempElementSet = ElementSet;
	    while (!TempElementSet.IsEmpty()) {
	      Element = TempElementSet;
	      if ((x = TempElementSet.Search (',')))
		{
		  Element.EraseAfter (x - 1);
		  TempElementSet.EraseBefore (x + 1);
		}
	      else
		{
		  TempElementSet.Clear();
		}
	      if (ElementSet == "H")
		{
		  pdb->HighlightedRecord(result, Before, After, &BriefString);
		}
	      else if (Element == "B")
		pdb->Headline(result, RecordSyntax, &BriefString);
	      else
		pdb->Present (result, Element, RecordSyntax, &BriefString);
	      if (BriefString.GetLength () > 0)
		{
		  if (ShowXML)
		    {
		      if (Element == "B")
			{
			  cout << "<HEADLINE>";
			  Element = "HEADLINE";
			}
		      else
			{
			  cout << "<VALUE NAME=\"" << Element << "\">";
			  Element = "VALUE";
			}	
		    }
		  else
		    cout << '`';
		  cout << BriefString;
		  if (ShowXML)
		    cout << "</" << Element << ">";
		  else
		    cout << '\'';
		  cout << endl;
		}
	      }
	    }
#if 0
	  if (AncestorElementSet.GetLength())
	    {
	      STRLIST list;
	      int     count = pdb->GetAncestorContent(result, AncestorElementSet, &list);
	      if (count)
		{
		  STRING content;
		  for (int i=1; i<= count; i++)
		    {
		      if (ShowXML) cout << "<!-- ";
		      cout << "** '" <<  AncestorElementSet << "' Fragment: " << endl;
		      list.GetEntry(i, &content);
		      cout << content << endl;
		      if (ShowXML) cout << "-->" << endl; 
		    }
		}
	    }
#else

	  // TODO: align the elements !!!
         if (!AncestorElementList.IsEmpty()) {
	    STRLIST *lists = NULL;

	    size_t    idx = 0;
	    size_t    max_items = 0;
	    for (const STRLIST *p = AncestorElementList.Next(); p != &AncestorElementList; p = p->Next()) {
              STRLIST list;
	      STRING AncestorElementSet = p->Value();
              int     count = pdb->GetAncestorContent(result, AncestorElementSet, &list);
              if (count)
                {
                  STRING content;

		  if (lists == NULL) lists = new STRLIST [max_items = count + 30];
                  for (int i=1; i<= count; i++)
                    {
//                      if (ShowXML) cout << "<!-- ";
//		      if (!Terse)
//                     	cout << "** '" <<  AncestorElementSet << "' Fragment: " << endl;
//		      else
//			cout << "\t\t" << "[" << i << "] " << AncestorElementSet << ": ";
                      list.GetEntry(i, &content);
//                    cout << content << endl;

		      if (i < max_items)
			lists[i-1].AddEntry( AncestorElementSet + ": " + content);
//                    if (ShowXML) cout << "-->" << endl; 
                    }
                } // if couunt
	        idx++;
             } // for 


	    if (ShowXML) cout << "<!-- ";
	    if (lists) for (size_t i = 0; !lists[i].IsEmpty(); i++)
	    {
	       cout << "\t[" << i+1 << "] " << lists[i] << endl;
	    }
	    if (ShowXML) cout << "-->" << endl;
	    if (lists) delete[] lists;


	  }
#endif
	  if (ShowHitContent && result.GetHitTotal())
            {
	      if (ShowXML)
		pdb->XMLContext(result, &string, &tstring, "MATCH");
	      else
		pdb->Context(result, &string, &tstring);

              if (ShowXML)
                cout << "<HIT TERM=\"" << tstring << "\">";
              else
                cout << "Evidence: ";
	      if (string.IsEmpty())
		cout << "** Unavailable **";
              else
		cout << string;
              if (ShowXML)
                cout << "</HIT>";
              cout << endl;
            }
	  if (ShowJson)
	    {
	      cout << pdb->JsonHitTable(result) << endl;
	    }
	  else if (ShowXML)
	    {
#if 1
	      cout << pdb->XMLHitTable(result) << endl;
#else
	      FCLIST HitTable;
	      result.GetHitTable(&HitTable);
	      dumpXMLHitTable(&HitTable, pdb, result); 
#endif
	    }
	  if (ShowXML)
	    cout << "</RESULT>" << endl << endl;
	} // for
      if (ShowXML)
	{
	  cout << "</RESULTS>" << endl;
	}
      pdb->EndRsetPresent (RecordSyntax);
      if (((QuitFlag) || (n == 0)) && ! ShellFlag )
	{
	  FileNum = 0;
	}
      else
	{
	  if (ShowXML) cout << "<!-- ";
	newline:
	  cout << endl << "Enter Query (=), [un]set option, range first-last or Select file #[,element|@element|@path]: ";
	cout.flush();
#ifdef LINUX
	  { STRING s;
	  if (s.FGet(stdin, sizeof(Selection)-1) == true)
	    strcpy(Selection, s.c_str());
	  else
	    Selection[0] = '\0'; }
#else
	  cin.getline (Selection, sizeof(Selection)-1, '\n');
#endif
	  if (*Selection == '\0' || *Selection == '\04' || *Selection == 26) nothing++;
	  else nothing = 0;
	  if (nothing > 1 || strncmp(Selection, "qui", 3) == 0 || strncmp(Selection, "exi", 3) == 0 ||
		*Selection == '\04' || *Selection == 26 ||
		((Selection[0]=='.' || Selection[0] == 'q') && Selection[1] == '\0') ||
		feof(stdin))
	    {
	      break;
	    }
	  // @element is shorthand for element/element
	  //
	  if (strncmp(Selection, "hel", 3) == 0 || *Selection == '?')
	    {
	       cout << "# Help: set/unset or NNN[,<ELEMENT>], NNN,<Ancestor>/<Descendant>, range nnn-mmm or =<Query Expression>" << endl;
	       goto newline;
	    }
	  if (strncmp(Selection, "range", 5) == 0)
	    {
	      char *tcp = &Selection[5];
	      while (isspace(*tcp)) tcp++;
	      if (*tcp == 'a')
		{
		  first = 1;
		  last  = 0;
	        }
	      else if (!isdigit(*tcp))
		{
		  cout << endl << "Bad range specification: range number-number or range a[ll]!";
		  goto newline;
		}
	      else
		sscanf(tcp, "%d-%d", &first, &last);
	      goto again;
	    }
	  else if (strncmp(Selection, "unset", 5) == 0)
	    {
	      char *tcp = &Selection[5];
              while (isspace(*tcp)) tcp++;
              if (strncasecmp(tcp, "ht", 2) == 0)
                PresentHtml = false;
	      else if (strncasecmp(tcp, "dat", 3) == 0)
		DateFlag = false;
              else if (strncasecmp(tcp, "xml", 2) == 0)
                ShowXML = PresentHtml = false;
              else if (strncasecmp(tcp, "sho", 3) == 0)
                ShowHitContent = false;
	      else if (strncasecmp(tcp, "anc", 3) == 0)
		AncestorElementList.Clear(); // Clear List
	      else if (strncasecmp(tcp, "pres", 4) == 0)
		ElementSet = BRIEF_MAGIC; // reset to Brief 
              else
                cout << endl << "Only unset Ancestor, Present, Date, XML, HTML or Show supported!";
	      strcpy(Selection, "set");
	    }
	  if (strncmp(Selection, "set", 3) == 0)
	    {
	      char *tcp = &Selection[3];
	      while (isspace(*tcp)) tcp++;
	      if (*tcp == '\0')
		{
		  cout << "Options Set:";
		  if (InfixQuery)    cout << " Infix";
		  if (RpnQuery)      cout << " RPN";
		  if (PresentHtml)   cout << " HTML";
		  if (ShowXML)       cout << " XML";
		  if (ShowHitContent)cout << " SHOW";
		  if (DateFlag)      cout << " DATE";
		  if (!ElementSet.IsEmpty() && ElementSet != BRIEF_MAGIC)  cout << " Present:{" << ElementSet << "}"; 
		  if (!AncestorElementList.IsEmpty()) cout << " Ancestors:{" << AncestorElementList << "}";
		}
	      else if (strncasecmp(tcp, "dat", 3) == 0)
		DateFlag = true;
	      else if (strncasecmp(tcp, "inf", 3) == 0)
		InfixQuery = 1, RpnQuery = 0;
	      else if (strncasecmp(tcp, "rpn", 3) == 0) 
		RpnQuery = 1, InfixQuery = 0;
	      else if (strncasecmp(tcp, "wor", 3) == 0)
		RpnQuery = InfixQuery = 0;
	      else if (strncasecmp(tcp, "ht", 2) == 0)
		PresentHtml = true;
	      else if (strncasecmp(tcp, "su", 2) == 0)
		PresentHtml = false;
	      else if (strncasecmp(tcp, "xm", 2) == 0)
		{
		  if (!ShowXML) cout << "<!-- ";
		  ShowXML = PresentHtml = 1; 
		}
	      else if (strncasecmp(tcp, "anc", 3) == 0 || strncasecmp(tcp, "pres", 3) == 0)
	      {
		  char *tp = tcp + 3;
		  // skip rest of option....
	          while (*tp && !isspace(*tp)) tp++;
		  while (*tp && isspace(*tp)) tp++;
		  // Now pointing to value
		  if (*tcp == 'p' || *tcp == 'P')
		     ElementSet = tp;
		  else if (*tp) 
		     AncestorElementList.AddEntry(tp); 
	      }
	      else if (strncasecmp(tcp, "cac", 3) == 0)
		{
		  char *tp = tcp + 3;
		  while (*tp && !isspace(*tp)) tp++;
		  while (*tp && isspace(*tp)) tp++;
		  if (*tp == '\0')
		    cout << "No cache size set?";
		  else
		    pdb->SetDbSearchCacheSize(atol(tp));
		}
	      else if (strncasecmp(tcp, "nos", 3) == 0)
		ShowHitContent = false;
	      else if (strncasecmp(tcp, "sho", 3) == 0)
		ShowHitContent = true;
	      else
		cout << endl << "Only set Ancestor <value>, Present [<element>], date, Infix, RPN, Words, XML, HTML, SUTRS, cachesize nnn, Show, Noshow supported!";
	      goto newline;
	    }
          else if (*Selection == '=')
            {
              delete prset;
              prset = NULL;
              QueryString = Selection+1;
              QueryString.Trim();
	      first = 1;
	      last = 0;
              goto again;
            }
	  FileNum = atoi (Selection);
	}
      if ((FileNum > n) || (FileNum < 0))
	{
	  cout << endl << "Select a number between 1 and " << n << '.' << endl;
	}
      if ((FileNum != 0) && (FileNum <= n) && (FileNum >= 1))
	{
	  prset->GetEntry (FileNum, &result);

	  STRING Buf;
	  STRING Full;
	  char *tcp = strchr (Selection, ',');
	  if (tcp) {
	    // @element but not @element/sub-element
	    if (*++tcp == '@' && strchr(tcp, '/') == NULL) {
	      Full = ++tcp;
	      Full.Cat("/") ;
	    }
	    Full.Cat(tcp);
	  } else if ((tcp = strchr(Selection, '@')) != NULL) { 
	    Full = ++tcp;
	    if (strchr(tcp, '/') == NULL) {
	      Full.Cat ("/");
	      Full.Cat (tcp);
	    }
	  } else if (Pager && isatty(fileno(stdout)))
	    Full = "h";
	  else
	    Full = "F";

	  int pos;
	  if ((pos = Full.Search("/")) > 0)
	    {
	      STRLIST list;
	      STRING  content;
	      STRING  fld = Full.c_str() + pos;
              int     count = pdb->GetAncestorContent(result, Full, &list);
	      Buf.Clear();
              for (int i=1; i<= count; i++)
                 {
                      if (ShowXML) Buf << "<" << fld << ">";
                      list.GetEntry(i, &content);
                      Buf << content;
                      if (ShowXML) Buf << "</" << fld << ">";
		      Buf << "\n";
                 } 
	    }
	  else if (TermPrefix.IsEmpty() && TermSuffix.IsEmpty())
	    {
	      if (Full == "h")
		pdb->HighlightedRecord(result, Before, After, &Buf);
	      else
		pdb->DocPresent (result, Full, RecordSyntax, &Buf);
	    }
	  else
	    {
	      pdb->HighlightedRecord (result, TermPrefix, TermSuffix, &Buf);
	    }
	  if (ShowXML)
	    cout << "-->";
	  if (Pager && isatty(fileno(stdout)))
	    {
	      FILE *Fp = _IB_popen(Pager, "w");
	      if (Fp)
		{
		  Buf.Print(Fp);
		  _IB_pclose(Fp, 1);
		}
	      else
		Buf.Print();
	    }
	  else
	    Buf.Print ();
	  if (ShowXML)
	    cout << "<!-- "; 
	  cout << "Enter <Return> to select another file: ";
	  cout.flush();
#ifdef LINUX
	  {STRING s;
          if (s.FGet(stdin, sizeof(Selection)-1) == true)
            strcpy(Selection, s.c_str());
          else
            Selection[0] = '\0'; }
#else
	  cin.getline (s, 5, '\n');
#endif
	  if (ShowXML)
	    cout << "-->";
	  cout << endl;
	}
	       else if (ShowXML) cout << "-->" << endl;
	    }
	  while (FileNum != 0 || ShellFlag);
	  delete prset;

	  // #################################################################################
	#if 0 /* HOUSEKEEPING CHECK 2008 March */
	  cerr << "Still " << result.GetHitTotal() << " FCLISTs in use" << endl;
	#endif

	  delete pdb;

	#if SHOW_RUSAGE
	  if (ShowRusage)
	    {
	  struct rusage rusage;
	  int    ru = RUSAGE_SELF;
	rusage:
	  if (getrusage(ru, &rusage) == 0)
	    {
	      long double cpu_time = rusage.ru_utime.tv_sec +
			rusage.ru_utime.tv_usec/1000000.0 +
			rusage.ru_stime.tv_sec + rusage.ru_stime.tv_usec/1000000.0;
	      long ticks = sysconf(_SC_CLK_TCK) ;
	      if (rusage.ru_utime.tv_usec || cpu_time ||  rusage.ru_maxrss) {
	      cerr << endl << endl << ((ru == RUSAGE_SELF) ? "Main" : "Subprocess" ) << " Job Statistics:" << endl <<
		// tv_sec // tv_usec;   
		"CPU time:               " << cpu_time << " seconds" << endl <<
		"    User time:          " << (rusage.ru_utime.tv_sec + 
			rusage.ru_utime.tv_usec/1000000.0) << " seconds" << endl <<
		"    System time:        " << (rusage.ru_stime.tv_sec + 
			rusage.ru_stime.tv_usec/1000000.0)  << " seconds" << endl <<
		"Max resident size:      " << MaxResidentKiB(rusage) << "k" << endl <<
		"Shared text memory:     " << (rusage.ru_ixrss)/ticks << "k" << endl <<
		"Unshared data:          " << (rusage.ru_idrss)/ticks << "k" << endl <<
		"Unshared stack:         " << (rusage.ru_isrss)/ticks << "k" << endl <<
		"Page reclaims:          " << rusage.ru_minflt << endl <<
		"       faults:          " << rusage.ru_majflt << endl <<
		"Swaps:                  " << rusage.ru_nswap << endl <<
		"File system in events:  " << rusage.ru_inblock << endl <<
		"           out event:   " << rusage.ru_oublock << endl <<
		"Context switches Vol.:  " << rusage.ru_nvcsw << endl <<
		"               Invol.:  " << rusage.ru_nivcsw << endl;
		cerr << endl;
	       }
	       if (ru == RUSAGE_SELF)
		{
		  ru = RUSAGE_CHILDREN;
		  goto rusage;
		}
	     };
	    }
	#endif
	  return 0;
}

namespace
{
  struct CLI_OPTION_DOC
  {
    const char *Group;
    const char *Option;
    const char *Argument;
    const char *Description;
  };

  static const CLI_OPTION_DOC CliOptions[] =
  {
    /*
     * Database and environment
     */
    {
      "database",
      "-d",
      "database",
      "Search the database having the specified root name."
    },
    {
      "database",
      "-fuel",
      "percent",
      "Set the available space fuel as a percentage."
    },
    {
      "database",
      "-cd",
      "directory",
      "Change the working directory before opening the database."
    },
    {
      "database",
      "-id",
      "document-id",
      "Request documents having the specified document identifier."
    },
    {
      "database",
      "-D",
      "file",
      "Load a result set from the specified file."
    },

    /*
     * Record presentation
     */
    {
      "presentation",
      "-p",
      "element-set",
      "Present the specified element set as the identifier with each result."
    },
    {
      "presentation",
      "-P",
      "ancestor|ancestor/descendant",
      "Present ancestor content for hits. May be specified repeatedly. "
      "The ancestor/descendant form selects a descendant within the named ancestor."
    },
    {
      "presentation",
      "-show",
      NULL,
      "Show the best hit neighborhood."
    },
    {
      "presentation",
      "-summary",
      NULL,
      "Show the record summary or description."
    },
    {
      "presentation",
      "-XML",
      NULL,
      "Present results using an XML-like structure."
    },
    {
      "presentation",
      "-Json",
      NULL,
      "Present search results using JSON."
    },
    {
      "presentation",
      "-H[TML]",
      NULL,
      "Use HTML record presentation."
    },
    {
      "presentation",
      "-q[uiet]",
      NULL,
      "Print results and exit immediately."
    },
    {
      "presentation",
      "-t[erse]",
      NULL,
      "Print terse result records."
    },
    {
      "presentation",
      "-tab",
      NULL,
      "Use tab-delimited terse output."
    },
    {
      "presentation",
      "-prefix",
      "text",
      "Add the specified prefix to matched terms in presented documents."
    },
    {
      "presentation",
      "-suffix",
      "text",
      "Add the specified suffix to matched terms in presented documents."
    },
    {
      "presentation",
      "-headline",
      "element",
      "Use an alternative headline display based on the specified element."
    },
    {
      "presentation",
      "-filename",
      NULL,
      "Display filenames only."
    },
    {
      "presentation",
      "-filesystem",
      NULL,
      "Equivalent to -q -filename -byterange."
    },

    /*
     * Result ordering
     */
    {
      "sorting",
      "-c",
      NULL,
      "Sort results chronologically."
    },
    {
      "sorting",
      "-cr",
      NULL,
      "Sort results chronologically from oldest to newest."
    },
    {
      "sorting",
      "-s",
      NULL,
      "Sort results by relevance score."
    },
    {
      "sorting",
      "-sc",
      NULL,
      "Sort results by score modified by category."
    },
    {
      "sorting",
      "-smag",
      "factor",
      "Sort by score and category using the specified magnetism factor."
    },
    {
      "sorting",
      "-scat",
      NULL,
      "Sort results by category."
    },
    {
      "sorting",
      "-snews",
      NULL,
      "Sort results by news rank."
    },
    {
      "sorting",
      "-h",
      NULL,
      "Sort results by the number of different matching terms. See -joint."
    },
    {
      "sorting",
      "-k",
      NULL,
      "Sort results by record key."
    },
    {
      "sorting",
      "-n",
      NULL,
      "Do not sort results; retain indexing order."
    },
    {
      "sorting",
      "-sort",
      "B[entley]|S[edgewick]|D[ualPivot]|T[im]|N[ative]",
      "Select the sorting implementation."
    },

    /*
     * Score normalization
     */
    {
      "normalization",
      "-AF_norm | -norm=AF",
      NULL,
      "Use AF normalization."
    },
    {
      "normalization",
      "-bytes_norm | -norm=bytes",
      NULL,
      "Use byte-count normalization."
    },
    {
      "normalization",
      "-euclidean_norm | -norm=E1",
      NULL,
      "Use Euclidean normalization."
    },
  {          
      "normalization",
      "-E2_norm | -norm=E2", 
      NULL, 
      "Use E2 normalization (pairwise term coherence and collective span). "
      "Hyperparameters (CoverageFloor, ProximityGain) set in [E2] of datebase ini"
    },    
    {
      "normalization",
      "-L1_norm | -norm=L1",
      NULL,
      "Use cosine L1 normalization."
    },
    {
      "normalization",
      "-L2_norm | -norm=L2",
      NULL,
      "Use cosine L2 normalization.."
    },
   {         
      "normalization",
      "-S2_norm | -norm=S2", 
      NULL, 
      "Use cosine S2 normalization. Similar to L2 but with saturation.."
    },  
   {          
      "normalization",
      "-BM25_norm | -norm=BM25", 
      NULL, 
      "Use BM25. Hyperparameters (K1,B,A,Regency,Pivot) set in [BM25] of datebase ini"
    },  
    {
      "normalization",
      "-log_norm | -norm=log",
      NULL,
      "Use logarithmic normalization."
    },
    {
      "normalization",
      "-max_norm | -norm=max",
      NULL,
      "Use maximum-score normalization."
    },
    {
      "normalization",
      "-no_norm",
      NULL,
      "Do not calculate or normalize scores."
    },

    /*
     * Query interpretation
     */
    {
      "query",
      "-scan",
      "field",
      "Use the field scan service."
    },
    {
      "query",
      "-shell",
      NULL,
      "Enter interactive search mode."
    },
    {
      "query",
      "-rpn",
      NULL,
      "Interpret the query using Reverse Polish Notation."
    },
    {
      "query",
      "-infix",
      NULL,
      "Interpret the query using conventional infix notation. "
      "Additional forms include ! for NOT and field/ for WITHIN:field."
    },
    {
      "query",
      "-words",
      NULL,
      "Interpret the remaining arguments as words."
    },
    {
      "query",
      "-and",
      NULL,
      "Interpret the remaining words as an intersection."
    },
    {
      "query",
      "-smart",
      "field",
      "Perform a fielded smart search."
    },
    {
      "query",
      "-regular",
      NULL,
      "Use a regular query supporting fields and weights but no operators."
    },
    {
      "query",
      "-syn",
      NULL,
      "Perform synonym expansion."
    },

    /*
     * Scoring and result reduction
     */
    {
      "scoring",
      "-priority",
      "factor",
      "Override the priority factor."
    },
    {
      "scoring",
      "-scale",
      "maximum",
      "Normalize scores into the range zero through the specified maximum."
    },
    {
      "scoring",
      "-top",
      "count",
      "Return at most the specified number of results."
    },
    {
      "scoring",
      "-negative",
      NULL,
      "Include results having negative scores."
    },
    {
      "scoring",
      "-positive",
      NULL,
      "Include only results having positive scores."
    },
    {
      "scoring",
      "-clip",
      "count",
      "Clip the result set at the specified count."
    },
    {
      "scoring",
      "-common",
      "threshold",
      "Set the common-word threshold."
    },
    {
      "scoring",
      "-reduce",
      NULL,
      "Reduce the result set using the minimum number of different matches."
    },
    {
      "scoring",
      "-reduce0",
      NULL,
      "Equivalent to -h -reduce."
    },
    {
      "scoring",
      "-drop_h",
      "count",
      "Drop results having fewer than the specified number of different matches."
    },
    {
      "scoring",
      "-drop_a",
      "score",
      "Drop results whose absolute score is below the specified value."
    },
    {
      "scoring",
      "-drop_s",
      "score",
      "Drop results whose scaled score is below the specified value."
    },

    /*
     * Result metadata
     */
    {
      "metadata",
      "-hits",
      NULL,
      "Display the total number of matching hits for each record."
    },
    {
      "metadata",
      "-joint",
      NULL,
      "Display the number of different matching terms for each record."
    },
    {
      "metadata",
      "-score",
      NULL,
      "Display unnormalized scores."
    },
   { 
      "metadata",
      "-rating",
      NULL,
      "Scale scores over the retrieved set and display as 1-5 star ratings."
    },
    {
      "metadata",
      "-date",
      NULL,
      "Display the record date."
    },
    {
      "metadata",
      "-datemodified",
      NULL,
      "Display the record modification date."
    },
    {
      "metadata",
      "-key",
      NULL,
      "Display the record key."
    },
    {
      "metadata",
      "-doctype",
      NULL,
      "Display the record document type."
    },
    {
      "metadata",
      "-byterange",
      NULL,
      "Display the byte range occupied by each document within its source file."
    },

    /*
     * Result ranges and date restrictions
     */
    {
      "range",
      "-range",
      "first[-last] | all",
      "Display results from the first position through the optional last position (or all)."
    },
    {
      "range",
      "-daterange",
      "date-range",
      "Restrict all searches to records whose record dates fall within the "
      "specified range."
    },
    {
      "range",
      "-startdoc",
      "position",
      "Start displaying the result set at the specified document position."
    },
    {
      "range",
      "-enddoc",
      "position",
      "Stop displaying the result set at the specified document position."
    },

    /*
     * Storage and document-type options
     */
    {
      "storage",
      "-o",
      "option",
      "Specify a document-type-specific option."
    },
    {
      "storage",
      "-save",
      "file",
      "Save the result set into the specified file."
    },
    {         
      "storage",
      "-load",
      "file", 
      "Load a result set from the specified file."
    },   

    /*
     * Diagnostics and process control
     */
    {
      "diagnostics",
      "-level",
      "0-255",
      "Set the message level."
    },
    {
      "diagnostics",
      "-debug",
      NULL,
      "Enable extensive debugging messages."
    },
    {
      "diagnostics",
      "-bench",
      NULL,
      "Display process resource usage."
    },
    {
      "diagnostics",
      "-pager",
      "program",
      "Use the specified program to page results."
    },
    {
      "diagnostics",
      "-more",
      NULL,
      "Equivalent to -pager /bin/more."
    },
    {
      "diagnostics",
      "-copyright",
      NULL,
      "Display the copyright statement."
    },
    {
      "diagnostics",
      "-help[=json|txt]",
      NULL,
      "Display command-line help, optionally as JSON."
    },
    {
      "diagnostics",
      "-qhelp[=json|txt]",
      NULL,
      "Display available query operators, optionally as JSON."
    },
    {
      "diagnostics",
      "-ohelp",
      NULL,   
      "Display ini options.."
    }
  };


  struct TERM_MODIFIER_DOC
  {
    const char *Form;
    const char *Description;
  };

  static const TERM_MODIFIER_DOC TermModifiers[] =
  {
    {
      "fieldname/searchterm",
      "Search for the term within the specified field."
    },
    {
      "searchterm*",
      "Perform right truncation."
    },
    {
      "*searchterm",
      "Perform left truncation. (Limited to indexedSIS block length)"
    },
    {
      "* and ?",
      "Use glob-pattern matching."
    },
    {
      "searchterm~",
      "Perform fuzzy Ratcliff matching."
    },
    {
      "searchterm#",
      "Perform phonetic Soundex matching."
    },
    {
      "searchterm=",
      "Perform exact case-dependent matching."
    },
    {
      "searchterm>",
      "Perform exact right-truncated matching; equivalent to =*."
    },
    {
      "searchterm.",
      "Perform exact-term matching so that, for example, auto does not "
      "match auto-mobile."
    },
    {
      "searchterm$",
      "Interpret the term as a freeform encoded address using a sparse vector."
    },
    {
      "searchterm@",
      "Interpret the term as an embeddings address using a dense vector."
    },
    {
      "searchterm:weight",
      "Apply a term weight. The default weight is 1; negative values lower rank."
    },
    {
      "\"literal phrase\"",
      "Perform a literal phrase search."
    }
  };


  static void WriteCliOptionJSON(
      std::ostream& out,
      const CLI_OPTION_DOC& option,
      const char *indent)
  {
    out << indent << "{\n";

    out << indent << "  \"group\": ";
    WriteJsonString(out, option.Group);
    out << ",\n";

    out << indent << "  \"option\": ";
    WriteJsonString(out, option.Option);
    out << ",\n";

    out << indent << "  \"argument\": ";

    if (option.Argument)
      WriteJsonString(out, option.Argument);
    else
      out << "null";

    out << ",\n";

    out << indent << "  \"description\": ";
    WriteJsonString(out, option.Description);
    out << "\n";

    out << indent << "}";
  }
}


static void HelpUsageJSON(const char *progname)
{
  const char *prog =
    progname && *progname ? progname : "Isearch";

  const std::string usage =
    std::string(prog) + " -d <database> [options] term...";

  const size_t optionCount =
    sizeof(CliOptions) / sizeof(CliOptions[0]);

  const size_t modifierCount =
    sizeof(TermModifiers) / sizeof(TermModifiers[0]);

  cout << "{\n";

  cout << "  \"program\": ";
  WriteJsonString(cout, prog);
  cout << ",\n";

  cout << "  \"usage\": ";
  WriteJsonString(cout, usage.c_str());
  cout << ",\n";

  /*
   * Command-line options
   */
  cout << "  \"options\": [\n";

  for (size_t i = 0; i < optionCount; ++i)
    {
      WriteCliOptionJSON(cout, CliOptions[i], "    ");

      if (i + 1 < optionCount)
        cout << ',';

      cout << '\n';
    }

  cout << "  ],\n";

  /*
   * Positional query argument
   */
  cout << "  \"arguments\": [\n";
  cout << "    {\n";
  cout << "      \"name\": \"terms\",\n";
  cout << "      \"syntax\": \"term...\",\n";
  cout << "      \"required\": true,\n";
  cout << "      \"description\": "
          "\"Terms, operands, or a complete query expression.\"\n";
  cout << "    }\n";
  cout << "  ],\n";

  /*
   * Query interpretation
   */
  cout << "  \"query_interpretation\": {\n";

  cout << "    \"default\": "
          "\"Smart search when no explicit query mode is selected.\",\n";

  cout << "    \"rpn\": "
          "\"With -rpn, the query expression must use Reverse Polish "
          "Notation.\",\n";

  cout << "    \"infix\": "
          "\"With -infix, the query expression uses conventional infix "
          "notation.\",\n";

  cout << "    \"words\": "
          "\"With -words or -regular, terms are combined using OR unless "
          "another mode changes that behavior.\"\n";

  cout << "  },\n";

  /*
   * Fielded query syntax
   */
  cout << "  \"fielded_search\": {\n";

  cout << "    \"syntax\": ";
  WriteJsonString(
      cout,
      "[[fieldname][relation]]searchterm[*][:weight]");
  cout << ",\n";

  cout << "    \"relations\": ["
          "\"<\", \">\", \">=\", \"<=\", \"<>\"],\n";

  cout << "    \"relation_semantics\": "
          "\"The meaning of each relation depends on the field datatype.\",\n";

  cout << "    \"field_prefix_syntax\": "
          "\"fieldname/searchterm\",\n";

  cout << "    \"term_modifiers\": [\n";

  for (size_t i = 0; i < modifierCount; ++i)
    {
      cout << "      {\n";

      cout << "        \"form\": ";
      WriteJsonString(cout, TermModifiers[i].Form);
      cout << ",\n";

      cout << "        \"description\": ";
      WriteJsonString(cout, TermModifiers[i].Description);
      cout << "\n";

      cout << "      }";

      if (i + 1 < modifierCount)
        cout << ',';

      cout << '\n';
    }

  cout << "    ]\n";
  cout << "  },\n";

  /*
   * Date-range syntax
   */
  cout << "  \"date_ranges\": {\n";

  cout << "    \"syntax\": [\n";
  cout << "      \"YYYY[MM[DD]][-YYYY[MM[DD]]]\",\n";
  cout << "      \"YYYY[MM[DD]]/[[YYYY]MM]DD\",\n";
  cout << "      \"ISO 8601 and other recognized date formats\"\n";
  cout << "    ],\n";

  cout << "    \"example\": "
          "\"2005 selects all records dated during the year 2005.\",\n";

  cout << "    \"record_date_note\": "
          "\"The -daterange option uses the record date rather than an "
          "ordinary indexed date field.\",\n";

  cout << "    \"scope_note\": "
          "\"-daterange restricts all searches, while the unary "
          "WITHIN:<daterange> operator applies only to its operand set.\"\n";

  cout << "  },\n";

  /*
   * Reserved syntax
   */
  cout << "  \"reserved_syntax\": [\n";
  cout << "    {\n";
  cout << "      \"prefix\": \"RECT\",\n";
  cout << "      \"syntax\": \"RECT{N,W,S,E}\",\n";
  cout << "      \"description\": "
          "\"Reserved for bounding-box searches in predefined numeric "
          "quadrant fields.\"\n";
  cout << "    }\n";
  cout << "  ]\n";

  cout << "}\n";
}


#if 1

static std::string OptionSyntax(const CLI_OPTION_DOC& doc)
{
  std::string text = doc.Option ? doc.Option : "";

  if (doc.Argument && *doc.Argument)
    {
      text += " ";
      text += doc.Argument;
    }

  return text;
}


static void WriteWrappedText(
    std::ostream& out,
    const std::string& text,
    size_t firstIndent,
    size_t continuationIndent,
    size_t width = 100)
{
  std::istringstream input(text);
  std::string word;
  size_t column = firstIndent;
  bool first = true;

  while (input >> word)
    {
      const size_t separator = first ? 0 : 1;

      if (!first && column + separator + word.size() > width)
        {
          out << '\n'
              << std::string(continuationIndent, ' ');

          column = continuationIndent;
          separator ? void() : void();
        }
      else if (!first)
        {
          out << ' ';
          ++column;
        }

      out << word;
      column += word.size();
      first = false;
    }

  out << '\n';
}


static void HelpUsageText( const char *progname, std::ostream& out)
{
  const char *prog =
      progname && *progname ? progname : "Isearch";

  const size_t optionCount =
      sizeof(CliOptions) / sizeof(CliOptions[0]);

  const size_t optionColumnWidth = 25;
  const size_t descriptionColumn = 2 + optionColumnWidth + 2;

  out << prog << " -d db [options] term...\n"
      << "options:\n";

  const char *currentGroup = NULL;

  for (size_t i = 0; i < optionCount; ++i)
    {
      const CLI_OPTION_DOC& doc = CliOptions[i];

      if (!currentGroup ||
          std::strcmp(currentGroup, doc.Group) != 0)
        {
          currentGroup = doc.Group;

          out << '\n'
              << currentGroup
              << ":\n";
        }

      const std::string syntax = OptionSyntax(doc);

      out << "  "
          << std::left
          << std::setw(optionColumnWidth)
          << syntax
          << std::right
          << "  ";

      WriteWrappedText(
          out,
          doc.Description ? doc.Description : "",
          descriptionColumn,
          descriptionColumn);
    }

  out << "\n"
      << "terms:\n"
      << "  Terms, operands, or a complete query expression.\n"
      << "  With -rpn, the expression must use Reverse Polish Notation.\n"
      << "  With -infix, conventional infix notation is expected.\n"
      << "  With -words or -regular, terms are combined using OR.\n"
      << "  The default query mode is Smart search.\n"
      << "\n"
      << "fielded search:\n"
      << "  [[fieldname][relation]]searchterm[*][:weight]\n"
      << "  Relations are <, >, >=, <= and <>. Their semantics depend\n"
      << "  upon the field datatype.\n"
      << "\n"
      << "term modifiers:\n";

  const size_t modifierCount =
      sizeof(TermModifiers) / sizeof(TermModifiers[0]);

  for (size_t i = 0; i < modifierCount; ++i)
    {
      out << "  "
          << std::left
          << std::setw(optionColumnWidth)
          << TermModifiers[i].Form
          << std::right
          << "  ";

      WriteWrappedText(
          out,
          TermModifiers[i].Description,
          descriptionColumn,
          descriptionColumn);
    }

  out << "\n"
      << "Special in-term characters: ";
       for (int i = 10; i < 127; i++) {
	 if (_ib_isdot((char)i )) out << (char)i;
       };
   out  << "\n   These may appear inside words (tokens) to be considered term characters. see ctype.c\n\n"
      << "date ranges:\n"
      << "  YYYY[MM[DD]][-YYYY[MM[DD]]]\n"
      << "  YYYY[MM[DD]]/[[YYYY]MM]DD\n"
      << "  ISO 8601 and other recognized date formats are accepted.\n"
      << "  Example: 2005 selects every record dated during 2005.\n"
      << "  -daterange restricts all searches; WITHIN:<daterange>\n"
      << "  restricts only its operand set.\n"
      << "\n"
      << "reserved syntax:\n"
      << "  RECT{N,W,S,E} is reserved for bounding-box searches in\n"
      << "  predefined numeric quadrant fields.\n";

   out << endl <<
        "Examples: " << prog << " -d POETRY truth 'beaut*' urn:2\n" << 
        "          " << prog << " -d FTP -headline L C++\n" <<
        "          " << prog << " -d WEBPAGES title/library\n" << 
        "          " << prog << " -d WEBPAGES  -rpn \"Harvard library\" WITHIN:title\n" << 
        "          " << prog << " -d STORIES -rpn title/cat title/dog OR title/mouse OR\n" << 
        "          " << prog << " -d STORIES -infix title/(cat or dog or mouse)\n" << 
        "          " << prog << " -d POETRY -rpn speaker/hamlet line/love AND:scene\n" << 
        "          " << prog << " -d POETRY -infix (speaker/hamlet and:scene line/love)\n" << 
        "          " << prog << " -d POETRY -infix act/(speaker/hamlet and:scene line/love)\n" << 
        "          " << prog << " -d MAIL -H -infix from/edz AND subject/CoreQuarry=\n" << 
        "          " << prog << " -d BILLS -rpn vendor/NONMONOTONIC price<100 AND\n" << 
	"          " << prog << " -d NEWS -rpn AMD GPU NVIDIA MAYBE PROMOTE\n" << 
        "          " << prog << " -d NEWS  -rpn xLSTM WITHIN:2026\n" << 
        "          " << prog << " -d SHAKESPEARE -P SPEECH/SPEAKER -P speech/line  -rpn out spot PEER" << endl <<
        "Note: \"Built-in\" Elements for -p and -headline: F for Full, B for Brief and S for Short.\n" << endl <<
        "Additional \"Special\" elements: R for Raw, H for Highlight/Hits; and if they exist," << endl <<
        "L for location/redirect and M for metadata." << endl << endl <<
        "In the response one can select not just the record but also all elements of a specific field as" << endl <<
        "well as specific contents where the hit occurs (similar to -P). Example:  1,speech/speech" << endl <<
        "to select the contents of a speech where the hit(s) in record #1 occurs." << endl <<
        "Shorthand for element/element is @element, so 1,@line or even 1@line is really 1,line/line" << endl <<
        "By contrast 1,speech returns ALL the speeches in record #1." << endl <<
        "The above 'special' elements may also be specified, e.g. 1,H" << endl << endl;
}



#endif
