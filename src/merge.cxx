/*
Copyright (c) 2020-26 Project re-Isearch and its contributors: See CONTRIBUTORS.
It is made available and licensed under the Apache 2.0 license: see LICENSE
*/
#include "merge.hxx"
#include "mergeunit.hxx"
#include "common.hxx"
#include "dirent.hxx"
#include <algorithm>
#include <memory>
#include <string>
#include <limits>

namespace {
struct Output {
  MERGE::INPUT names;
  FILE *index = nullptr, *sis = nullptr;
  std::vector<char> indexBuffer, sisBuffer;
  bool keep = false;
  explicit Output(const MERGE::INPUT& paths) : names(paths) {}
  ~Output() {
    if (index) fclose(index);
    if (sis) fclose(sis);
    if (!keep) { UnlinkFile(names.index); UnlinkFile(names.sis); }
  }
  bool Open() {
    index = fopen(names.index, "wb");
    sis = fopen(names.sis, "wb");
    if (!index || !sis) return false;
    indexBuffer.resize(256*1024); sisBuffer.resize(64*1024);
    return setvbuf(index, indexBuffer.data(), _IOFBF, indexBuffer.size()) == 0 &&
           setvbuf(sis, sisBuffer.data(), _IOFBF, sisBuffer.size()) == 0;
  }
  bool Finish() {
    const bool goodStreams = !ferror(index) && !ferror(sis);
    const bool goodIndex = fclose(index) == 0; index = nullptr;
    const bool goodSis = fclose(sis) == 0; sis = nullptr;
    return keep = goodStreams && goodIndex && goodSis;
  }
};

// A winner tree stores input indices, not keys or posting objects. Replaying
// one changed leaf needs at most ceil(log2(k)) key comparisons. Equal keys are
// resolved by input order, preserving the existing partition order of postings.
class Tournament {
public:
  explicit Tournament(const std::vector<std::unique_ptr<MERGEUNIT>>& streams,
                      MERGE::STATS& counts)
    : inputs(streams), stats(counts), charset(streams.front()->CharsetId()) {
    while (leaves < inputs.size()) leaves *= 2;
    nodes.assign(2*leaves, None);
    for (size_t i = 0; i < inputs.size(); ++i)
      if (!inputs[i]->Empty()) nodes[leaves+i] = i;
    for (size_t i = leaves; --i;) nodes[i] = Winner(nodes[2*i], nodes[2*i+1]);
  }
  size_t Head() const { return nodes[1]; }
  void Replay(size_t input) {
    size_t i = leaves+input;
    nodes[i] = inputs[input]->Empty() ? None : input;
    while ((i /= 2) != 0) nodes[i] = Winner(nodes[2*i], nodes[2*i+1]);
  }
  static constexpr size_t None = size_t(-1);

private:
  size_t Winner(size_t a, size_t b) {
    if (a == None) return b;
    if (b == None) return a;
    ++stats.comparisons;
    const int cmp = charset.CompareSIS(inputs[a]->Key(), inputs[a]->KeyLength(),
                                       inputs[b]->Key(), inputs[b]->KeyLength());
    return cmp < 0 || (cmp == 0 && a < b) ? a : b;
  }
  const std::vector<std::unique_ptr<MERGEUNIT>>& inputs;
  MERGE::STATS& stats;
  CHARSET charset;
  size_t leaves = 1;
  std::vector<size_t> nodes;
};

bool ZipPass(const std::vector<MERGE::INPUT>& paths, const MERGE::INPUT& destination,
             UINT2 magic, unsigned long long capacity, long commonThreshold,
             std::vector<STRING> *commonWords, MERGE::STATS& stats)
{
  std::vector<std::unique_ptr<MERGEUNIT>> inputs;
  capacity = std::min(capacity, (unsigned long long)std::numeric_limits<GPTYPE>::max());
  unsigned long long count = 0;
  for (const auto& path : paths) {
    auto input = std::make_unique<MERGEUNIT>();
    if (!input->Open(path.index, path.sis, magic)) return false;
    if (!inputs.empty() && (input->Width() != inputs.front()->Width() ||
                           input->CharsetId() != inputs.front()->CharsetId())) {
      message_log(LOG_ERROR, "Merge: incompatible SIS prefix widths or character sets");
      return false;
    }
    if (input->PostingCount() >= capacity || count >= capacity-input->PostingCount()) {
      message_log(LOG_ERROR, "Merge: posting capacity exceeded");
      return false;
    }
    count += input->PostingCount();
    inputs.emplace_back(std::move(input));
  }
  Output output(destination);
  if (!output.Open()) {
    message_log(LOG_ERRNO, "Merge: cannot create '%s' and '%s'",
                destination.index.c_str(), destination.sis.c_str());
    return false;
  }
  if (::Write(magic, output.index) != 2 ||
      putc(int(inputs.front()->Width()), output.sis) == EOF ||
      putc(inputs.front()->CharsetId(), output.sis) == EOF)
    return false;

  Tournament tree(inputs, stats);
  const CHARSET charset(inputs.front()->CharsetId());
  const size_t width = inputs.front()->Width();
  std::vector<UCHR> buffer(1024*1024), key(width+1);
  GPTYPE written = 0;
  size_t terms = 0;
  while (tree.Head() != Tournament::None) {
    const MERGEUNIT& first = *inputs[tree.Head()];
    std::copy(first.Row(), first.Row()+width+1, key.begin());
    const size_t keyLength = std::min(size_t(key[0]), width);
    const GPTYPE start = written;
    do {
      const size_t selected = tree.Head();
      MERGEUNIT& input = *inputs[selected];
      const GPTYPE add = input.HeadCount();
      if (!input.CopyPostings(output.index, buffer)) return false;
      written += add;
      if (!input.Advance()) return false;
      tree.Replay(selected);
      if (tree.Head() == Tournament::None) break;
    } while (charset.CompareSIS(key.data()+1, keyLength, inputs[tree.Head()]->Key(),
                               inputs[tree.Head()]->KeyLength()) == 0);

    if (fwrite(key.data(), 1, key.size(), output.sis) != key.size() ||
        ::Write(written-1, output.sis) != int(sizeof(GPTYPE)))
      return false;
    ++terms;
    if (commonWords && written-start > GPTYPE(std::max(0L, commonThreshold))) {
      STRING word; word.Assign(reinterpret_cast<const char *>(key.data()+1), keyLength);
      commonWords->push_back(word);
    }
  }
  if (written != count || !output.Finish()) return false;
  stats.postings = written; stats.terms = terms;
  return true;
}

struct Temporaries {
  std::vector<MERGE::INPUT> paths;
  ~Temporaries() {
    for (const auto& path : paths) { UnlinkFile(path.index); UnlinkFile(path.sis); }
  }
};
} // namespace

