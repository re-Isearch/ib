/* Copyright (c) 2026 Project re-Isearch and its contributors. Apache-2.0. */
#include "zugferd_pdf.hxx"

#ifdef IB_HAVE_ZUGFERD
#include <qpdf/FileInputSource.hh>
#include <qpdf/Pipeline.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFEmbeddedFileDocumentHelper.hh>
#include <qpdf/QPDFFileSpecObjectHelper.hh>
#include <algorithm>
#include <cstdio>
#include <memory>
#include <set>
#include <stdexcept>
#include <vector>

namespace {
// Give qpdf a seekable view of a RECORD, without copying the PDF into RAM or
// reading adjacent records in a concatenated source file.
class PdfRange : public InputSource {
  FileInputSource input;
  qpdf_offset_t first, length;
public:
  PdfRange(const std::string& name, std::uint64_t start, std::uint64_t end)
    : input(name.c_str()), first(0), length(0) {
    input.seek(0, SEEK_END);
    const auto size = input.tell();
    if (size <= 0 || start >= static_cast<std::uint64_t>(size) || (end && end < start))
      throw std::runtime_error("Invalid or empty PDF record range");
    first = static_cast<qpdf_offset_t>(start);
    const auto last = end ? std::min(end, static_cast<std::uint64_t>(size - 1))
                          : static_cast<std::uint64_t>(size - 1);
    length = static_cast<qpdf_offset_t>(last - start + 1);
    rewind();
  }
  const std::string& getName() const override { return input.getName(); }
  qpdf_offset_t tell() override { return input.tell() - first; }
  void seek(qpdf_offset_t offset, int whence) override {
    const auto base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? tell() :
                      whence == SEEK_END ? length : -1;
    if (base < 0 || (offset < 0 && offset < -base) ||
        (offset >= 0 && offset > length - base))
      throw std::runtime_error("PDF seek outside the source record");
    input.seek(first + base + offset, SEEK_SET);
  }
  void rewind() override { input.seek(first, SEEK_SET); }
  size_t read(char* buffer, size_t size) override {
    last_offset = tell();
    return input.read(buffer, std::min(size, static_cast<size_t>(length - last_offset)));
  }
  void unreadCh(char) override { seek(-1, SEEK_CUR); }
  qpdf_offset_t findAndSkipNextEOL() override {
    char ch;
    while (read(&ch, 1)) {
      if (ch != '\r' && ch != '\n') continue;
      const auto found = tell() - 1;
      while (read(&ch, 1)) {
        if (ch != '\r' && ch != '\n') { unreadCh(ch); break; }
      }
      return found;
    }
    return tell();
  }
};

class XmlBuffer : public Pipeline {
  size_t limit;
public:
  std::string value;
  bool exceeded = false;
  explicit XmlBuffer(size_t maximum) : Pipeline("invoice XML", nullptr), limit(maximum) {}
  void write(const unsigned char* data, size_t size) override {
    if (size > limit - value.size()) {
      exceeded = true;
      throw std::runtime_error("Invoice XML exceeds MAXXMLBYTES");
    }
    value.append(reinterpret_cast<const char*>(data), size);
  }
  void finish() override {}
};

std::string ascii_lower(std::string value) {
  for (char& ch : value) if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
  return value;
}
bool invoice_name(const std::string& value) {
  const auto name = ascii_lower(value);
  return name == "factur-x.xml" || name == "zugferd-invoice.xml" || name == "xrechnung.xml";
}

}
#endif

namespace ib_zugferd {
bool Available() {
#ifdef IB_HAVE_ZUGFERD
  return true;
#else
  return false;
#endif
}

Invoice Extract(const std::string& filename, std::uint64_t start, std::uint64_t end, size_t maximum) {
  Invoice result;
#ifdef IB_HAVE_ZUGFERD
  bool claimed_invoice = false;
  try {
    if (!maximum) throw std::runtime_error("MAXXMLBYTES must be positive");
    QPDF pdf;
    pdf.setSuppressWarnings(true);
    pdf.setAttemptRecovery(false);
    pdf.processInputSource(std::make_shared<PdfRange>(filename, start, end));
    if (pdf.isEncrypted()) throw std::runtime_error("Encrypted PDFs are not supported as invoice containers");

    std::vector<QPDFObjectHandle> streams;
    std::set<QPDFObjGen> seen;
    auto consider = [&](QPDFFileSpecObjectHelper& spec, const std::string& key) {
      const auto name = spec.getFilename();
      if (!invoice_name(name) && !invoice_name(key)) return;
      claimed_invoice = true;
      auto stream = spec.getEmbeddedFileStream();
      if (!stream.isStream()) throw std::runtime_error("Invoice attachment has no embedded file stream");
      if (seen.insert(stream.getObjGen()).second) {
        streams.push_back(stream);
        if (result.filename.empty()) result.filename = name.empty() ? key : name;
      }
    };

    auto associated = pdf.getRoot().getKey("/AF");
    if (associated.isArray()) {
      for (int i = 0; i < associated.getArrayNItems(); ++i) {
        auto file = associated.getArrayItem(i);
        if (file.isDictionary()) { QPDFFileSpecObjectHelper spec(file); consider(spec, ""); }
      }
    }
    QPDFEmbeddedFileDocumentHelper attachments(pdf);
    for (const auto& entry : attachments.getEmbeddedFiles()) consider(*entry.second, entry.first);
    if (streams.empty()) return result;
    if (streams.size() != 1) throw std::runtime_error("Ambiguous PDF: more than one invoice XML attachment");

    XmlBuffer buffer(maximum);
    bool filtered = false;
    const bool decoded = streams.front().pipeStreamData(&buffer, &filtered, 0, qpdf_dl_specialized, true);
    if (buffer.exceeded) throw std::runtime_error("Invoice XML exceeds MAXXMLBYTES");
    if (!decoded) throw std::runtime_error("Cannot decode the invoice attachment stream");
    if (buffer.value.empty()) throw std::runtime_error("Empty invoice attachment");
    result.xml = std::move(buffer.value);
    result.status = Status::Invoice;
  } catch (const std::exception& error) {
    result.status = claimed_invoice ? Status::InvalidInvoice : Status::NotInvoice;
    result.error = error.what();
  }
#else
  (void)filename; (void)start; (void)end; (void)maximum;
  result.error = "ZUGFeRD support requires a build with qpdf (IB_ZUGFERD=ON)";
#endif
  return result;
}
}
