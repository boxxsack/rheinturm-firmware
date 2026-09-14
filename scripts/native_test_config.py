"""Select the production source and host libraries for each native test suite."""

import subprocess
from pathlib import Path

Import("env")  # type: ignore[name-defined]  # Provided by PlatformIO/SCons.


TEST_SOURCES = {
    "test_ble_auth_failure_policy": "BleAuthFailurePolicy.cpp",
    "test_ota_image_verifier": "OtaImageVerifier.cpp",
}

test_name = env.get("PIOTEST_RUNNING_NAME")
source = TEST_SOURCES.get(test_name)
if source is None:
    known_tests = ", ".join(sorted(TEST_SOURCES))
    print(f"error: native test source is not configured for {test_name!r}; expected one of: {known_tests}")
    env.Exit(1)

env.Replace(SRC_FILTER=["-<*>", f"+<{source}>"])

if test_name == "test_ota_image_verifier":
    project_dir = Path(env.subst("$PROJECT_DIR"))
    flags_script = project_dir / "scripts" / "mbedtls_flags.sh"
    result = subprocess.run(
        [str(flags_script)],
        cwd=project_dir,
        check=False,
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        print(result.stderr.strip())
        env.Exit(result.returncode)
    env.MergeFlags(result.stdout.strip())