bool MERGE::Zip(const std::vector<INPUT>& sources, const INPUT& output, UINT2 magic,
                unsigned long long capacity, long commonThreshold,
                std::vector<STRING> *commonWords, STATS *statistics)
{
  if (sources.empty() || capacity == 0) return false;
  for (const auto& source : sources)
    if (source.index == output.index || source.sis == output.sis ||
        source.index == output.sis || source.sis == output.index)
      return false;

  // Reserve descriptors for the database, outputs, and unrelated open files.
  const int descriptorLimit = _IB_kernel_max_file_descriptors();
  size_t fanIn = MaxFanIn;
  if (descriptorLimit > 0)
    fanIn = std::min(fanIn, size_t(std::max(2, (descriptorLimit-64)/2)));

  Temporaries temporary;
  STATS stats;
  std::vector<INPUT> inputs(sources);
  size_t pass = 0;
  while (inputs.size() > fanIn) {
    std::vector<INPUT> next;
    for (size_t begin = 0; begin < inputs.size(); begin += fanIn) {
      const size_t end = std::min(begin+fanIn, inputs.size());
      if (end-begin == 1) { next.push_back(inputs[begin]); continue; }
      const STRING suffix = STRING().form(".zip.%lu.%lu",
                                          (unsigned long)pass, (unsigned long)next.size());
      const INPUT destination = {output.index+suffix, output.sis+suffix};
      temporary.paths.push_back(destination);
      const std::vector<INPUT> group(inputs.begin()+begin, inputs.begin()+end);
      if (!ZipPass(group, destination, magic, capacity, commonThreshold, nullptr, stats))
        return false;
      next.push_back(destination);
    }
    inputs.swap(next);
    ++pass;
  }
  std::vector<STRING> words;
  if (!ZipPass(inputs, output, magic, capacity, commonThreshold,
               commonWords ? &words : nullptr, stats))
    return false;
  if (commonWords) commonWords->swap(words);
  stats.passes = pass+1;
  if (statistics) *statistics = stats;
  return true;
}

void MERGE::RemoveTemporaries(const INPUT& output)
{
  for (const STRING& path : {output.index, output.sis}) {
    const std::string name(path.c_str());
    const size_t slash = name.find_last_of("/\\");
    const std::string directory = slash == std::string::npos ? "." : name.substr(0, slash+1);
    const std::string prefix = name.substr(slash == std::string::npos ? 0 : slash+1)+".zip.";
    DIR *dir = opendir(directory.c_str());
    if (!dir) continue;
    while (const auto *entry = readdir(dir)) {
      const std::string file(entry->d_name);
      if (file.compare(0, prefix.size(), prefix) != 0) continue;
      unsigned long pass, group;
      char extra;
      if (sscanf(file.c_str()+prefix.size(), "%lu.%lu%c", &pass, &group, &extra) == 2)
        UnlinkFile(STRING((directory+"/"+file).c_str()));
    }
    closedir(dir);
  }
}
