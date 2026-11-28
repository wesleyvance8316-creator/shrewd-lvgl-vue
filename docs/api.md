# API 总览

`shrewd-lv-view` 0.1.0 —— 面向嵌入式 Linux 的 LVGL 响应式框架,**纯 C**。
消费者只 include 一个头:

```c
#include "shrewd-lv-view/view.h"
```

> 教程见 [guide.md](guide.md);设计与取舍见 [design.md](design.md)、
> [architecture.md](architecture.md)。

---

## 0. 三句话的心智模型

1. **数据 → UI 是绑定**:`ref` 是数据格,`bind_*` 把它接到 widget 上。数据一改,
   UI 自动跟着变 —— 没有"刷新界面"这回事。
2. **绑定就是 effect**:底层是 shrewd-basic 的反应核心(读即登记、写即通知)。
   你写的绑定函数会被自动重跑,不需要自己管理依赖。
3. **页面是"描述"不是"对象"**:导航栈里存的是 `{页面, 参数}`,widget 树由释放
   策略单独决定何时建/销。所以**栈深 ≠ 实例数**,递归嵌套天然支持。

铁律只有一条:**所有 `ref_set` 必须发生在跑 `lv_timer_handler()` 的那个线程**。
别的线程要写数据,走桥(§6)。

---

## 1. 约定与错误

| | |
|---|---|
| 返回值 | `int`;`0` 成功,负值失败,数据走出参 |
| 内存 | 一律 `shrewd_basic_platform_*`,不碰 `malloc` |
| 命名 | `shrewd_lv_view_*` / `SHREWD_LV_VIEW_*` |

```c
const char *shrewd_lv_view_version(void);
const char *shrewd_lv_view_error_name(int code);   /* 不认识的码回落 shrewd-basic */
```

**shrewd-basic 的失败原样透传,不跨层重编码** —— 号段不相交,来源一眼可辨
(`docs/design.md` §12)。本仓的码只用于两件事:本仓自己的检查、LVGL 的失败
(LVGL 没有错误码)。

| 号段 | 归属 |
|---|---|
| `-0x60xx` | 通用:`ARG_INVALID` `MEM_NOMEM` `NOT_FOUND` `FULL` `NOT_MOUNTED` |
| `-0x61xx` | 导航:`NAV_STACK_FULL` `NAV_CANCELLED` `NAV_EMPTY` |
| `-1..-9999` | shrewd-basic 的(原样返回) |

---

## 2. 应用装配

框架**不带 main、不持有主循环**(`docs/design.md` §9.2)。

```c
typedef struct shrewd_lv_view_app { /* … */ } shrewd_lv_view_app_t;

int  shrewd_lv_view_app_init(shrewd_lv_view_app_t *, lv_display_t *display);
int  shrewd_lv_view_app_use(shrewd_lv_view_app_t *, const shrewd_lv_view_plugin_t *);
int  shrewd_lv_view_app_mount(shrewd_lv_view_app_t *, shrewd_lv_view_page_t *, const void *params);
int  shrewd_lv_view_app_step(shrewd_lv_view_app_t *);          /* drain 桥 + lv_timer_handler */
void shrewd_lv_view_app_set_bridge(shrewd_lv_view_app_t *, shrewd_lv_view_bridge_t *);
```

`app_init` 在没有 tick 源时会装一个 `clock_gettime` 的默认源 —— **没有 tick 源
`lv_timer_handler()` 里的定时器永不推进**。要自己给就在 `app_init` 之前
`lv_tick_set_cb()`。

---

## 3. 壳(持久层)

标题栏 / 底栏不进页面栈、一直活着,**它们的 UI 是导航状态的绑定**。

```c
typedef struct shrewd_lv_view_shell {
    lv_obj_t *root, *header, *title, *content, *footer;
    shrewd_lv_view_nav_t *nav;
    uint32_t tab_count;
} shrewd_lv_view_shell_t;

int shrewd_lv_view_shell_create(shrewd_lv_view_shell_t *, lv_display_t *,
                                shrewd_lv_view_nav_t *, int32_t header_height,
                                int32_t footer_height);        /* 0 = 不要底栏 */
int shrewd_lv_view_shell_add_tab(shrewd_lv_view_shell_t *, const char *label);
```

`shell_create` 会把 `nav->host` 指到内容区,**标题自动跟 `nav->current_name`**;
`add_tab` 建按钮 + 点击切分支 + 高亮跟 `nav->current_branch`。**全程不手动同步。**

---

## 4. 绑定:数据 → UI

### 4.1 机制(一份)

```c
typedef struct shrewd_lv_view_target {
    void (*write)(void *obj, const void *value, size_t size);   /* ref → widget */
    int  (*read)(void *obj, void *out, size_t size);            /* widget → ref,NULL = 单向 */
    uint32_t changed_event;                                     /* 写回触发的事件码 */
} shrewd_lv_view_target_t;

int shrewd_lv_view_bind(void *obj, const shrewd_lv_view_target_t *target,
                        shrewd_basic_reflect_ref_t *ref);
```

