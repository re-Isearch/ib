/*-@@@
File:           jsondoc.cxx
Version:        1.02
Description:    Class JSONDOC - JSON Document Type
Author:         Based on COLONDOC by Edward C. Zimmermann
Copyright:      Copyright (c) 2026 re-Isearch Project
                Licensed under the Apache 2.0 license

Core principle
==============
re-Isearch is coordinate-based.  We do NOT store field values in the
index.  For every JSON leaf value we record:

   fieldname  →  FC { start, end }

where start/end are absolute byte offsets into the original ingested
file pointing at the value content (for strings: the bytes INSIDE the
quotes; for primitives: the literal bytes).  The engine fetches the
actual text directly from the file at retrieval time, exactly as
COLONDOC does for the text following a colon tag.
@@@-*/

#include "jsondoc.hxx"
#include "common.hxx"
#include "doc_conf.hxx"


// ---------------------------------------------------------------------------
// Detector (Determine the flavour of JSON, e.g. which Doctype to use)
// --------------------------------------------------------------------------

enum class JsonFlavor { JSON, JSONL, JSONLD, EJSON, ESBULK, CIRRUS, UNKNOWN };


static const char *JsonClass( JsonFlavor flavor)
{
  switch (flavor) {
    case JsonFlavor::JSONL:   return "NDJSON";
    case JsonFlavor::JSONLD:  return "JSON-LD";
    case JsonFlavor::EJSON:   return "EJSON";
    case JsonFlavor::JSON:    return "JSON";
    case JsonFlavor::CIRRUS:  return "CIRRUSNDJSON";
    case JsonFlavor::ESBULK:  return "ESBULKNDJSON";
    case JsonFlavor::UNKNOWN: return "UNKNOWN";
  }
}

// Right now either plain newline JSON or the ES Bulk format
// (to be extended as needed)
bool inline _IsBulkActionLine(const char *buf, size_t len)
{
  size_t i =0;
  while (i < len && (buf[i] == ' ' || buf[i] == '\t')) ++i;
  if (i >= len || buf[i] != '{') return false;
  ++i;
  while (i < len && (buf[i] == ' ' || buf[i] == '\t')) ++i;
  if (i >= len || buf[i] != '"') return false;
  ++i;
  size_t keyStart = i;
  while (i < len && buf[i] != '"') ++i;
  if (i >= len) return false;
  const size_t keyLen = i - keyStart;
  
  static const char *actions[] = { "index", "create", "update", "delete" };
  for (size_t a = 0; a < sizeof(actions)/sizeof(actions[0]); ++a)
    {
      const size_t alen = strlen(actions[a]);
      if (alen == keyLen && memcmp(buf + keyStart, actions[a], alen) == 0)
        return true;
    }
  return false;
}

inline bool _IsBulkActionLine(const STRING& content)
{   
  const char *buf = content.c_str();
  const size_t len = content.Len();
  return _IsBulkActionLine(buf, len);
}


inline JsonFlavor classify_nl_buffer(const STRING& content) {
    // Look at first line structure
    if (content[0] == '{' && content.find("wikitext"))
        return JsonFlavor::CIRRUS;
     if (_IsBulkActionLine (content.c_str(), content.Len()))
        return JsonFlavor::ESBULK;
    return JsonFlavor::JSONL; 
}

static JsonFlavor identify_flavor(const STRING& content) {
    if (content.empty()) return JsonFlavor::UNKNOWN;
    const size_t len = content.size();

    // 1. Check for JSONL (Multiple JSON objects separated by newlines)
    // Heuristic: First line ends with '}' and second line starts with '{'
    size_t first_newline = content.Find('\n');
    if (first_newline && first_newline < len ) {
        unsigned next_char = content.find_first_not_of(" \t\n\r", first_newline);
        if (next_char < len  && content[next_char] == '{') {
	    return classify_nl_buffer(content);
        }
    }

    // 2. Check for JSON-LD (Look for reserved @context keyword)
    if (content.Find("\"@context\"") < len) {
        return JsonFlavor::JSONLD;
    }

    // 3. Check for EJSON (Look for MongoDB-style $ keys like $date or $oid)
    if (content.Find("\"$") < len) {
        return JsonFlavor::EJSON;
    }

    // 4. Fallback to standard JSON
    const CHR ch  = content.GetChr(content.FirstNonWhiteSpace()); // First non-white character
    if (ch == '{' || ch == '[') {
        return JsonFlavor::JSON;
    }

    return JsonFlavor::UNKNOWN;
}

static JsonFlavor detectJSON(FILE *fp, off_t Start = 0)
{
  char buffer[1024];
  if (Start && ::fseek(fp, Start, 0) == -1)
    return JsonFlavor::UNKNOWN;
  if (::fread(buffer, sizeof(char), sizeof(buffer) - 1, fp) <= 0)
    return JsonFlavor::UNKNOWN;
  return identify_flavor(buffer);
}
const char *JSONDETECT::Description(PSTRLIST List) const
{
  if (List) {
    const STRING ThisDoctype("JSONDETECT");
    if (Doctype != ThisDoctype && List->IsEmpty())
      List->AddEntry(Doctype);
    List->AddEntry(ThisDoctype);
    DOCTYPE::Description(List);
  }
  return "JSON Document Type detector -- determines which JSON class to use";
}
void JSONDETECT::ParseRecords(const RECORD& FileRecord)
{
  STRING fn(FileRecord.GetFullFileName());
  RECORD Record(FileRecord); // copy so we can adjust start/end
  PFILE fp = Db->ffopen(fn, "rb");
  auto typ = detectJSON(fp);
  Db->ffclose(fp);
  STRING handler = JsonClass(typ);
  message_log (LOG_INFO, "Handling '%s' as %s", fn.c_str(), handler.c_str());
  Record.SetDocumentType (handler);
   Db->ParseRecords (Record);
}


// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

JSONDOC::JSONDOC(PIDBOBJ DbParent, const STRING& Name)
  : COLONDOC(DbParent, Name),
    m_PathSep(JSON_PATH_SEP),
    m_IndexArrayElements(true),
    m_AutoFieldTypes(true)
{
  STRING opt( Getoption("PathSep") );
  if (opt.GetLength() == 1)
    m_PathSep = opt.GetChr(1);

  m_IndexArrayElements = Getoption("IndexArrayElements", "true").GetBool();
  m_AutoFieldTypes     = Getoption("AutodetectFieldtypes", "Y").GetBool();
}

