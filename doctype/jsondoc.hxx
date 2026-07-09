/*-@@@
File:           jsondoc.hxx
Version:        1.02
Description:    Class JSONDOC - JSON Document Type
Author:         Based on COLONDOC by Edward C. Zimmermann
Copyright:      Copyright (c) 2026 re-Isearch Project
                Licensed under the Apache 2.0 license

Notes:
  Parses simple JSON documents and indexes each key-value pair by
  recording the FC (field coordinates = absolute byte offsets into the
  original file) of each leaf value.  No copy of the value is stored
  in the index.

  Nested objects use '|' as a path separator (configurable), e.g.

    { "author": { "first": "John", "last": "Doe" } }

  produces fields  author|first  and  author|last.

  Arrays are indexed by element number (0-based):

    { "keywords": ["search", "retrieval"] }

  produces fields  keywords|0  and  keywords|1.

  Configurable via doctype options in the .ini / IDB option string:
    PathSep=<char>              default: |
    IndexArrayElements=true     default: true
@@@-*/

#ifndef JSONDOC_HXX
#define JSONDOC_HXX

#include "colondoc.hxx"

#ifndef JSON_PATH_SEP
# define JSON_PATH_SEP '.'
#endif

#ifndef JSON_MAX_DEPTH
# define JSON_MAX_DEPTH 32
#endif


// Service Class to re-route JSON to the appropriate JSON doctype
class JSONDETECT : public DOCTYPE {
public:
  JSONDETECT(PIDBOBJ DbParent, const STRING& Name):DOCTYPE(DbParent, Name){;}
  const char *Description(PSTRLIST List) const;
  void ParseRecords(const RECORD& FileRecord);
} ;


class JSONDOC : public COLONDOC {
  friend class JSONLDDOC;
  friend class EJSONDOC;
  friend class ESBULKNDJSON;
public:
  JSONDOC(PIDBOBJ DbParent, const STRING& Name);

  const char *Description(PSTRLIST List) const;

  void ParseRecords(const RECORD& FileRecord);
  void ParseFields(PRECORD NewRecord);

  void BeforeIndexing();

  GPTYPE ParseWords(UCHR* DataBuffer, GPTYPE DataLength,
	GPTYPE DataOffset, GPTYPE* GpBuffer, GPTYPE GpLength);

  void SourceMIMEContent(PSTRING StringPtr) const;

  ~JSONDOC();

protected:
  // Parses an already-loaded buffer (shared by ParseFields and by
  // subclasses, like CIRRUSNDJSON, that need to peek at the buffer
  // before deciding whether to parse it).
  void ParseBuffer(const char *buf, size_t recLen, PRECORD record, GPTYPE base, const STRING& FileName);


  // ---------------------------------------------------------------
  // Recursive descent parser.
  //
  // Every function receives:
  //   json   - null-terminated buffer of the whole record
  //   pos    - current read cursor (modified in place)
  //   prefix - field-name path accumulated so far
  //   depth  - current recursion depth
  //   record - the RECORD being built
  //   base   - absolute file offset of json[0], so that
  //            FC.start = base + pos_before_value_content
  //            FC.end   = base + pos_after_value_content - 1
  // ---------------------------------------------------------------

  virtual void ParseValue (const char *json, size_t recLen,
	size_t& pos, const STRING& prefix, int depth, PRECORD record, GPTYPE base);

  virtual void ParseObject(const char *json, size_t recLen,
	 size_t& pos, const STRING& prefix, int depth, PRECORD record, GPTYPE base);

  virtual void ParseArray (const char *json, size_t recLen,
	 size_t& pos, const STRING& prefix, int depth, PRECORD record, GPTYPE base);

  // Skip a quoted string; returns buffer-relative indices of the
  // content bytes INSIDE the quotes (the range we want the FC to cover).
  void SkipString   (const char *json, size_t recLen,
	 size_t& pos, size_t& contentStart, size_t& contentEnd);

  // Skip a primitive (number/bool/null); returns its buffer-relative range.
  void SkipPrimitive(const char *json, size_t recLen,
	size_t& pos, size_t& valueStart,   size_t& valueEnd);

