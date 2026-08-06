#include <iostream>
#include <iomanip>
#include <cstring>

namespace
{
  void WriteJsonString(std::ostream& out, const char *text)
  {
    out << '"';

    if (text)
      {
        const unsigned char *ptr =
          reinterpret_cast<const unsigned char *>(text);

        while (*ptr)
          {
            switch (*ptr)
              {
              case '"':
                out << "\\\"";
                break;

              case '\\':
                out << "\\\\";
                break;

              case '\b':
                out << "\\b";
                break;

              case '\f':
                out << "\\f";
                break;

              case '\n':
                out << "\\n";
                break;

              case '\r':
                out << "\\r";
                break;

              case '\t':
                out << "\\t";
                break;

              default:
                if (*ptr < 0x20)
                  {
                    const std::ios::fmtflags flags = out.flags();
                    const char fill = out.fill();

                    out << "\\u"
                        << std::hex
                        << std::setw(4)
                        << std::setfill('0')
                        << static_cast<unsigned int>(*ptr);

                    out.flags(flags);
                    out.fill(fill);
                  }
                else
                  {
                    out << static_cast<char>(*ptr);
                  }
                break;
              }

            ++ptr;
          }
      }

    out << '"';
  }
}
