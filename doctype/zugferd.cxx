/* Copyright (c) 2026 Project re-Isearch and its contributors. Apache-2.0. */
#include "common.hxx"
#include "zugferd.hxx"
#include "zugferd_pdf.hxx"
#include "doc_conf.hxx"
#include <algorithm>
#include <cerrno>
#include <fcntl.h>
#include <limits>
#include <unistd.h>

ZUGFERD::ZUGFERD(PIDBOBJ parent, const STRING& name) : XML(parent, name),
  max_xml_bytes(32 * 1024 * 1024)
{
  const STRING option = Getoption("MAXXMLBYTES", "33554432");
  try {
    size_t used = 0;
    const auto limit = std::stoull(option.c_str(), &used);
    if (!limit || used != option.GetLength() || limit > std::numeric_limits<int>::max())
      throw std::invalid_argument("MAXXMLBYTES");
    max_xml_bytes = static_cast<size_t>(limit);
  } catch (const std::exception&) {
    message_log(LOG_ERROR, "%s: Invalid MAXXMLBYTES '%s'; using 33554432", Doctype.c_str(), option.c_str());
  }
}

const char* ZUGFERD::Description(PSTRLIST list) const
{
  if (list->IsEmpty() && Doctype != "ZUGFERD") list->AddEntry(Doctype);
  list->AddEntry("ZUGFERD");
  XML::Description(list);
  return "ZUGFeRD/Factur-X PDF invoices: extract embedded CII XML in-process,\n"
         "index it as XML and preserve the original PDF resource. Requires qpdf\n"
         "and Expat. MAXXMLBYTES limits decoded invoice XML (default 33554432).\n"
         "SOURCE (SUTRS) / PDF return the PDF; FULLTEXT (SUTRS) / XML return XML.";
}

void ZUGFERD::SourceMIMEContent(PSTRING value) const { *value = "application/pdf"; }
void ZUGFERD::SourceMIMEContent(const RESULT&, PSTRING value) const { SourceMIMEContent(value); }

bool ZUGFERD::ParseIfInvoice(const RECORD& record)
{
  if (!Db || !ib_zugferd::Available()) return false;
  auto invoice = ib_zugferd::Extract(record.GetFullFileName().c_str(),
      record.GetRecordStart(), record.GetRecordEnd(), max_xml_bytes);
  if (invoice.status == ib_zugferd::Status::NotInvoice) return false;
  if (invoice.status == ib_zugferd::Status::InvalidInvoice) {
    message_log(LOG_ERROR, "%s: Rejecting invoice '%s': %s", Doctype.c_str(),
                record.GetFullFileName().c_str(), invoice.error.c_str());
    return true;
  }
  StoreInvoice(record, invoice.xml);
  return true;
}

void ZUGFERD::ParseRecords(const RECORD& record)
{
  if (!ParseIfInvoice(record))
    message_log(LOG_ERROR, "%s: '%s': %s", Doctype.c_str(), record.GetFullFileName().c_str(),
        ib_zugferd::Available() ? "No readable ZUGFeRD/Factur-X invoice attachment" :
        "qpdf/Expat support is unavailable; configure with IB_ZUGFERD=ON");
}

bool ZUGFERD::StoreInvoice(const RECORD& source, const std::string& xml)
{
  const STRING filename = source.GetFullFileName();
  INODE inode(filename);
  if (!inode.Ok()) {
    message_log(LOG_ERRNO, "%s: Cannot stat '%s'", Doctype.c_str(), filename.c_str());
    return false;
  }
  const STRING storage_base = inode.Key(source.GetRecordStart(), source.GetRecordEnd());
  const STRING base_key = source.GetKey().IsEmpty() ? storage_base : source.GetKey();
  STRING directory;
  Db->ComposeDbFn(&directory, DbExtCat);
  if (MkDir(directory) == -1) {
    message_log(LOG_ERRNO, "%s: Cannot create catalog '%s'", Doctype.c_str(), directory.c_str());
    return false;
  }
  AddTrailingSlash(&directory);
  directory.Cat(static_cast<long>(storage_base.CRC16()) % 1000);
  if (MkDir(directory) == -1) {
    message_log(LOG_ERRNO, "%s: Cannot create catalog '%s'", Doctype.c_str(), directory.c_str());
    return false;
  }

  STRING key, outfile;
  FILE* output = nullptr;
  for (unsigned long version = 0; !output; ++version) {
    key = base_key;
    if (version) key.form("%s.%lu", base_key.c_str(), version);
    if (Db->KeyLookup(key)) continue;
    // Cache filenames are generated from inode/range, never from an attachment
    // filename or a user-supplied record key.
    outfile.form("%s%s.%lu.xml", AddTrailingSlash(directory).c_str(), storage_base.c_str(), version);
    const int descriptor = open(outfile.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0666);
    if (descriptor < 0) {
      if (errno == EEXIST) continue;
      message_log(LOG_ERRNO, "%s: Cannot create '%s'", Doctype.c_str(), outfile.c_str());
      return false;
    }
    output = fdopen(descriptor, "wb");
    if (!output) { close(descriptor); unlink(outfile.c_str()); return false; }
  }

  // Keep the cache valid XML from byte zero. The FILTER resource sidecar
  // below retains the source path, MIME type and original PDF record range.
  bool ok = fwrite(xml.data(), 1, xml.size(), output) == xml.size();
  ok = fclose(output) == 0 && ok;
  if (!ok) {
    message_log(LOG_ERRNO, "%s: Cannot write '%s'", Doctype.c_str(), outfile.c_str());
    unlink(outfile.c_str());
    return false;
  }

  STRING pathfile;
  if (!Db->_write_resource_path(outfile, source, "application/pdf", &pathfile)) {
    message_log(LOG_ERRNO, "%s: Cannot write resource path '%s'", Doctype.c_str(), pathfile.c_str());
    unlink(outfile.c_str());
    if (!pathfile.IsEmpty()) unlink(pathfile.c_str());
    return false;
  }
  RECORD record(source);
  record.SetFullFileName(outfile);
  record.SetOrigPathname(filename);
  record.SetDocumentType(Doctype);
  record.SetKey(key);
  record.SetRecordStart(0);
  record.SetRecordEnd(xml.size() - 1);
  record.SetCharset(CHARSET("UTF-8"));
  if (!record.GetDate().Ok()) record.SetDate(inode.mdate);
  if (!record.GetDateModified().Ok()) record.SetDateModified(inode.mdate);
  if (!record.GetDateCreated().Ok()) record.SetDateCreated(inode.cdate);
  message_log(LOG_INFO, "%s: Extracted %lu bytes of invoice XML from '%s'", Doctype.c_str(),
              static_cast<unsigned long>(xml.size()), filename.c_str());
  Db->DocTypeAddRecord(record);
  return true;
}

