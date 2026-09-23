/*-
File:           tagml.cxx
Version:        1.00
Description:    Class TAGML - Text-As-Graph Markup Language
Copyright:      Copyright (c) 2026 re-Isearch Project
                Licensed under the Apache 2.0 license

Notes:
  This is deliberately an indexing-oriented TAGML parser.  It maps markup
  ranges directly to IB field coordinates, so overlapping markup needs no
  tree representation.  The initial implementation is lexical: all fields
  are text fields.  Type inference can be layered on later without changing
  the TAGML range parser.
@@@-*/

#include <ctype.h>
#include <string.h>

#include <map>
#include <string>
#include <vector>

#include "tagml.hxx"
#include "common.hxx"


namespace {

enum TOKEN_KIND {
  TOK_NONE = 0,
  TOK_START,
  TOK_END,
  TOK_MILESTONE,
  TOK_COMMENT,
  TOK_NAMESPACE,
  TOK_VARIATION_START,
  TOK_VARIATION_END
};


struct ANNOTATION_SPAN {
  std::string name;
  size_t      start;
  size_t      end;

  ANNOTATION_SPAN() : start(0), end(0) {}
};


struct TAGML_TOKEN {
  TOKEN_KIND kind;
  size_t     start;
  size_t     end;

  std::string identifier;

  bool optional;
  bool resume;
  bool suspend;
  bool unsupportedAnnotations;

  size_t payloadStart;
  size_t payloadEnd;

  std::vector<ANNOTATION_SPAN> annotations;

  TAGML_TOKEN()
    : kind(TOK_NONE), start(0), end(0),
      optional(false), resume(false), suspend(false),
      unsupportedAnnotations(false),
      payloadStart(0), payloadEnd(0)
  {}
};


struct MARKUP_STATE {
  STRING fieldName;
  FCT    ranges;
  GPTYPE segmentStart;