JSONDOC::~JSONDOC() {}

// ---------------------------------------------------------------------------
// Description / MIME
// ---------------------------------------------------------------------------

const char *JSONDOC::Description(PSTRLIST List) const
{
  if (List) {
    const STRING ThisDoctype("JSON");
    if (Doctype != ThisDoctype && List->IsEmpty())
      List->AddEntry(Doctype);
    List->AddEntry(ThisDoctype);
    COLONDOC::Description(List);
  }
  return "JSON Document Type – indexes key/value pairs by field coordinates;\n\
Limited to \"pure JSON\" (RFC 8259). Nested keys are joined with path-separator.\n\
Indexing options (defined in .ini):\n\
  [General]\n\
  AutodetectFieldtypes=True|False // Guess fieldtypes\n\
  IndexArrayElements=True|False\n  PathSep=Character (default '.')";
}

void JSONDOC::SourceMIMEContent(PSTRING StringPtr) const
{
  if (StringPtr)
    *StringPtr = "application/json";
}

// ---------------------------------------------------------------------------
// ParseRecords
//
// Scans the file buffer for top-level JSON values — either objects { }
// or arrays [ ] — and emits one RECORD per value via Db->DocTypeAddRecord.
// Multiple JSON values may appear consecutively in the same file
// (newline-delimited JSON / JSON-seq style), separated by arbitrary
// whitespace or comments.
//
// The state machine mirrors the BibTeX parser:
//   ScanToStart  – looking for the opening '{' or '['
//   InRecord     – inside a value; counting brace/bracket depth
//
// Strings (double-quoted) are tracked so that braces/brackets inside
// string values are not counted.  JSON escape sequences (including \")
// are handled.  C-style // and /* */ comments are skipped.
// ---------------------------------------------------------------------------

void JSONDOC::ParseRecords(const RECORD& FileRecord)
{
  STRING fn(FileRecord.GetFullFileName());
  RECORD Record(FileRecord); // copy so we can adjust start/end

  // Always map the whole file from byte 0.
  // ParseRecords is responsible for discovering record boundaries, so
  // it must see the entire file regardless of what RecordStart/End the
  // engine passes in (the engine may pass a pre-guessed single-record
  // range that only covers the first record).
  GPTYPE RecStart = 0;
  GPTYPE RecEnd   = 0;
  {
    PFILE fp2 = Db->ffopen(fn, "rb");
    if (fp2) { RecEnd = GetFileSize(fp2); ffclose(fp2); }
  }

#ifndef NO_MMAP
  MMAP mapping(fn, RecStart, RecEnd, MapSequential);
  if (!mapping.Ok()) {
    message_log(LOG_ERRNO, "%s::ParseRecords: Could not map '%s' into memory",
                Doctype.c_str(), fn.c_str());
    return;
  }
  PCHR   RecBuffer    = (PCHR)mapping.Ptr();
  GPTYPE ActualLength = mapping.Size();

#else
  // RecStart / RecEnd already resolved above
  PFILE fp = Db->ffopen(fn, "rb");
  if (!fp) {
    message_log(LOG_ERRNO, "%s::ParseRecords: Could not access '%s'",
                Doctype.c_str(), fn.c_str());
    return;
  }
  if (RecEnd <= RecStart) {
    message_log(LOG_WARN, "zero-length record '%s'[%ld-%ld] -- skipping",
                (const char *)fn, (long)RecStart, (long)RecEnd);
    ffclose(fp);
    return;
  }
  if (fseek(fp, RecStart, 0) == -1) {
    message_log(LOG_ERRNO, "%s::ParseRecords(): Seek '%s' to %ld failed",
                Doctype.c_str(), fn.c_str(), (long)RecStart);
    ffclose(fp);
    return;
  }

  GPTYPE RecLength = RecEnd - RecStart + 1;
  PCHR   RecBuffer = (PCHR)recBuffer.Want(RecLength + 2);
  GPTYPE ActualLength = (GPTYPE)fread(RecBuffer, 1, RecLength, fp);
  ffclose(fp);

  if (ActualLength == 0) {
    message_log(LOG_ERRNO, "%s::ParseRecords(): Failed to fread '%s'",
                Doctype.c_str(), fn.c_str());
    return;
  }
  if (ActualLength != RecLength) {
    message_log(LOG_WARN, "%s::ParseRecords(): Expected %ld bytes, got %ld",
                Doctype.c_str(), (long)RecLength, (long)ActualLength);
  }
  RecBuffer[ActualLength] = '\0';
#endif

  // ------------------------------------------------------------------
  // State machine
  //
  // braceDepth and bracketDepth are tracked independently because JSON
  // freely nests objects inside arrays and vice versa.  A single
  // depth counter using only one delimiter pair breaks on e.g.
  // {"a": [1,2]} because '['/']' would not match '{'/'}' and depth
  // would never return to zero correctly.
  // ------------------------------------------------------------------
  enum { ScanToStart, InRecord } State = ScanToStart;

  off_t  Start        = 0;
  int    braceDepth   = 0;
  int    bracketDepth = 0;
  bool   topIsObject  = true;
  bool   inString     = false;
  int    errors       = 0;

  for (off_t i = 0; i < (off_t)ActualLength; i++)
    {
      char c = RecBuffer[i];

      // ---- string handling (takes priority over everything else) ----
      if (inString)
        {
          if (c == '\\')
            i++;
          else if (c == '"')
            inString = false;
          continue;
        }

      // ---- comment handling (only outside strings) ----
      if (c == '/' && (i + 1) < (off_t)ActualLength)
        {
          if (RecBuffer[i+1] == '/')
            {
              while (++i < (off_t)ActualLength &&
                     RecBuffer[i] != '\n' && RecBuffer[i] != '\r')
                /* loop */;
              continue;
            }
          else if (RecBuffer[i+1] == '*')
            {
              i += 2;
              while (i + 1 < (off_t)ActualLength &&
                     !(RecBuffer[i] == '*' && RecBuffer[i+1] == '/'))
                i++;
              if (i + 1 < (off_t)ActualLength) i++;
              continue;
            }
        }

      // ---- enter string ----
      if (c == '"')
        {
          inString = true;
          continue;
        }

      // ---- state machine ----
      if (State == ScanToStart)
        {
          if (c == '{')
            {
              topIsObject  = true;
              // Do NOT update Start here — it was set to i+1 after the
              // previous record closed, giving contiguous coverage with
              // no gaps.  The engine requires records to be contiguous.
              braceDepth   = 1;
              bracketDepth = 0;
              State        = InRecord;
            }
          else if (c == '[')
            {
              topIsObject  = false;
              bracketDepth = 1;
              braceDepth   = 0;
              State        = InRecord;
            }
        }
      else // InRecord
        {
          if      (c == '{') braceDepth++;
          else if (c == '[') bracketDepth++;
          else if (c == '}')
            {
              braceDepth--;
              if (topIsObject && braceDepth == 0 && bracketDepth == 0)
                {
                  // Record ends at the closing '}' — do NOT advance i
                  // past trailing whitespace here; ScanToStart handles
                  // that naturally, and skipping here can push Start
                  // past ActualLength causing a record overflow.
                  Record.SetRecordStart((GPTYPE)(RecStart + Start));
                  Record.SetRecordEnd  ((GPTYPE)(RecStart + i));
                  Db->DocTypeAddRecord(Record);
                  Start        = i + 1;
                  braceDepth   = 0;
                  bracketDepth = 0;
                  State        = ScanToStart;
                }
            }
          else if (c == ']')
            {
              bracketDepth--;
              if (!topIsObject && bracketDepth == 0 && braceDepth == 0)
                {
                  Record.SetRecordStart((GPTYPE)(RecStart + Start));
                  Record.SetRecordEnd  ((GPTYPE)(RecStart + i));
                  Db->DocTypeAddRecord(Record);
                  Start        = i + 1;
                  braceDepth   = 0;
                  bracketDepth = 0;
                  State        = ScanToStart;
                }
            }
        }
    }

  // ------------------------------------------------------------------
  // Post-scan error / residual checks (mirrors BibTeX pattern)
  // ------------------------------------------------------------------
  if (inString) {
    message_log(LOG_ERROR, "%s: Runaway string in '%s'",
                Doctype.c_str(), (const char *)fn);
    errors++;
  }
  if (State == InRecord) {
    message_log(LOG_ERROR, "%s: Unterminated JSON value in '%s' (braceDepth=%d bracketDepth=%d)",
                Doctype.c_str(), (const char *)fn, braceDepth, bracketDepth);


    errors++;
  }

  // Handle any trailing unparsed bytes
  if ((off_t)Start < (off_t)ActualLength)
    {
      if (errors) {
        message_log(LOG_WARN, "Marking %s %ld-%ld deleted",
                    (const char *)fn,
                    (long)(RecStart + Start),
                    (long)(RecStart + ActualLength));
      } else if (Start != 0) {
        message_log(LOG_INFO, "Ignoring %s trailing bytes %ld-%ld",
                    (const char *)fn,
                    (long)(RecStart + Start),
                    (long)(RecStart + ActualLength));
      } else {
        message_log(LOG_INFO, "Ignoring '%s', no valid %s records found",
                    (const char *)fn, Doctype.c_str());
      }
      Record.SetBadRecord();
      Record.SetDocumentType("<NIL>");
      Record.SetRecordStart((GPTYPE)(RecStart + Start));
      Record.SetRecordEnd  ((GPTYPE)(RecStart + ActualLength));
      Db->DocTypeAddRecord(Record);
    }
}

