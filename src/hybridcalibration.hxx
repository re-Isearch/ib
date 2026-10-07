/*
Copyright (c) 2026 Project re-Isearch and its contributors: See CONTRIBUTORS.
It is made available and licensed under the Apache 2.0 license: see LICENSE
*/
#ifndef HYBRIDCALIBRATION_HXX
#define HYBRIDCALIBRATION_HXX

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

// Absolute, result-set-independent vector evidence. The anchors are expressed
// in the source's score domain, including Schmate's (1 + similarity) / 2.
class VectorEvidenceCalibration
{
public:
  double noise = 0.5;
  double strong = 1.0;
  double floor = 0.0;

  static bool Finite(double value)
  {
    // IB permits -ffast-math builds, where isfinite can be optimized away.
    // Inspect the IEEE-754 exponent so invalid external scores stay invalid.
    static_assert(sizeof(double) == sizeof(uint64_t), "64-bit scores required");
    static_assert(std::numeric_limits<double>::is_iec559, "IEEE-754 scores required");
    uint64_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return (bits & UINT64_C(0x7ff0000000000000)) != UINT64_C(0x7ff0000000000000);
  }

  // Uniform histogram boundary counts: 0, ..., total samples. Precompute
  // surprisal so each candidate needs only an O(1) lookup and interpolation.
  bool SetBackground(const std::string& counts, double minimum, double maximum)
  {
    information.clear();
    if (!Finite(minimum) || !Finite(maximum) || maximum <= minimum ||
        !Finite(maximum - minimum))
      return false;

    std::istringstream input(counts);
    std::vector<double> cumulative;
    double count;
    while (input >> count)
    {
      if (!Finite(count) || count < 0.0 || count != std::floor(count) ||
          (!cumulative.empty() && count < cumulative.back()))
        return false;
      cumulative.push_back(count);
      if (cumulative.size() > 4097) return false;
    }
    if (!input.eof() || cumulative.size() < 3 || cumulative.front() != 0.0 ||
        cumulative.back() <= 0.0)
      return false;

    cdfMinimum = minimum;
    cdfMaximum = maximum;
    const double total = cumulative.back();
    information.reserve(cumulative.size());
    for (double value : cumulative)
      information.push_back(std::log1p(total) - std::log1p(total - value));

    // A background with no resolution between the chosen anchors cannot
    // calibrate them. Let the caller fall back to the fixed score anchors.
    if (Information(strong) <= Information(noise))
    {
      information.clear();
      return false;
    }
    return true;
  }

  double Evidence(double score) const
  {
    // Invalid native scores supply no evidence, even with a legacy floor.
    if (!Finite(score))
      return 0.0;
    double x;
    if (information.empty())
      x = (score - noise) / (strong - noise);
    else
    {
      const double low = Information(noise);
      x = (Information(score) - low) / (Information(strong) - low);
    }
    x = std::clamp(x, 0.0, 1.0);
    x = x * x * (3.0 - 2.0 * x); // smoothstep; monotonic and bounded
    return floor + (1.0 - floor) * x;
  }

private:
  std::vector<double> information;
  double cdfMinimum = 0.0;
  double cdfMaximum = 1.0;

  double Information(double score) const
  {
    const double position = std::clamp(
        (score - cdfMinimum) / (cdfMaximum - cdfMinimum), 0.0, 1.0) *
        (information.size() - 1);
    const size_t bin = static_cast<size_t>(position);
    if (bin + 1 >= information.size())
      return information.back();
    return information[bin] + (position - bin) *
        (information[bin + 1] - information[bin]);
  }
};

#endif