  MARKUP_STATE() : segmentStart(0) {}
};


static inline bool _tagml_space(unsigned char ch)
{
  return isspace(ch) != 0;
}


static inline bool _tagml_name_char(unsigned char ch)
{
  return isalnum(ch) || ch == '_' || ch == '-' || ch == ':';
}


static bool _tagml_escaped(const UCHR *buf, size_t pos)
{
  size_t n = 0;

  while (pos > 0 && buf[pos - 1] == '\\')
    {
      --pos;
      ++n;
    }

  return (n & 1) != 0;
}


static std::string _tagml_base_name(const std::string& identifier)
{
  const std::string::size_type p = identifier.find('|');

  if (p == std::string::npos)
    return identifier;

  return identifier.substr(0, p);
}


static STRING _tagml_field_name(const std::string& identifier)
{
  const std::string name(_tagml_base_name(identifier));
  return STRING(name.c_str());
}


static bool _tagml_find_comment_end(const UCHR *buf, size_t len,
                                    size_t start, size_t *end)
{
  for (size_t p = start; p + 1 < len; ++p)
    {
      if (buf[p] == '!' && buf[p + 1] == ']' &&
          !_tagml_escaped(buf, p))
        {
          *end = p + 1;
          return true;
        }
    }

  return false;
}


static bool _tagml_find_namespace_end(const UCHR *buf, size_t len,
                                      size_t start, size_t *end)
{
  for (size_t p = start; p < len; ++p)
    {
      if (buf[p] == ']' && !_tagml_escaped(buf, p))
        {
          *end = p;
          return true;
        }
    }

  return false;
}


// Find the terminator of an opening tag or milestone.
//
// Nested [] and {} are counted so that list/object/rich-text annotation
// values do not accidentally terminate the outer tag.  We don't interpret
// those values yet, but we can safely step over them.
static bool _tagml_find_open_end(const UCHR *buf, size_t len,
                                 size_t start, size_t *end,
                                 bool *milestone)
{
  size_t squareDepth = 0;
  size_t objectDepth = 0;
  UCHR quote = 0;

  for (size_t p = start + 1; p < len; ++p)
    {
      const UCHR ch = buf[p];

      if (quote)
        {
          if (ch == '\\' && p + 1 < len)
            {
              ++p;
              continue;
            }
          if (ch == quote)
            quote = 0;
          continue;
        }

      if (ch == '\\' && p + 1 < len)
        {
          ++p;
          continue;
        }

      if (ch == '\'' || ch == '"')
        {
          quote = ch;
          continue;
        }

      if (ch == '[')
        {
          ++squareDepth;
          continue;
        }

      if (ch == ']')
        {
          if (squareDepth)
            {
              --squareDepth;
              continue;
            }

          if (objectDepth == 0)
            {
              *end = p;
              *milestone = true;
              return true;
            }
        }

      if (ch == '{')
        {
          ++objectDepth;
          continue;
        }

      if (ch == '}')
        {
          if (objectDepth)
            --objectDepth;
          continue;
        }

      if (ch == '>' && squareDepth == 0 && objectDepth == 0)
        {
          *end = p;
          *milestone = false;
          return true;
        }
    }

  return false;
}


static void _tagml_parse_annotations(const UCHR *buf,
                                     size_t begin, size_t end,
                                     TAGML_TOKEN *token)
{
  size_t p = begin;

  while (p < end)
    {
      while (p < end && _tagml_space(buf[p]))
        ++p;

      if (p >= end)
        break;

      const size_t nameStart = p;

      // :id is used by TAGML even though ':' is not in the older grammar's
      // annotationName production.  Accept ':' here deliberately.
      if (buf[p] == ':')
        ++p;

      while (p < end)
        {
          if (buf[p] == '-' && p + 1 < end && buf[p + 1] == '>')
            break;
          if (!_tagml_name_char(buf[p]))
            break;
          ++p;
        }

      if (p == nameStart)
        {
          ++p;
          continue;
        }

      std::string name(reinterpret_cast<const char *>(buf + nameStart),
                       p - nameStart);

      bool reference = false;

      if (p + 1 < end && buf[p] == '-' && buf[p + 1] == '>')
        {
          reference = true;
          p += 2;
        }
      else
        {
          while (p < end && _tagml_space(buf[p]))
            ++p;

          if (p >= end || buf[p] != '=')
            {
              while (p < end && !_tagml_space(buf[p]))
                ++p;
              continue;
            }

          ++p;

          while (p < end && _tagml_space(buf[p]))
            ++p;
        }

      if (p >= end)
        break;

      ANNOTATION_SPAN span;
      span.name = name;

      if (!span.name.empty() && span.name[0] == ':')
        span.name.erase(0, 1);

      if (buf[p] == '\'' || buf[p] == '"')
        {
          const UCHR quote = buf[p++];
          const size_t valueStart = p;

          while (p < end)
            {
              if (buf[p] == '\\' && p + 1 < end)
                {
                  p += 2;
                  continue;
                }

              if (buf[p] == quote)
                break;

              ++p;
            }

          if (p > valueStart)
            {
              span.start = valueStart;
              span.end = p - 1;
              token->annotations.push_back(span);
            }

          if (p < end && buf[p] == quote)
            ++p;

          continue;
        }

      // Nested objects, lists and rich-text annotation values are recognized
      // by the outer token scanner, but not interpreted in v1.
      if (!reference && (buf[p] == '{' || buf[p] == '['))
        {
          token->unsupportedAnnotations = true;
          break;
        }

      const size_t valueStart = p;

      while (p < end && !_tagml_space(buf[p]))
        ++p;

      if (p > valueStart)
        {
          span.start = valueStart;
          span.end = p - 1;
          token->annotations.push_back(span);
        }
    }
}


static bool _tagml_parse_token(const UCHR *buf, size_t len,
                               size_t pos, TAGML_TOKEN *token)
{
  if (!buf || !token || pos >= len || _tagml_escaped(buf, pos))
    return false;

  *token = TAGML_TOKEN();
  token->start = pos;

  if (buf[pos] == '[')
    {
      if (pos + 1 >= len)
        return false;

      // Namespace declaration is syntactically close to a comment but has a
      // different terminator.
      if (pos + 4 < len &&
          buf[pos + 1] == '!' &&
          buf[pos + 2] == 'n' &&
          buf[pos + 3] == 's' &&
          _tagml_space(buf[pos + 4]))
        {
          size_t end;

          if (!_tagml_find_namespace_end(buf, len, pos + 5, &end))
            return false;

          token->kind = TOK_NAMESPACE;
          token->end = end;
          return true;
        }

      if (buf[pos + 1] == '!')
        {
          size_t end;

          if (!_tagml_find_comment_end(buf, len, pos + 2, &end))
            return false;

          token->kind = TOK_COMMENT;
          token->end = end;
          token->payloadStart = pos + 2;
          token->payloadEnd = end >= 2 ? end - 2 : pos + 1;
          return true;
        }

      size_t end;
      bool milestone = false;

      if (!_tagml_find_open_end(buf, len, pos, &end, &milestone))
        return false;

      size_t p = pos + 1;

      if (p < end && buf[p] == '?')
        {
          token->optional = true;
          ++p;
        }
      else if (p < end && buf[p] == '+')
        {
          token->resume = true;
          ++p;
        }

      const size_t nameStart = p;

      while (p < end && !_tagml_space(buf[p]))
        ++p;

      if (p == nameStart)
        return false;

      token->identifier.assign(
          reinterpret_cast<const char *>(buf + nameStart),
          p - nameStart);

      token->kind = milestone ? TOK_MILESTONE : TOK_START;
      token->end = end;

      _tagml_parse_annotations(buf, p, end, token);
      return true;
    }


  if (buf[pos] == '<')
    {
      if (pos + 1 < len && buf[pos + 1] == '|')
        {
          token->kind = TOK_VARIATION_START;
          token->end = pos + 1;
          return true;
        }

      size_t end = pos + 1;

      while (end < len)
        {
          if (buf[end] == ']' && !_tagml_escaped(buf, end))
            break;
          ++end;
        }

      if (end >= len)
        return false;

      size_t p = pos + 1;

      if (p < end && buf[p] == '?')
        {
          token->optional = true;
          ++p;
        }
      else if (p < end && buf[p] == '-')
        {
          token->suspend = true;
          ++p;
        }

      const size_t nameStart = p;

      while (p < end && !_tagml_space(buf[p]))
        ++p;

      while (p > nameStart && _tagml_space(buf[p - 1]))
        --p;

      if (p == nameStart)
        return false;

      token->identifier.assign(
          reinterpret_cast<const char *>(buf + nameStart),
          p - nameStart);

      token->kind = TOK_END;
      token->end = end;
      return true;
    }


  if (buf[pos] == '|' && pos + 1 < len && buf[pos + 1] == '>')
    {
      token->kind = TOK_VARIATION_END;
      token->end = pos + 1;
      return true;
    }

  return false;
}


template <class MAP>
static std::string _tagml_resolve_key(const MAP& table,
                                      const std::string& identifier)
{
  typename MAP::const_iterator exact = table.find(identifier);

  if (exact != table.end() && !exact->second.empty())
    return identifier;

  const std::string base(_tagml_base_name(identifier));
  std::string found;

  for (typename MAP::const_iterator it = table.begin();
       it != table.end(); ++it)
    {
      if (it->second.empty())
        continue;

      if (_tagml_base_name(it->first) == base)
        {
          if (!found.empty())
            return std::string(); // Ambiguous self-overlap: require layer.
          found = it->first;
        }
    }

  return found;
}


static bool _tagml_preserve_annotation_byte(const TAGML_TOKEN& token,
                                            size_t pos)
{
  for (size_t i = 0; i < token.annotations.size(); ++i)
    {
      if (pos >= token.annotations[i].start &&
          pos <= token.annotations[i].end)
        return true;
    }

  return false;
}


static void _tagml_blank(UCHR *buf, size_t start, size_t end)
{
  if (!buf || end < start)
    return;

  for (size_t p = start; p <= end; ++p)
    buf[p] = ' ';
}

} // namespace



