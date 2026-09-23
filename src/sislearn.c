/*
 * sislearn.c -- cheap vocabulary/frequency analysis for Isearch SIS files.
 *
 * This intentionally does not open an IDB/INDEX.  It mirrors sisdump.c and
 * recovers each SIS bucket's global term frequency from adjacent .inx end
 * positions.  The output is intended to seed/train natural-query rewrite
 * policy (NORMAL/PROMOTE/FILTER), not to change indexing semantics.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <arpa/inet.h>

#include "gdt-sys.h"

typedef unsigned long long ULL;

typedef struct {
  char *term;
  ULL tf;
  unsigned int sources;
  unsigned char overflow;   /* len == StringCompLength: SIS prefix bucket */
} TERM_ENTRY;

typedef struct {
  TERM_ENTRY *entry;
  size_t used;
  size_t size;
} TERM_TABLE;

typedef struct {
  ULL sis_records;
  ULL indexed_occurrences;
  ULL overflow_records;
  unsigned int files;
} TRAIN_STATS;

static GPTYPE GP(const void *ptr, int y)
{
#if IS_LITTLE_ENDIAN == 0
  GPTYPE Gpp;
  memcpy(&Gpp, (const unsigned char *)ptr + y, sizeof(GPTYPE));
  return Gpp;
#else
# ifdef O_BUILD_IB64
  const unsigned char *x = (const unsigned char *)ptr;
  return (((GPTYPE)x[y])   << 56) + (((GPTYPE)x[y+1]) << 48) +
         (((GPTYPE)x[y+2]) << 40) + (((GPTYPE)x[y+3]) << 32) +
         (((GPTYPE)x[y+4]) << 24) + (((GPTYPE)x[y+5]) << 16) +
         (((GPTYPE)x[y+6]) << 8)  + x[y+7];
# else
  const unsigned char *x = (const unsigned char *)ptr;
  return (((GPTYPE)x[y])   << 24) + (((GPTYPE)x[y+1]) << 16) +
         (((GPTYPE)x[y+2]) << 8)  + x[y+3];
# endif
#endif
}

static char *copy_term(const unsigned char *s, size_t n)
{
  char *p = (char *)malloc(n + 1);
  if (p == NULL)
    return NULL;
  memcpy(p, s, n);
  p[n] = '\0';
  return p;
}

static int append_term(TERM_TABLE *table, const unsigned char *term,
                       size_t len, ULL tf, int overflow)
{
  TERM_ENTRY *p;

  if (table->used == table->size) {
    size_t new_size = table->size ? table->size * 2 : 4096;
    p = (TERM_ENTRY *)realloc(table->entry, new_size * sizeof(*p));
    if (p == NULL)
      return 0;
    table->entry = p;
    table->size = new_size;
  }

  p = &table->entry[table->used];
  p->term = copy_term(term, len);
  if (p->term == NULL)
    return 0;
  p->tf = tf;
  p->sources = 1;
  p->overflow = overflow ? 1 : 0;
  table->used++;
  return 1;
}

static int read_sis(const char *filename, TERM_TABLE *table, TRAIN_STATS *stats)
{
  FILE *fp;
  unsigned char *term = NULL;
  unsigned char posbuf[sizeof(GPTYPE)];
  GPTYPE previous = 0;
  ULL entry_no = 0;
  int comp_length, charset, len;

  fp = fopen(filename, "rb");
  if (fp == NULL) {
    perror(filename);
    return 0;
  }

  comp_length = fgetc(fp);
  charset = fgetc(fp);
  if (comp_length == EOF || charset == EOF || comp_length <= 0) {
    fprintf(stderr, "%s: invalid SIS header\n", filename);
    fclose(fp);
    return 0;
  }

  term = (unsigned char *)malloc((size_t)comp_length + 1);
  if (term == NULL) {
    fprintf(stderr, "%s: out of memory\n", filename);
    fclose(fp);
    return 0;
  }

  fprintf(stderr, "# %s: StringCompLength=%d charset=%d\n",
          filename, comp_length, charset);

  while ((len = fgetc(fp)) != EOF) {
    GPTYPE current;
    ULL tf;
    size_t term_len;
    int overflow;

    if (fread(term, 1, (size_t)comp_length, fp) != (size_t)comp_length ||
        fread(posbuf, 1, sizeof(GPTYPE), fp) != sizeof(GPTYPE)) {
      fprintf(stderr, "%s: truncated SIS record after %llu entries\n",
              filename, entry_no);
      free(term);
      fclose(fp);
      return 0;
    }

    if (len < 0 || len > comp_length) {
      fprintf(stderr, "%s: invalid term length %d (limit %d)\n",
              filename, len, comp_length);
      free(term);
      fclose(fp);
      return 0;
    }

    current = GP(posbuf, 0);
    if (entry_no == 0) {
      /* INDEX::findIt(): first bucket is [0..current], inclusive. */
      tf = (ULL)current + 1ULL;
    } else {
      if (current < previous) {
        fprintf(stderr, "%s: non-monotonic SIS position at entry %llu\n",
                filename, entry_no + 1);
        free(term);
        fclose(fp);
        return 0;
      }
      /* Later buckets are [previous+1..current], inclusive. */
      tf = (ULL)(current - previous);
    }

    term_len = (size_t)len;
    overflow = (len >= comp_length);
    term[term_len] = '\0';

    if (!append_term(table, term, term_len, tf, overflow)) {
      fprintf(stderr, "%s: out of memory\n", filename);
      free(term);
      fclose(fp);
      return 0;
    }

    stats->sis_records++;
    stats->indexed_occurrences += tf;
    if (overflow)
      stats->overflow_records++;

    previous = current;
    entry_no++;
  }

  stats->files++;
  free(term);
  fclose(fp);
  return 1;
}

