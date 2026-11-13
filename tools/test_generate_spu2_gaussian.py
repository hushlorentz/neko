import decimal
import math
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


GENERATOR = Path(__file__).with_name("generate_spu2_gaussian.py")


class GaussianGeneratorTests(unittest.TestCase):
    def run_generator(self, *arguments):
        return subprocess.run(
            [sys.executable, "-B", str(GENERATOR), *map(str, arguments)],
            capture_output=True, text=True, check=False,
        )

    def test_check_missing_output_does_not_create_file(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "coefficients.hpp"
            result = self.run_generator("--check", "--output", output)
            self.assertEqual(result.returncode, 1, result.stderr)
            self.assertIn("missing", result.stderr)
            self.assertFalse(output.exists())

    def test_check_stale_output_preserves_it_and_generation_is_repeatable(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "coefficients.hpp"
            output.write_text("stale\n", encoding="ascii")
            result = self.run_generator("--check", "--output", output)
            self.assertEqual(result.returncode, 1, result.stderr)
            self.assertIn("differ", result.stderr)
            self.assertEqual(output.read_text(encoding="ascii"), "stale\n")
            generated = self.run_generator("--output", output)
            self.assertEqual(generated.returncode, 0, generated.stderr)
            expected = output.read_bytes()
            checked = self.run_generator("--check", "--output", output)
            self.assertEqual(checked.returncode, 0, checked.stderr)
            repeated = self.run_generator("--output", output)
            self.assertEqual(repeated.returncode, 0, repeated.stderr)
            self.assertEqual(output.read_bytes(), expected)

    def test_precision_and_ambient_rounding_do_not_change_coefficients(self):
        from generate_spu2_gaussian import calculate_coefficients

        reference = calculate_coefficients(112)
        with decimal.localcontext() as context:
            context.prec = 7
            context.rounding = decimal.ROUND_UP
            self.assertEqual(calculate_coefficients(80), reference)
            self.assertEqual(context.prec, 7)
            self.assertEqual(context.rounding, decimal.ROUND_UP)
        self.assertEqual(len(reference), 256)
        for phase, row in enumerate(reference):
            self.assertEqual(row, tuple(reversed(reference[255 - phase])))
            self.assertTrue(32638 <= sum(row) <= 32642)
            self.assertTrue(all(-32768 <= value <= 32767 for value in row))

    def test_coefficients_match_independent_binary64_evaluation(self):
        from generate_spu2_gaussian import calculate_coefficients

        def kernel(position):
            window = (
                0.42 + 0.5 * math.cos(2 * math.pi * position / 1023)
                + 0.08 * math.cos(4 * math.pi * position / 1023)
            )
            return math.sin(math.pi * position / 500) * window / position

        normalization = 4177920 / sum(kernel(n + 0.5) for n in range(512))
        for phase, actual in enumerate(calculate_coefficients()):
            taps = [normalization * kernel(position) for position in (
                256.5 + phase, 0.5 + phase, 255.5 - phase, 511.5 - phase,
            )]
            correction = (sum(taps) - 32640) / 4
            expected = [tap - correction for tap in taps]
            for value in expected:
                self.assertGreater(0.5 - abs(value - round(value)), 1e-8)
            self.assertEqual(actual, tuple(round(value) for value in expected))


if __name__ == "__main__":
    unittest.main()