`applying` 抑制、`LV_EVENT_DELETE` 自清、写回接线都在这一个函数里。

### 4.2 便捷绑定(薄壳)

| API | 方向 | ref 槽位 |
|---|---|---|
| `shrewd_lv_view_bind_label_text(obj, ref)` | 单向 | `char[N]` |
| `shrewd_lv_view_bind_slider_value(obj, ref)` | 双向 | `int32_t` |
| `shrewd_lv_view_bind_textarea_text(obj, ref)` | 双向 | `char[N]` |
| `shrewd_lv_view_bind_tab_bar(bar, ref)` | 单向 | `uint32_t` |

**新增一个 widget 属性 = 加一张静态 target 表 + 一个 3 行便捷函数**,不改机制。

### 4.3 v-model

**组件级 v-model 不需要新机制** —— 把 `ref` 递进参数里即可,父既不写"更新"也不
听"事件"。比 Vue 的 `:modelValue` + `@update:modelValue` 省一层。

> **写回只由用户交互驱动**:程序化的 `lv_*_set_value()` **不发** `VALUE_CHANGED`,
> 所以"ref → widget"和"widget → ref"天然分得开,不成环。

---

## 5. 列表与条件渲染

**两种变化走两个机制**:内容变化 → 反应式(item 里的 ref);结构变化 → 对账
(source ref)。所以 `build` **只跑一次**。

```c
typedef struct shrewd_lv_view_list {
    const void *items;
    uint32_t count;
} shrewd_lv_view_list_t;

typedef struct shrewd_lv_view_list_desc {
    const void *(*at)(void *ctx, uint32_t i);   /* NULL = items + i * item_size */
    uint32_t (*key)(void *ctx, uint32_t i);     /* 稳定标识,必需 */
    size_t item_size;
    size_t instance_size;                       /* 0 = 无实例;框架分配、销毁时释放 */
    int32_t item_height;                        /* > 0 = 开启虚拟化(必须等高) */
    lv_obj_t *(*build)(void *ctx, lv_obj_t *container, const void *item, void *instance);
    void (*teardown)(void *ctx, void *instance);
} shrewd_lv_view_list_desc_t;

int shrewd_lv_view_bind_list(lv_obj_t *container,
                             shrewd_basic_reflect_ref_t *source,
                             const shrewd_lv_view_list_desc_t *desc, void *ctx);
```

对账规则:**按 key 增删复用**;顺序变化只挪不重建;key 集合 0 或 1 时退化成
条件渲染。容器里**只放列表子项**。

**虚拟化**:`item_height > 0` 时只物化**可视窗口**那几项,内存 O(可视) 而不是
O(总数)。滚动时窗口边缘上 teardown + build;框架把控件摆到 `(容器宽, item_height)`
并在容器里放一根撑高度的 spacer。**行高必须等高**。

---

## 6. 跨线程桥

```c
int  shrewd_lv_view_bridge_init(shrewd_lv_view_bridge_t *);
void shrewd_lv_view_bridge_dispose(shrewd_lv_view_bridge_t *);
int  shrewd_lv_view_bridge_post(shrewd_lv_view_bridge_t *, shrewd_basic_reflect_ref_t *,
                                const void *value, size_t size);   /* 任意线程 */
int  shrewd_lv_view_bridge_drain(shrewd_lv_view_bridge_t *);       /* UI 线程 */
int  shrewd_lv_view_bridge_bind_dbus(shrewd_lv_view_bridge_t *, shrewd_basic_dbus_t *,
                                     const char *pattern, shrewd_basic_reflect_ref_t *,
                                     shrewd_lv_view_dbus_bind_t *, shrewd_basic_dbus_handle_t **);
```

`post` 只入队,`drain` 一次性吃掉整队再逐个 `ref_set` —— 50 个字段变化不会唤醒
UI 线程 50 次。`bind_dbus` 把 sb_dbus 的分发线程直接接到桥上。

---

## 7. 页面管理

### 7.1 页面 = 片段 + 导航标识

```c
typedef struct shrewd_lv_view_page {
    const shrewd_lv_view_page_desc_t *desc;
    lv_obj_t *root;
} shrewd_lv_view_page_t;          /* 调用方持有,永久 */

typedef struct shrewd_lv_view_page_desc {
    const char *name;                              /* 恢复时按它找回实例 */
    const shrewd_basic_reflect_type_t *param_type; /* 参数的 reflect 描述 */
    shrewd_lv_view_release_t release;
    size_t cache_count;
    int  (*create)(shrewd_lv_view_page_t *, lv_obj_t *container, const void *params);
    int  (*enter)(shrewd_lv_view_page_t *);
    void (*exit)(shrewd_lv_view_page_t *);
    void (*destroy)(shrewd_lv_view_page_t *);
} shrewd_lv_view_page_desc_t;
```