static int lexical_compare(const void *a_, const void *b_)
{
  const TERM_ENTRY *a = (const TERM_ENTRY *)a_;
  const TERM_ENTRY *b = (const TERM_ENTRY *)b_;
  int cmp = strcmp(a->term, b->term);
  if (cmp)
    return cmp;
  return (int)a->overflow - (int)b->overflow;
}

static int frequency_compare(const void *a_, const void *b_)
{
  const TERM_ENTRY *a = (const TERM_ENTRY *)a_;
  const TERM_ENTRY *b = (const TERM_ENTRY *)b_;
  if (a->tf < b->tf)
    return 1;
  if (a->tf > b->tf)
    return -1;
  return strcmp(a->term, b->term);
}

static size_t merge_terms(TERM_TABLE *table)
{
  size_t in, out = 0;

  if (table->used == 0)
    return 0;

  qsort(table->entry, table->used, sizeof(table->entry[0]), lexical_compare);

  for (in = 0; in < table->used; ++in) {
    if (out != 0 &&
        table->entry[out - 1].overflow == table->entry[in].overflow &&
        strcmp(table->entry[out - 1].term, table->entry[in].term) == 0) {
      table->entry[out - 1].tf += table->entry[in].tf;
      table->entry[out - 1].sources += table->entry[in].sources;
      free(table->entry[in].term);
    } else {
      if (out != in)
        table->entry[out] = table->entry[in];
      out++;
    }
  }

  table->used = out;
  return out;
}

static size_t utf8_chars(const char *s)
{
  const unsigned char *p = (const unsigned char *)s;
  size_t n = 0;
  while (*p) {
    if ((*p & 0xC0) != 0x80)
      ++n;
    ++p;
  }
  return n;
}

static const char *token_shape(const char *s)
{
  const unsigned char *p = (const unsigned char *)s;
  int alpha = 0, digit = 0, punct = 0, other = 0;

  while (*p) {
    if (*p < 0x80) {
      if (isalpha(*p)) alpha = 1;
      else if (isdigit(*p)) digit = 1;
      else if (ispunct(*p)) punct = 1;
      else other = 1;
    } else {
      other = 1; /* UTF-8: do not pretend the byte is ASCII punctuation. */
    }
    ++p;
  }

  if (alpha && !digit && !punct && !other) return "alpha";
  if (!alpha && digit && !punct && !other) return "numeric";
  if (alpha && digit && !punct && !other) return "alnum";
  if (punct) return "punct/mixed";
  return "other/mixed";
}

static int ascii_alpha(const char *s)
{
  const unsigned char *p = (const unsigned char *)s;
  if (*p == '\0')
    return 0;
  while (*p) {
    if (*p >= 0x80 || !isalpha(*p))
      return 0;
    ++p;
  }
  return 1;
}

/* v is sorted by descending TF. q is the usual ascending quantile [0,1]. */
static ULL tf_quantile(const TERM_ENTRY *v, size_t n, double q)
{
  size_t asc, desc;
  if (n == 0)
    return 0;
  if (q < 0.0) q = 0.0;
  if (q > 1.0) q = 1.0;
  asc = (size_t)(q * (double)(n - 1));
  desc = (n - 1) - asc;
  return v[desc].tf;
}

static void usage(const char *prog)
{
  fprintf(stderr,
    "Usage: %s [-p tail-percent] [-n max-rows] sisfile [sisfile ...]\n"
    "\n"
    "  -p pct   inspect the highest-frequency pct%% of exact SIS terms\n"
    "           (default 0.25)\n"
    "  -n rows  cap report rows (default 1000; 0 means no cap)\n"
    "\n"
    "The program does not classify FILTER terms.  It emits conservative\n"
    "rewrite-training candidates: very common 2/3-character ASCII words get\n"
    "a SHORT_COMMON prior; other high-frequency terms are marked PROBE.\n",
    prog);
}

