
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
      << "  --all          Run all tests; default\n"
      << "  -h, --help     Display this help\n";
}

bool ParseOptions(int argc, char** argv, Options* options)
{
  bool selected_test = false;

  options->run_search = false;
  options->run_cow = false;
  options->run_duplicate = false;

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

  std::unique_ptr<IRSET> result =
      RunSearch(database, options.query, tests);

  if (!result)
    return;

  std::cout
      << "Query: " << options.query << '\n'
      << "Entries: " << result->GetTotalEntries() << '\n'
      << "Hits: " << result->GetHitTotal() << '\n';
}

void TestCopyOnWrite(
    VIDB& database,
    const Options& options,
    TestContext& tests)
{
  std::cout << "\n[copy-on-write]\n";

  std::unique_ptr<IRSET> searched =
      RunSearch(database, options.query, tests);

  if (!searched)
    return;

  /*
   * Copy the result into a local value, then destroy the original
   * heap-allocated search result. This also tests that the copy owns a
   * valid representation independently of the outer wrapper.
   */

// std::cerr << "COW 1: search result acquired\n";

  IRSET original(*searched);
  searched.reset();

//  std::cerr << "COW 2: original wrapper copied\n";

  const size_t original_entries = original.GetTotalEntries();

  const size_t original_hits = original.GetHitTotal();

  IB_CHECK(tests, original_entries > 0);

  /*
   * This should initially share the atomicIRSET representation when
   * IRSET is configured as _IRSET.
   */
  IRSET copy(original);
// std::cerr << "COW 3: second wrapper copied\n";


#if defined(IB_USE_COW_IRSET)
  IB_CHECK(tests, original.IsShared());
  IB_CHECK(tests, copy.IsShared());
  IB_CHECK(tests, original.UseCount() == 2);
  IB_CHECK(tests, copy.UseCount() == 2);
#endif

  IB_CHECK(
      tests,
      copy.GetTotalEntries() == original_entries);

  IB_CHECK(
      tests,
      copy.GetHitTotal() == original_hits);

  /*
   * Clear is a strong COW test: the copied result is changed completely,
   * while the original result must remain intact.
   */
  copy.Clear();

// std::cerr << "COW 4: copy cleared\n";


  IB_CHECK(tests, copy.GetTotalEntries() == 0);

  IB_CHECK(
      tests,
      original.GetTotalEntries() == original_entries);

  IB_CHECK(
      tests,
      original.GetHitTotal() == original_hits);

#if defined(IB_USE_COW_IRSET)
  IB_CHECK(tests, original.IsUnique());
  IB_CHECK(tests, copy.IsUnique());
  IB_CHECK(tests, original.UseCount() == 1);
  IB_CHECK(tests, copy.UseCount() == 1);
#endif

  /*
   * A second copy tests another mutating forwarding method. Resize()
   * previously contained a path that could bypass detachment.
   */
  IRSET resized(original);

#if defined(IB_USE_COW_IRSET)
  IB_CHECK(tests, resized.IsShared());
#endif

  resized.Resize(0);

  IB_CHECK(tests, resized.GetTotalEntries() == 0);

  IB_CHECK(
      tests,
      original.GetTotalEntries() == original_entries);

#if defined(IB_USE_COW_IRSET)
  IB_CHECK(tests, resized.IsUnique());
  IB_CHECK(tests, original.IsUnique());
#endif
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

  return tests.Result();
}

#ifndef IB_NO_STANDALONE_MAIN

int main(int argc, char** argv)
{
  return _Ibtest_main(argc, argv);
}

#endif
