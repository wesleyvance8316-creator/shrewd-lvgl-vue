# shrewd-lvgl-vue(shrewd-lv-view)

Vue-style reactive architecture for LVGL in **pure C** - no templates,
no code generation, no intermediate steps. Built for complex business
on embedded Linux LVGL; the data plane is
[shrewd-basic](https://github.com/wesleyvance8316-creator/shrewd-basic)
(sb_dbus typed data bus feeds the reactive core).

**Naming law**: every public identifier is `shrewd_lv_view_*`
(macros `SHREWD_LV_VIEW_*`), matching shrewd-basic's `shrewd_basic_*`
house style; consumers include only `shrewd-lv-view/view.h`.

- **Start here**: [docs/guide.md](docs/guide.md) (tutorial) ·
  [docs/api.md](docs/api.md) (API overview) ·
  [docs/benchmarks.md](docs/benchmarks.md) (memory & timing baseline)
- Design & charter: [docs/initiation.md](docs/initiation.md) ·
  [docs/architecture.md](docs/architecture.md) (model) ·
  [docs/design.md](docs/design.md) (decisions)
- Build:

  ```
  tools/fetch-deps.sh && tools/build-deps.sh
  cmake -S . -B build -DCMAKE_PREFIX_PATH=build/prefix
  cmake --build build && ctest --test-dir build
  cmake --install build --prefix build/prefix
  ```

  最后一步把**框架自己**也装进同一个前缀 —— 消费者应用靠
  `find_package(shrewd-lv-view CONFIG)` 找它,缺了就编不过。

- Dependencies: lvgl 9.6.0 and shrewd-basic, pinned in
  [deps/versions.lock](deps/versions.lock). Each is built as its own
  project and imported through `find_package` - no `add_subdirectory`,
  no `FetchContent` into the build tree (docs/initiation.md §9).
- Style: clang-format / clang-tidy gates, same conventions as shrewd-basic
  (`tools/check-style.sh`, pre-commit hooks: `git config core.hooksPath .githooks`).

Status: **0.1.0, pre-1.0** - API free to change. Working: reactive bindings
(v-model, loop suppression), page navigation (stack, `pop_until` /
`remove_until`, branches, modal, guards, power-loss restore), list
reconciliation, cross-thread bridge + sb_dbus, shell (header + tab bar),
component ecosystem (fragments + widgets + a packaged component library),
scaffold, and a running prototype. See [docs/guide.md](docs/guide.md) §10
for the pitfalls worth knowing first.
