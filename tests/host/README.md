# Host C tests

These tests build portable C modules with a native C11 compiler. The initial
`common` project tests the test harness itself; it does not implement or validate
sensor algorithms. The harness is header-only, has no third-party dependencies,
and does not allocate memory dynamically.

## Prerequisites

Run commands from the repository root. An individual C test project needs:

- CMake 3.16 or newer;
- Ninja;
- a C11 compiler (strict warning flags are applied to GNU and Clang); and
- working AddressSanitizer and UndefinedBehaviorSanitizer runtime support for
  the default GNU/Clang host configuration.

Python 3 is also required for the all-project runner. The runner and its static
checks are provided by `tools/ci/run_host_tests.py`; see that command's help for
any additional static-analysis prerequisites. No ROS installation or embedded
cross-compiler is needed for the `common` project.

## Build and run the common project

Configure, build, and run exactly this independent CMake/CTest project:

```bash
cmake -S tests/host/common -B build/host/common -G Ninja
cmake --build build/host/common
ctest --test-dir build/host/common --output-on-failure --no-tests=error
```

The smoke executable can also be run directly:

```bash
./build/host/common/test_infrastructure
```

It exercises successful and deliberately failing assertions in isolated,
output-suppressed contexts. Normal output therefore contains only passing test
cases. It also checks a fixed xorshift32 reference sequence, repeatability from a
seed, and deterministic zero-seed handling. This PRNG is test infrastructure,
not a cryptographic generator or a validated statistical sampler.

## Run all host tests

Use the shared runner to discover and execute the host-test projects:

```bash
python3 tools/ci/run_host_tests.py
```

Run only its static checks with:

```bash
python3 tools/ci/run_host_tests.py --static-only
```

Static-only execution does not provide runtime, sanitizer, target-build, or
hardware coverage.

## Sanitizers

`OAMR_SANITIZE` defaults to `ON`. For GNU/Clang builds on supported non-Windows
hosts, this adds AddressSanitizer and UndefinedBehaviorSanitizer to compilation
and linking, while retaining frame pointers. To request a separate, explicit
sanitizer build:

```bash
cmake -S tests/host/common -B build/host/common-sanitize -G Ninja -DOAMR_SANITIZE=ON
cmake --build build/host/common-sanitize
ctest --test-dir build/host/common-sanitize --output-on-failure --no-tests=error
```

On a host without sanitizer support, configure a separate build directory with:

```bash
cmake -S tests/host/common -B build/host/common-no-sanitize -G Ninja -DOAMR_SANITIZE=OFF
cmake --build build/host/common-no-sanitize
ctest --test-dir build/host/common-no-sanitize --output-on-failure --no-tests=error
```

Sanitizers are not target firmware verification. Unsupported non-GNU/Clang
compilers build without these flags; the Windows GNU/Clang configuration rejects
`OAMR_SANITIZE=ON`, so use `OFF` there explicitly.

## Harness usage

Include `test_harness.h`, define `void test_name(void)` functions, and call them
from `main`:

```c
#include "test_harness.h"

static void example(void)
{
    CHECK(2u + 2u == 4u);
}

int main(void)
{
    RUN(example);
    return TEST_END();
}
```

`CHECK` evaluates its condition once, records every assertion, prints the source
location and expression on failure, and continues the current test. `RUN`
counts a test case as failed when any of its checks fail or when it executes no
checks. `TEST_END` prints the totals and returns zero only when every test case
executed at least one check and all checks passed. The default context is
intended for ordinary single-threaded `main`-driven tests, not concurrent test
execution.

## Add a future module project

Create `tests/host/<module>/` with its own `CMakeLists.txt` and test source. Each
module project must remain independently configurable by the runner and by a
developer:

```bash
cmake -S tests/host/<module> -B build/host/<module> -G Ninja
cmake --build build/host/<module>
ctest --test-dir build/host/<module> --output-on-failure --no-tests=error
```

For each new project:

1. Declare a C-only CMake project, require C11 without compiler extensions, call
   `enable_testing()`, build at least one real executable, and register it with
   `add_test`. An empty test directory is not a passing test suite.
2. Apply the GNU/Clang warning policy used by `common/CMakeLists.txt`: `-Wall
   -Wextra -Werror -Wconversion -Wshadow -Wstrict-prototypes -Wpedantic`.
   Reuse the small target-setup function as a template when creating the new
   standalone project; do not require another module's build tree.
3. Provide the `OAMR_SANITIZE` option and sanitizer compile/link behavior for
   every host target in that project. Keep test executables registered even
   when sanitizers are disabled.
4. Add `../common` to the test target's private include directories, or include
   `../common/test_harness.h` by its relative path. The harness needs no
   separately linked library.
5. Compile the portable production `.c` module directly into the test
   executable. Put hardware-facing interfaces behind narrow test doubles; do
   not compile embedded HAL initialization into host tests.
6. Keep portable production modules free of dynamic allocation. Use bounded
   storage and deterministic fixtures, including explicit seeds for any test
   randomness. Cover valid input, boundaries, malformed input, and state/error
   transitions relevant to the module.
7. Use `build/host/<module>` for artifacts and never commit generated build
   files. Do not import unverified sensor algorithms merely to obtain test
   coverage.

## Level 2 limitations

Passing these tests is **Level 2: host build and execution evidence only**. A
host compiler, mock, deterministic fixture, or sanitizer cannot establish:

- successful STM32H723 cross-compilation, linking, startup, or memory placement;
- actual timing, interrupt, DMA, cache, watchdog, or concurrent behavior;
- sensor electrical/protocol operation, actuator or CAN behavior, or network
  communication;
- electrical safety, motion safety, fault safety, or physical system
  correctness.

Host and static results do not substitute for target builds, electrical tests,
controlled bench procedures, or hardware validation. No hardware verification
is claimed by this infrastructure.
