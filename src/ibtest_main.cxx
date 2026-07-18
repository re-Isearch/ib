
/*
 * CoreQuarry / ib regression and diagnostic test tool.
 *
 * Initial focus:
 *   - database opening
 *   - real search execution
 *   - _IRSET copy-on-write behavior
 *   - Duplicate() ownership
 *   - result replacement and mutation isolation
 */

#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iostream>
#include <memory>
#include <string>

#include "common.hxx"
#include "vidb.hxx"
#include "squery.hxx"
#include "irset.hxx"

namespace {

struct Options {
  STRING database;
  STRING query = "to be or none";

  bool verbose = false;
  bool run_search = true;
  bool run_cow = true;
  bool run_duplicate = true;
  bool run_operators = false;
};

class TestContext {
public:
  explicit TestContext(bool verbose)
      : verbose_(verbose)
  {
  }

  void Check(bool condition,
             const char* expression,
             const char* file,
             int line)
  {
    ++checks_;

    if (condition) {
      if (verbose_)
        std::cout << "PASS: " << expression << '\n';
      return;
    }

    ++failures_;

    std::cerr
        << "FAIL: " << expression
        << " at " << file << ':' << line
        << '\n';
  }

  int Result() const
  {
    std::cout
        << "\nChecks: " << checks_
        << ", failures: " << failures_
        << '\n';

    return failures_ == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
  }

private:
  bool verbose_;
  unsigned checks_ = 0;
  unsigned failures_ = 0;
};

#define IB_CHECK(ctx, expression) \
  (ctx).Check(                     \
      static_cast<bool>(expression), \
      #expression,                 \
      __FILE__,                    \
      __LINE__)

void Usage(const char* program)
{
  std::cout
      << "Usage: " << program << " -d database [options] [query]\n"
      << "\n"
      << "Options:\n"
      << "  -d DB          Database root name\n"
      << "  -q QUERY       Smart-search query\n"
      << "  -v             Verbose test output\n"
      << "  --search       Run only search smoke test\n"
      << "  --cow          Run only copy-on-write tests\n"
      << "  --duplicate    Run only Duplicate() tests\n"
      << "  --operators    Run Wikipedia PEER/FOCUS/REDUCE regressions\n"
      << "  --all          Run search, COW, and Duplicate() tests; default\n"
      << "  -h, --help     Display this help\n";
}

bool ParseOptions(int argc, char** argv, Options* options)
{
  bool selected_test = false;

  options->run_search = false;
  options->run_cow = false;
  options->run_duplicate = false;
  options->run_operators = false;

  for (int i = 1; i < argc; ++i) {
    const char* argument = argv[i];

    if (std::strcmp(argument, "-h") == 0 ||
        std::strcmp(argument, "--help") == 0) {
      Usage(argv[0]);
      return false;
    }

    if (std::strcmp(argument, "-v") == 0) {
      options->verbose = true;
      continue;
    }

    if (std::strcmp(argument, "-d") == 0) {
      if (++i >= argc) {
        std::cerr << "-d requires a database name\n";
        return false;
      }

      options->database = argv[i];
      continue;
    }

    if (std::strcmp(argument, "-q") == 0) {
      if (++i >= argc) {
        std::cerr << "-q requires a query\n";
        return false;
      }

      options->query = argv[i];
      continue;
    }

    if (std::strcmp(argument, "--search") == 0) {
      options->run_search = true;
      selected_test = true;
      continue;
    }

    if (std::strcmp(argument, "--cow") == 0) {
      options->run_cow = true;
      selected_test = true;
      continue;
    }

    if (std::strcmp(argument, "--duplicate") == 0) {
      options->run_duplicate = true;
      selected_test = true;
      continue;
    }

    if (std::strcmp(argument, "--operators") == 0) {
      options->run_operators = true;
      selected_test = true;
      continue;
    }

    if (std::strcmp(argument, "--all") == 0) {
      options->run_search = true;
      options->run_cow = true;
      options->run_duplicate = true;
      selected_test = true;
      continue;
    }

    /*
     * Preserve the convenient Isearch behavior where remaining text can
     * be treated as the query.
     */
    options->query = argument;

    for (++i; i < argc; ++i) {
      options->query.Cat(" ");
      options->query.Cat(argv[i]);
    }

    break;
  }

  if (!selected_test) {
    options->run_search = true;
    options->run_cow = true;
    options->run_duplicate = true;
  }

  if (options->database.IsEmpty()) {
    std::cerr << "No database specified; use -d DB\n";
    return false;
  }

  return true;
}

std::unique_ptr<IRSET> RunSearch(
    VIDB& database,
    const STRING& query,
    TestContext& tests)
{
  /*
   * SearchSmart() is also useful for the Python API because it accepts the
   * user-facing query representation and returns PIRSET.
   */
  PIRSET result = database.SearchSmart(query);

  IB_CHECK(tests, result != nullptr);

  if (result == nullptr)
    return nullptr;

  IB_CHECK(tests, result->GetTotalEntries() > 0);

  return std::unique_ptr<IRSET>(result);
}

void TestSearch(
    VIDB& database,
    const Options& options,
    TestContext& tests)
{
  std::cout << "\n[search]\n";


std::cerr << "\n[duplicate]\n";
std::cerr << "DUP 1: before RunSearch\n";


  std::unique_ptr<IRSET> result =
      RunSearch(database, options.query, tests);

std::cerr << "DUP 2: RunSearch returned\n";


  if (!result)
    return;

  std::cout
      << "Query: " << options.query << '\n'
      << "Entries: " << result->GetTotalEntries() << '\n'
      << "Hits: " << result->GetHitTotal() << '\n';
}


std::unique_ptr<IRSET> RunRpnSearch(
    VIDB& database,
    const STRING& query,
    TestContext& tests)
{
  PIRSET result = database.SearchRpn(query, ByScore);

  IB_CHECK(tests, result != nullptr);

  return std::unique_ptr<IRSET>(result);
}

bool ReadEntry(
    const IRSET& result,
    size_t position,
    IRESULT* entry)
{
  return result.GetEntry(position, entry);
}

UINT MaximumAuxCount(const IRSET& result)
{
  UINT maximum = 0;
  IRESULT entry;

  for (size_t position = 1;
       position <= result.GetTotalEntries();
       ++position) {
    if (ReadEntry(result, position, &entry) &&
        entry.GetAuxCount() > maximum) {
      maximum = entry.GetAuxCount();
    }
  }

  return maximum;
}

bool HasMinimumAuxCount(
    const IRSET& result,
    UINT minimum)
{
  IRESULT entry;

  for (size_t position = 1;
       position <= result.GetTotalEntries();
       ++position) {
    if (!ReadEntry(result, position, &entry) ||
        entry.GetAuxCount() < minimum) {
      return false;
    }
  }

  return true;
}

bool HasExactAuxCount(
    const IRSET& result,
    UINT expected)
{
  IRESULT entry;

  for (size_t position = 1;
       position <= result.GetTotalEntries();
       ++position) {
    if (!ReadEntry(result, position, &entry) ||
        entry.GetAuxCount() != expected) {
      return false;
    }
  }

  return true;
}

bool SameResultSet(
    const IRSET& left,
    const IRSET& right)
{
  if (left.GetTotalEntries() != right.GetTotalEntries())
    return false;

  IRESULT left_entry;
  IRESULT right_entry;

  for (size_t position = 1;
       position <= left.GetTotalEntries();
       ++position) {
    if (!ReadEntry(left, position, &left_entry) ||
        !ReadEntry(right, position, &right_entry)) {
      return false;
    }

    if (left_entry.GetIndex() != right_entry.GetIndex() ||
        left_entry.GetHitCount() != right_entry.GetHitCount() ||
        left_entry.GetAuxCount() != right_entry.GetAuxCount() ||
        left_entry.GetScore() != right_entry.GetScore()) {
      return false;
    }
  }

  return true;
}

long ElapsedMilliseconds(clock_t started)
{
  return static_cast<long>(
      (clock() - started) * 1000 / CLOCKS_PER_SEC);
}

void TestOperators(
    VIDB& database,
    TestContext& tests)
{
  std::cout << "\n[operators: Wikipedia fixture]\n";

  static const STRING broad_query(
      "willian shakespeare OR is OR a OR");
  static const STRING reduce_zero_query(
      "willian shakespeare OR is OR a OR REDUCE:0");
  static const STRING reduce_three_query(
      "willian shakespeare OR is OR a OR REDUCE:3");
  static const STRING reduce_five_query(
      "willian shakespeare OR is OR a OR REDUCE:5");
  static const STRING focus_three_query(
      "willian shakespeare OR is OR a OR FOCUS:3");
  static const STRING focus_five_query(
      "willian shakespeare OR is OR a OR FOCUS:5");
  static const STRING peer_query(
      "william shakespeare PEER is PEER a PEER");

  std::unique_ptr<IRSET> broad =
      RunRpnSearch(database, broad_query, tests);
  std::unique_ptr<IRSET> reduce_zero =
      RunRpnSearch(database, reduce_zero_query, tests);
  std::unique_ptr<IRSET> reduce_three =
      RunRpnSearch(database, reduce_three_query, tests);
  std::unique_ptr<IRSET> reduce_five =
      RunRpnSearch(database, reduce_five_query, tests);
  std::unique_ptr<IRSET> focus_three =
      RunRpnSearch(database, focus_three_query, tests);
  std::unique_ptr<IRSET> focus_five =
      RunRpnSearch(database, focus_five_query, tests);

  if (!broad || !reduce_zero || !reduce_three ||
      !reduce_five || !focus_three || !focus_five) {
    return;
  }

  const UINT maximum_joint = MaximumAuxCount(*broad);

  IB_CHECK(tests, maximum_joint == 3);

  IB_CHECK(tests, reduce_zero->GetTotalEntries() == 39);
  IB_CHECK(tests, HasExactAuxCount(*reduce_zero, maximum_joint));

  IB_CHECK(tests, reduce_three->GetTotalEntries() == 39);
  IB_CHECK(tests, HasMinimumAuxCount(*reduce_three, 3));

  IB_CHECK(tests, reduce_five->GetTotalEntries() == 0);

  IB_CHECK(tests, focus_three->GetTotalEntries() == 39);
  IB_CHECK(tests, HasExactAuxCount(*focus_three, 3));
  IB_CHECK(tests, SameResultSet(*focus_three, *reduce_three));

  IB_CHECK(tests, focus_five->GetTotalEntries() > 0);
  IB_CHECK(tests, HasMinimumAuxCount(*focus_five, 2));

  /*
   * Verify that strict reduction filters the existing score ordering
   * rather than reordering surviving IRESULTs.
   */
  size_t reduced_position = 1;
  IRESULT broad_entry;
  IRESULT reduced_entry;

  for (size_t broad_position = 1;
       broad_position <= broad->GetTotalEntries() &&
       reduced_position <= reduce_three->GetTotalEntries();
       ++broad_position) {
    IB_CHECK(
        tests,
        ReadEntry(*broad, broad_position, &broad_entry));

    if (broad_entry.GetAuxCount() < 3)
      continue;

    IB_CHECK(
        tests,
        ReadEntry(*reduce_three, reduced_position, &reduced_entry));

    IB_CHECK(
        tests,
        broad_entry.GetIndex() == reduced_entry.GetIndex());

    ++reduced_position;
  }

  IB_CHECK(
      tests,
      reduced_position == reduce_three->GetTotalEntries() + 1);

  const clock_t peer_started = clock();
  std::unique_ptr<IRSET> peer =
      RunRpnSearch(database, peer_query, tests);
  const long peer_ms = ElapsedMilliseconds(peer_started);

  if (peer) {
    IB_CHECK(tests, peer->GetTotalEntries() == 21);

    IRESULT first;
    IB_CHECK(tests, ReadEntry(*peer, 1, &first));
    IB_CHECK(tests, first.GetHitCount() == 6686);
  }

  const clock_t focus_started = clock();
  std::unique_ptr<IRSET> timed_focus =
      RunRpnSearch(database, focus_five_query, tests);
  const long focus_ms = ElapsedMilliseconds(focus_started);

  const clock_t reduce_started = clock();
  std::unique_ptr<IRSET> timed_reduce =
      RunRpnSearch(database, reduce_three_query, tests);
  const long reduce_ms = ElapsedMilliseconds(reduce_started);

  IB_CHECK(tests, timed_focus != nullptr);
  IB_CHECK(tests, timed_reduce != nullptr);

  std::cout
      << "PEER: " << peer_ms << " ms\n"
      << "FOCUS: " << focus_ms << " ms\n"
      << "REDUCE: " << reduce_ms << " ms\n";

#if defined(IB_ENABLE_PERFORMANCE_GUARDS) && !defined(DEBUG)
  IB_CHECK(tests, peer_ms < 5000);
  IB_CHECK(tests, focus_ms < 5000);
  IB_CHECK(tests, reduce_ms < 5000);
#endif
}

void TestCopyOnWrite(
    VIDB& database,
    const Options& options,
    TestContext& tests)
{
  std::cerr << "\n[copy-on-write]\n";

  {
    std::unique_ptr<IRSET> searched =
        RunSearch(database, options.query, tests);

    if (!searched)
      return;

    std::cerr << "COW 1: search result acquired\n";

    const size_t original_entries =
        searched->GetTotalEntries();
    const size_t original_hits =
        searched->GetHitTotal();

    IRSET original(*searched);
    searched.reset();

    std::cerr << "COW 2: original wrapper copied\n";

    {
      IRSET copy(original);

      std::cerr << "COW 3: second wrapper copied\n";

      copy.Clear();

      std::cerr << "COW 4: copy cleared\n";

      IB_CHECK(tests, copy.GetTotalEntries() == 0);
      IB_CHECK(tests,
               original.GetTotalEntries() == original_entries);
      IB_CHECK(tests,
               original.GetHitTotal() == original_hits);
    }

    std::cerr << "COW 4a: cleared copy destroyed\n";

    {
#if 0
      std::cerr << "COW 5: constructing resized\n";

      IRSET resized(original);

      std::cerr << "COW 6: resized constructed\n";
      std::cerr << "COW 7: before Resize(0)\n";

      resized.Resize(0);
      std::cerr << "COW 8: after Resize(0)\n";
      IB_CHECK(tests, resized.GetTotalEntries() == 0);
#else
      std::cerr << "Skip COW 5 - 8\n";

#endif
      IB_CHECK(tests,
               original.GetTotalEntries() == original_entries);
      IB_CHECK(tests,
               original.GetHitTotal() == original_hits);

      std::cerr << "COW 9: leaving resized scope\n";
    }

    std::cerr << "COW 10: resized destroyed\n";

#if defined(IB_USE_COW_IRSET)
    std::cerr
        << "COW 10a: original use="
        << original.UseCount()
        << " unique=" << original.IsUnique()
        << '\n';
#endif

    /*
     * Force accesses to the original immediately before destruction.
     */
    std::cerr
        << "COW 10b: original entries="
        << original.GetTotalEntries()
        << " hits=" << original.GetHitTotal()
        << '\n';



std::cerr << "COW 10c: validating original entries\n";

IRESULT result;

for (size_t i = 1;
     i <= original.GetTotalEntries();
     ++i) {
  if (!original.GetEntry(i, &result)) {
    std::cerr << "COW invalid entry at " << i << '\n';
    break;
  }

  volatile const size_t hits =
      result.GetHitCount();

  (void)hits;
}

std::cerr << "COW 10d: all original entries readable\n";

    std::cerr << "COW 11: original about to be destroyed\n";
  }

  std::cerr << "COW 12: all COW objects destroyed\n";

std::cerr << "COW 12: all COW objects destroyed\n";

std::cerr << "COW 13: probing next search\n";

std::unique_ptr<IRSET> probe =
    RunSearch(database, options.query, tests);

std::cerr << "COW 14: probe search returned\n";

}

void TestDuplicate(
    VIDB& database,
    const Options& options,
    TestContext& tests)
{
  std::cout << "\n[duplicate]\n";

  std::unique_ptr<IRSET> original =
      RunSearch(database, options.query, tests);

  if (!original)
    return;

  const size_t original_entries =
      original->GetTotalEntries();

  /*
   * Duplicate() returns OPOBJ*. The returned object must have a distinct
   * wrapper address and must be independently deletable.
   */
  std::unique_ptr<OPOBJ> duplicated_object(
      original->Duplicate());

  IB_CHECK(tests, duplicated_object != nullptr);

  IB_CHECK(
      tests,
      duplicated_object.get() !=
          static_cast<OPOBJ*>(original.get()));

  /*
   * IRSET is expected to be polymorphic through OPERAND/OPOBJ.
   */
  IRSET* duplicate =
      dynamic_cast<IRSET*>(duplicated_object.get());

  IB_CHECK(tests, duplicate != nullptr);

  if (duplicate == nullptr)
    return;

  IB_CHECK(
      tests,
      duplicate->GetTotalEntries() == original_entries);

#if defined(IB_USE_COW_IRSET)
  IB_CHECK(tests, original->IsShared());
  IB_CHECK(tests, duplicate->IsShared());
#endif

  duplicate->Clear();

  IB_CHECK(tests, duplicate->GetTotalEntries() == 0);

  IB_CHECK(
      tests,
      original->GetTotalEntries() == original_entries);

#if defined(IB_USE_COW_IRSET)
  IB_CHECK(tests, original->IsUnique());
  IB_CHECK(tests, duplicate->IsUnique());
#endif
}

} // namespace

