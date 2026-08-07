# Starting a new project from this template

A one-time checklist. Work top to bottom: three steps have to happen in order and
say so in their heading, and ⚠ elsewhere marks a way to break something quietly.
Every `- [ ]` is something to do; everything else is why.

When you reach the end you delete this file. That is also what switches
`cmake/check_artifacts.cmake` from "am I still the template?" to "is this project
clean?", so a finished project keeps checking itself with nothing to wire up.

> **Scope:** the first commit only. Keeping a project *in sync* with the template
> afterwards is a different problem — see issue CT-10 (#11), not this file.
>
> **If you are editing the template itself:** this file gives instructions to the
> *fork*. Where it says "pick one," `AGENTS.md`'s "keep both patterns present"
> still governs this repo.

---

## Step 0 — Get the tree ⚠ before any `cmake -B`

- [ ] `git clone <this repo> myproject` (preferred) or `cp -r`
- [ ] `cd myproject && rm -rf .git && git init && git add -A && git commit -m "Initial commit from cpp-template"`
- [ ] `rm -f include/version.hpp`
- [ ] `rm -rf build*/`

> Two traps here, both silent.
>
> `include/version.hpp` is **generated and gitignored**. A `git clone` never
> carries it, but a `cp -r` does — and then you are building against the
> template's stale version numbers until the first configure overwrites them.
>
> `cmake/version.cmake` runs `git describe` from inside `cmake/`. If you copied
> this tree into a subdirectory of some *other* git repo and skipped `git init`,
> git walks up and answers with the **enclosing** repo's tags. Your project
> silently takes someone else's version number and nothing warns you.

---

## Step 1 — Make it yours

What to do with each file:

| Replace wholesale | Keep the shared sections, replace the intro | Copy verbatim |
| --- | --- | --- |
| `LICENSE.md` | `README.md` | `.github/workflows/ci.yml` |
| `src/bin/main.cpp` | `AGENTS.md` | `.clangd`, `.gitignore` |
| `src/lib/lib.cpp`, `include/lib.hpp` | `example/consumer/` (or delete it) | `cmake/toolchain/*`, `cmake/version*.cmake` |
| | | `cmake/install.cmake`, `cmake/project-config.cmake.in` |
| | | `example/public-dep/` (or delete it) |

> The two install files name nothing project-specific — they build every name
> out of `${PROJECT_NAME}`, which follows your directory. They are the reason
> `find_package(<name> CONFIG)` works against your project without you writing
> any packaging code.
>
> `cmake/project-config.cmake.in` is the one you may still need to *add* to —
> one line per dependency your library links. See Step 3. `cmake/install.cmake`
> stays untouched either way.

- [ ] **Project name.** The default is the **directory name** (`CMakeLists.txt:17-23`).
      Either name the directory what you want the project called, or replace
      `${ProjectId}` with a literal in `project()`.

  > The name is load-bearing beyond the binary. It becomes the executable target,
  > the library target `<name>_lib` / `<name>::lib`, **and the option names**
  > `<name>_BUILD_LIB`, `<name>_BUILD_BIN`, `<name>_TESTS`, `<name>_INSTALL`,
  > plus the deps variable `<name>_DEPS`.
  > So every `-D` flag you copy out of the README changes with it:
  > `-Dcpp-template_TESTS=OFF` becomes `-Dmyproject_TESTS=OFF`.
  >
  > It is also your **package name**: anyone consuming your project writes
  > `find_package(<name> CONFIG)` and links `<name>::lib`. And because the name
  > follows the directory, a downstream FetchContent must pin `SOURCE_DIR` — the
  > default checkout path `<name>-src` would otherwise rename your project. The
  > README's "Consume this project" block has the copy-paste.
  >
  > Make the directory name match the GitHub repo name. CI derives the project
  > name from the checkout directory, so if they differ, CI builds under a
  > different name than you do locally.

- [ ] **`LICENSE.md` — replace it.** It is not a license. It is a four-line note
      explaining why the template deliberately ships without one, so that you can
      choose. Choosing is this step.
- [ ] **`README.md`** — replace the title, the badge, the first-person preamble
      and the feature bullets. Keep `## Cheat sheet` and `## Continuous integration`.
- [ ] **`README.md` badge** — replace `gobha-me/cpp-template` in the CI badge URL
      with your own `owner/repo`. This is the only edit CI needs.
- [ ] **`AGENTS.md`** — replace the preamble and `## What this repo is` with what
      *your* project is. Keep `## Conventions that matter here`,
      `## Testing philosophy` and `## How to verify a change` — those are the
      conventions you are inheriting on purpose.
- [ ] **`.github/workflows/ci.yml` — copy verbatim, zero edits.** It hardcodes
      nothing project-specific; the project name comes from the checkout
      directory. Leave `fetch-depth: 0` alone or `git describe` stops finding
      tags and every build reports `0.0.0`.

---

## Step 2 — Gut the demo code ⚠ before Step 3

- [ ] **`src/bin/main.cpp`** — 100% demo. Replace it.

  > It uses `std::cerr` **without** `#include <iostream>`; today it compiles only
  > because argparse and fmt drag `<iostream>` in transitively. Remove those two
  > includes without adding `<iostream>` and the error reads like a broken
  > toolchain rather than a missing include.

- [ ] **`src/bin/CMakeLists.txt`** — drop the `PRIVATE argparse` /
      `PRIVATE fmt::fmt-header-only` lines for whatever you stopped using.
      (The fmt target is `fmt::fmt-header-only`, not `fmt::fmt`.)
- [ ] **`src/lib/lib.cpp` and `include/lib.hpp`** — replace both, and rename the
      namespace in each. They are a pair: the header declares the library's
      public API, the source defines it.

  > The example tests and `example/consumer/main.cpp` call into that namespace
  > too, so rename it there as well — or do this after Step 4 below, which
  > decides the fate of both.

- [ ] **`src/lib/CMakeLists.txt` — pick one.** Keep the compiled `STATIC` target,
      or delete it and uncomment the header-only `INTERFACE` variant below it.
      Delete the one you did not pick.

  > The template ships both on purpose, as a worked example of each. Your project
  > should ship the one it uses.
  >
  > ⚠ Picking `INTERFACE` means there is no translation unit, so every function
  > in `include/lib.hpp` must become a definition rather than a declaration: mark
  > each one `inline` and move its body into the header. Skip that and the header
  > still compiles everywhere while nothing that calls it links.
  >
  > `cmake/install.cmake` needs **no** edit either way — it reads the target's
  > type. One thing does change, though: an inlined header that reads the
  > generated version constants makes installing `include/version.hpp`
  > load-bearing rather than a convenience, so leave that install rule alone.

- [ ] **Link the library into the binary.** Nothing links `${PROJECT_NAME}::lib`
      from `src/bin/` yet, so anything you put in `src/lib/` is compiled into an
      archive the executable never reads. Add it to the existing call in
      `src/bin/CMakeLists.txt`:

  ```cmake
  target_link_libraries(${PROJECT_NAME}
    PRIVATE ${PROJECT_NAME}::lib
    PRIVATE fmt::fmt-header-only
  )
  ```

  > Tests are already handled: `test/CMakeLists.txt` links the library into every
  > auto-discovered test, behind an `if (TARGET ...)` guard so that
  > `-D<name>_BUILD_LIB=OFF` still configures. A test directory with its own
  > `CMakeLists.txt` is the exception — it does its own linking, and
  > `test/02example/` shows the line.

---

## Step 3 — Prune the dependencies

- [ ] Edit the **contents** of the `${PROJECT_NAME}_DEPS` list at `CMakeLists.txt:58-62`.

  > `${PROJECT_NAME}_DEPS` looks like a placeholder you are supposed to fill in.
  > It is not — `${PROJECT_NAME}` expands on its own. Change the names *inside*
  > the list; leave the variable name exactly as it is.
  >
  > The two `list(REMOVE_ITEM …)` blocks just below drop a dep when the only
  > component that needed it is switched off — that is what keeps a project
  > consuming yours from downloading your test framework. Removing a name that
  > is no longer in the list is a harmless no-op, so if you prune the list you
  > can leave them; adjust them only if you keep a dep under a different name.

- [ ] Remove `fmtlib` and `argparse` if you dropped them — **after** Step 2, or
      the build breaks on the demo's includes.
- [ ] Remove `catch2` only if you also delete `test/` or configure with
      `-D<name>_TESTS=OFF`.

> The list is the switch, not the filesystem. A recipe in `cmake/deps/` that is
> not on the list is inert, so deleting recipe files is optional tidying. The
> reverse is fatal: a name on the list with no recipe file stops configuration
> with a `FATAL_ERROR`.

**If your library links a dependency** — as opposed to your executable or your
tests — that dependency becomes part of what your project exports, and it needs
three things rather than one. This is the single most common way a project built
from this checklist fails the first time someone tries to consume it.

- [ ] In the recipe's FetchContent branch:
      `set(<DEP>_INSTALL ${${PROJECT_NAME}_INSTALL})`. Not a fixed `OFF` — that
      is right only for a dependency of the executable. `OFF` here makes
      `install(EXPORT)` fail during generation, because the dependency's target
      is then in no export set.
- [ ] In `cmake/project-config.cmake.in`, above the `include(...)` line:
      `include(CMakeFindDependencyMacro)` and `find_dependency(<dep>)`. Without
      it, `find_package` on your installed project reports imported targets that
      are "referenced, but are missing".
- [ ] Nothing in `cmake/install.cmake`. It stays verbatim.

> A `PRIVATE` link counts. CMake records it on the exported target as
> `$<LINK_ONLY:...>` — a consumer still has to link it — so "private" does not
> mean "invisible to consumers". Judge by *what links it*, not by the keyword.
>
> `cmake/deps/catch2.cmake` writes all of this up, including the two policy and
> install-directory traps. `example/public-dep/` is a runnable worked example:
> it builds a fork exactly like the one described here.

---

## Step 4 — Prune the tests

- [ ] Delete `test/01example/`, `test/02example/`, `test/10example/`.

  > `01example` is the minimum a test dir can be: a `test.cpp`, no CMakeLists,
  > linked against the library for you — and its empty `file.cpp` is there to
  > show that every `*.cpp` in the dir is globbed into the target. `10example` is
  > a failure-matrix test against the library's public header. `02example` is the
  > only worked example of a test that brings its own `CMakeLists.txt` — read it
  > before you delete it if you will ever need custom build control, because such
  > a dir does its own linking.

- [ ] **Keep `test/main.cpp`.** It provides `main()` for every test directory
      that does not bring its own `CMakeLists.txt`.
- [ ] **`cmake/startup.sh` / `cmake/shutdown.sh` — fill in or leave.** ctest runs
      them as the `runners` fixture before and after the suite. That is where
      service dependencies (database, broker, container) belong, not in
      `test/main.cpp`, which runs once per test binary. Leaving them empty costs
      two trivially passing tests.

  > If you delete them, also delete the two `add_test` calls and the
  > `FIXTURES_SETUP` / `FIXTURES_CLEANUP` lines in `test/CMakeLists.txt`, plus
  > the `FIXTURES_REQUIRED` property set inside the discovery loop.
  >
  > ⚠ Keep the executable bit. ctest runs them by path with no interpreter, so a
  > `cp` without `-p`, or a zip download, breaks every test run with a permission
  > error. Rule B5 catches this one.
- [ ] **Keep `test/20failure-testing/`** until you have written your own
      failure-matrix test to replace it.

  > It is doing two jobs: it is the canonical example for this repo's testing
  > philosophy, and it is the `std::expected` canary — the reason CI pins Clang
  > 20. Delete it and CI's `Install Clang` step becomes droppable, along with the
  > guarantee it was buying.

- [ ] **`example/consumer/` — keep it or delete it.** It is a miniature
      downstream project that builds against yours three ways
      (`add_subdirectory`, FetchContent, installed `find_package`) and is the
      only check that your library is actually consumable.

  > Keeping it costs one rename: `example/consumer/main.cpp` calls into the demo
  > namespace, so it belongs in the Step 2 rename above. Deleting it is fine if
  > nothing will ever depend on your library — drop the `consumer` jobs from
  > `.github/workflows/ci.yml` too, and be aware that nothing then exercises the
  > not-top-level path, where the install/export wiring lives.

- [ ] **`example/public-dep/` — keep it if your library links a dependency.** It
      synthesises a fork of your project that has one, and checks that the
      install and export rules survive it (both halves of the Step 3 note
      above). If your library links nothing, it is proving something you cannot
      break, and deleting it is reasonable.

  > It builds `example/consumer/` as its downstream project, so the two are a
  > pair in that direction: delete `example/consumer/` and this harness stops
  > working. It says so rather than failing obscurely.
  >
  > Deleting it means dropping the `public dependency` job from
  > `.github/workflows/ci.yml`. Needs no rename — nothing in it refers to the
  > demo namespace.

- [ ] **Keep `test/30sanitizer-smoke/`.** It is the proof that the sanitizer
      toolchains are actually engaged rather than silently no-ops.

  > ⚠ `test/CMakeLists.txt` names this directory inside `if (TARGET
  > 30sanitizer-smoke-test)`. Rename or delete the directory and that guard goes
  > inert **silently** — tests stay green and the sanitizer proof is simply gone.
  > If you rename it, update that line too. `check_artifacts.cmake` rule B4
  > catches this one for you.

- [ ] If you rename `TEMPLATE_UBSAN`, rename it in **both**
      `cmake/toolchain/undefined.cmake` and `test/30sanitizer-smoke/test.cpp` —
      or in neither.

  > A one-sided rename compiles the UBSan case out on GCC ≤ 13, and the binary
  > still exits 0. Rule B3 catches this one.

---

## Step 5 — First build, both compilers

```bash
cmake -B build && cmake --build build --parallel \
  && ctest --test-dir build --output-on-failure -E artifact-check-selftest

cmake -B build-clang -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain/clang.cmake \
  && cmake --build build-clang --parallel \
  && ctest --test-dir build-clang --output-on-failure -E artifact-check-selftest
```

- [ ] Both green.

> **Why `-E artifact-check-selftest`.** You are between two worlds right now.
> While `NEW_PROJECT.md` is still here that test runs in template mode, where it
> asserts the template's demo artifacts are still *present* — and you have spent
> the last four steps deleting them, so it is failing for the right reason. It
> is checking this repo's invariants, not your project's. Step 7 deletes this
> file, which flips the same check into enforcement mode, and then it goes green
> and stays in your suite for good. Excluding it by name for two steps beats
> learning to scroll past a red test.
>
> Run these from the repo root — the toolchain paths are relative. "Builds on
> both compilers" is the convention you are inheriting (`AGENTS.md`, "How to
> verify a change"); CI enforces it, so it is cheaper to find out now.
>
> Needs GCC 13+, or Clang 19+ with libstdc++ (any Clang with libc++). Stock
> Clang 18 on Ubuntu 24.04 cannot compile `test/20failure-testing`.

---

## Step 6 — First tag ⚠ tag, then re-configure

- [ ] `git tag -a v0.1.0 -m "v0.1.0"`
- [ ] `git push origin v0.1.0`
- [ ] `cmake -B build` again
- [ ] Confirm the configure banner reads `myproject:0.1.0 (tweak=0 dirty=0)`

> The version is read at **configure** time. Tagging does not change an existing
> build tree until you re-run `cmake -B`.
>
> The tag must look like `v1.2.3` — optionally `r`-prefixed or bare, but exactly
> three numeric components. `v1.2`, `v1.2.3.4` and `v1.2.3-rc1` are rejected **by
> design**; you get `0.0.0` and a `STATUS` line saying which rule you broke.
> Until you tag anything, `0.0.0` with "no git tags reachable" is the expected,
> correct output.

---

## Step 7 — Prove it is clean

- [ ] `cmake -P cmake/check_artifacts.cmake` → prints `CLEAN`

  > It reads; it never edits. Every `FAIL` line names a file and line and maps
  > back to a step above. Two things it will *not* flag, correctly: the workflow
  > comment naming the template it came from, and the checker's own list of
  > search patterns — that file is excluded from its own scan.

- [ ] `git rm NEW_PROJECT.md && git commit -m "Bootstrap complete"`
- [ ] `cmake -B build` — re-configure so the flipped check registers
- [ ] `ctest --test-dir build --output-on-failure` — no exclusions this time, all green

  > Deleting this file is what flips `check_artifacts.cmake` out of template
  > mode. After the re-configure, the `artifact-check-selftest` you were
  > excluding in Step 5 is replaced by an `artifact-check` test that fails if a
  > template leftover ever reappears — and that one stays in your suite for the
  > life of the project.

- [ ] Push, and confirm CI is green: 8 build/test jobs, `library disabled`,
      two `consumer` jobs, `public dependency`, and `version-parse-selftest`.
      Minus whichever example harnesses you deleted in Step 4.
