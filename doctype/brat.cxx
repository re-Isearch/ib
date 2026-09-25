/*-
File:           brat.cxx
Version:        1.00
Description:    BRAT stand-off annotation doctype
Copyright:      Copyright (c) 2026 re-Isearch Project
                Licensed under the Apache 2.0 license

Why stand-off annotations fit IB
-------------------------------

Stand-off annotation keeps the primary text unchanged and stores annotation
ranges in a separate resource.  That is very close to IB's native model:

    annotated span              -> FC
    discontinuous annotation    -> FCT containing several FCs
    overlapping annotations     -> overlapping FC/FCT ranges
    annotation type             -> field name

No tree rewrite is necessary for overlap, and no graph database is necessary
merely to represent ranges over text.  Graph-like relations can be layered on
later when annotations have stable identities and genuinely semantic links.

BRAT is the first adapter because its on-disk format is deliberately small and
has mature open-source annotation tools.  A document normally consists of:

    document.txt                immutable UTF-8 text
    document.ann                annotations referring to character offsets

BRAT text-bound annotations use zero-based, start-inclusive/end-exclusive
character offsets.  Discontinuous spans are separated by semicolons:

    T1  Person    10 24
    T2  Location  40 45;52 60

This implementation parses T records only.  BRAT relation/event/attribute/
normalization/note records (R/E/A/M/N/#) are deliberately left for the next
layer; their IDs matter once relations are persisted, but are not required for
range search.

Interchange notes
-----------------

The normalized IB representation is intentionally useful beyond BRAT.

* W3C Web Annotation TextPositionSelector uses the same [start,end) character
  idea, while DataPositionSelector uses [start,end) byte positions.  W3C Web
  Annotation also has an RDF vocabulary/JSON-LD representation, so exporting a
  normalized annotation graph to RDF/Turtle is a serialization problem rather
  than a different text model.
  https://www.w3.org/TR/annotation-model/

* TEI P5 has <standOff>, <span>, <spanGrp>, linking and feature structures.
  Export to TEI still requires a policy for anchors/IDs and vocabulary, but the
  underlying ranges need not be rediscovered.
  https://tei-c.org/release/doc/tei-p5-doc/en/html/ref-standOff.html

* Scholarly and lexicographic projects can use the same separation even when
  TEI/XML remains the publication/interchange form.  A CGLO-like workflow, for
  example, can keep corrected transcription stable and carry page, lemma,
  reference or editorial layers as sidecar ranges, then generate TEI/JSON for
  delivery.  This is an architectural option, not a claim that BRAT replaces
  the richer semantics of TEI.

Offset policy
-------------

BRAT itself specifies UTF-8 text and character offsets.  IB ultimately needs
byte FCs.  Offsets=characters therefore walks the source text once and resolves
only the character positions actually referenced by the .ann file.  It does
not allocate a character-to-byte table for the whole document.

When the record explicitly uses an 8-bit/non-UTF-8 charset, character offsets
are byte offsets.  Offsets=bytes is also available explicitly; this is useful
for byte-oriented stand-off producers and matches the W3C DataPositionSelector
coordinate convention.

References:
  https://brat.nlplab.org/standoff.html
@@@-*/

#include <algorithm>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "brat.hxx"
#include "common.hxx"


namespace {

struct BRAT_RANGE {
  unsigned long long start;
  unsigned long long end;       // exclusive

