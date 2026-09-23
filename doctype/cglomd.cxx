#include <ctype.h>
#include <string.h>
#include <vector>

#include "cglomd.hxx"
#include "common.hxx"


const char *CGLOMD::Description(PSTRLIST List) const
{
  const STRING ThisDoctype("CGLOMD");

  if (List)
    {
      if (List->IsEmpty() && Doctype != ThisDoctype)
        List->AddEntry(Doctype);

      List->AddEntry(ThisDoctype);
      MARKDOWN::Description(List);
    }

  return
    "CGLO Markdown document type.\n"
    "Markdown with embedded CGLO XML section markers.\n"
    "Each <div type=\"section\" ...> begins a separate record.\n"
    "Markdown parsing and indexing are inherited from MARKDOWN.";
}



//
// Is p pointing at:
//
//     <div ... type="section" ...>
//
// Attribute order and whitespace around '=' do not matter.
//
static bool IsCgloSectionDiv(const char *p, const char *end)
{
  if (!p || !end || end - p < 4)
    return false;

  if (memcmp(p, "<div", 4) != 0)
    return false;

  p += 4;

  // Must really be a div tag, not <division> etc.
  if (p < end && *p != '>' && !isspace((unsigned char)*p))
    return false;

  // Find end of opening tag.
  const char *gt = p;
  while (gt < end && *gt != '>' &&
         *gt != '\n' && *gt != '\r')
    ++gt;

  if (gt >= end || *gt != '>')
    return false;

  //
  // Search attributes for:
  //
  //      type = "section"
  //      type = 'section'
  //
  while (p < gt)
    {
      while (p < gt && isspace((unsigned char)*p))
        ++p;

      if (p >= gt)
        break;

      const char *name = p;

      while (p < gt &&
             (isalnum((unsigned char)*p) ||
              *p == '_' || *p == '-' || *p == ':' || *p == '.'))
        ++p;

      const size_t nameLen = (size_t)(p - name);

      while (p < gt && isspace((unsigned char)*p))
        ++p;

      if (p >= gt || *p != '=')
        {
          // Skip malformed/bare attribute.
          while (p < gt && !isspace((unsigned char)*p))
            ++p;
          continue;
        }

      ++p; // '='

      while (p < gt && isspace((unsigned char)*p))
        ++p;

      if (p >= gt)
        break;

      char quote = 0;

      if (*p == '"' || *p == '\'')
        quote = *p++;

      const char *value = p;

      if (quote)
        {
          while (p < gt && *p != quote)
            ++p;
        }
      else
        {
          while (p < gt && !isspace((unsigned char)*p) && *p != '>')
            ++p;
        }

      const size_t valueLen = (size_t)(p - value);

      if (nameLen == 4 &&
          memcmp(name, "type", 4) == 0 &&
          valueLen == 7 &&
          memcmp(value, "section", 7) == 0)
        return true;

      if (quote && p < gt && *p == quote)
        ++p;
    }

  return false;
}


void CGLOMD::ParseRecords(const RECORD& FileRecord)
{
  const STRING FileName(FileRecord.GetFullFileName());

  PFILE fp = Db->ffopen(FileName, "rb");
  if (!fp)
    {
      message_log(LOG_ERRNO,
                  "%s::ParseRecords: cannot open '%s'",
                  Doctype.c_str(), FileName.c_str());
      return;
    }

  //
  // Determine the physical file size.
  //
  if (fseek(fp, 0L, SEEK_END) != 0)
    {
      message_log(LOG_ERRNO,
                  "%s::ParseRecords: cannot seek '%s'",
                  Doctype.c_str(), FileName.c_str());
      Db->ffclose(fp);
      return;
    }

  const long physicalSize = ftell(fp);

  if (physicalSize <= 0)
    {
      Db->ffclose(fp);
      return;
    }

  off_t RecStart = FileRecord.GetRecordStart();
  off_t RecEnd   = FileRecord.GetRecordEnd();

  if (RecStart < 0)
    RecStart = 0;

  if (RecEnd == 0 || RecEnd >= (off_t)physicalSize)
    RecEnd = (off_t)physicalSize - 1;

  if (RecStart > RecEnd)
    {
      Db->ffclose(fp);
      return;
    }

  const size_t RecLength =
      (size_t)(RecEnd - RecStart + 1);

  if (fseek(fp, (long)RecStart, SEEK_SET) != 0)
    {
      message_log(LOG_ERRNO,
                  "%s::ParseRecords: cannot seek '%s' to %ld",
                  Doctype.c_str(),
                  FileName.c_str(),
                  (long)RecStart);
      Db->ffclose(fp);
      return;
    }

  std::vector<char> buffer(RecLength + 1);

  const size_t got =
      fread(&buffer[0], 1, RecLength, fp);

  Db->ffclose(fp);

  if (got == 0)
    return;

  buffer[got] = '\0';

  //
  // Absolute offsets of:
  //
  //      <div type="section" ...>
  //
  std::vector<off_t> sections;

  size_t lineStart = 0;

  while (lineStart < got)
    {
      size_t lineEnd = lineStart;

      while (lineEnd < got &&
             buffer[lineEnd] != '\n' &&
             buffer[lineEnd] != '\r')
        ++lineEnd;

      //
      // Permit indentation before the <div>.
      //
      size_t p = lineStart;

      while (p < lineEnd &&
             (buffer[p] == ' ' || buffer[p] == '\t'))
        ++p;

      if (p < lineEnd &&
          IsCgloSectionDiv(&buffer[p],
                           &buffer[lineEnd]))
        {
          sections.push_back(RecStart + (off_t)p);
        }

      //
      // Advance over CR/LF/CRLF.
      //
      if (lineEnd >= got)
        break;

      if (buffer[lineEnd] == '\r' &&
          lineEnd + 1 < got &&
          buffer[lineEnd + 1] == '\n')
        lineStart = lineEnd + 2;
      else
        lineStart = lineEnd + 1;
    }


  //
  // Not CGLO-style combined Markdown.
  //
  // Just index it as one normal Markdown record.
  //
  if (sections.empty())
    {
      RECORD Record(FileRecord);

      Record.SetRecordStart(RecStart);
      Record.SetRecordEnd(RecStart + (off_t)got - 1);

      Db->DocTypeAddRecord(Record);
      return;
    }


  //
  // Each <div type="section"...> begins a record.
  //
  // Any material before the first section -- e.g. their odd <head>
  // preamble -- is intentionally NOT indexed as a record.
  //
  RECORD Record(FileRecord);

  for (size_t i = 0; i < sections.size(); ++i)
    {
      const off_t start = sections[i];

      const off_t end =
          (i + 1 < sections.size())
            ? sections[i + 1] - 1
            : RecStart + (off_t)got - 1;

      if (end < start)
        continue;

      Record.SetRecordStart(start);
      Record.SetRecordEnd(end);

      Db->DocTypeAddRecord(Record);
    }
}