// ---------------------------------------------------------------------------
// ParseObject  { "key" : value , … }
// ---------------------------------------------------------------------------

void JSONDOC::ParseObject(const char *json, size_t recLen,
        size_t& pos, const STRING& prefix, int depth, PRECORD record, GPTYPE base)
{
  if (depth > JSON_MAX_DEPTH) {
    message_log(LOG_WARN, "JSONDOC: max nesting depth exceeded – skipping subtree");
    int brace = 1;
    ++pos; // skip opening '{'
    while (pos < recLen && brace > 0) {
      if      (json[pos] == '{') ++brace; 
      else if (json[pos] == '}') --brace; 
      ++pos;    
    }
    return;
  }

  ++pos; // consume '{'


  const STRING localPrefix = prefix;
  while (true) {
    SkipWhitespace(json, pos, recLen);

    if (pos >= recLen) break;

    // --- FIX 2: Consume the closing brace cleanly before breaking ---
    if (json[pos] == '}') {
      ++pos; 
      break;
    }

    if (json[pos] != '"') {
      // malformed – skip to next separator
      while (pos < recLen && json[pos] && json[pos] != ',' && json[pos] != '}')
        ++pos;
      if (pos < recLen && json[pos] == ',') ++pos;
      continue;
    }

    // Parse key
    size_t kStart, kEnd;
    SkipString(json, recLen, pos, kStart, kEnd);
    
    // --- FIX 1: Verify your pointer layout logic here ---
    // If SkipString returns kEnd pointing past the string, use (kEnd - kStart)
    const int keyLen = (kEnd - kStart) + 1; 
    const char* keyPtr = &json[kStart];


    STRING fullpath;
    // Build full path
    if (prefix.GetLength() > 0) {
      fullpath  = prefix;
      fullpath  += m_PathSep;
    }
    fullpath.append(keyPtr, keyLen);

    SkipWhitespace(json, pos, recLen);
    if (pos < recLen && json[pos] == ':') ++pos;
    SkipWhitespace(json, pos, recLen);

    // Parse internal value element
    ParseValue(json, recLen, pos, fullpath, depth + 1, record, base);

    SkipWhitespace(json, pos, recLen);
    
    // Handle list separator
    if (pos < recLen && json[pos] == ',') {
      ++pos;
    }
  }
}

// ---------------------------------------------------------------------------
// ParseArray  [ value , … ]
// ---------------------------------------------------------------------------

#if 1


void JSONDOC::ParseArray(const char *json, size_t recLen, size_t& pos,
                         const STRING& prefix, int depth,
                         PRECORD record, GPTYPE base)
{
  ++pos; // consume '['

  int index = 0;

  while (true) {
    SkipWhitespace(json, pos, recLen);
    if (pos >= recLen) break; 

    // --- CLEAN ROUTINE ---
    if (json[pos] == ']') {
      ++pos; // Consume ']' cleanly
      break; 
    }
  
    if (m_IndexArrayElements) {
      char indexBuf[32];
      snprintf(indexBuf, sizeof(indexBuf), "%d", index);
      STRING elemKey = prefix;
      elemKey += m_PathSep;
      elemKey += indexBuf;
      ParseValue(json, recLen, pos, elemKey, depth + 1, record, base);
    } else {
      // All elements indexed under the parent key
      ParseValue(json, recLen, pos, prefix, depth + 1, record, base);
    }

    ++index;
    SkipWhitespace(json, pos, recLen);
    if (pos < recLen && json[pos] == ',') ++pos;
  }
}


