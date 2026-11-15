#!/usr/bin/env python3
"""MB-02: independent C-ABI and bounded hex-protocol negative controls.

The real mulbench.c is compiled for every case. Deliberately defective
candidates exercise the outer process status; mathematical correctness is
checked independently using Python integers, not candidate-owned hashes.

Host tests are not physical ARM32, a sanitizer, or an FP acceptance receipt.
"""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "mulbench.c"
CC = os.environ.get("CC", "cc")
FLAGS = ["-O2", "-std=c11", "-D_POSIX_C_SOURCE=200809L",
         "-Wall", "-Wextra", "-Werror"]

MUTANTS = {
    "past-output": "out[na + nb] = 0; return mul_schoolbook(a,na,b,nb,out);",
    "before-output": "out[-1] = 0; return mul_schoolbook(a,na,b,nb,out);",
    "alter-input": (
        "int rc = mul_schoolbook(a,na,b,nb,out); "
        "((uint32_t *)a)[0] ^= 1; return rc;"
    ),
    "omit-high-zero": (
        "size_t n=na+nb; uint32_t *tmp=calloc(n,sizeof(*tmp));"
        "if(!tmp) return 8;"
        "int rc=mul_schoolbook(a,na,b,nb,tmp);"
        "if(!rc) memcpy(out,tmp,(n-1)*sizeof(*out));"
        "free(tmp);return rc;"
    ),
    "no-write": "return 0;",
    "status-after-output": (
        "mul_schoolbook(a,na,b,nb,out); return 7;"
    ),
    "wrong-answer": (
        "memset(out,0,(na+nb)*sizeof(*out)); return 0;"
    ),
}


def compile_source(executable, mutant=None):
    argv = [CC, *FLAGS]
    if mutant is not None:
        provider = executable.with_suffix(".c")
        provider.write_text(
            "#include <stddef.h>\n#include <stdint.h>\n"
            "#include <stdlib.h>\n#include <string.h>\n"
            "extern int mul_schoolbook(const uint32_t*,size_t,"
            "const uint32_t*,size_t,uint32_t*);\n"
            "int mul_candidate(const uint32_t *a,size_t na,"
            "const uint32_t *b,size_t nb,uint32_t *out){"
            "(void)a;(void)b;(void)na;(void)nb;(void)out;"
            + MUTANTS[mutant] + "}\n",
            encoding="utf-8",
        )
        argv += ["-DHAVE_CANDIDATE", str(SOURCE), str(provider)]
    else:
        argv += [str(SOURCE)]
    argv += ["-o", str(executable)]
    subprocess.run(argv, check=True, text=True, capture_output=True)


def run_program(executable, payload=b"", *arguments):
    return subprocess.run(
        [str(executable), *arguments], input=payload,
        capture_output=True, timeout=30,
    )


class MB02(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix="ick-mb02-")
        cls.directory = Path(cls.tmp.name)
        cls.baseline = cls.directory / "baseline"
        compile_source(cls.baseline)
        cls.mutants = {}
        for name in MUTANTS:
            target = cls.directory / name
            compile_source(target, name)
            cls.mutants[name] = target

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_valid_signed_hex_spacing_and_unterminated_last_line(self):
        payload = (
            b"\t+0x0a  -0x0b \r\n"
            b"ffffffff ffffffff\n"
            b"-1 1\n"
            b"0 0\n"
            b"1 2"
        )
        result = run_program(self.baseline, payload, "--pipe")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            [int(line, 16) for line in result.stdout.splitlines()],
            [-110, (2**32 - 1)**2, -1, 0, 2],
        )

    def test_refuse_malformed_and_ambiguous_hex_records(self):
        invalid = [
            b"1\n", b"xyz 42\n", b"1 2 3\n", b"1 2 extra\n",
            b"1 2 #comment\n", b"1 2\x00hidden\n",
            b"\x00 1\n", b"--1 2\n", b"+ 2\n",
            b"1 2 " + b"0" * 1_048_576 + b"\n",
        ]
        for payload in invalid:
            with self.subTest(payload=repr(payload[:32])):
                result = run_program(self.baseline, payload, "--pipe")
                self.assertNotEqual(result.returncode, 0)
                self.assertEqual(result.stdout, b"")

    def test_command_line_limits_and_extra_arguments(self):
        cases = [
            ("--pipe", "extra"),
            ("--bench", "-1"),
            ("--bench", "+32"),
            ("--bench", "0"),
            ("--bench", "1e3"),
            ("--bench", "1048577"),
            ("--bench", "64", "0"),
            ("--bench", "64", "32"),
            ("--bench", "64", "3junk"),
            ("--bench", "64", "3", "extra"),
            ("--bench", "184467440737095516161", "2"),
        ]
        for args in cases:
            with self.subTest(args=args):
                result = run_program(self.baseline, b"", *args)
                self.assertNotEqual(result.returncode, 0)
                self.assertEqual(result.stdout, b"")

    def test_buffer_and_producer_mutants_fail_outer_acceptance(self):
        # Both limbs of (2^64-1)^2 are significant, with a high carry.
        a = 2**64 - 1
        b = 2**64 - 1
        payload = f"{a:x} {b:x}\n".encode()
        baseline = run_program(self.baseline, payload, "--pipe")
        self.assertEqual(baseline.returncode, 0, baseline.stderr)
        expected = a*b
        self.assertEqual(int(baseline.stdout.strip(), 16), expected)
        for name, executable in self.mutants.items():
            with self.subTest(mutant=name):
                result = run_program(executable, payload, "--pipe")
                accepted = (
                    result.returncode == 0
                    and len(result.stdout.splitlines()) == 1
                    and int(result.stdout.strip(), 16) == expected
                )
                self.assertFalse(accepted, f"{name} fooled outer exactness check")
                if name != "wrong-answer":
                    # The checked C adapter must itself refuse bad writes,
                    # changed inputs, missing stores and failing status.
                    self.assertNotEqual(result.returncode, 0, name)
                    self.assertEqual(result.stdout, b"", name)

    def test_reusable_direct_call_guard_and_provider_defects(self):
        probe = Path(__file__).with_name("direct_call_guard.c")
        binary = self.directory / "direct-call"
        obj = self.directory / "mulbench.o"
        subprocess.run(
            [CC, *FLAGS, "-Dmain=ick_mulbench_other_main", "-c",
             str(SOURCE), "-o", str(obj)],
            check=True, capture_output=True, text=True,
        )
        subprocess.run(
            [CC, *FLAGS, str(probe), str(obj), "-o", str(binary)],
            check=True, capture_output=True, text=True,
        )
        result = run_program(binary)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(b"admission_refusals\t", result.stdout)
        self.assertIn(b"MB02_DIRECT_CALL_GUARD\tPASS", result.stdout)
        for defect in ("before-output", "after-output", "alter-input",
                       "omit-upper-zero", "no-write", "status-after-output",
                       "wrong-answer"):
            with self.subTest(direct_defect=defect):
                result = run_program(binary, b"", defect)
                self.assertNotEqual(result.returncode, 0, result.stdout)
                self.assertNotIn(b"MB02_DIRECT_CALL_GUARD\tPASS", result.stdout)


if __name__ == "__main__":
    unittest.main(verbosity=2)