  void SkipWhitespace(const char *json, size_t& pos) const;
  void SkipWhitespace(const char *json, size_t& pos, size_t len) const;

  STRING SanitiseFieldName(const STRING& raw) const;

  // Register fieldname + FC with the engine.  contents is used only
  // transiently for field-type autodetection (GuessFieldType); it is
  // never stored in the index.
  void AddField(PRECORD record,
                const STRING& fieldname,
                GPTYPE start, GPTYPE end,
                const STRING& contents);

  STRING m_ScratchPath; // Holds onto internal capacity between records
  char m_PathSep;
  bool m_IndexArrayElements;
  bool m_AutoFieldTypes;
};


// NDJSON (Newline Delimited JSON) 
class NDJSON : public JSONDOC {
  friend class ESBULKNDJSON;
public:
  NDJSON(PIDBOBJ DbParent, const STRING& Name) : JSONDOC (DbParent, Name) {
     newLinesInQuotes = Getoption("NewLinesInQuotes", "true").GetBool();
  }
  const char *Description(PSTRLIST List) const;
  void ParseRecords(const RECORD& FileRecord);
private:
  bool newLinesInQuotes;
};


typedef JSONDOC* PJSONDOC;

class CIRRUSNDJSON : public NDJSON {
public:
  CIRRUSNDJSON(PIDBOBJ DbParent, const STRING& Name) : NDJSON(DbParent, Name) {
    // Cirrus array fields (outgoing_link, category, template, …) should
    // be repeatable fields, not flattened to key|0, key|1, … key|N.
    m_IndexArrayElements = Getoption("IndexArrayElements", "false").GetBool();
  }
  const char *Description(PSTRLIST List) const;
  void ParseFields(PRECORD NewRecord);

protected:
  bool IsBulkActionLine(const char *buf, size_t len) const;
};

typedef CIRRUSNDJSON* PCIRRUSNDJSON;



// ---------------------------------------------------------------------------
// ESBULKNDJSON
//
// Variant of NDJSON for files in the Elasticsearch "Bulk API" NDJSON
// format (used by, among others, Wikimedia Cirrus search dumps:
// *-cirrussearch-content.json.gz / *-cirrussearch-general.json.gz).
//
// Bulk-format files interleave an "action" line with its document body:
//   {"index":{"_id":"123"}}
//   {"title":"...", "text":"...", "outgoing_link":["...","..."], ...}
//
// Action verbs: index, create, update, delete.
//   - index/create/update: body line follows; the action's "_id" is used
//     as the record's Key, so the engine's existing same-key
//     versioning/replacement logic applies (this matters especially for
//     "update", which may carry only partial fields).
//   - delete: single line, NO body follows; we call Db->DeleteByKey()
//     directly and do not index anything for it.
//
// Also defaults IndexArrayElements to False, since bulk-format documents
// (e.g. Cirrus dumps) are array-heavy (outgoing_link, category, template,
// external_link, auxiliary_text, heading, ...) and we want those treated
// as repeatable fields rather than flattened into hundreds of positional
// field names (outgoing_link|0 .. outgoing_link|N).
// ---------------------------------------------------------------------------

class ESBULKNDJSON : public NDJSON {
public:
  ESBULKNDJSON(PIDBOBJ DbParent, const STRING& Name) : NDJSON(DbParent, Name) {
    m_IndexArrayElements = Getoption("IndexArrayElements", "false").GetBool();
    DebugMode = false;
  }
  const char *Description(PSTRLIST List) const;
  void ParseRecords(const RECORD& FileRecord);

protected:
  enum BulkAction { ActionNone, ActionIndex, ActionCreate, ActionUpdate, ActionDelete };

  // Inspects a single line's bytes. Returns ActionNone if this is NOT
  // a recognized bulk action line (i.e. it's a document body line).
  // On a recognized action line, extracts "_id" into idOut if present.
  BulkAction ParseBulkAction(const unsigned char *buf, size_t len, STRING& idOut,
    STRING& indexOut) const;
private:
  bool DebugMode;
};

typedef ESBULKNDJSON* PESBULKNDJSON;



#endif /* JSONDOC_HXX */