  BRAT_RANGE() : start(0), end(0) {}
};


struct BRAT_ANNOTATION {
  std::string id;
  std::string type;
  std::vector<BRAT_RANGE> ranges;
};


static bool _brat_read_line(PFILE fp, std::string *line)
{
  if (!fp || !line)
    return false;

  line->clear();

  int ch;
  while ((ch = fgetc(fp)) != EOF)
    {
      if (ch == '\n')
        break;
      if (ch != '\r')
        line->push_back((char)ch);
    }

  return ch != EOF || !line->empty();
}


static bool _brat_has_suffix(const STRING& Filename, const STRING& Suffix)
{
  const std::string name(Filename.c_str());
  const std::string suffix(Suffix.c_str());

  if (suffix.empty() || name.size() < suffix.size())
    return false;

  const size_t start = name.size() - suffix.size();

  for (size_t i = 0; i < suffix.size(); ++i)
    {
      const unsigned char a = (unsigned char)name[start + i];
      const unsigned char b = (unsigned char)suffix[i];

      if (tolower(a) != tolower(b))
        return false;
    }

  return true;
}


#if 1

// Extend to support both file.txt/file.ann as well as file and file.ann 
static STRING _brat_annotation_filename(const STRING& TextFile, const STRING& AnnotationExt)
{
  std::string path(TextFile.c_str());
  const std::string::size_type slash = path.find_last_of("/\\");
  const std::string::size_type dot = path.find_last_of('.');

  if (dot != std::string::npos &&
      (slash == std::string::npos || dot > slash))
    path.erase(dot);

  path += AnnotationExt.c_str();

  // Canonical BRAT convention:
  //     foo.txt -> foo.ann
  STRING annotation(path.c_str());
  if (FileExists(annotation))
    return annotation;

  // Generic sidecar convention:
  //     foo.txt -> foo.txt.ann
  annotation = TextFile;
  annotation.Cat(AnnotationExt);

  if (FileExists(annotation))
    return annotation;

  // Return the canonical BRAT name so the caller's existing diagnostic
  // reports the conventional filename when neither exists.
  return STRING(path.c_str());
}


#else

static STRING _brat_annotation_filename(const STRING& TextFile,
                                        const STRING& AnnotationExt)
{
  std::string path(TextFile.c_str());
  const std::string::size_type slash = path.find_last_of("/\\");
  const std::string::size_type dot = path.find_last_of('.');

  if (dot != std::string::npos &&
      (slash == std::string::npos || dot > slash))
    path.erase(dot);

  path += AnnotationExt.c_str();
  return STRING(path.c_str());
}

#endif

static bool _brat_parse_text_bound(const std::string& line,
                                   BRAT_ANNOTATION *annotation)
{
  if (!annotation || line.empty())
    return false;

  size_t tab1 = line.find('\t');
  if (tab1 == std::string::npos)
    return false;

  annotation->id = line.substr(0, tab1);
  if (annotation->id.empty() || annotation->id[0] != 'T')
    return false;

  size_t tab2 = line.find('\t', tab1 + 1);
  const std::string spec =
      line.substr(tab1 + 1,
                  tab2 == std::string::npos
                    ? std::string::npos
                    : tab2 - tab1 - 1);

  const size_t split = spec.find_first_of(" \t");
  if (split == std::string::npos)
    return false;

  annotation->type = spec.substr(0, split);
  annotation->ranges.clear();

  size_t p = spec.find_first_not_of(" \t", split);
  if (p == std::string::npos)
    return false;

  while (p < spec.size())
    {
      const size_t semi = spec.find(';', p);
      const std::string range =
          spec.substr(p,
                      semi == std::string::npos
                        ? std::string::npos
                        : semi - p);

      std::istringstream in(range);
      BRAT_RANGE r;
      std::string extra;

      if (!(in >> r.start >> r.end) || (in >> extra) ||
          r.end <= r.start)
        return false;

      annotation->ranges.push_back(r);

      if (semi == std::string::npos)
        break;

      p = semi + 1;
      while (p < spec.size() && isspace((unsigned char)spec[p]))
        ++p;
    }

  return !annotation->type.empty() && !annotation->ranges.empty();
}


static void _brat_collect_positions(
    const std::vector<BRAT_ANNOTATION>& annotations,
    std::vector<unsigned long long> *positions)
{
  positions->clear();

  for (size_t i = 0; i < annotations.size(); ++i)
    {
      for (size_t n = 0; n < annotations[i].ranges.size(); ++n)
        {
          positions->push_back(annotations[i].ranges[n].start);
          positions->push_back(annotations[i].ranges[n].end);
        }
    }

  std::sort(positions->begin(), positions->end());
  positions->erase(std::unique(positions->begin(), positions->end()),
                   positions->end());
}


static size_t _brat_utf8_width(unsigned char lead)
{
  if (lead < 0x80)
    return 1;
  if ((lead & 0xE0) == 0xC0)
    return 2;
  if ((lead & 0xF0) == 0xE0)
    return 3;
  if ((lead & 0xF8) == 0xF0)
    return 4;

  // Invalid UTF-8 lead/continuation byte.  Treat it as one unit so that
  // malformed input degrades deterministically instead of losing sync.
  return 1;
}


// Resolve only requested Unicode-code-point positions to byte positions.
// BRAT texts are specified as UTF-8, so this is normally the character mode.
static bool _brat_resolve_utf8_positions(
    PFILE fp,
    const std::vector<unsigned long long>& positions,
    std::map<unsigned long long, unsigned long long> *resolved)
{
  if (!fp || !resolved)
    return false;

  resolved->clear();

  if (fseek(fp, 0, SEEK_SET) != 0)
    return false;

  size_t wanted = 0;
  unsigned long long charPos = 0;
  unsigned long long bytePos = 0;

  while (wanted < positions.size() && positions[wanted] == 0)
    {
      (*resolved)[positions[wanted]] = 0;
      ++wanted;
    }

  int ch;
  while (wanted < positions.size() && (ch = fgetc(fp)) != EOF)
    {
      size_t width = _brat_utf8_width((unsigned char)ch);
      size_t consumed = 1;

      while (consumed < width)
        {
          const int next = fgetc(fp);
          if (next == EOF)
            break;
          ++consumed;
        }

      bytePos += consumed;
      ++charPos;

      while (wanted < positions.size() &&
             positions[wanted] == charPos)
        {
          (*resolved)[positions[wanted]] = bytePos;
          ++wanted;
        }
    }

  return wanted == positions.size();
}


static bool _brat_record_is_utf8(const RECORD& Record)
{
  // A default-constructed RECORD has locale id 0, whose charset accessor
  // happens to look like ASCII.  That is not an explicit request for 8-bit
  // offset semantics.  BRAT itself specifies UTF-8, so an unspecified locale
  // follows BRAT and counts Unicode code points.
  if (!Record.GetLocale().Ok())
    return true;

  const char *charset = Record.GetCharsetCode();

  if (!charset || !*charset)
    return true;

  const STRING name(charset);

  return name.CaseEquals("UTF-8") ||
         name.CaseEquals("UTF8")  ||
         name.CaseEquals("UTF_8") ||
         name.CaseEquals("ISO_10646-1/AnnexD:2000");
}

} // namespace