#else

void JSONDOC::ParseArray(const char *json, size_t recLen, size_t& pos,
                         const STRING& prefix, int depth,
                         PRECORD record, GPTYPE base)
{
  ++pos; // consume '['
  int index = 0;

  while (true) {
    SkipWhitespace(json, pos, recLen);
    if (pos >= recLen) break;
    if (json[pos] == ']') {
      if (json[pos] == ']') ++pos;
      break;
    }

    if (m_IndexArrayElements) {
      char indexBuf[32];
      snprintf(indexBuf, sizeof(indexBuf), "%d", index);
      STRING elemKey = prefix;
      elemKey += m_PathSep;
      elemKey += indexBuf;
      ParseValue(json, recLen, pos, elemKey, depth + 1, record, base);
    } else {
      // All elements indexed under the parent key
      ParseValue(json, recLen, pos, prefix, depth + 1, record, base);
    }

    ++index;
    SkipWhitespace(json, pos, recLen);
    if (json[pos] == ',') ++pos;
  }
}
#endif

// ---------------------------------------------------------------------------
// ParseValue  – dispatch on first character
// ---------------------------------------------------------------------------

void JSONDOC::ParseValue(const char *json, size_t recLen,
	size_t& pos, const STRING& prefix, int depth, PRECORD record, GPTYPE base)
{
  SkipWhitespace(json, pos, recLen);
  char c = json[pos];

  if (c == '{') {
    // Nested object: recurse; no FC for the container itself, only leaves
    ParseObject(json, recLen, pos, prefix, depth, record, base);

  } else if (c == '[') {
    ParseArray(json, recLen, pos, prefix, depth, record, base);

  } else if (c == '"') {
    // FC covers the content bytes INSIDE the quotes, mirroring
    // how COLONDOC points at the text after the colon/whitespace.
    size_t contentStart, contentEnd;
    SkipString(json, recLen, pos, contentStart, contentEnd);
    if (contentEnd >= contentStart) {
      STRING contents;
      for (size_t i = contentStart; i <= contentEnd; ++i)
        contents += json[i];
      AddField(record,
               SanitiseFieldName(prefix),
               (GPTYPE)(base + contentStart),
               (GPTYPE)(base + contentEnd),
               contents);
    }

  } else {
    // Number / bool / null
    size_t valStart, valEnd;
    SkipPrimitive(json, recLen, pos, valStart, valEnd);
    if (valEnd >= valStart) {
      // Skip null – don't index the word "null" as a field value
      size_t len = valEnd - valStart + 1;
      bool isNull = (len == 4 &&
                     json[valStart]   == 'n' && json[valStart+1] == 'u' &&
                     json[valStart+2] == 'l' && json[valStart+3] == 'l');
      if (!isNull) {
        STRING contents;
        for (size_t i = valStart; i <= valEnd; ++i)
          contents += json[i];
        AddField(record,
                 SanitiseFieldName(prefix),
                 (GPTYPE)(base + valStart),
                 (GPTYPE)(base + valEnd),
                 contents);
      }
    }
  }
}

// ---------------------------------------------------------------------------
// SkipString
//
// pos must be on the opening '"'.
// After return pos is past the closing '"'.
// contentStart/contentEnd are buffer-relative indices of the first and
// last bytes of the string content (INSIDE the quotes).
// For an empty string, contentEnd < contentStart.
// ---------------------------------------------------------------------------

void JSONDOC::SkipString(const char *json, size_t recLen,
	size_t& pos, size_t& contentStart, size_t& contentEnd)
{
  ++pos; // skip opening '"'
  contentStart = pos;

  while (pos < recLen && json[pos] != '"') {
    if (json[pos] == '\\') {
      ++pos; // skip escape prefix
      if (json[pos] == 'u') pos += 4; // skip 4 hex digits of \uXXXX
    }
    ++pos;
  }

  contentEnd = (pos > contentStart) ? pos - 1 : contentStart - 1;

  if (json[pos] == '"') ++pos; // skip closing '"'
}

// ---------------------------------------------------------------------------
// SkipPrimitive  (number / bool / null)
// ---------------------------------------------------------------------------

void JSONDOC::SkipPrimitive(const char *json, size_t recLen,
	size_t& pos, size_t& valueStart, size_t& valueEnd)
{
  valueStart = pos;
  while (pos < recLen &&
         json[pos] != ',' && json[pos] != '}' &&
         json[pos] != ']' && !isspace((unsigned char)json[pos])) {
    ++pos;
  }
  valueEnd = (pos > valueStart) ? pos - 1 : valueStart - 1;
}

// ---------------------------------------------------------------------------
// SkipWhitespace  (tolerates // and /* */ comments found in JSON5/configs)
// ---------------------------------------------------------------------------

void JSONDOC::SkipWhitespace(const char *json, size_t& pos) const
{
  while (json[pos]) {
    if (isspace((unsigned char)json[pos])) {
      ++pos;
    } else if (json[pos] == '/' && json[pos+1] == '/') {
      while (json[pos] && json[pos] != '\n') ++pos;
    } else if (json[pos] == '/' && json[pos+1] == '*') {
      pos += 2;
      while (json[pos] && !(json[pos] == '*' && json[pos+1] == '/')) ++pos;
      if (json[pos]) pos += 2;
    } else {
      break;
    }
  }
}

