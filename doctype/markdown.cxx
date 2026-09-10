/*-
File:           markdown.cxx
Version:        1.00
Description:    Class MARKDOWN - Markdown Document Type
Copyright:      Copyright (c) 2026 re-Isearch Project
                Licensed under the Apache 2.0 license
@@@-*/

#include <ctype.h>
#include <string.h>

#include "markdown.hxx"
#include "common.hxx"


MARKDOWN::MARKDOWN(PIDBOBJ DbParent, const STRING& Name)
  : DOCTYPE(DbParent, Name)
{
  NormalizeEntities = Getoption("NormalizeEntities", "Y").GetBool();
  IgnoreHTMLTags     = Getoption("IgnoreHTMLTags", "Y").GetBool();
}

MARKDOWN::~MARKDOWN() {}


const char *MARKDOWN::Description(PSTRLIST List) const
{
  const STRING ThisDoctype("MARKDOWN");
  if (List) {
    if (List->IsEmpty() && Doctype != ThisDoctype)
      List->AddEntry(Doctype);
    List->AddEntry(ThisDoctype);
    DOCTYPE::Description(List);
  }

  return "Markdown document type.\n\
Indexes ATX headings H1..H6 as nested section fields.\n\
Fenced code blocks are ignored while detecting headings.\n\
Inline Markdown presentation syntax is otherwise treated as plain text.\n\
Options:\n\
  NormalizeEntities=Y|N  (default Y)\n\
  IgnoreHTMLTags=Y|N     (default Y)";
}


void MARKDOWN::SourceMIMEContent(PSTRING StringPtr) const
{
  if (StringPtr)
    *StringPtr = "text/markdown";
}


void MARKDOWN::AddSection(PRECORD Record, int Level, GPTYPE Start, GPTYPE End)
{
  if (!Record || Level < 1 || Level > 6 || End < Start)
    return;

  char namebuf[4];
  snprintf(namebuf, sizeof(namebuf), "H%d", Level);
  STRING name(UnifiedName(namebuf));
  if (name.IsEmpty())
    return;

  FC fc;
  fc.SetFieldStart(Start);
  fc.SetFieldEnd(End);

  FCT fct;
  fct.AddEntry(fc);

  DF df;
  df.SetFieldName(name);
  df.SetFct(fct);
  Record->AddEntry(df);

  if (Db) {
    DFD dfd;
    dfd.SetFieldName(name);
    dfd.SetFieldType(FIELDTYPE::text);
    Db->DfdtAddEntry(dfd);
  }
}


static inline bool _md_space(unsigned char ch)
{
  return ch == ' ' || ch == '\t';
}


static inline bool _md_scheme_char(unsigned char ch)
{
  return isalnum(ch) || ch == '+' || ch == '-' || ch == '.';
}


static bool _md_is_autolink(const UCHR *p, GPTYPE len)
{
  if (!p || len < 3 || p[0] != '<' || p[len-1] != '>')
    return false;

  // Email autolink: deliberately permissive. The point here is to avoid
  // deleting useful address text, not to validate RFC 5322.
  for (GPTYPE i = 1; i + 1 < len; ++i) {
    if (p[i] == '@')
      return true;
  }

  // URI autolink: scheme ':' ...
  GPTYPE i = 1;
  if (!isalpha((unsigned char)p[i]))
    return false;
  ++i;
  while (i + 1 < len && _md_scheme_char((unsigned char)p[i]))
    ++i;
  return i + 1 < len && p[i] == ':';
}


void MARKDOWN::ZapHtmlTags(UCHR *Buffer, GPTYPE Length)
{
  if (!Buffer || Length == 0)
    return;

  for (GPTYPE i = 0; i < Length; ++i) {
    if (Buffer[i] != '<' || i + 1 >= Length)
      continue;

    GPTYPE j = i + 1;
    while (j < Length && Buffer[j] != '>' && Buffer[j] != '\n' && Buffer[j] != '\r')
      ++j;

    if (j >= Length || Buffer[j] != '>')
      continue;

    const GPTYPE span = j - i + 1;
    if (_md_is_autolink(Buffer + i, span)) {
      i = j;
      continue;
    }

    GPTYPE p = i + 1;
    bool looksHtml = false;

    if (Buffer[p] == '/' && p + 1 < j)
      ++p;

    if (p < j && (isalpha((unsigned char)Buffer[p]) || Buffer[p] == '!' || Buffer[p] == '?'))
      looksHtml = true;

    if (!looksHtml)
      continue;

    for (GPTYPE k = i; k <= j; ++k)
      Buffer[k] = ' ';
    i = j;
  }
}


