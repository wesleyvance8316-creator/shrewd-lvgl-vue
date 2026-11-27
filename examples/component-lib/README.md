# demo-components —— 独立组件库的样板

这是 **`shrewd-lv-view` 组件生态的第三档**(docs/design.md §7.7):一个**普通 C 库**,
不是框架的一部分。它证明那句话 ——

> 组件库本身就是普通 C 库:`#include "shrewd-lv-view/view.h"`,导出自己的头,
> 打成 CMake 包(`find_package` 导入)—— 与 shrewd-lv-view 被消费的方式完全一致。
> **命名前缀用组件库自己的,不占用 `shrewd_`。**

所以这里的一切都叫 `demo_*`,不叫 `shrewd_*`。

## 里面有什么

| 产出物 | 文件 | 属于 |
|---|---|---|
| **控件**(新的 `lv_obj` 类型) | `demo/gauge.h` `library/gauge.c` | §7.3,要 lvgl 私有头(第三档) |
| **片段**(可复用的 UI 部分) | `demo/card.h` `library/card.c` | §7.2,含 v-model / 槽 / 设计 token |
| **绑定胶水** | `demo_bind_gauge_value()` | §6:第三方也能扩绑定 |

**绑定胶水是组件库该出的东西**:控件除了 widget,还要给它接上响应式绑定的那张
target 表 + 便捷函数。框架不用改 —— 这正是 §6「新增一个 widget 属性 = 加一张静态
target 表 + 一个 3 行的便捷函数」。

## 建 / 装 / 用

与依赖同处一个 prefix:

```sh
# 1. 先有依赖和框架(见仓库根的 tools/build-deps.sh)
# 2. 建组件库
cmake -S . -B build -DCMAKE_PREFIX_PATH=<prefix>
cmake --build build
cmake --install build --prefix <同一个 prefix>
# 3. 自测
ctest --test-dir build
```

消费者只需要:

```cmake
find_package(demo-components CONFIG REQUIRED)
target_link_libraries(app PRIVATE demo-components::demo-components)
```

```c
#include "demo/gauge.h"

lv_obj_t *g = demo_gauge_create(parent);
demo_bind_gauge_value(g, &some_ref);   /* 就接上了 */
```

`examples/prototype` 就是这么用的。

## 两个只有真打包才撞得到的点

1. **warnings 目标要 `$<BUILD_INTERFACE:...>` 包住** —— 静态库会把私有链接依赖记进
   导出集,而它是本库自己的,不该跟着包走(shrewd-lv-view 同样处理)。
2. **带写回的 target 表不能是 `const`** —— 写回要挂自定义事件 id,而它是**运行时**
   `lv_event_register_id()` 注册的,只能在 `init` 里填。label 那种没有自定义事件的
   才能写死成 `static const`。
