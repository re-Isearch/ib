#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <vector>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFEmbeddedFileDocumentHelper.hh>
#include <qpdf/QPDFEFStreamObjectHelper.hh>
#include <qpdf/QPDFFileSpecObjectHelper.hh>
#include <qpdf/QPDFWriter.hh>
#include "common.hxx"
#include "pdf.hxx"
#include "zugferd.hxx"
#include "zugferd_pdf.hxx"
#include "idb.hxx"

namespace fs = std::filesystem;
static void require(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
static std::string read_file(const fs::path& name) {
  std::ifstream input(name, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(input), {});
}
static void write_file(const fs::path& name, const std::string& value) {
  std::ofstream output(name, std::ios::binary);
  output.write(value.data(), value.size());
  require(output.good(), "write fixture file");
}
static const std::string invoice_xml =
  "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
  "<rsm:CrossIndustryInvoice xmlns:rsm=\"urn:un:unece:uncefact:data:standard:CrossIndustryInvoice:100\" "
  "xmlns:ram=\"urn:un:unece:uncefact:data:standard:ReusableAggregateBusinessInformationEntity:100\">"
  "<rsm:ExchangedDocument><ram:ID>RE-2026-4711</ram:ID><ram:TypeCode>380</ram:TypeCode>"
  "</rsm:ExchangedDocument><rsm:SupplyChainTradeTransaction>"
  "<ram:Name>Papier &amp; Büromaterial</ram:Name><ram:DuePayableAmount>119.00</ram:DuePayableAmount>"
  "</rsm:SupplyChainTradeTransaction></rsm:CrossIndustryInvoice>";

struct Attachment { std::string name, data; bool utf16_name = false; };
static void pdf_fixture(const fs::path& filename, const std::vector<Attachment>& inputs,
                        bool associated = true, bool names = true, bool compressed = false) {
  QPDF pdf;
  pdf.emptyPDF();
  QPDFEmbeddedFileDocumentHelper attachments(pdf);
  auto array = QPDFObjectHandle::newArray();
  unsigned key = 0;
  for (const auto& input : inputs) {
    auto stream = pdf.newStream(input.data);
    auto spec = QPDFFileSpecObjectHelper::createFileSpec(
        pdf, input.name, QPDFEFStreamObjectHelper(stream));
    spec.getObjectHandle().replaceKey("/AFRelationship", QPDFObjectHandle::newName("/Alternative"));
    if (input.utf16_name) {
      std::string encoded("\xfe\xff", 2);
      for (char ch : input.name) { encoded += '\0'; encoded += ch; }
      spec.getObjectHandle().replaceKey("/UF", QPDFObjectHandle::newString(encoded));
    }
    if (associated) array.appendItem(spec.getObjectHandle());
    // Deliberately use an arbitrary name-tree key: the /UF or /F filename is
    // authoritative, and an attachment need not be the first entry.
    if (names) attachments.replaceEmbeddedFile("attachment-" + std::to_string(++key), spec);
  }
  if (associated) pdf.getRoot().replaceKey("/AF", array);
  QPDFWriter writer(pdf, filename.c_str());
  writer.setObjectStreamMode(compressed ? qpdf_o_generate : qpdf_o_disable);
  writer.setCompressStreams(compressed);
  writer.write();
}

static void reader_tests() {
  require(ib_zugferd::Available(), "invoice backend enabled");
  for (bool compressed : {false, true}) {
    for (const auto& name : {"factur-x.xml", "ZUGFeRD-invoice.xml", "zugferd-invoice.xml", "xrechnung.xml"}) {
      pdf_fixture("invoice.pdf", {{"support.xml", "<support/>"}, {name, invoice_xml, true}},
                  true, true, compressed);
      auto found = ib_zugferd::Extract("invoice.pdf");
      require(found.status == ib_zugferd::Status::Invoice && found.xml == invoice_xml,
              "decode the selected invoice exactly, including compressed objects and UTF-16 filenames");
    }
  }
  for (bool associated : {false, true}) {
    pdf_fixture("invoice.pdf", {{"factur-x.xml", invoice_xml}}, associated, !associated, true);
    require(ib_zugferd::Extract("invoice.pdf").xml == invoice_xml, "accept AF-only and name-tree-only containers");
  }
  pdf_fixture("invoice.pdf", {{"ZUGFeRD-invoice.xml",
      "<CrossIndustryDocument xmlns=\"urn:ferd:CrossIndustryDocument:invoice:1p0\"/>"}});
  require(ib_zugferd::Extract("invoice.pdf").status == ib_zugferd::Status::Invoice,
          "recognize the ZUGFeRD 1 CII namespace without a prefix");
  pdf_fixture("invoice.pdf", {{"factur-x.xml", invoice_xml}}, true, true, true);
  const auto bytes = read_file("invoice.pdf");
  const std::string prefix(4097, 'X');
  write_file("collection.bin", prefix + bytes + "adjacent record is not PDF");
  require(ib_zugferd::Extract("collection.bin", prefix.size(), prefix.size()+bytes.size()-1).xml == invoice_xml,
          "honor an inclusive PDF record range inside a container");

  pdf_fixture("ordinary.pdf", {{"support.xml", invoice_xml}});
  require(ib_zugferd::Extract("ordinary.pdf").status == ib_zugferd::Status::NotInvoice,
          "an arbitrary XML attachment is not an invoice attachment");
  pdf_fixture("ordinary.pdf", {});
  require(ib_zugferd::Extract("ordinary.pdf").status == ib_zugferd::Status::NotInvoice,
          "ordinary PDF is not a hybrid invoice");
  pdf_fixture("invalid.pdf", {{"factur-x.xml", "<Invoice>unrelated namespace</Invoice>"}});
  require(ib_zugferd::Extract("invalid.pdf").status == ib_zugferd::Status::InvalidInvoice,
          "a filename alone cannot validate invoice XML");
  pdf_fixture("invalid.pdf", {{"factur-x.xml", invoice_xml + "<broken>"}});
  require(ib_zugferd::Extract("invalid.pdf").status == ib_zugferd::Status::InvalidInvoice,
          "reject malformed XML, including trailing markup");
  std::string utf16("\xff\xfe", 2);
  const auto ascii_xml = invoice_xml.substr(invoice_xml.find("<rsm:"), invoice_xml.find("<ram:Name>") - invoice_xml.find("<rsm:")) +
      "</rsm:SupplyChainTradeTransaction></rsm:CrossIndustryInvoice>";
  for (char ch : ascii_xml) { utf16 += ch; utf16 += '\0'; }
  pdf_fixture("invalid.pdf", {{"factur-x.xml", utf16}});
  require(ib_zugferd::Extract("invalid.pdf").status == ib_zugferd::Status::InvalidInvoice,
          "reject UTF-16 XML even without an encoding declaration");
  const auto declaration_end = invoice_xml.find("?>") + 2;
  pdf_fixture("invalid.pdf", {{"factur-x.xml", invoice_xml.substr(0, declaration_end) +
      "<!DOCTYPE rsm:CrossIndustryInvoice SYSTEM \"file:///must-not-read\">" + invoice_xml.substr(declaration_end)}});
  const auto dtd = ib_zugferd::Extract("invalid.pdf");
  require(dtd.status == ib_zugferd::Status::InvalidInvoice && dtd.error.find("DTD") != std::string::npos,
          "reject external DTDs before resolving them");
  pdf_fixture("invalid.pdf", {{"factur-x.xml", invoice_xml}, {"xrechnung.xml", invoice_xml}});
  require(ib_zugferd::Extract("invalid.pdf").status == ib_zugferd::Status::InvalidInvoice,
          "reject two distinct invoice streams rather than choose one");
  pdf_fixture("large.pdf", {{"factur-x.xml", invoice_xml + std::string(1024 * 1024, ' ')}}, true, true, true);
  auto oversized = ib_zugferd::Extract("large.pdf", 0, 0, 1024);
  require(oversized.status == ib_zugferd::Status::InvalidInvoice && oversized.error.find("MAXXMLBYTES") != std::string::npos,
          "bound decoded XML size even when the compressed stream is tiny");
}

struct Database : IDBOBJ {
  std::vector<RECORD> records;
  mutable std::unique_ptr<ZUGFERD> invoice;
  Database() { SetWorkingDirectory(GetCwd()); }
  STRING& ComposeDbFn(PSTRING value, const CHR* extension) const override {
    return *value = STRING("test-index") + extension;
  }
  void DocTypeAddRecord(const RECORD& record) override { records.push_back(record); }
  DOCTYPE* GetDocTypePtr(const DOCTYPE_ID&) const override {
    if (!invoice) invoice.reset(new ZUGFERD(const_cast<Database*>(this), "ZUGFERD"));
    return invoice.get();
  }
};
struct Pdf : ADOBE_PDFDOC {
  bool filtered = false;
  explicit Pdf(Database& db) : ADOBE_PDFDOC(&db, "PDF") { Filter = "unit-test-filter"; }
  off_t RunPipe(FILE* output, const STRING&) override {
    filtered = true;
    const char text[] = "Title: Ordinary PDF\n\nPDF text fallback.\n";
    return fwrite(text, 1, sizeof(text)-1, output);
  }
};
static RESULT result_for(const RECORD& record) {
  RESULT result;
  result.SetPathname(record.GetPathname());
  result.SetRecordStart(record.GetRecordStart());
  result.SetRecordEnd(record.GetRecordEnd());
  result.SetKey(record.GetKey());
  return result;
}

static void doctype_tests() {
  Database db;
  Pdf pdf(db);
  RECORD input;
  input.SetFullFileName(STRING((fs::current_path()/"invoice & details.pdf").c_str()));
  input.SetDocumentType("PDF");
  input.SetKey("customer/supplied-key");
  pdf_fixture("invoice & details.pdf", {{"factur-x.xml", invoice_xml}}, true, true, true);
  const auto source_bytes = read_file("invoice & details.pdf");
  pdf.ParseRecords(input);
  require(!pdf.filtered && db.records.size() == 1, "invoice PDF never invokes the PDF text filter");
  RECORD indexed = db.records.back();
  require(indexed.GetDocumentType().DocumentType() == "ZUGFERD" &&
          indexed.GetKey() == input.GetKey() && indexed.GetRecordStart() == 0 &&
          indexed.GetRecordEnd()-indexed.GetRecordStart()+1 == invoice_xml.size(),
          "queued XML record retains type/key and exact byte range");
  require(indexed.GetOrigPathname().GetFullFileName() == input.GetFullFileName(), "retain original pathname in RECORD");
  require(fs::path(indexed.GetFullFileName().c_str()).filename().string().find("supplied-key") == std::string::npos,
          "never use a supplied key as a cache path");
  require(db._get_resource_path(indexed.GetFullFileName()) == input.GetFullFileName(), "resource sidecar resolves the original PDF");
  db.invoice->ParseFields(&indexed);
  require(!indexed.IsBadRecord() && indexed.GetDftPtr()->GetTotalEntries() > 0, "inherit XML field parsing");
  const auto result = result_for(indexed);
  STRING value;
  db.invoice->Present(result, "XML", SutrsRecordSyntax, &value);
  require(std::string(value.c_str(), value.GetLength()) == invoice_xml, "XML presentation returns the exact extracted bytes");
  db.invoice->Present(result, SOURCE_MAGIC, SutrsRecordSyntax, &value);
  require(std::string(value.c_str(), value.GetLength()) == source_bytes, "source presentation returns the exact original PDF");
  static_cast<const DOCTYPE&>(*db.invoice).Present(result, "PDF", &value);
  require(std::string(value.c_str(), value.GetLength()) == source_bytes, "default presentation syntax also returns binary PDF bytes");
  db.invoice->Present(result, "$Path", SutrsRecordSyntax, &value);
  require(value == input.GetFullFileName(), "present original path");
  db.invoice->Present(result, "XML", HtmlRecordSyntax, &value);
  require(value.Search("application/xml") && !value.Search("application/pdf"), "serve XML with the correct MIME type");
  db.invoice->Present(result, BRIEF_MAGIC, HtmlRecordSyntax, &value);
  require(value.Search("invoice &amp; details.pdf"), "escape the original filename in HTML headlines");

  // The same PDF may be a record in a larger source, with a relative resource
  // path. Retrieval must return just that record, not its prefix or suffix.
  db.records.clear();
  db.setUseRelativePaths(true);
  const std::string prefix(8193, 'X');
  write_file("invoices.bin", prefix + source_bytes + "next invoice");
  input.SetFullFileName(STRING((fs::current_path()/"invoices.bin").c_str()));
  input.SetRecordStart(prefix.size());
  input.SetRecordEnd(prefix.size() + source_bytes.size() - 1);
  db.invoice->ParseRecords(input);
  require(db.records.size() == 1, "queue a PDF range with relative paths");
  const auto ranged = result_for(db.records.back());
  db.invoice->Present(ranged, "PDF", SutrsRecordSyntax, &value);
  require(std::string(value.c_str(), value.GetLength()) == source_bytes,
          "original PDF retrieval honors the source range and relative path");
  db.invoice->Present(ranged, "XML", SutrsRecordSyntax, &value);
  require(std::string(value.c_str(), value.GetLength()) == invoice_xml, "range extraction retains exact XML");
  input.SetRecordStart(0);
  input.SetRecordEnd(0);
  input.SetKey("");

  db.records.clear();
  pdf.filtered = false;
  input.SetFullFileName(STRING((fs::current_path()/"ordinary.pdf").c_str()));
  pdf.ParseRecords(input);
  require(pdf.filtered && db.records.size() == 1, "ordinary PDFs retain their existing filter path");
  db.records.clear();
  pdf.filtered = false;
  input.SetFullFileName(STRING((fs::current_path()/"invalid.pdf").c_str()));
  pdf.ParseRecords(input);
  require(!pdf.filtered && db.records.empty(), "invalid invoices do not silently become PDF text records");
}

static void index_tests() {
  const STRING stem((fs::current_path()/"invoice-index").c_str());
  STRLIST options;
  options.AddEntry("FILTER=NULL"); // Prove PDF detection needs no text filter.
  {
    IDB db(stem, options);
    require(db.SetLocale("de.UTF-8"), "select UTF-8 invoice collation");
    db.SetIndexingMemory(4 * 1024 * 1024, true);
    for (unsigned i = 0; i != 3; ++i) {
      const auto path = fs::current_path()/("input-" + std::to_string(i) + ".pdf");
      pdf_fixture(path, {{"factur-x.xml", invoice_xml}}, true, true, i != 0);
      RECORD input;
      input.SetFullFileName(STRING(path.c_str()));
      input.SetDocumentType(i == 0 ? "FACTUR-X" : i == 1 ? "ZUGFERD" : "PDF");
      require(db.AddRecord(input), "queue invoice PDFs through the registered doctypes and alias");
    }
    require(db.Index(), "index extracted invoice XML");
  }
  IDB db(stem, true);
  require(db.GetTotalRecords() == 3, "persist one record per invoice");
  std::ostringstream errors;
  require(db.GetMainIndex()->CheckIndex(errors) == 0,
          "every posting retains its source offset across multiple extracted files");
  std::unique_ptr<IRSET> matches(db.TermSearch("Büromaterial", "RAM:NAME"));
  require(matches && matches->GetTotalEntries() == 3, "search UTF-8 invoice values within XML fields");
}

int main() {
  const auto previous = fs::current_path();
  const auto directory = previous/"zugferd-fixtures";
  try {
    require(fs::create_directory(directory), "create fixture directory");
    fs::current_path(directory);
    reader_tests();
    doctype_tests();
    index_tests();
    fs::current_path(previous);
    fs::remove_all(directory);
    std::cout << "ZUGFeRD extraction, PDF hand-off, XML fields and source retrieval: PASS\n";
    return 0;
  } catch (const std::exception& error) {
    fs::current_path(previous);
    std::cerr << "ZUGFeRD regression: " << error.what() << '\n';
    return 1;
  }
}