void MARKDOWN::ParseFields(PRECORD NewRecord)
{
  if (!NewRecord || Db == NULL)
    return;

  STRING FileName;
  NewRecord->GetFullFileName(&FileName);

  const GPTYPE recStart = NewRecord->GetRecordStart();
  GPTYPE recEnd = NewRecord->GetRecordEnd();
  if (recEnd == 0) {
    const off_t length = NewRecord->GetLength();
    if (length <= 0)
      return;
    recEnd = recStart + (GPTYPE)length - 1;
  }
  if (recEnd < recStart)
    return;

  const size_t recLen = (size_t)(recEnd - recStart + 1);

  PFILE fp = Db->ffopen(FileName, "rb");
  if (!fp) {
    message_log(LOG_ERRNO, "%s::ParseFields: cannot open '%s'",
                Doctype.c_str(), FileName.c_str());
    return;
  }

  if (fseek(fp, (long)recStart, SEEK_SET) != 0) {
    message_log(LOG_ERRNO, "%s::ParseFields: cannot seek '%s' to %ld",
                Doctype.c_str(), FileName.c_str(), (long)recStart);
    Db->ffclose(fp);
    return;
  }

  UCHR *buf = new UCHR[recLen + 1];
  const size_t nread = fread(buf, 1, recLen, fp);
  Db->ffclose(fp);
  buf[nread] = '\0';
  if (nread == 0) {
    delete [] buf;
    return;
  }

  GPTYPE open[7] = {0,0,0,0,0,0,0};
  bool haveOpen[7] = {false,false,false,false,false,false,false};

  bool inFence = false;
  UCHR fenceChar = 0;
  unsigned fenceLen = 0;

  size_t lineStart = 0;
  while (lineStart < nread) {
    size_t lineEnd = lineStart;
    while (lineEnd < nread && buf[lineEnd] != '\n' && buf[lineEnd] != '\r')
      ++lineEnd;

    size_t p = lineStart;
    unsigned indent = 0;
    while (p < lineEnd && buf[p] == ' ' && indent < 4) {
      ++p;
      ++indent;
    }

    // Fenced code blocks may be indented by at most three spaces.
    if (indent <= 3 && p < lineEnd && (buf[p] == '`' || buf[p] == '~')) {
      const UCHR ch = buf[p];
      size_t q = p;
      while (q < lineEnd && buf[q] == ch)
        ++q;
      const unsigned run = (unsigned)(q - p);

      if (run >= 3) {
        if (!inFence) {
          inFence = true;
          fenceChar = ch;
          fenceLen = run;
        }
        else if (ch == fenceChar && run >= fenceLen) {
          bool onlySpace = true;
          for (size_t r = q; r < lineEnd; ++r) {
            if (!_md_space(buf[r])) {
              onlySpace = false;
              break;
            }
          }
          if (onlySpace) {
            inFence = false;
            fenceChar = 0;
            fenceLen = 0;
          }
        }

        goto next_line;
      }
    }

    if (!inFence && indent <= 3 && p < lineEnd && buf[p] == '#') {
      size_t q = p;
      while (q < lineEnd && buf[q] == '#')
        ++q;
      const int level = (int)(q - p);

      if (level >= 1 && level <= 6 &&
          (q == lineEnd || _md_space(buf[q]))) {
        const GPTYPE headingStart = (GPTYPE)lineStart;

        // A new Hn terminates Hn and every deeper open section. The parent
        // sections stay open until a heading at their own or a higher level.
        for (int l = 6; l >= level; --l) {
          if (haveOpen[l]) {
            const GPTYPE end = headingStart > 0 ? headingStart - 1 : 0;
            AddSection(NewRecord, l, open[l], end);
            haveOpen[l] = false;
          }
        }

        open[level] = headingStart;
        haveOpen[level] = true;
      }
    }

next_line:
    if (lineEnd >= nread)
      break;
    if (buf[lineEnd] == '\r' && lineEnd + 1 < nread && buf[lineEnd+1] == '\n')
      lineStart = lineEnd + 2;
    else
      lineStart = lineEnd + 1;
  }

  const GPTYPE eof = (GPTYPE)nread - 1;
  for (int l = 6; l >= 1; --l) {
    if (haveOpen[l])
      AddSection(NewRecord, l, open[l], eof);
  }

  delete [] buf;
}


GPTYPE MARKDOWN::ParseWords(UCHR *DataBuffer, GPTYPE DataLength,
                            GPTYPE DataOffset, GPTYPE *GpBuffer,
                            GPTYPE GpLength)
{
  if (IgnoreHTMLTags)
    ZapHtmlTags(DataBuffer, DataLength);

  if (NormalizeEntities)
    Entities.normalize((char *)DataBuffer, DataLength);

  return DOCTYPE::ParseWords(DataBuffer, DataLength,
                             DataOffset, GpBuffer, GpLength);
}