TAGML::TAGML(PIDBOBJ DbParent, const STRING& Name)
  : DOCTYPE(DbParent, Name)
{
  CommentField = Getoption("CommentField", "COMMENTS");
  if (CommentField.IsEmpty())
    CommentField = "COMMENTS";
}


TAGML::~TAGML()
{
}


const char *TAGML::Description(PSTRLIST List) const
{
  const STRING ThisDoctype("TAGML");

  if (List)
    {
      if (List->IsEmpty() && Doctype != ThisDoctype)
        List->AddEntry(Doctype);

      List->AddEntry(ThisDoctype);
      DOCTYPE::Description(List);
    }

  return
    "TAGML (Text-As-Graph Markup Language) document type.\n"
    "Maps TAGML markup ranges directly to internal field coordinates and\n"
    "supports overlap, layers and suspend/resume discontinuity without imposing\n"
    "an XML-style tree. Simple annotation values are indexed in TAG@ANNOTATION\n"
    "fields; comments are indexed in COMMENTS by default. All fields are\n"
    "lexical text fields in this initial implementation.\n\n"
    "Multiple independent top-level TAGML markup regions in one file are\n"
    "treated as separate records. Namespace declarations and comments do\n"
    "not themselves start records.\n\n"
    "Text-variation branches and nested/list/rich-text annotation values are\n"
    "recognized but their higher-level semantics are not interpreted yet.\n\n"
    "Options:\n"
    "  CommentField=<field>  (default COMMENTS)";
}


