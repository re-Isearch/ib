#include "fchits.hxx"
#include "attrlist.hxx"
#include <cassert>
#include <cstdint>

int main()
{
  static_assert(sizeof(FCSOURCE) == 8, "evidence source must be 64 bits");
  static_assert(FIELDTYPE::any == 0, "legacy lexical type convention");
  static_assert(FIELDTYPE::__last == 255, "datatype byte boundary");

  // First SIS entry is ordinal 1, even though its underlying entry index is 0.
  constexpr FCSOURCE first = MakeSISSourceId(0, 1);
  static_assert(first != 0 && IsLexicalSource(first), "first lexical term has identity");
  static_assert(SourceIdentity(first) == 1, "one-based SIS identity preserved");
  static_assert(SourceType(first) == FIELDTYPE::any, "lexical low byte remains zero");
  static_assert(SourceShard(first) == 0, "initial SIS shard");
  static_assert(MakeSISSourceId(0, 0) == 0, "missing SIS entry remains unknown");

  // A term has a stable source within a shard; the same ordinal in a
  // different shard is not the same lexical source.
  constexpr FCSOURCE term = MakeSISSourceId(1, 1234);
  static_assert(term == MakeSISSourceId(1, 1234), "same lexical source");
  static_assert(term != MakeSISSourceId(2, 1234), "distinct SIS shards");
  static_assert(SourceIdentity(term) == 1234, "ordinal round-trip");
  static_assert(SourceShard(term) == 1, "shard round-trip");

  // Exact 48-bit boundary; overflowing may not alias another source.
  static_assert(MakeSISSourceId(255, FCSOURCE_SIS_MASK) ==
                0xFFFFFFFFFFFFFF00ULL, "maximum SIS identity");
  static_assert(MakeSISSourceId(255, FCSOURCE_SIS_MASK + 1) == 0,
                "reject SIS identity overflow");
  static_assert(MakeSISSourceId(256, 1) == 0, "reject shard overflow");

  constexpr auto integerSource = MakeTypedSourceId(FIELDTYPE::integer);
  constexpr auto dateSource = MakeTypedSourceId(FIELDTYPE::date);
  constexpr auto hnswSource = MakeTypedSourceId(FIELDTYPE::db_hnsw);
  constexpr auto hnsw2Source = MakeTypedSourceId(FIELDTYPE::db_hnsw2);

  static_assert(IsTypedSource(integerSource), "numeric evidence is typed");
  static_assert(!IsLexicalSource(integerSource), "typed source is not lexical");
  static_assert(SourceIdentity(integerSource) == 0, "typed source has no SIS identity");
  static_assert(SourceType(integerSource) == FIELDTYPE::integer, "datatype extraction");
  static_assert(integerSource == MakeTypedSourceId(FIELDTYPE::integer),
                "same datatype has the same diversity identity");
  static_assert(integerSource != dateSource, "integer and date diversify");
  static_assert(hnswSource != hnsw2Source, "vector model types diversify");
  static_assert(integerSource != first, "typed and lexical namespaces disjoint");
  static_assert(!IsTypedSource(0) && !IsLexicalSource(0), "unknown remains unknown");

  // A source tagged as text without a SIS identity is not a lexical term.
  static_assert(!IsLexicalSource(MakeTypedSourceId(FIELDTYPE::text)),
                "text type alone cannot establish lexical provenance");

  return 0;
}
