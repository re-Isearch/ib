/* Copyright (c) 2026 Project re-Isearch and its contributors. Apache-2.0. */
#ifndef IB_ZUGFERD_PDF_HXX
#define IB_ZUGFERD_PDF_HXX

#include <cstddef>
#include <cstdint>
#include <string>

namespace ib_zugferd {
enum class Status { NotInvoice, Invoice, InvalidInvoice };
struct Invoice {
  Status status = Status::NotInvoice;
  std::string filename;
  std::string xml;
  std::string error;
};

bool Available();
// Record ranges are inclusive; end == 0 means through EOF, as in RECORD.
Invoice Extract(const std::string& filename, std::uint64_t start = 0,
                std::uint64_t end = 0, std::size_t max_xml_bytes = 32 * 1024 * 1024);
}
#endif