int main(int argc, char **argv)
{
  TERM_TABLE table = {0, 0, 0};
  TRAIN_STATS stats = {0, 0, 0, 0};
  TERM_ENTRY *exact = NULL;
  size_t exact_count = 0, overflow_count = 0;
  size_t candidate_count, report_count;
  size_t i, j;
  double tail_percent = 0.25;
  unsigned long max_rows = 1000;
  int first_file = 1;

  for (i = 1; i < (size_t)argc; ++i) {
    if (strcmp(argv[i], "-p") == 0) {
      char *end = NULL;
      if (++i >= (size_t)argc) { usage(argv[0]); return 2; }
      errno = 0;
      tail_percent = strtod(argv[i], &end);
      if (errno || end == argv[i] || *end || tail_percent <= 0.0 || tail_percent > 100.0) {
        fprintf(stderr, "Invalid tail percent: %s\n", argv[i]);
        return 2;
      }
    } else if (strcmp(argv[i], "-n") == 0) {
      char *end = NULL;
      if (++i >= (size_t)argc) { usage(argv[0]); return 2; }
      errno = 0;
      max_rows = strtoul(argv[i], &end, 10);
      if (errno || end == argv[i] || *end) {
        fprintf(stderr, "Invalid row count: %s\n", argv[i]);
        return 2;
      }
    } else if (argv[i][0] == '-') {
      usage(argv[0]);
      return 2;
    } else {
      first_file = (int)i;
      break;
    }
  }

  if (first_file == 1 && argc > 1 && argv[1][0] == '-') {
    /* Options consumed all arguments. */
    first_file = argc;
  }
  if (argc == 1 || first_file >= argc) {
    usage(argv[0]);
    return 1;
  }

  for (i = (size_t)first_file; i < (size_t)argc; ++i) {
    if (!read_sis(argv[i], &table, &stats)) {
      for (j = 0; j < table.used; ++j)
        free(table.entry[j].term);
      free(table.entry);
      return 3;
    }
  }

  merge_terms(&table);

  for (i = 0; i < table.used; ++i) {
    if (table.entry[i].overflow)
      overflow_count++;
    else
      exact_count++;
  }

  if (exact_count) {
    exact = (TERM_ENTRY *)malloc(exact_count * sizeof(*exact));
    if (exact == NULL) {
      fprintf(stderr, "Out of memory\n");
      return 4;
    }
    for (i = 0, j = 0; i < table.used; ++i)
      if (!table.entry[i].overflow)
        exact[j++] = table.entry[i];
    qsort(exact, exact_count, sizeof(*exact), frequency_compare);
  }

  candidate_count = (size_t)((tail_percent / 100.0) * (double)exact_count);
  if (candidate_count == 0 && exact_count)
    candidate_count = 1;
  if (candidate_count > exact_count)
    candidate_count = exact_count;

  report_count = candidate_count;
  if (max_rows && report_count > (size_t)max_rows)
    report_count = (size_t)max_rows;

  printf("# sislearn-v1\n");
  printf("# files=%u\n", stats.files);
  printf("# signature.sis_unique_keys=%llu\n", (ULL)table.used);
  printf("# signature.exact_tokens=%llu\n", (ULL)exact_count);
  printf("# signature.overflow_prefixes=%llu\n", (ULL)overflow_count);
  printf("# sis_records=%llu\n", stats.sis_records);
  printf("# indexed_occurrences=%llu\n", stats.indexed_occurrences);
  printf("# tf.q50=%llu\n", tf_quantile(exact, exact_count, 0.50));
  printf("# tf.q90=%llu\n", tf_quantile(exact, exact_count, 0.90));
  printf("# tf.q99=%llu\n", tf_quantile(exact, exact_count, 0.99));
  printf("# tf.q999=%llu\n", tf_quantile(exact, exact_count, 0.999));
  printf("# candidate_tail_percent=%.6g\n", tail_percent);
  printf("# candidate_terms=%llu\n", (ULL)candidate_count);
  printf("# reported_terms=%llu\n", (ULL)report_count);
  printf("rank\tterm\ttf\tppm\tchars\tshape\tprior\tsources\n");

  for (i = 0; i < report_count; ++i) {
    size_t chars = utf8_chars(exact[i].term);
    const char *prior =
      (chars >= 2 && chars <= 3 && ascii_alpha(exact[i].term))
        ? "SHORT_COMMON"
        : "PROBE";
    double ppm = stats.indexed_occurrences
      ? (1000000.0 * (double)exact[i].tf / (double)stats.indexed_occurrences)
      : 0.0;

    printf("%llu\t%s\t%llu\t%.3f\t%llu\t%s\t%s\t%u\n",
           (ULL)(i + 1), exact[i].term, exact[i].tf, ppm,
           (ULL)chars, token_shape(exact[i].term), prior,
           exact[i].sources);
  }

  free(exact);
  for (i = 0; i < table.used; ++i)
    free(table.entry[i].term);
  free(table.entry);
  return 0;
}
