# shrewd-lvgl-vue(shrewd-lv-view)

Vue-style reactive architecture for LVGL in **pure C** - no templates,
no code generation, no intermediate steps. Built for complex business
on embedded Linux LVGL; the data plane is
[shrewd-basic](https://github.com/wesleyvance8316-creator/shrewd-basic)
(sb_dbus typed data bus feeds the reactive core).

**Naming law**: every public identifier is `shrewd_lv_view_*`
(macros `SHREWD_LV_VIEW_*`), matching shrewd-basic's `shrewd_basic_*`
house style; consumers include only `shrewd-lv-view/view.h`.

- Design & charter: [docs/initiation.md](docs/initiation.md)
- Build: `cmake -S . -B build && cmake --build build && ctest --test-dir build`
- Style: clang-format / clang-tidy gates, same conventions as shrewd-basic
  (`tools/check-style.sh`, pre-commit hooks: `git config core.hooksPath .githooks`).

Status: **0.1.0 skeleton** - API free to change.