extern "C" int _Ibtest_main(int argc, char** argv);

int _Ibtest_main(int argc, char** argv)
{
#ifdef __GNUG__
  std::ios::sync_with_stdio(false);
#endif

  Options options;

  if (!ParseOptions(argc, argv, &options))
    return argc > 1 ? EXIT_FAILURE : EXIT_SUCCESS;

  TestContext tests(options.verbose);

  /*
   * VIDB supports opening a database for searching through the
   * VIDB(const STRING&, bool) constructor.
   */
  VIDB database(options.database, true);

  IB_CHECK(tests, database.Ok());

  if (!database.Ok()) {
    std::cerr
        << "Unable to open database: "
        << options.database
        << '\n';

    if (database.ErrorMessage() != nullptr)
      std::cerr << database.ErrorMessage() << '\n';

    return tests.Result();
  }

  if (options.run_search)
    TestSearch(database, options, tests);

  if (options.run_cow)
    TestCopyOnWrite(database, options, tests);

  if (options.run_duplicate)
    TestDuplicate(database, options, tests);

  if (options.run_operators)
    TestOperators(database, tests);

  return tests.Result();
}

#ifndef IB_NO_STANDALONE_MAIN

int main(int argc, char** argv)
{
  return _Ibtest_main(argc, argv);
}

#endif