void JSONDOC::SkipWhitespace(const char *json, size_t& pos, size_t len) const
{   
  while (pos < len) {
    const unsigned char ch = static_cast<unsigned char>(json[pos]);

    // 1. Hot Path: Skip standard ASCII whitespace fast
    // Avoid isspace() overhead (which handles locale sync overhead)
    // Checking the common space types directly or via a fast unrolled layout
    if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') {
      ++pos;
      continue;
    }

    // 2. Comments Path: Check bounds carefully before peering ahead
    if (ch == '/' && (pos + 1 < len)) {
      char next_ch = json[pos + 1];

      if (next_ch == '/') {
        // Line Comment: Scan until newline or end of record buffer
        pos += 2;
        while (pos < len && json[pos] != '\n') {
          ++pos;
        }
        continue;
      } 
      
      if (next_ch == '*') {
        // Block Comment: Scan until closing tokens or end of buffer safely
        pos += 2;
        while (pos + 1 < len && !(json[pos] == '*' && json[pos + 1] == '/')) {
          ++pos;
        }
        // Advance past the closing '*/' if we found it before the buffer limit
        if (pos + 1 < len) {
          pos += 2;
        } else {
          pos = len; // Malformed input terminated safely at boundary
        }
        continue;
      }
    }

    // 3. Structural Token Hit: Break instantly
    break;
  }                       
}

// ---------------------------------------------------------------------------
// SanitiseFieldName
// ---------------------------------------------------------------------------

STRING JSONDOC::SanitiseFieldName(const STRING& raw) const
{
  STRING safe;
  for (size_t i = 1; i <= (size_t)raw.GetLength(); ++i) {
    char ch = raw.GetChr(i);
    if (isalnum((unsigned char)ch) || ch == '_' ||
        ch == '-' || ch == '.' || ch == m_PathSep)
      safe += ch;
    else
      safe += '_';
  }
  return safe;
}

// ---------------------------------------------------------------------------
// AddField
//
// Registers fieldname + FC with the engine.  No value is stored in the
// index — the engine reads bytes [start, end] from the original file at
// query time.
//
// 'contents' is the transient string of those same bytes, used solely
// for field-type autodetection via GuessFieldType (inherited from
// METADOC).  It is never written to the index.
//
// FCT::AddEntry accepts:
//   AddEntry(const FC&     fc)
//   AddEntry(const FCLIST& fct)
//   AddEntry(const FCLIST* fctPtr)
// We use the FC& overload since we have exactly one coordinate range
// per leaf value.
// ---------------------------------------------------------------------------

void JSONDOC::AddField(PRECORD record,
                       const STRING& fieldname,
                       GPTYPE start, GPTYPE end,
                       const STRING& contents)
{
  // Handle field/path unification (which also handles flags to ignore fields/paths)
  const STRING name ( UnifiedName(fieldname) );

  if (!record || name.IsEmpty() || end < start)
    return;

  // FC offsets are record-relative: the engine adds the record base
  // (GetRecordStart) when resolving absolute GPs, so subtract it here.
  // For record 1 (recStart=0) this is a no-op.
  GPTYPE recStart = record->GetRecordStart();
  FC fc;
  fc.SetFieldStart(start - recStart);
  fc.SetFieldEnd  (end   - recStart);

  FCT fct;
  fct.AddEntry(fc);          // FC& overload

  DF df;
  df.SetFieldName(name);
  df.SetFct(fct);

  if (Db) {
    DFD dfd;
    // Autodetect field type from the transient content string.
    // GuessFieldType calls Db->AddFieldType internally when it makes a
    // determination, so we only need to call it — we don't act on the
    // return value here.
    FIELDTYPE ft;
    if (m_AutoFieldTypes && contents.GetLength() > 0)
      ft = GuessFieldType(name, contents);
    dfd.SetFieldName(name);
    if (ft.Defined()) dfd.SetFieldType(ft); // Set the type
    Db->DfdtAddEntry(dfd);  // register field name globally (idempotent)
  }

  record->AddEntry(df);  // preserve fields already added for this record
}


const char *NDJSON::Description(PSTRLIST List) const {
  if (List) {
    const STRING ThisDoctype("NDJSON");
    if (Doctype != ThisDoctype && List->IsEmpty())
      List->AddEntry(Doctype);
    List->AddEntry(ThisDoctype);
    JSONDOC::Description(List);
  }
  return "Newline Delimited JSON Document Type\n\
Specific Indexing options (defined in .ini):\n\
  [General]\n\
  NewLinesInQuotes=True|False // Default False (as in NDJSON specification)";
}


// We accept newlines in fields if within quotes!!!!
void NDJSON::ParseRecords(const RECORD& FileRecord)
{
  const STRING FileName (FileRecord.GetFullFileName() );
  MMAP         FileMap (FileName, MapSequential); // We are sequentially reading
  if (!FileMap.Ok())
    {
      message_log (LOG_ERRNO, "%s could not access '%s'", Doctype.c_str(), FileName.c_str());
      return;			// File not accessed
    }
  off_t RecStart = FileRecord.GetRecordStart();
  off_t RecEnd = FileRecord.GetRecordEnd();
  const off_t FileEnd = FileMap.Size();

  if (RecEnd == 0) RecEnd = FileEnd;

  if (RecEnd - RecStart <= 0)
    {
      message_log (LOG_WARN, "zero-length record '%s'[%ld-%ld] -- skipping",
	FileName.c_str(), (long)RecStart, (long)RecEnd);
      return;
    }
  else if (RecStart > FileEnd)
    {
      message_log (LOG_ERROR, "%s::ParseRecords(): Seek '%s' to %ld failed",
	Doctype.c_str(), FileName.c_str(), RecStart);
      return;
    }
  else if (RecEnd > FileEnd)
    {
      message_log (LOG_WARN, "%s::ParseRecord(): End after EOF (%d>%d) in '%s'?",
	Doctype.c_str(), RecEnd, FileEnd, FileName.c_str());
      RecEnd = FileEnd;
    }
  RECORD Record (FileRecord);
  off_t Position = RecStart;
  
  unsigned char  ci = 0;
  const unsigned char *ptr = FileMap.Ptr(); // NOTE: This is a memory mapped file!

  const char quoteChar = '\"'; // We assume ONLY " for quotes
  bool inQuote = false;
  bool escaped = false;
  while (Position < RecEnd)
    {
      // Need to handle newlines in quoted columns
      for (; !(!inQuote && (ci == '\n' || ci =='\r')) && Position <= RecEnd; ci=*ptr++, Position++) {
	if (escaped) escaped = false;
	else if (ci == '\\') 
	  escaped = true;
	else if (newLinesInQuotes && ci == quoteChar) {
	  // if (*ptr == quoteChar) ptr++, Position++; // skip
	  // else 
	  inQuote = !inQuote;
	}
      }
      // I may be in a quote but that's an error so I'm in trouble anyway..
      // as I'll keep going until end of file... ignoring the CR and LF.

      if (*ptr == '\r' || *ptr == '\n') {
        ptr++; Position++; // Skip second newline (such as in MSDOS)
      }

      if (RecStart != Position)
	{
	  Record.SetRecordStart(RecStart);
	  Record.SetRecordEnd(Position-1);
//cerr << "Record is: " << RecStart << "-" << Position-1 << endl;
	  Db->DocTypeAddRecord(Record);
	}
      if (ci=='\n' || ci == '\r')
	ci=0;// save an EOF, but hide a newline so it will loop again
      RecStart = Position;
    }
  if (inQuote) message_log (LOG_ERROR, "Runaway record. Missing end-quotes (%c). Check format.", quoteChar);
};


