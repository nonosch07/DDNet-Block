#!/usr/bin/env python3
"""Run clang-tidy the way the check-clang-tidy CI job does, and optionally apply
its fixes.

The CI job builds with clang-tidy wired in as the compiler wrapper, so a finding
anywhere fails the build. This reproduces that build locally and can auto-apply
every fix clang-tidy knows how to make.

	scripts/fix_clang_tidy.py --check      # report only, like CI
	scripts/fix_clang_tidy.py              # apply fixes to src/block
	scripts/fix_clang_tidy.py 'src/game/.*'

Read the warning under --help before trusting the output: clang-tidy's renames
can produce code that does not compile, so this always rebuilds afterwards.
"""

import argparse
import os
import shutil
import subprocess
import sys

os.chdir(os.path.dirname(__file__) + "/..")

CLANG_VERSION = 20
# Named build* so the repo's existing /build* ignore rule covers it; a build
# directory must never show up as untracked files to commit.
BUILD_DIR = "build-clang-tidy"

# Same options the check-clang-tidy workflow configures with. Keeping these in
# step matters: a finding depends on which CONF_* defines are active, so a
# different configuration finds a different set of problems than CI does.
CMAKE_ARGS = [
	"-DCLIENT=OFF",
	"-DANTIBOT=ON",
	"-DDOWNLOAD_GTEST=ON",
	"-DMYSQL=ON",
	"-DUPNP=ON",
	"-DWEBSOCKETS=ON",
]

DEFAULT_FILTER = "src/block/.*"


def find_binary(name):
	for candidate in (f"{name}-{CLANG_VERSION}", name):
		path = shutil.which(candidate)
		if not path:
			continue
		if candidate.endswith(str(CLANG_VERSION)):
			return path
		# an unversioned binary is only safe if it happens to be the right version
		try:
			out = subprocess.check_output([path, "--version"], text=True)
		except (OSError, subprocess.CalledProcessError):
			continue
		if f"version {CLANG_VERSION}." in out:
			return path
	return None


def configure_and_build(build_dir):
	"""clang-tidy cannot parse anything until the generated sources exist, so the
	tree has to be built once before it is worth running."""
	if not os.path.exists(os.path.join(build_dir, "build.ninja")):
		print(f"configuring {build_dir} ...")
		cmd = [
			"cmake",
			"-B",
			build_dir,
			"-G",
			"Ninja",
			"-DCMAKE_BUILD_TYPE=Debug",
			"-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
			"-DCMAKE_C_COMPILER=clang",
			"-DCMAKE_CXX_COMPILER=clang++",
			*CMAKE_ARGS,
		]
		if subprocess.run(cmd, check=False).returncode != 0:
			print("\ncmake failed.", file=sys.stderr)
			return False

	print("building once so the generated sources exist ...")
	return subprocess.run(["ninja", "-C", build_dir, "all", "testrunner"], check=False).returncode == 0


def main():
	p = argparse.ArgumentParser(
		description="Reproduce the check-clang-tidy CI job, and optionally apply its fixes.",
		epilog=("WARNING: clang-tidy's automatic fixes are not always correct. Its renames can collide a local with the parameter it shadows, and modernize-avoid-bind cannot handle parameter packs. This script rebuilds afterwards so such breakage is caught immediately, but review the diff before committing."),
	)
	p.add_argument("filter", nargs="?", default=DEFAULT_FILTER, help=f"regex of files to check (default: {DEFAULT_FILTER})")
	p.add_argument("-n", "--check", action="store_true", help="Don't fix, only report (what CI does)")
	p.add_argument("-j", "--jobs", type=int, default=0, help="Number of jobs")
	p.add_argument("--build-dir", default=BUILD_DIR, help=f"Build directory to use (default: {BUILD_DIR})")
	args = p.parse_args()

	run_clang_tidy = find_binary("run-clang-tidy")
	clang_tidy = find_binary("clang-tidy")
	if not run_clang_tidy or not clang_tidy:
		print(
			f"Found no clang-tidy {CLANG_VERSION}. On Debian/Ubuntu:\n    sudo apt-get install clang-tidy-{CLANG_VERSION}\nCI pins this version, and other versions report a different set of findings.",
			file=sys.stderr,
		)
		return 1

	if not configure_and_build(args.build_dir):
		return 1

	cmd = [run_clang_tidy, "-p", args.build_dir, "-quiet", "-clang-tidy-binary", clang_tidy]
	if args.jobs > 0:
		cmd += ["-j", str(args.jobs)]
	if not args.check:
		cmd += ["-fix"]
	cmd += [args.filter]

	print(f"running clang-tidy over {args.filter} ...")
	proc = subprocess.run(cmd, capture_output=True, text=True, check=False)
	findings = [line for line in proc.stdout.splitlines() if ": warning: " in line or ": error: " in line]

	if args.check:
		for line in findings:
			print(line)
		print(f"\n{len(findings)} finding(s)")
		return 1 if findings else 0

	if not findings:
		print("nothing to fix")
		return 0

	print(f"applied fixes for {len(findings)} finding(s); reformatting ...")
	subprocess.run([sys.executable, "scripts/fix_style.py"], check=False)

	print("rebuilding to make sure the fixes compile ...")
	if subprocess.run(["ninja", "-C", args.build_dir, "all", "testrunner"], check=False).returncode != 0:
		print(
			"\nThe tree no longer builds. This is usually clang-tidy renaming a local\nonto the parameter it shadows; fix those by hand, then run this again.",
			file=sys.stderr,
		)
		return 1

	print("\nDone. Review the diff, then re-run with --check to confirm CI is happy.")
	return 0


if __name__ == "__main__":
	sys.exit(main())
