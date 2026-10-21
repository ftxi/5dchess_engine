# Contributing

## Development setup

The native code requires CMake 3.20 or newer and a compiler with C++23 support.

See the [build guide](docs/usage/building.md) for setup instructions and the [core architecture guide](docs/architecture/core.md) for code structure.

## Making changes

Use existing features and utilities where possible before implementing new ones. Keep changes focused and avoid unrelated formatting changes.

Before introducing a new dependency for C++ or the UI, open an issue to discuss it.

- `test/` and `tools/` are reserved for C++ files. Put python source files and shell scripts inside `scripts/` if they worths reusing. 

## C++ style

Follow the surrounding code, including these conventions:

- Place pointer and reference symbols next to the variable name: `int *p`, `const auto &x`.
- When both qualifiers appear, put `const` before `static`.
- For very long argument lists, put arguments on separate lines like this:
```cpp
int very_long_function_name<with_template>(
    (a+b)*c-d?e:f,
    another_argument,
    [&a](int x) -> float {
        return a*x;
});
```
- RTTI is forbidden in `src/` and `tools/`.
- There is no indentation for namespace blocks. On the closing brace, add a comment with `/* namespace <name> */` or `/* anonymous namespace */` if the namespace is anonymous. Example:
```c
#ifndef LIBRARY_H
#define LIBRARY_H
// code
#endif /* LIBRARY_H */
```

## Testing

For engine-strength experiments, use the [canonical workflow](docs/engine/testing.md)
with its frozen reference and balanced position subsets.

Build the affected targets and run the relevant tests.

When changing code in `src/core/`, `src/misc/`, or `tools/`, check whether the change affects tool behavior or tools-related tests. If it does, enable both `TOOLS` and `TEST` so that tools-related tests are included. For other changes, build and test the affected components.

Test files in `test/` are grouped by related functionality.

To build and run tests with tools enabled:

```sh
cmake -S . -B build -DTOOLS=ON -DTEST=ON -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS=-fno-rtti
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

For Python scripts and bindings, run from the repository root:

```sh
python -m unittest discover -s scripts/test -v
```

See the [Python test guide](scripts/test/README.md) for dependencies and coverage. Skipped tests do not verify the skipped behavior. JavaScript UI tests live in `ui/test/`; see the [web UI test guide](ui/test/README.md).

Add a regression test for behavior fixes only when the test is practical, maintainable and useful for future development. Do not introduce tests in this repository that are platform-specific, require new dependencies, or require files from external or gitignored sources.

## Documentation

Update documentation when changing user-visible behavior.

Keep the root README focused on the project overview, quick start, limitations, and documentation navigation. Put detailed instructions and feature explanations in the relevant page under `docs/`.

Keep each piece of guidance in one primary location and link to it elsewhere. Add new pages to the [documentation guide](docs/README.md), and update links when moving files.

After implementing a new feature or refactoring, do not edit `docs/architecture/` or `docs/README.md` before the code becomes stable.

Experimental results belong in `docs/reports/`. Distinguish measured results from expectations or hypotheses.

For documentation-only changes, check changed links and verify changed command examples. Run code tests when the change also affects code behavior.