const char *CIRRUSNDJSON::Description(PSTRLIST List) const {
  if (List) {
    const STRING ThisDoctype("CIRRUSNDJSON");
    if (Doctype != ThisDoctype && List->IsEmpty())
      List->AddEntry(Doctype);
    List->AddEntry(ThisDoctype);
    NDJSON::Description(List);
  }
  return "Elasticsearch Bulk-format NDJSON (e.g. Wikimedia Cirrus search dumps).\n\
Automatically skips interleaved {\"index\":{...}} action lines and treats\n\
JSON arrays as repeatable fields (IndexArrayElements defaults to False).\n\
Options (in .ini):\n\
  IndexArrayElements=False    (default — set True to flatten arrays instead)\n\
  PathSep=<char>              default: |";
}

// A bulk action line is a JSON object whose single top-level key is
// one of: index, create, update, delete -- e.g. {"index":{"_id":"123"}}
// Real Cirrus document bodies never start with one of these as the
// sole/first key, so this check is cheap and safe.
bool CIRRUSNDJSON::IsBulkActionLine(const char *buf, size_t len) const
{
  return _IsBulkActionLine(buf, len);
}

void CIRRUSNDJSON::ParseFields(RECORD* Record)
{
  if (!Record) return;

  STRING FileName;
  Record->GetFullFileName(&FileName);

  // Ask the session wrapper for the address base.
  // If the queue is on the same file, it returns immediately with zero cost.
  // If the file name shifts, it automatically handles unmapping and new mapping.
  const unsigned char* FileBase = m_FileSession.GetMemoryBase(FileName, MapSequential);
  if (!FileBase) {
    return; // Failure logs already handled internally
  }
  GPTYPE base   = Record->GetRecordStart();
  GPTYPE recEnd = Record->GetRecordEnd();
  size_t recLen = (size_t)(recEnd - base + 1);

  // Instant zero-copy memory dereference
  const char* RecBuffer = reinterpret_cast<const char*>(FileBase + base);

  if (IsBulkActionLine(RecBuffer, recLen)) {
    return; 
  }
  ParseBuffer(RecBuffer, recLen, Record, base, FileName);
}



void JSONDOC::ParseFields(PRECORD NewRecord)
{
  if (!NewRecord)
    return;

  STRING FileName;
  NewRecord->GetFullFileName(&FileName);

  GPTYPE base   = NewRecord->GetRecordStart();
  GPTYPE recEnd = NewRecord->GetRecordEnd();
  size_t recLen = (size_t)(recEnd - base + 1);

  PFILE fp = Db->ffopen(FileName, "rb");
  if (!fp) {
    message_log(LOG_ERROR, "JSONDOC::ParseFields: cannot open '%s'",
                (const char*)FileName);
    return;
  }
  if (fseek(fp, (long)base, SEEK_SET) != 0) {
    message_log(LOG_ERROR, "JSONDOC::ParseFields: fseek to %ld failed in '%s'",
                (long)base, (const char*)FileName);
    Db->ffclose(fp);
    return;
  }
  char *buf = new char[recLen + 1];
  size_t nRead = fread(buf, 1, recLen, fp);
  Db->ffclose(fp);
  buf[nRead] = '\0';

  ParseBuffer(buf, recLen, NewRecord, base, FileName);
  delete[] buf;
}

// Factored out so subclasses (e.g. CIRRUSNDJSON) can peek at the
// loaded buffer before committing to a full parse, without opening
// and reading the file a second time.
void JSONDOC::ParseBuffer(const char *buf, size_t recLen, PRECORD record, GPTYPE base, const STRING& FileName)
{
  size_t pos = 0;
  SkipWhitespace(buf, pos, recLen);
  
  if (buf[pos] == '{') {
    STRING emptyPrefix;
    ParseObject(buf, recLen, pos, emptyPrefix, 0, record, base);
  } else if (buf[pos] == '[') {
    STRING rootKey("_root_");
    ParseArray(buf, recLen,  pos, rootKey, 0, record, base);
  } else {
    message_log(LOG_WARN,
                "JSONDOC: '%s' offset %ld does not start with '{' or '[', "
                "falling back to COLONDOC parser",
                FileName.c_str(), (long)base);
    COLONDOC::ParseFields(record);
  }
}


inline STRING _createESKey(const STRING& id, const STRING& index)
{
  return "E$" + id + ":" + index;
}


const char *ESBULKNDJSON::Description(PSTRLIST List) const {
  if (List) {
    const STRING ThisDoctype("ESBULKNDJSON");
    if (Doctype != ThisDoctype && List->IsEmpty())
      List->AddEntry(Doctype);
    List->AddEntry(ThisDoctype);
    NDJSON::Description(List);
  }
  return "Elasticsearch Bulk API formatted NDJSON (e.g. Wikimedia Cirrus dumps).\n\
Automatically pairs {\"index\"/\"create\"/\"update\":{\"_id\":...}} action lines\n\
with their following document body line, using \"_id\" as the record Key so\n\
the engine's same-key versioning applies. {\"delete\":{\"_id\":...}} action\n\
lines call DeleteByKey() directly and are not indexed.\n\
Options (in .ini):\n\
  IndexArrayElements=False    (default -- set True to flatten arrays to key|0, key|1, ...)\n\
  PathSep=<char>              default: |";
}

// A bulk action line is a JSON object whose single top-level key is
// one of: index, create, update, delete -- e.g. {"index":{"_id":"123"}}
// Real document bodies never start with one of these as the sole/first
// key, so this check is cheap and safe.


