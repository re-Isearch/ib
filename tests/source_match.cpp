#include "source_match.hxx"
#include <cstring>
#include <stdexcept>
#include <string>

static void require(bool yes, const char *message)
{
  if (!yes) throw std::runtime_error(message);
}

static source_match::OrderedResult compare(const std::string &query,
                                          const std::string &source)
{
  return source_match::compare_folded(
      reinterpret_cast<const unsigned char *>(query.data()), query.size(),
      reinterpret_cast<const unsigned char *>(source.data()), source.size());
}

int main()
{
  const std::string phrase = u8"la défense";
  require(compare(phrase, phrase).order == 0 &&
          compare(phrase, phrase).bytes == phrase.size(),
          "identical UTF-8 phrase compares equal");
  const std::string spaced = u8"la   défense";
  require(compare(phrase, spaced).order == 0 &&
          compare(phrase, spaced).bytes == spaced.size(),
          "separator collapsing retains consumed source byte count");
  require(compare(phrase, u8"la defense").order != 0, "accent is significant");
  require(compare(phrase, u8"la défonse").order != 0, "wrong letter is rejected");
  require(compare(u8"café", u8"caféteria").order == 0, "prefix remains valid");
  require(compare(u8"café", u8"cafê").order != 0, "different accent rejected");
  require(compare(phrase, u8"la déf").order > 0, "truncated source rejected");
  require(compare(u8"la déf", phrase).order == 0, "query may be a prefix");
  const std::string text = u8"go café next";
  const auto *data = reinterpret_cast<const unsigned char *>(text.data());
  const auto word = [](const unsigned char *p) {
    return *p >= 0x80 || (*p >= 'a' && *p <= 'z');
  };
  require(source_match::align_start(data, text.size(), 7, true, word) == 3,
          "continuation byte aligns to start of word");
  require(source_match::align_start(data, text.size(), 8, true, word) == 9,
          "separator advances to next whole word");
  require(source_match::align_start(data, text.size(), 2, true, word) == 3,
          "first separator handled safely");
}
