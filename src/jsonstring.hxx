#include <iostream>
#include <iomanip>
#include <cstring>

namespace
{

#if 1

// Support both oth std::ostream and STRING

template <class OUT>
OUT& WriteJsonString(OUT& out, const char *text)
{
  static const char hex[] = "0123456789abcdef";

  out << '"';

  if (text)
    {
      const unsigned char *p =
        reinterpret_cast<const unsigned char *>(text);

      while (*p)
        {
          switch (*p)
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
              if (*p < 0x20)
                {
                  out << "\\u00"
                      << hex[(*p >> 4) & 0x0f]
                      << hex[*p & 0x0f];
                }
              else
                {
                  out << static_cast<char>(*p);
                }
              break;
            }

          ++p;
        }
    }

  out << '"';

  return out;
}



#else
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
#endif
}