**四阶段**:创建 / 激活 / 失活 / 销毁,顺序确定、一次不多一次不少。

| `release` | 含义 |
|---|---|
| `RESIDENT` | 建一次不销毁 |
| `DESTROY_ON_EXIT` | 失活即销毁(默认) |
| `CACHE` | 留 `cache_count` 个已建的,按最久未激活淘汰 |

**释放策略管的是 widget 树,不是实例** —— 状态与视图分离,所以敢释放。

### 7.2 操作集(作用在当前栈:模态非空时是它,否则当前分支)

| API | 语义 | 产生历史 |
|---|---|---|
| `shrewd_lv_view_nav_push(nav, page, params)` | 前进 | 是 |
| `shrewd_lv_view_nav_pop(nav)` | 返回一层 | 弹栈 |
| `shrewd_lv_view_nav_pop_until(nav, pred, ctx)` | 按谓词连续 pop,**留下**命中项 | 弹栈 |
| `shrewd_lv_view_nav_remove_until(nav, page, params, pred, ctx)` | **压栈并清下层**(命中项留下) | 是 |
| `shrewd_lv_view_nav_replace(nav, page, params)` | 同级跳转,不产生历史 | 否 |
| `shrewd_lv_view_nav_modal_push(nav, page, params)` | 压进模态栈 | 是 |

```c
typedef int (*shrewd_lv_view_nav_pred_fn)(void *ctx, const shrewd_lv_view_page_t *page);
```

**按谓词不按索引**:谓词从栈顶往下找第一个满足的,递归场景的歧义自然消解为
"取最近的那个"。`nav_remove_until` 的 `pred` 传 `NULL` = 恒不命中 → 锚点以下全清。

查询:`shrewd_lv_view_nav_top` / `_nav_depth` / `_nav_in_modal`。

### 7.3 分支与模态

```c
int shrewd_lv_view_nav_switch_branch(nav, size_t branch);   /* 换分支,不产生历史 */
```

**分支各持独立栈**(tab 切换不产生历史),**模态是独立一层**叠在 LVGL top layer 上。
模态压着时不允许 `switch_branch`。

### 7.4 守卫

```c
typedef enum shrewd_lv_view_guard_result {
    SHREWD_LV_VIEW_GUARD_ALLOW,     /* 放行 */
    SHREWD_LV_VIEW_GUARD_CANCEL,    /* 取消 */
    SHREWD_LV_VIEW_GUARD_REDIRECT,  /* 改道到别的页面 */
    SHREWD_LV_VIEW_GUARD_CONSUME,   /* 吃掉请求,不真走 */
} shrewd_lv_view_guard_result_t;

typedef shrewd_lv_view_guard_result_t (*shrewd_lv_view_guard_fn)(
        void *ctx, shrewd_lv_view_nav_request_t *);   /* req->to / from / redirect */

void shrewd_lv_view_nav_set_guard(shrewd_lv_view_nav_t *, shrewd_lv_view_guard_fn, void *ctx);
```

`CONSUME` 是"再按一次退出"这类交互的机制。

### 7.5 栈恢复

```c
int shrewd_lv_view_nav_register(nav, page);   /* push 会顺带登记 */
int shrewd_lv_view_nav_save_size(const nav *, size_t *out);
int shrewd_lv_view_nav_save(const nav *, void *buf, size_t size);
int shrewd_lv_view_nav_restore(nav, const void *buf, size_t size);
```

存到哪归使用者(flash / 文件 / shm),框架只产出有效快照。**要恢复的页面必须先登记**
—— 快照里只有 `name`,得能找回实例。

---

## 8. 组件生态

```c
typedef void (*shrewd_lv_view_slot_fn)(lv_obj_t *slot, void *ctx);
```

框架只提供这**一个约定类型 + 生命周期保证**。其余是组件库自己的事:

| 产出物 | 是什么 | 怎么做 |
|---|---|---|
| **片段** | 可复用的 UI 部分 | 状态 struct + 创建/销毁函数,状态调用方内嵌 |
| **控件** | 新的 `lv_obj` 类型 | `lv_obj_class_t` + 属性表 + 事件,**要 lvgl 私有头** |

组件库是**普通 C 库**,打成 CMake 包 `find_package` 导入,**前缀用自己的,不占用
`shrewd_`**。样板见 `examples/component-lib/`(`demo-components`)。

> 控件作者的四个坑记在 `docs/design.md` §7.3;**带写回的 target 表不能是 `const`**
> —— 写回挂的自定义事件 id 是运行时注册的。

---

## 9. 常量

| | |
|---|---|
| `SHREWD_LV_VIEW_NAV_DEFAULT_LIMIT` | 32(每分支栈深上限,安全底线) |
| `SHREWD_LV_VIEW_NAV_BRANCHES` | 8(tab 数上限) |
| `SHREWD_LV_VIEW_MAX_PLUGINS` | 8 |
| `SHREWD_LV_VIEW_VERSION_STRING` | "0.1.0" |