bool ZUGFERD::GetResourcePath(const RESULT& result, PSTRING path) const
{
  if (!Db) return false;
  const STRING original = Db->_get_resource_path(result.GetFullFileName());
  if (path) *path = original;
  return original != result.GetFullFileName() && FileExists(original);
}

bool ZUGFERD::ReadSource(const RESULT& result, PSTRING output) const
{
  STRING path;
  if (!GetResourcePath(result, &path)) return false;
  // The resource sidecar also retains the source range in a concatenated file.
  FC range;
  FILE* fp = fopen(result.GetFullFileName() + _DbExt(ExtPath), "rb");
  if (!fp) return false;
  STRING saved_path, mime;
  const bool ok = saved_path.Read(fp) && range.Read(fp) && mime.Read(fp);
  fclose(fp);
  if (!ok) return false;
  const off_t size = GetFileSize(path);
  const auto start = range.GetFieldStart();
  const auto end = range.GetFieldEnd() ? range.GetFieldEnd() : size > 0 ? size - 1 : 0;
  if (size <= 0 || start > end || end >= static_cast<UINT8>(size)) return false;
  // STRING::Fread is a text reader and truncates at NUL. PDF streams need a
  // binary read, including when only one PDF record of a container is wanted.
  fp = fopen(path, "rb");
  if (!fp) return false;
  bool read_ok = fseeko(fp, start, SEEK_SET) == 0;
  auto remaining = end - start + 1;
  char buffer[65536];
  while (read_ok && remaining) {
    const size_t want = std::min<UINT8>(remaining, sizeof(buffer));
    const size_t count = fread(buffer, 1, want, fp);
    if (count != want) { read_ok = false; break; }
    output->Cat(STRING(buffer, count));
    remaining -= count;
  }
  read_ok = fclose(fp) == 0 && read_ok;
  if (!read_ok) output->Clear();
  return read_ok;
}

void ZUGFERD::DocPresent(const RESULT& result, const STRING& element, const STRING& syntax,
                        PSTRING output) const
{
  const bool source = element.Equals("PDF") ||
      (syntax == SutrsRecordSyntax ? element.Equals(SOURCE_MAGIC) : element.Equals(FULLTEXT_MAGIC));
  const bool xml = element.Equals("XML") ||
      (syntax == SutrsRecordSyntax ? element.Equals(FULLTEXT_MAGIC) : element.Equals(SOURCE_MAGIC));
  output->Clear();
  if (source || xml) {
    STRING body;
    if (source) ReadSource(result, &body);
    else result.GetRecordData(&body);
    if (syntax == HtmlRecordSyntax && !body.IsEmpty())
      *output = DOCTYPE::Httpd_Content_type(result, source ? "application/pdf" : "application/xml");
    output->Cat(body);
  } else XML::DocPresent(result, element, syntax, output);
}

void ZUGFERD::Present(const RESULT& result, const STRING& element, PSTRING output) const
{
  if (element.Equals("$Path") || element.Equals(BRIEF_MAGIC) ||
      element.Equals(FULLTEXT_MAGIC) || element.Equals(SOURCE_MAGIC) ||
      element.Equals("PDF") || element.Equals("XML"))
    Present(result, element, SutrsRecordSyntax, output);
  else XML::Present(result, element, output);
}

void ZUGFERD::Present(const RESULT& result, const STRING& element, const STRING& syntax,
                     PSTRING output) const
{
  if (element.Equals("$Path")) {
    output->Clear();
    GetResourcePath(result, output);
  } else if (element.Equals(BRIEF_MAGIC)) {
    STRING path;
    output->Clear();
    if (GetResourcePath(result, &path)) {
      if (syntax == HtmlRecordSyntax) HtmlCat(result, RemovePath(path), output, false);
      else *output = RemovePath(path);
    }
  } else if (element.Equals(FULLTEXT_MAGIC) || element.Equals(SOURCE_MAGIC) ||
             element.Equals("PDF") || element.Equals("XML"))
    DocPresent(result, element, syntax, output);
  else XML::Present(result, element, syntax, output);
}