// Scan raw JSON bytes for a top-level string field by name.
// Returns empty STRING if not found. No full parse needed --
// wiki/title are always top-level scalars in Cirrus body lines.
static inline STRING ExtractJsonStringField(const unsigned char *buf, size_t len, const char *field)
{
  // Build search pattern: "field":"
  char pat[64];
  snprintf(pat, sizeof(pat), "\"%s\"", field);
  size_t patLen = strlen(pat);

  const void *found = memmem(buf, len, pat, patLen);
  if (!found) return NulString;

  const unsigned char *p   = (const unsigned char *)found + patLen;
  const unsigned char *end = buf + len;

  // Skip whitespace and colon
  while (p < end && (*p == ' ' || *p == '\t' || *p == ':')) ++p;
  if (p >= end || *p != '"') return NulString;
  ++p;

  const unsigned char *valStart = p;
  while (p < end && *p != '"') ++p;
  if (p >= end) return NulString;

  return STRING((const char *)valStart, (size_t)(p - valStart));
}

#if 0

void CIRRUSNDJSON::ParseRecords(const RECORD& FileRecord)
{
  // ... identical setup to ESBULKNDJSON::ParseRecords ...
  // Only difference: body record Key = wiki + ":" + _id

  // In the body-line branch, after:
  //   Record.SetRecordStart(RecStart);
  //   Record.SetRecordEnd(Position-1);
  // and before Db->DocTypeAddRecord(Record):

  if (havePendingId && pendingId.GetLength())
    {
      // Scan body bytes for "wiki":"<value>" to namespace the key.
      STRING wiki = ExtractJsonStringField(lineStart, lineLen, "wiki");
      if (wiki.GetLength())
        Record.SetKey(wiki + ":" + pendingId);
      else
        Record.SetKey(pendingId);   // fallback: no wiki field in this dump
    }

  Db->DocTypeAddRecord(Record);
  havePendingId = false;
  pendingId.Clear();
}

#endif


#if 1


ESBULKNDJSON::BulkAction ESBULKNDJSON::ParseBulkAction(
    const unsigned char *buf, size_t len, STRING& idOut, STRING& indexOut) const
{
  idOut.Clear();
//  indexOut.Clear(); // Defaults to "" if not defined in the action object

  size_t i = 0;
  while (i < len && (buf[i] == ' ' || buf[i] == '\t')) ++i;
  if (i >= len || buf[i] != '{') return ActionNone;
  ++i;
  while (i < len && (buf[i] == ' ' || buf[i] == '\t')) ++i;
  if (i >= len || buf[i] != '"') return ActionNone;
  ++i;
  size_t keyStart = i;
  while (i < len && buf[i] != '"') ++i;
  if (i >= len) return ActionNone;
  const size_t keyLen = i - keyStart;

  BulkAction action = ActionNone;
  if      (keyLen == 5 && memcmp(buf+keyStart, "index",  5) == 0) action = ActionIndex;
  else if (keyLen == 6 && memcmp(buf+keyStart, "create", 6) == 0) action = ActionCreate;
  else if (keyLen == 6 && memcmp(buf+keyStart, "update", 6) == 0) action = ActionUpdate;
  else if (keyLen == 6 && memcmp(buf+keyStart, "delete", 6) == 0) action = ActionDelete;
  if (action == ActionNone) return ActionNone;

  // 1. Pull "_index":"..." out of the action's inner object, if present
  const char *indexKey = "\"_index\"";
  const void *foundIndex = memmem(buf + i, len - i, indexKey, 8); // length of "_index" is 8
  if (foundIndex)
    {
      const unsigned char *p   = (const unsigned char *)foundIndex + 8;
      const unsigned char *end = buf + len;
      while (p < end && (*p == ' ' || *p == ':' || *p == '\t')) ++p;
      if (p < end && *p == '"')
        {
          ++p;
          const unsigned char *valStart = p;
          while (p < end && *p != '"') ++p;
          if (p < end)
            indexOut = STRING((const char*)valStart, (size_t)(p - valStart));
        }
    }

  // 2. Pull "_id":"..." out of the action's inner object, if present
  const char *idKey = "\"_id\"";
  const void *foundId = memmem(buf + i, len - i, idKey, 5); // length of "_id" is 5
  if (foundId)
    {
      const unsigned char *p   = (const unsigned char *)foundId + 5;
      const unsigned char *end = buf + len;
      while (p < end && (*p == ' ' || *p == ':' || *p == '\t')) ++p;
      if (p < end && *p == '"')
        {
          ++p;
          const unsigned char *valStart = p;
          while (p < end && *p != '"') ++p;
          if (p < end)
            idOut = STRING((const char*)valStart, (size_t)(p - valStart));
        }
    }

  return action;
}



#else
ESBULKNDJSON::BulkAction ESBULKNDJSON::ParseBulkAction(const unsigned char *buf, size_t len, STRING& idOut) const
{
  idOut.Clear();

  size_t i = 0;
  while (i < len && (buf[i] == ' ' || buf[i] == '\t')) ++i;
  if (i >= len || buf[i] != '{') return ActionNone;
  ++i;
  while (i < len && (buf[i] == ' ' || buf[i] == '\t')) ++i;
  if (i >= len || buf[i] != '"') return ActionNone;
  ++i;
  size_t keyStart = i;
  while (i < len && buf[i] != '"') ++i;
  if (i >= len) return ActionNone;
  const size_t keyLen = i - keyStart;

  BulkAction action = ActionNone;
  if      (keyLen == 5 && memcmp(buf+keyStart, "index",  5) == 0) action = ActionIndex;
  else if (keyLen == 6 && memcmp(buf+keyStart, "create", 6) == 0) action = ActionCreate;
  else if (keyLen == 6 && memcmp(buf+keyStart, "update", 6) == 0) action = ActionUpdate;
  else if (keyLen == 6 && memcmp(buf+keyStart, "delete", 6) == 0) action = ActionDelete;
  if (action == ActionNone) return ActionNone;

  // Pull "_id":"..." out of the action's inner object, if present.
  const char *idKey = "\"_id\"";
  const void *found = memmem(buf + i, len - i, idKey, 5);
  if (found)
    {
      const unsigned char *p   = (const unsigned char *)found + 5;
      const unsigned char *end = buf + len;
      while (p < end && (*p == ' ' || *p == ':' || *p == '\t')) ++p;
      if (p < end && *p == '"')
        {
          ++p;
          const unsigned char *valStart = p;
          while (p < end && *p != '"') ++p;
          if (p < end)
            idOut = STRING((const char*)valStart, (size_t)(p - valStart));
        }
    }
  return action;
}
#endif

