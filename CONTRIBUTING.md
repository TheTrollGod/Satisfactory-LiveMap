# Contributing to Live Map

The most useful contribution is a good issue. Most fixes are made by the maintainer,
so a clear report gets a problem fixed faster than anything else.

## Reporting a bug

1. Check the [troubleshooting table](docs/INSTALL.md#10-troubleshooting) and the
   [open issues](https://github.com/TheTrollGod/Satisfactory-LiveMap/issues). If your
   problem is already reported, add a comment with your details instead of a new issue.
2. Open a [bug report](https://github.com/TheTrollGod/Satisfactory-LiveMap/issues/new?template=bug_report.yml).
   The form asks for:
   - **Live Map version**: the release tag, e.g. `v0.1.0-build.6`.
   - **Where the mod runs**: Linux server, Windows server, or hosting from the game.
   - **Game and SML versions.**
   - **What happened and how to reproduce it**, step by step.
   - **Log lines**: run `grep LiveMap FactoryGame.log` and paste the output (log locations are in
     [docs/INSTALL.md](docs/INSTALL.md#7-verification)). For a crash, attach the whole log.
   - **Browser console errors** if the problem is in the map page (press F12, Console tab).

One problem per issue. Before posting logs or screenshots, remove anything you
don't want public, such as server IPs and player names.

## Requesting a feature

Open a [feature request](https://github.com/TheTrollGod/Satisfactory-LiveMap/issues/new?template=feature_request.yml).
Describe what you're trying to do rather than the exact solution. "I want to see
which trains are stuck" leads to a better result than "add a red train icon".

## Security problems

Don't open a public issue for something that lets people attack a server. Use
[private vulnerability reporting](https://github.com/TheTrollGod/Satisfactory-LiveMap/security/advisories/new)
instead.

## Pull requests

Pull requests are welcome, but for anything bigger than a small fix, open an issue
first so the approach can be agreed before you spend time on it.

### Setup

What you need depends on what you change:

| Change | Needs |
|---|---|
| Map page (`web/`) | Node 22. `cd web && npm ci && npm run dev`, open `http://localhost:5173/?mock` for fake data |
| Server core (`Mod/LiveMap/Source/LiveMap/Private/Core/`) | CMake and a C++20 compiler (Linux, macOS or WSL). No Unreal needed |
| Game integration (`LiveMapSubsystem`, collectors) | The full SML modding setup; see the [Dev setup](README.md#dev-setup) section of the README |

Most map-page and core changes can be built and tested without the game, against the
simulator:

```bash
cmake -S tests/core -B build/core && cmake --build build/core
ctest --test-dir build/core --output-on-failure
./build/core/livemap_sim 8086 &          # fake world on port 8086
cd web && npm run dev                    # map page against the simulator
```

### Workflow

1. Fork the repo and create a branch from `main`, e.g. `fix/train-icons`.
2. Make the change. Keep it focused on one thing.
3. Run the checks for what you touched (above). Add a test when you fix a bug in the
   core.
4. Push to your fork and open a pull request against `main`. The template asks what
   changed, which issue it fixes (`Fixes #123`), and how you tested it.
5. CI runs the core tests, the web build and the end-to-end test on your PR. They
   must pass before merge.

When a PR is merged, `main` builds and publishes a new release automatically.

### Code rules

- Only touch Unreal objects (`UObject`, actors, components) on the game thread. The
  server threads only ever see plain strings and structs.
- The core under `Private/Core/` stays engine-free so it can be tested outside Unreal.
  No exceptions or RTTI there (matching Unreal).
- Don't commit game assets or anything extracted from the game.
- Match the style of the file you're editing.