void TAGML::SourceMIMEContent(PSTRING StringPtr) const
{
  if (StringPtr)
    *StringPtr = "text/tagml";
}


void TAGML::AddField(PRECORD Record, const STRING& FieldName,
                     GPTYPE Start, GPTYPE End)
{
  if (!Record || End < Start)
    return;

  FCT fct;
  fct.AddEntry(FC(Start, End));
  AddField(Record, FieldName, fct);
}


void TAGML::AddField(PRECORD Record, const STRING& FieldName,
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


void TAGML::ParseRecords(const RECORD& FileRecord)
{
  const STRING FileName(FileRecord.GetFullFileName());

  PFILE fp = Db->ffopen(FileName, "rb");

  if (!fp)
    {
      message_log(LOG_ERRNO, "%s::ParseRecords: cannot open '%s'",
                  Doctype.c_str(), FileName.c_str());
      return;
    }

  off_t recStart = FileRecord.GetRecordStart();
  off_t recEnd = FileRecord.GetRecordEnd();
  const off_t fileSize = GetFileSize(fp);

  if (recStart < 0)
    recStart = 0;

  if (recEnd == 0 || recEnd >= fileSize)
    recEnd = fileSize - 1;

  if (fileSize <= 0 || recEnd < recStart)
    {
      Db->ffclose(fp);
      return;
    }

  const size_t length = (size_t)(recEnd - recStart + 1);

  if (fseek(fp, (long)recStart, SEEK_SET) != 0)
    {
      message_log(LOG_ERRNO, "%s::ParseRecords: cannot seek '%s'",
                  Doctype.c_str(), FileName.c_str());
      Db->ffclose(fp);
      return;
    }

  std::vector<UCHR> buffer(length + 1);
  const size_t got = fread(&buffer[0], 1, length, fp);
  Db->ffclose(fp);

  if (got == 0)
    return;

  buffer[got] = 0;

  std::map<std::string, std::vector<int> > active;
  std::map<std::string, std::vector<int> > suspended;

  size_t activeTotal = 0;
  size_t suspendedTotal = 0;

  std::vector<size_t> topStarts;

  bool outsideText = false;
  bool malformed = false;

  for (size_t p = 0; p < got; )
    {
      TAGML_TOKEN token;

      if (_tagml_parse_token(&buffer[0], got, p, &token))
        {
          switch (token.kind)
            {
            case TOK_NAMESPACE:
            case TOK_COMMENT:
              // Legal between records; attached to the preceding record by
              // the byte-range splitter below (or the first record as header).
              break;

            case TOK_START:
              if (token.resume)
                {
                  std::string key =
                      _tagml_resolve_key(suspended, token.identifier);

                  if (key.empty())
                    {
                      malformed = true;
                      break;
                    }

                  suspended[key].pop_back();
                  --suspendedTotal;
                  active[key].push_back(1);
                  ++activeTotal;
                }
              else
                {
                  if (activeTotal == 0 && suspendedTotal == 0)
                    topStarts.push_back(token.start);

                  active[token.identifier].push_back(1);
                  ++activeTotal;
                }
              break;

            case TOK_END:
              {
                std::string key =
                    _tagml_resolve_key(active, token.identifier);

                if (key.empty())
                  {
                    malformed = true;
                    break;
                  }

                active[key].pop_back();
                --activeTotal;

                if (token.suspend)
                  {
                    suspended[key].push_back(1);
                    ++suspendedTotal;
                  }
              }
              break;

            case TOK_MILESTONE:
              if (activeTotal == 0 && suspendedTotal == 0)
                outsideText = true;
              break;

            case TOK_VARIATION_START:
            case TOK_VARIATION_END:
              if (activeTotal == 0 && suspendedTotal == 0)
                outsideText = true;
              break;

            default:
              break;
            }

          if (malformed)
            break;

          p = token.end + 1;
          continue;
        }

      if (activeTotal == 0 && suspendedTotal == 0 &&
          !_tagml_space(buffer[p]))
        outsideText = true;

      ++p;
    }

  if (activeTotal || suspendedTotal)
    malformed = true;


  // Only split when there is an unambiguous sequence of two or more
  // independent top-level markup regions.  Otherwise retain normal
  // one-file/one-record behaviour.
  if (malformed || outsideText || topStarts.size() < 2)
    {
      RECORD Record(FileRecord);
      Record.SetRecordStart(recStart);
      Record.SetRecordEnd(recStart + (off_t)got - 1);
      Db->DocTypeAddRecord(Record);
      return;
    }


  RECORD Record(FileRecord);

  for (size_t i = 0; i < topStarts.size(); ++i)
    {
      const off_t start =
          (i == 0)
            ? recStart
            : recStart + (off_t)topStarts[i];

      const off_t end =
          (i + 1 < topStarts.size())
            ? recStart + (off_t)topStarts[i + 1] - 1
            : recStart + (off_t)got - 1;

      if (end < start)
        continue;

      Record.SetRecordStart(start);
      Record.SetRecordEnd(end);
      Db->DocTypeAddRecord(Record);
    }
}


void TAGML::ParseFields(PRECORD NewRecord)
{
  if (!NewRecord || Db == NULL)
    return;

  STRING FileName;
  NewRecord->GetFullFileName(&FileName);

  const GPTYPE recStart = NewRecord->GetRecordStart();
  GPTYPE recEnd = NewRecord->GetRecordEnd();

  PFILE fp = Db->ffopen(FileName, "rb");

  if (!fp)
    {
      message_log(LOG_ERRNO, "%s::ParseFields: cannot open '%s'",
                  Doctype.c_str(), FileName.c_str());
      return;
    }

  if (recEnd == 0)
    {
      const off_t fileSize = GetFileSize(fp);
      if (fileSize <= (off_t)recStart)
        {
          Db->ffclose(fp);
          return;
        }
      recEnd = (GPTYPE)fileSize - 1;
    }

  if (recEnd < recStart)
    {
      Db->ffclose(fp);
      return;
    }

  const size_t recLen = (size_t)(recEnd - recStart + 1);

  if (fseek(fp, (long)recStart, SEEK_SET) != 0)
    {
      message_log(LOG_ERRNO, "%s::ParseFields: cannot seek '%s'",
                  Doctype.c_str(), FileName.c_str());
      Db->ffclose(fp);
      return;
    }

  std::vector<UCHR> buffer(recLen + 1);
  const size_t got = fread(&buffer[0], 1, recLen, fp);
  Db->ffclose(fp);

  if (got == 0)
    return;

  buffer[got] = 0;

  std::map<std::string, std::vector<MARKUP_STATE> > active;
  std::map<std::string, std::vector<MARKUP_STATE> > suspended;

  bool warnedVariation = false;
  bool warnedComplexAnnotation = false;

  for (size_t p = 0; p < got; )
    {
      TAGML_TOKEN token;

      if (!_tagml_parse_token(&buffer[0], got, p, &token))
        {
          ++p;
          continue;
        }

      if (token.unsupportedAnnotations && !warnedComplexAnnotation)
        {
          message_log(LOG_WARN,
                      "%s: nested/list/rich TAGML annotation values are "
                      "not interpreted yet in '%s'",
                      Doctype.c_str(), FileName.c_str());
          warnedComplexAnnotation = true;
        }

      if (token.kind == TOK_COMMENT)
        {
          if (token.payloadEnd >= token.payloadStart)
            AddField(NewRecord, CommentField,
                     (GPTYPE)token.payloadStart,
                     (GPTYPE)token.payloadEnd);

          p = token.end + 1;
          continue;
        }

      if (token.kind == TOK_VARIATION_START ||
          token.kind == TOK_VARIATION_END)
        {
          if (!warnedVariation)
            {
              message_log(LOG_WARN,
                          "%s: TAGML text variation is indexed linearly; "
                          "branch semantics are not interpreted yet in '%s'",
                          Doctype.c_str(), FileName.c_str());
              warnedVariation = true;
            }

          p = token.end + 1;
          continue;
        }

      if (token.kind == TOK_START || token.kind == TOK_MILESTONE)
        {
          const STRING baseName(_tagml_field_name(token.identifier));

          for (size_t n = 0; n < token.annotations.size(); ++n)
            {
              STRING annotationField(baseName);
              annotationField.Cat("@");
              annotationField.Cat(token.annotations[n].name.c_str());

              AddField(NewRecord, annotationField,
                       (GPTYPE)token.annotations[n].start,
                       (GPTYPE)token.annotations[n].end);
            }

          if (token.kind == TOK_MILESTONE)
            {
              p = token.end + 1;
              continue;
            }

          if (token.resume)
            {
              const std::string key =
                  _tagml_resolve_key(suspended, token.identifier);

              if (key.empty())
                {
                  message_log(LOG_WARN,
                              "%s: resume of non-suspended TAGML markup '%s' "
                              "at offset %lu in '%s'",
                              Doctype.c_str(), token.identifier.c_str(),
                              (unsigned long)token.start, FileName.c_str());
                  p = token.end + 1;
                  continue;
                }

              MARKUP_STATE state(suspended[key].back());
              suspended[key].pop_back();

              state.segmentStart = (GPTYPE)token.end + 1;
              active[key].push_back(state);
            }
          else
            {
              MARKUP_STATE state;
              state.fieldName = baseName;
              state.segmentStart = (GPTYPE)token.end + 1;
              active[token.identifier].push_back(state);
            }

          p = token.end + 1;
          continue;
        }


      if (token.kind == TOK_END)
        {
          const std::string key =
              _tagml_resolve_key(active, token.identifier);

          if (key.empty())
            {
              message_log(LOG_WARN,
                          "%s: close of non-open TAGML markup '%s' at "
                          "offset %lu in '%s'",
                          Doctype.c_str(), token.identifier.c_str(),
                          (unsigned long)token.start, FileName.c_str());
              p = token.end + 1;
              continue;
            }

          MARKUP_STATE state(active[key].back());
          active[key].pop_back();

          if ((GPTYPE)token.start > state.segmentStart)
            state.ranges.AddEntry(
                FC(state.segmentStart, (GPTYPE)token.start - 1));

          if (token.suspend)
            {
              suspended[key].push_back(state);
            }
          else
            {
              AddField(NewRecord, state.fieldName, state.ranges);
            }

          p = token.end + 1;
          continue;
        }


      p = token.end + 1;
    }


  // Preserve already completed segments of malformed discontinuous markup,
  // but do not invent a range for an unclosed current segment.
  for (std::map<std::string, std::vector<MARKUP_STATE> >::iterator it =
           active.begin(); it != active.end(); ++it)
    {
      for (size_t n = 0; n < it->second.size(); ++n)
        {
          if (!it->second[n].ranges.IsEmpty())
            AddField(NewRecord, it->second[n].fieldName,
                     it->second[n].ranges);

          message_log(LOG_WARN,
                      "%s: unclosed TAGML markup '%s' in '%s'",
                      Doctype.c_str(), it->first.c_str(), FileName.c_str());
        }
    }

  for (std::map<std::string, std::vector<MARKUP_STATE> >::iterator it =
           suspended.begin(); it != suspended.end(); ++it)
    {
      for (size_t n = 0; n < it->second.size(); ++n)
        {
          if (!it->second[n].ranges.IsEmpty())
            AddField(NewRecord, it->second[n].fieldName,
                     it->second[n].ranges);

          message_log(LOG_WARN,
                      "%s: suspended TAGML markup '%s' was not resumed in '%s'",
                      Doctype.c_str(), it->first.c_str(), FileName.c_str());
        }
    }
}


void TAGML::BlotSyntax(UCHR *Buffer, GPTYPE Length)
{
  if (!Buffer || Length == 0)
    return;

  const size_t len = (size_t)Length;

  for (size_t p = 0; p < len; )
    {
      TAGML_TOKEN token;

      if (!_tagml_parse_token(Buffer, len, p, &token))
        {
          ++p;
          continue;
        }

      switch (token.kind)
        {
        case TOK_COMMENT:
          // Keep the payload lexical/searchable, but remove comment syntax.
          if (token.payloadStart > token.start)
            _tagml_blank(Buffer, token.start, token.payloadStart - 1);
          if (token.payloadEnd < token.end)
            _tagml_blank(Buffer, token.payloadEnd + 1, token.end);
          break;

        case TOK_NAMESPACE:
        case TOK_END:
          _tagml_blank(Buffer, token.start, token.end);
          break;

        case TOK_START:
        case TOK_MILESTONE:
          // Keep simple annotation values lexical so their field coordinates
          // have term positions to search.  Everything else in the tag is
          // markup syntax and is blotted.
          for (size_t q = token.start; q <= token.end; ++q)
            {
              if (!_tagml_preserve_annotation_byte(token, q))
                Buffer[q] = ' ';
            }
          break;

        case TOK_VARIATION_START:
        case TOK_VARIATION_END:
          // v1 indexes all branches linearly; only the branch delimiters go.
          _tagml_blank(Buffer, token.start, token.end);
          break;

        default:
          break;
        }

      p = token.end + 1;
    }
}


GPTYPE TAGML::ParseWords(UCHR *DataBuffer, GPTYPE DataLength,
                         GPTYPE DataOffset, GPTYPE *GpBuffer,
                         GPTYPE GpLength)
{
  BlotSyntax(DataBuffer, DataLength);

  return DOCTYPE::ParseWords(DataBuffer, DataLength,
                             DataOffset, GpBuffer, GpLength);
}


INT TAGML::GetTerm(const STRING& Filename, CHR *Buffer,
                   off_t Offset, size_t Length)
{
  INT count = DOCTYPE::GetTerm(Filename, Buffer, Offset, Length);

  if (count <= 0)
    return count;

  BlotSyntax(reinterpret_cast<UCHR *>(Buffer), (GPTYPE)count);

  return count;
}