// We accept newlines in fields if within quotes!!!!
//
// ES bulk-format files interleave an "action" line with its document
// body line. "delete" actions are a single line with NO following body.
// We pair action+body, set the record Key from "_id" so the engine's
// existing same-key versioning applies, and call Db->DeleteByKey() for
// "delete" actions directly (no body is indexed for those).

void ESBULKNDJSON::ParseRecords(const RECORD& FileRecord)
{
  const STRING FileName (FileRecord.GetFullFileName() );

  off_t RecStart = FileRecord.GetRecordStart();
  off_t RecEnd   = FileRecord.GetRecordEnd();

  if (RecEnd - RecStart <= 0)
    {
      message_log (LOG_WARN, "zero-length record '%s'[%ld-%ld] -- skipping",
        FileName.c_str(), (long)RecStart, (long)RecEnd);
      return;
    }

  const unsigned char* FileBase = m_FileSession.GetMemoryBase(FileName, MapSequential);
  if (!FileBase) return; // Failure logs already handled internally

  const off_t FileEnd = m_FileSession.Size();

  size_t recLen = (size_t)(RecEnd - RecStart + 1);

  if (RecEnd == 0) RecEnd = FileEnd;

  if (RecEnd - RecStart <= 0)
    {
      message_log (LOG_WARN, "zero-length record '%s'[%ld-%ld] -- skipping",
        FileName.c_str(), (long)RecStart, (long)RecEnd);
      return;
    }
  else if (RecStart > FileEnd)
    {
      message_log (LOG_ERROR, "%s::ParseRecords(): Read '%s' at offset %ld failed",
        Doctype.c_str(), FileName.c_str(), RecStart);
      return;
    }
  else if (RecEnd > FileEnd)
    {
      message_log (LOG_WARN, "%s::ParseRecord(): End after EOF (%d>%d) in '%s'?",
        Doctype.c_str(), RecEnd, FileEnd, FileName.c_str());
      RecEnd = FileEnd;
    }

  off_t Position = RecStart;
  unsigned char  ci = 0;
  const unsigned char* ptr = reinterpret_cast<const unsigned char*>(FileBase + RecStart);

  const char quoteChar = '\"';
  bool inQuote = false;
  bool escaped = false;

  // Carries the pending action's id across to the *next* line (its body),
  // within this single sequential ParseRecords call.
  STRING pendingId;
  bool   havePendingId = false;

  STRING ES_index = FileRecord.GetFileName().Left('-');
  bool   test_wiki = true;

  while (Position < RecEnd)
    {
      for (; !(!inQuote && (ci == '\n' || ci =='\r')) && Position <= RecEnd; ci=*ptr++, Position++) {
	if (escaped) escaped = false;
 	else if (ci == '\\') escaped = true;
        else if (newLinesInQuotes && ci == quoteChar) {
          // if (*ptr == quoteChar) ptr++, Position++;
          // else
	  inQuote = !inQuote;
        }
      }
      if (*ptr == '\r' || *ptr == '\n') {
        ptr++; Position++;
      }

      if (RecStart != Position)
        {
          const unsigned char *lineStart = reinterpret_cast<const unsigned char*>(FileBase + RecStart);

          const size_t         lineLen   = (size_t)(Position - 1 - RecStart);

          STRING     thisId;
          BulkAction thisAction = ParseBulkAction(lineStart, lineLen, thisId, ES_index);

          if (thisAction != ActionNone)
            {
              // Every action line still becomes a record -- bad/deleted,
              // never returned, but its bytes are accounted for so the
              // GP address space stays gapless.
              RECORD Record (FileRecord);
              Record.SetRecordStart(RecStart);
              Record.SetRecordEnd(Position-1);
              Record.SetDocumentType("PLAINTEXT"); // No fields -- junk
              Record.SetBadRecord();
              Db->DocTypeAddRecord(Record);

              if (thisAction == ActionDelete)
                {
                  if (thisId.GetLength())
                    {
                      if (!Db->DeleteByKey(_createESKey(thisId, ES_index)))
                        {
                            message_log(LOG_INFO, "%s: DeleteByKey('%s') found nothing to delete",
                                        Doctype.c_str(), thisId.c_str());
                        }
                      else 
                        message_log(LOG_DEBUG, "%s: deleted record with key '%s'", Doctype.c_str(), thisId.c_str());
                    }
                  else
                    {
                      message_log(LOG_WARN, "%s: delete action with no _id at offset %ld in '%s'",
                                  Doctype.c_str(), (long)RecStart, FileName.c_str());
                    }
                  havePendingId = false;
                }
              else
                {
                  // index / create / update -- body is the next line.
                  pendingId     = thisId;
                  havePendingId = true;
                }
            }
          else
            {
              // This line is a document body.
              RECORD Record (FileRecord);
              Record.SetRecordStart(RecStart);
              Record.SetRecordEnd(Position-1);

              if (havePendingId && pendingId.GetLength())
                {
		  if (test_wiki) {
		    const unsigned char *lineStart = reinterpret_cast<const unsigned char*>(FileBase + RecStart);
		    const size_t         lineLen   = (size_t)(Position - 1 - RecStart);
		    // "wiki":"abwiki" 
		    STRING wiki = ExtractJsonStringField(lineStart, lineLen, "wiki");
		    if (wiki.GetLength()) {
			cerr << "Got a wiki: " << wiki << endl;
			ES_index = wiki;
		    }
		    test_wiki = false;
		  }
                  SetKey(&Record, _createESKey(pendingId, ES_index ));
                }
              else if (!havePendingId && DebugMode)
                {
                  message_log(LOG_DEBUG, "%s: body line with no preceding action line at %ld",
                              Doctype.c_str(), (long)RecStart);
                }

              Db->DocTypeAddRecord(Record);
              havePendingId = false;
              pendingId.Clear();
            }
        }
      if (ci=='\n' || ci == '\r')
        ci=0;
      RecStart = Position;
    }
  if (inQuote) message_log (LOG_ERROR, "Runaway record. Missing end-quotes (%c). Check format.", quoteChar);
}