BRAT::BRAT(PIDBOBJ DbParent, const STRING& Name)
  : DOCTYPE(DbParent, Name), ByteOffsets(false)
{
  AnnotationExt = Getoption("AnnotationExt", ".ann");
  if (AnnotationExt.IsEmpty())
    AnnotationExt = ".ann";
  else if (AnnotationExt.GetChr(1) != '.')
    AnnotationExt.Prepend(".");

  const STRING mode(Getoption("Offsets", "characters"));

  if (mode.CaseEquals("bytes"))
    ByteOffsets = true;
  else if (mode.CaseEquals("characters") ||
           mode.CaseEquals("chars") ||
           mode.CaseEquals("utf8"))
    ByteOffsets = false;
  else
    {
      message_log(LOG_WARN,
                  "%s: unknown Offsets='%s'; using character offsets",
                  Doctype.c_str(), mode.c_str());
      ByteOffsets = false;
    }
}


BRAT::~BRAT()
{
}


const char *BRAT::Description(PSTRLIST List) const
{
  const STRING ThisDoctype("BRAT");

  if (List)
    {
      if (List->IsEmpty() && Doctype != ThisDoctype)
        List->AddEntry(Doctype);

      List->AddEntry(ThisDoctype);
      DOCTYPE::Description(List);
    }

  return
    "BRAT stand-off text annotation document type.\n"
    "Indexes the primary text normally and maps BRAT T annotations to IB\n"
    "field coordinates. Overlapping spans map directly to overlapping FCs;\n"
    "discontinuous BRAT annotations map to one FCT containing several FCs.\n"
    "The first implementation intentionally handles text-bound T records only.\n"
    "Relations, events, attributes, normalization and notes are ignored.\n\n"
    "The same range model is suitable for other stand-off systems, including\n"
    "W3C Web Annotation position selectors and TEI standOff export/import.\n\n"
    "Options:\n"
    "  AnnotationExt=.ann              sidecar extension (default .ann)\n"
    "  Offsets=characters|bytes        default characters\n"
    "Characters are UTF-8 code points unless the record explicitly uses an\n"
    "8-bit/non-UTF-8 charset, in which case characters and bytes coincide.";
}


void BRAT::ParseRecords(const RECORD& FileRecord)
{
  const STRING Filename(FileRecord.GetFullFileName());

  // If a directory/glob containing both halves of a BRAT collection is passed
  // to Iindex, do not index the annotation sidecar as if it were source text.
  if (_brat_has_suffix(Filename, AnnotationExt))
    return;

  DOCTYPE::ParseRecords(FileRecord);
}


void BRAT::AddField(PRECORD Record, const STRING& FieldName,
                    const FCT& Fct)
{
  if (!Record || Fct.IsEmpty())
    return;

  STRING name(UnifiedName(FieldName));

  if (name.IsEmpty())
    return;

  DF df;
  df.SetFieldName(name);
  df.SetFct(Fct);
  Record->AddEntry(df);

  if (Db)
    {
      DFD dfd;
      dfd.SetFieldName(name);
      dfd.SetFieldType(FIELDTYPE::text);
      Db->DfdtAddEntry(dfd);
    }
}


