"""Exercise the configuration generator with bounded and invalid score input."""
import pathlib
import subprocess
import sys
import unittest

GENERATOR = pathlib.Path(__file__).resolve().parents[1] / "utils" / "vector_background.py"


class BackgroundGeneratorTest(unittest.TestCase):
    def run_generator(self, data, *arguments):
        return subprocess.run([sys.executable, str(GENERATOR), *arguments], input=data,
                              text=True, capture_output=True)

    def test_histogram_and_field(self):
        result = self.run_generator("# sample\n0\n0.25\n0.5\n0.75\n1\n", "--field", "ABSTRACT", "--bins", "4")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("[Hybrid:ABSTRACT]", result.stdout)
        self.assertIn("VectorCDF=0 1 2 3 5", result.stdout)

    def test_invalid_samples(self):
        for data in ("", "nan\n", "inf\n", "1.1\n", "-0.1\n", "0.5 text\n"):
            with self.subTest(data=data):
                result = self.run_generator(data)
                self.assertNotEqual(result.returncode, 0)
                self.assertEqual(result.stdout, "")

    def test_profile_continuations(self):
        result = self.run_generator("0.5\n" * 1000, "--bins", "512")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(all(len(line) < 256 for line in result.stdout.splitlines()))
        values = result.stdout.split("VectorCDF=", 1)[1].replace("\\\n", "")
        counts = list(map(int, values.split()))
        self.assertEqual(len(counts), 513)
        self.assertEqual((counts[0], counts[-1]), (0, 1000))
        self.assertEqual(counts, sorted(counts))

    def test_invalid_options(self):
        for arguments in (("--bins", "0"), ("--minimum", "nan"),
                          ("--maximum", "0"), ("--field", "A]\n[Other")):
            with self.subTest(arguments=arguments):
                self.assertNotEqual(self.run_generator("0.5\n", *arguments).returncode, 0)


if __name__ == "__main__":
    unittest.main()
