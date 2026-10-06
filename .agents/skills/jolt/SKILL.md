---
name: jolt
description: Write, run, debug, test, and build Jolt programs; port Clojure code to Jolt; and use Jolt native interop. Apply when the target runtime is Jolt, especially for Clojure compatibility, deps.edn, standalone builds, or C FFI. Do not apply to ordinary JVM Clojure work merely because the syntax matches.
---

# Jolt

Use existing Clojure knowledge for the language. Jolt compiles Clojure to Scheme,
normally running on Chez, with its own runtime and Java-shaped compatibility
shims. Concentrate on the host boundary and verified differences.

## Choose the reference you need

Read only the relevant file or section; these are independent task guides.

| Task | Reference |
| --- | --- |
| Port Clojure; check reader branches, numbers, strings, identity, or Java assumptions | [Clojure differences](references/clojure-differences.md) |
| Run a script/project; resolve deps; use nREPL; test; build a binary | [Projects and tooling](references/projects-and-tooling.md) |
| Bind C; manage pointers, layouts, callbacks; embed Jolt in C/C++ | [Native interop](references/native-interop.md) |
| Use threads/fibers, Scheme calls, reader extensions, images, or optimize code | [Runtime and extensions](references/runtime-and-extensions.md) |
| Resolve conflicting docs; locate implementation/tests; change Jolt itself | [Sources and verification](references/sources-and-verification.md) |

## Establish the target

Check the project's `deps.edn`, task/test commands, and any existing Jolt
conventions. Identify the executable with `command -v jolt` and `jolt --version`;
use `jolt --help` for the installed CLI. A checkout is a separate version:
`git -C "$HOME/src/jolt" rev-parse HEAD`. Its `bin/jolt` requires Chez; an installed
standalone `jolt` does not. Run project commands from the project directory.

This guide was checked on 2026-10-06 against Jolt **v0.8.16** and source commit
`fb941836647cdaa307df9c50356493040be4e0a5`. Recheck version-sensitive behavior on
the actual target. The site, README, comments, and even registry prose sometimes
describe older behavior; use a small runtime probe and the corresponding
implementation/test when they conflict. See the [source map](references/sources-and-verification.md).

## Carry these rules into the task

- Preserve ordinary Clojure idioms: collections, macros, protocols, transducers,
  ratios, BigDecimal, STM, futures, and runtime evaluation are supported. Loading
  a namespace alone does not prove every API used by the application works.
- There is no JVM or arbitrary Java bytecode execution. Check the specific shim
  or use `jolt.ffi`; do not infer classloader, GC, or thread lifetime behavior.
- For current Jolt, reader features are `:jolt`, `:clj`, `:default`, **not `:bb`**.
  First matching clause wins: put `:jolt` before `:clj`. Namespace files resolve
  `.jolt` before `.clj` before `.cljc`; use a `.jolt` sibling for a whole-host override.
- Strings use Unicode codepoint indices, not UTF-16 units. Translate offsets at
  Java/JavaScript protocol boundaries deliberately.
- A current release build direct-links by default. Use `--dev` or
  `--no-direct-link` when application vars must stay redefinable; test the built
  artifact if building is part of the task.
- Test changed behavior on Jolt, and on JVM Clojure too when preserving both
  targets is required. Keep Jolt-only syntax and host assumptions out of portable
  code paths.