void BRAT::ParseFields(PRECORD NewRecord)
{
  if (!NewRecord || Db == NULL)
    return;

  const STRING TextFile(NewRecord->GetFullFileName());
  const STRING AnnFile(_brat_annotation_filename(TextFile, AnnotationExt));

  PFILE afp = Db->ffopen(AnnFile, "rb");

  if (!afp)
    {
      message_log(LOG_WARN,
                  "%s: no annotation sidecar '%s' for '%s'",
                  Doctype.c_str(), AnnFile.c_str(), TextFile.c_str());
      return;
    }

  std::vector<BRAT_ANNOTATION> annotations;
  std::string line;
  size_t lineNo = 0;
  size_t ignored = 0;

  while (_brat_read_line(afp, &line))
    {
      ++lineNo;

      if (lineNo == 1 && line.size() >= 3 &&
          (unsigned char)line[0] == 0xEF &&
          (unsigned char)line[1] == 0xBB &&
          (unsigned char)line[2] == 0xBF)
        line.erase(0, 3);

      if (line.empty())
        continue;

      if (line[0] != 'T')
        {
          ++ignored;
          continue;
        }

      BRAT_ANNOTATION annotation;

      if (!_brat_parse_text_bound(line, &annotation))
        {
          message_log(LOG_WARN,
                      "%s: malformed BRAT text annotation at %s:%lu",
                      Doctype.c_str(), AnnFile.c_str(),
                      (unsigned long)lineNo);
          continue;
        }

      annotations.push_back(annotation);
    }

  Db->ffclose(afp);

  if (ignored)
    message_log(LOG_DEBUG,
                "%s: ignored %lu non-text-bound BRAT annotations in '%s'",
                Doctype.c_str(), (unsigned long)ignored, AnnFile.c_str());

  if (annotations.empty())
    return;

  PFILE tfp = Db->ffopen(TextFile, "rb");

  if (!tfp)
    {
      message_log(LOG_ERRNO, "%s: cannot open text file '%s'",
                  Doctype.c_str(), TextFile.c_str());
      return;
    }

  const off_t fileSizeOff = GetFileSize(tfp);

  if (fileSizeOff < 0)
    {
      Db->ffclose(tfp);
      return;
    }

  const unsigned long long fileSize =
      (unsigned long long)fileSizeOff;

  const bool utf8Characters =
      !ByteOffsets && _brat_record_is_utf8(*NewRecord);

  std::map<unsigned long long, unsigned long long> byteOffsets;

  if (utf8Characters)
    {
      std::vector<unsigned long long> positions;
      _brat_collect_positions(annotations, &positions);

      if (!_brat_resolve_utf8_positions(tfp, positions, &byteOffsets))
        {
          message_log(LOG_ERROR,
                      "%s: BRAT character offset exceeds UTF-8 text in '%s'",
                      Doctype.c_str(), TextFile.c_str());
          Db->ffclose(tfp);
          return;
        }
    }

  Db->ffclose(tfp);

  const unsigned long long recStart =
      (unsigned long long)NewRecord->GetRecordStart();

  unsigned long long recEnd =
      (unsigned long long)NewRecord->GetRecordEnd();

  if (fileSize && recEnd == 0)
    recEnd = fileSize - 1;

  for (size_t i = 0; i < annotations.size(); ++i)
    {
      FCT fct;
      bool valid = true;

      for (size_t n = 0; n < annotations[i].ranges.size(); ++n)
        {
          unsigned long long start = annotations[i].ranges[n].start;
          unsigned long long end = annotations[i].ranges[n].end;

          if (utf8Characters)
            {
              std::map<unsigned long long, unsigned long long>::const_iterator
                  s = byteOffsets.find(start);
              std::map<unsigned long long, unsigned long long>::const_iterator
                  e = byteOffsets.find(end);

              if (s == byteOffsets.end() || e == byteOffsets.end())
                {
                  valid = false;
                  break;
                }

              start = s->second;
              end = e->second;
            }

          // In an explicitly non-UTF-8/8-bit charset, character positions
          // and bytes are identical; ByteOffsets takes the same path.
          if (end <= start || end > fileSize)
            {
              valid = false;
              break;
            }

          const unsigned long long inclusiveEnd = end - 1;

          if (start < recStart || inclusiveEnd > recEnd)
            {
              valid = false;
              break;
            }

          fct.AddEntry(FC((GPTYPE)(start - recStart),
                          (GPTYPE)(inclusiveEnd - recStart)));
        }

      if (!valid || fct.IsEmpty())
        {
          message_log(LOG_WARN,
                      "%s: skipping out-of-range BRAT annotation '%s' in '%s'",
                      Doctype.c_str(), annotations[i].id.c_str(),
                      AnnFile.c_str());
          continue;
        }

      AddField(NewRecord, STRING(annotations[i].type.c_str()), fct);
    }
}

