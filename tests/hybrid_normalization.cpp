#ifndef HYBRID_CALIBRATION_ONLY
#include "irset.hxx"
#endif
#include "hybridcalibration.hxx"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <map>
#include <string>
#include <vector>

static void require(bool condition, const char* message)
{
  if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}

static bool close(double a, double b) { return std::abs(a - b) < 1e-10; }

#ifndef HYBRID_CALIBRATION_ONLY
class Profile : public IDBOBJ
{
public:
  std::map<std::string, std::string> values;
  void ProfileGetString(const STRING& section, const STRING& key,
                        const STRING& fallback, PSTRING result) const override
  {
    const auto found = values.find(std::string(section.c_str()) + "/" + key.c_str());
    *result = found == values.end() ? fallback : STRING(found->second.c_str());
  }
};

static void populate(atomicIRSET& set, const std::vector<double>& scores)
{
  size_t index = 0;
  for (double score : scores)
  {
    IRESULT result;
    result.SetIndex(++index);
    result.SetScore(score);
    set.FastAddEntry(result);
  }
}
#endif

int main()
{
  VectorEvidenceCalibration calibration;
  require(close(calibration.Evidence(0.5), 0), "noise anchor");
  require(close(calibration.Evidence(0.75), 0.5), "midpoint");
  require(close(calibration.Evidence(1), 1), "strong anchor");
  require(close(calibration.Evidence(-1), 0), "lower clamp");
  require(close(calibration.Evidence(2), 1), "upper clamp");
  require(calibration.SetBackground("0 50 90 99 100", 0, 1), "valid CDF");
  require(close(calibration.Evidence(0.5), 0), "CDF noise anchor");
  require(close(calibration.Evidence(1), 1), "CDF strong anchor");
  require(calibration.Evidence(0.75) > 0 && calibration.Evidence(0.75) < 1, "CDF interior");
  for (double score = 0; score < 1; score += 0.01)
    require(calibration.Evidence(score) <= calibration.Evidence(score + 0.01), "CDF monotonicity");
  for (const auto* malformed : {"0 100 90", "1 2 3", "0 0 0", "0 1 nope", "0 0.5 1"})
    require(!calibration.SetBackground(malformed, 0, 1), "reject invalid CDF");
  require(close(calibration.Evidence(0.75), 0.5), "invalid CDF fallback");

#ifndef HYBRID_CALIBRATION_ONLY
  Profile profile;
  atomicIRSET strong(&profile), weak(&profile), expanded(&profile);
  populate(strong, {0.95, 0.90});
  populate(weak, {0.45, 0.40});
  populate(expanded, {0.95, 0.90, 1.0});
  strong.ComputeScoresHybridNormalization(2);
  weak.ComputeScoresHybridNormalization(2);
  expanded.ComputeScoresHybridNormalization(2);
  require(weak[0].GetScore() == 0 && weak[1].GetScore() == 0, "weak query supplies no evidence");
  require(strong[0].GetScore() < 2, "no manufactured full-score winner");
  require(close(strong[0].GetScore(), expanded[0].GetScore()) &&
          close(strong[1].GetScore(), expanded[1].GetScore()), "candidate population independence");
  const double saved = strong[0].GetScore();
  strong.ComputeScoresHybridNormalization(2);
  strong.ComputeScores(2, MaxNormalization);
  require(close(strong[0].GetScore(), saved), "no double or lexical renormalization");
  require(close(strong.GetMaxScore(), saved), "actual maximum metadata");
  require(close(strong.GetMinScore(), strong[1].GetScore()), "actual minimum metadata");

  profile.values["Hybrid/VectorNoise"] = "0.6";
  profile.values["Hybrid/VectorStrong"] = "0.8";
  profile.values["Hybrid:ABSTRACT/VectorNoise"] = "0.5";
  profile.values["Hybrid:ABSTRACT/VectorStrong"] = "1";
  atomicIRSET global(&profile), field(&profile);
  populate(global, {0.7}); populate(field, {0.7});
  global.ComputeScoresHybridNormalization(1);
  field.ComputeScoresHybridNormalization(1, "ABSTRACT");
  require(close(global[0].GetScore(), 0.5), "global anchors");
  require(close(field[0].GetScore(), 0.352), "field overrides");

  profile.values["Hybrid:ABSTRACT/VectorCDF"] = "0 50 90 99 100";
  atomicIRSET background(&profile);
  populate(background, {0.5, 0.75, 1.0});
  background.ComputeScoresHybridNormalization(1, "ABSTRACT");
  require(calibration.SetBackground("0 50 90 99 100", 0, 1), "CDF reference");
  require(close(background[1].GetScore(), calibration.Evidence(0.75)), "field CDF reaches real IRSET");
  require(background[0].GetScore() == 0 && background[2].GetScore() == 1, "CDF anchors in IRSET");

  profile.values["Hybrid/VectorNoise"] = "nan";
  profile.values["Hybrid/VectorStrong"] = "0.4";
  profile.values["Hybrid/VectorFloor"] = "0.4";
  profile.values["Hybrid/VectorCDF"] = "0 100 99";
  atomicIRSET invalid(&profile);
  populate(invalid, {0.4, 0.75, std::numeric_limits<double>::quiet_NaN(),
                     std::numeric_limits<double>::infinity()});
  invalid.ComputeScoresHybridNormalization(1);
  require(close(invalid[0].GetScore(), 0.4), "configured legacy floor");
  require(close(invalid[1].GetScore(), 0.7), "bad config falls back");
  require(invalid[2].GetScore() == 0 && invalid[3].GetScore() == 0, "invalid scores carry zero evidence");

  atomicIRSET signedWeight;
  populate(signedWeight, {0.75, 1.0});
  signedWeight.ComputeScoresHybridNormalization(-2);
  require(close(signedWeight.GetMinScore(), -2) && close(signedWeight.GetMaxScore(), -1), "negative weight metadata");
  atomicIRSET empty;
  empty.ComputeScoresHybridNormalization(1);
  require(empty.GetMinScore() == 0 && empty.GetMaxScore() == 0, "empty metadata");

  atomicIRSET invalidWeight;
  populate(invalidWeight, {0.75});
  invalidWeight.ComputeScoresHybridNormalization(std::numeric_limits<float>::infinity());
  require(invalidWeight[0].GetScore() == 0, "invalid weights carry zero evidence");

  std::cout << "Hybrid normalization regressions passed\n";
#else
  calibration.floor = 0.4;
  require(calibration.Evidence(std::numeric_limits<double>::quiet_NaN()) == 0 &&
          calibration.Evidence(std::numeric_limits<double>::infinity()) == 0, "invalid source scores");
  require(!calibration.Finite(std::numeric_limits<double>::infinity()), "finite guard under fast math");
  std::cout << "Hybrid calibration regressions passed\n";
#endif
}
