# shrewd-lv-view 详细设计

> 状态:**设计稿 v7(2026-11-26);设计已决,构建配置与脚手架已落地**。
> 上游:[architecture.md](architecture.md)(模型与架构——为什么是响应式、
> 数据/事件/页面的关系、线程模型、页面管理)。**先读那份**,本文假定它已被接受。
> 更上游:[initiation.md](initiation.md)(立项、目标与非目标)。
> 本文只谈"代码怎么定形",不重复"为什么是这个模型"。
> §15 是**已决汇总与下一步**;D1–D5 已落定,决策写在各自小节。

## 0. 读法

§1 是**硬约束**——不是我的偏好,是从 `react.c` / LVGL 9.6 头文件里实测出来的行为,
以及家族约定。方案里的每一条形状都由它们推出来,所以先看 §1,再看 §2 往后。
如果你不同意 §1 的某条推论,多半是那条约束本身要重新处理。

## 1. 硬约束

### 1.1 反应核心的真实语义(实测自 shrewd-basic `library/reflect/react.c`)

| # | 行为 | 对本层的后果 |
|---|---|---|
| C1 | `ref_set` 写完槽位后**无条件**通知订阅者,不做等值判断 | 只要有人写 ref,绑定就会重跑。**回路是必然的**,必须有显式抑制(§4) |
| C2 | effect 重入:运行中再次触发只置 `REACT_F_DIRTY`,当前轮结束后**重跑**,**不递归** | 栈不会爆;但一个 effect 可能被多跑一次,绑定写 widget 要幂等 |
| C3 | 每次运行先**清空依赖边再重收** | 条件依赖(分支里才读的 ref)是正确的,可以放心用 |
| C4 | `ref_get` 在 effect 之外是纯读,不登记 | 状态方读自己的值不会误订阅 |
| C5 | 依赖边由 `shrewd_basic_platform_*` 分配;**分配失败时静默漏跟踪** | 内存紧张下会出现"改了不刷新"。要记进约束与排查手册 |
| C6 | `effect_dispose` 只断边,**不释放 effect 本体**(调用方内嵌) | 绑定记录必须由本层自己分配、自己释放(§3) |
| C7 | effect 对象是**调用方内嵌**的 | 绑定记录里内嵌 effect,记录本身走 `shrewd_basic_platform_alloc` |

### 1.2 LVGL 9.6 的真实约束(实测自 `deps/lvgl/include/lvgl`)

| # | 行为 | 对本层的后果 |
|---|---|---|
| L1 | `lv_obj_add_event_cb(obj, cb, filter, user_data)` → `lv_event_dsc_t*` | 绑定的清理钩子有处可挂(`LV_EVENT_DELETE`) |
| L2 | `LV_USE_EXT_DATA` **默认 0** | `lv_event_desc_set_external_data` 那套带析构的私有数据**用不了**。清理必须自己写 |
| L3 | `lv_label_get_text` / `lv_textarea_get_text` 返回 **LVGL 自持指针** | 写回 ref **必须拷贝**,不能把指针塞进槽位 |
| L4 | `lv_label_set_text` 自持拷贝(除非 `_static`) | 写方向安全:ref 里的字符串可以被状态方随后改写 |
| L5 | `lv_slider_set_value(obj, v, anim)` 带 anim 参数 | 绑定一律传 `LV_ANIM_OFF`——反应式刷新不该带动画 |
| L6 | 无头可跑:`lv_display_create` + `set_buffers` + `set_flush_cb` + `lv_tick_set_cb` | 单测不需要显示器,§13 有夹具 |
| L7 | `lv_async_call(cb, user_data)` 由 `lv_timer_handler()` 驱动 | 跨线程桥(§11)的前提是调用方主循环在跑 `lv_timer_handler` |

### 1.3 家族约定

- 命名:公共标识符一律 `shrewd_lv_view_*`,宏 `SHREWD_LV_VIEW_*`。
- 内存:一律 `shrewd_basic_platform_alloc/calloc/free`,不碰 `malloc`。
- 错误:非布尔接口返回 `int`,`SHREWD_BASIC_ERR_OK`(=0)成功、负值失败,数据走出参。
- 文本:shrewd-basic 的 reflect **已经区分两种字符串语义**——`KIND_STRING`(`char *`
  指针,所有权在状态方)与 `FIELD_TEXT`(定长内联缓冲,按 C 串渲染/比较)。本层**对齐
  这套语义,不另造**。

## 2. 核心抽象:绑定就是 effect

Vue 的 `watchEffect(() => el.textContent = state.value)` 换到 C 就是:

```c
static void bind_label_text_fn(void *ctx) {
    shrewd_lv_view_bind_t *b = ctx;
    const char *s = *(const char * const *)shrewd_basic_reflect_ref_get(b->ref);
    b->applying = 1;
    lv_label_set_text((lv_obj_t *)b->obj, s);
    b->applying = 0;
}

int shrewd_lv_view_bind_label_text(lv_obj_t *obj, shrewd_basic_reflect_ref_t *ref) {
    /* 分配绑定记录 → effect_init(fn, record) → 挂 LV_EVENT_DELETE */
}
```

`effect_init` 会**立刻跑一次**(收依赖),之后 `ref_set` 自动重跑。整个"状态 → UI 自动
传播"就这一件事,其余全是它的簿记。

由 C2,`bind_label_text_fn` 必须**幂等**:重跑一次只是把同样的字符串再写一遍,不产生副作用。

## 3. 生命周期:widget 即作用域

**铁律:绑定的生命周期 = widget 的生命周期。**(D1 已决)

> **调研依据**:Vue 3 的 `effectScope` —— "an effect scope object which can **capture
> the reactive effects created within it** so that these effects can be **disposed
> together**",`scope.stop()` 一次性释放。React 的 `useEffect` cleanup、Android 的
> `LiveData.observe` 随生命周期自动解除,同构。**这是作用域模型,不是逐个解绑**:
> 使用者不记不解绑,**漏解绑在结构上不可能**。只有 Flutter 偏手动(`dispose()` 要
> 自己调),未采纳——与"使用者不操心"的目标相反。

绑定记录由本层 `shrewd_basic_platform_alloc` 分配(C7),内嵌一个 effect。每个绑定在
它的 widget 上挂一个 `LV_EVENT_DELETE` 回调,`user_data` 指向记录。widget 被删时:
`effect_dispose`(断订阅)→ 释放记录。

    组件 root 被 lv_obj_delete
        → LVGL 自底向上删整棵子树,逐个发 LV_EVENT_DELETE
            → 每个绑定各自解绑、自清
    组件的 dispose 因此只需一行:lv_obj_delete(root)

**契约(必须写进头文件文档)**:状态 struct 由调用方内嵌,它必须活得比 widget 久。
`dispose()` 先删 widget(级联解绑),之后状态 struct 才可复用或出作用域。反过来
(先释放状态、后删 widget)会留悬挂引用。

三个层次的所有权分开看:

| 对象 | 所有者 | 释放时机 |
|---|---|---|
| 状态 struct(含 ref 本体与槽位) | 调用方 | widget 树删除**之后** |
| 绑定记录(含 effect) | 绑定层 | widget 删除时(级联) |
| 依赖边 | 反应核心 | 重跑时清,或 `effect_dispose` |
| LVGL widget | LVGL | `lv_obj_delete` |

## 4. 双向绑定与回路抑制

`v-model` = 单向绑定(ref → widget)+ 事件写回(widget → ref)。

由 C1,`ref_set` 无条件通知,**回路是必然的**:

    ref_set → effect 重跑 → lv_slider_set_value → LV_EVENT_VALUE_CHANGED
          → 写回回调 → ref_set → …

**抑制机制:`applying` 标志,集中在绑定记录里**。effect 往 widget 写时置位,写回回调
第一行检查,置位期间直接返回。这样抑制只有一处实现,不靠各绑定自觉。

要不要顺带做等值判断?**不作为正确性手段**——`applying` 已经保证不回路。但 widget
侧可以做一次"值没变就跳过 `lv_*_set_*`",减少无效 invalidate,属于可选优化,不该
被当成安全性的一部分。

写回的方向还有一个细节:widget → ref 时若 ref 的槽位就是 widget 刚才读到的那份数据,
`ref_set` 仍然通知所有订阅者(含自己)。`applying` 已经挡住自己这一条;其他订阅者
会照常跑,这是对的(它们确实该知道值变了)。

## 5. 值类型与槽位语义

ref 是**定宽字节槽**(`void *slot; size_t size`)。绑定层提供两类适配:

**数值**——槽位即 `int32_t` 等,直接读写。

**文本**——对齐 shrewd-basic 的两种语义:

| 槽位 | 类比 | 写向 widget | widget 写回 | 适用 |
|---|---|---|---|---|
| `char[N]` 定长缓冲 | `FIELD_TEXT` | `lv_*_set_text(obj, slot)` | `strncpy` 拷进 slot,整块 `ref_set` | **双向(v-model)** |
| `const char *` 指针 | `KIND_STRING` | `lv_*_set_text(obj, ptr)` | 不支持(需状态方另有缓冲) | 单向展示 |

关键点(L3):widget 读回来的是 **LVGL 自持指针**,写回 ref 必须**拷贝**;而写向 widget
(L4)是 LVGL 自持拷贝,状态方随后改自己的缓冲是安全的,只要记得 `ref_set` 通知一下。

**这与 LVGL 自己的约定一致**(D3 已决,已从 vendored 源码核实):

| LVGL API | 语义 |
|---|---|
| `lv_label_set_text` | *"Memory will be allocated to store the text by the label"* —— **拷贝,安全** |
| `lv_label_set_text_static` | *"It will not be saved by the label so the 'text' variable has to be 'alive' while the label exists"* —— **借用,契约是字符串活过 widget** |

所以本层是**「默认安全 + 变体借用」**:`char[N]` 是安全默认(可 v-model),`const char *`
是借用变体,**契约与 LVGL 的 `_static` 完全相同**——不给使用者新的心智负担。便捷函数的
命名沿用 `_static` 后缀的习惯。

`char[N]` 槽位的代价是长度上限与整块写。这是 C 里换所有权干净的价钱,先接受它;
指针形态留给"只展示、状态方自持内存"的场景。

## 6. API 形状:两层(D2 已决)

**底层是机制,上层是手感。** 机制只有一份,便捷函数是薄壳。

```c
/* 底层:一个通用绑定 + 属性适配器 */
typedef struct shrewd_lv_view_target {
    void (*write)(void *obj, const void *value, size_t size); /* ref → widget */
    int  (*read)(void *obj, void *out, size_t size);          /* widget → ref,无则 NULL */
    uint32_t changed_event;   /* 写回触发的事件码,read 为 NULL 时忽略 */
} shrewd_lv_view_target_t;

int shrewd_lv_view_bind(void *obj,
                        const shrewd_lv_view_target_t *target,
                        shrewd_basic_reflect_ref_t *ref);

/* 上层:每个 widget/属性一个便捷函数 */
int shrewd_lv_view_bind_label_text(lv_obj_t *obj, shrewd_basic_reflect_ref_t *ref);
int shrewd_lv_view_bind_slider_value(lv_obj_t *obj, shrewd_basic_reflect_ref_t *ref);
int shrewd_lv_view_bind_textarea_text(lv_obj_t *obj, shrewd_basic_reflect_ref_t *ref);
int shrewd_lv_view_bind_tab_bar(lv_obj_t *bar, shrewd_basic_reflect_ref_t *ref);
```

- `write` 为 NULL 的 target 不合法;`read` 为 NULL 即**单向**;`read` 非 NULL 即**双向**,
  自动挂 `changed_event` 做写回。`applying` 抑制在底层,不进适配器。
- 新增一个 widget 属性 = 加一张静态 target 表 + 一个 3 行的便捷函数,不改机制。

为什么不只做便捷函数?那样 `applying`、DELETE 清理、写回接线会散进每个函数里,改一处
要改 N 处。为什么不只做底层?因为立项文档写了"API 即产品",调用点的手感是交付物。

## 7. 组件与组件生态

组件生态是"**使用者怎么开发、怎么用组件**"的形式化定义。机制面与默认值已全部落定;
调研依据与被堵死的路见 §7.8。

> **已落地**:`shrewd_lv_view_slot_fn`(§7.5 框架唯一提供的东西)、片段样板
> `tests/demo_card.{c,h}`(§7.2 + v-model §7.4 + 槽 §7.5 + 设计 token §7.6)、
> 控件样板 `tests/demo_gauge.{c,h}`(§7.3,含四个坑的实测)。单测
> `test_component.c` / `test_widget.c`。

### 7.1 两种产出物

组件库有两种产出物,**层次不同、形态不同**:

| | **控件(widget class)** | **片段(UI 片段)** |
|---|---|---|
| 是什么 | **新的 `lv_obj` 类型** | **可复用的 UI 部分**(一棵树) |
| 状态放哪 | 对象实例里(`instance_size`) | 状态 struct 里(ref) |
| 怎么用 | 当 widget 用,可放任意容器 | 放进容器 |
| props | **LVGL property**(ID + 类型) | **创建函数参数**(可序列化) |
| 生命周期 | LVGL 的构造 / 析构 | **四阶段钩子**(自实现) |
| 事件 | LVGL event + 自定义 event id | LVGL event(绑内部 widget) |
| 组合 | 内部建子 widget | **作用域嵌套** |
| 典型例子 | 自定义 gauge、color picker、chart | 卡片、表单、列表项、页面 |

**选择准则**:要当"控件"用(可放任意容器、要属性反射、要像原生 widget)→ **控件**;
要"复用一段 UI + 状态 + 生命周期"→ **片段**。片段内部可以用控件,方向不反。

### 7.2 片段:UI 片段

对齐立项文档:**状态 struct + 创建函数 + 销毁函数**,状态 struct 调用方内嵌。

```c
typedef struct {
    lv_obj_t *root;
    /* 状态:ref 本体 + 各自的槽位 */
    shrewd_basic_reflect_ref_t title;   char   title_buf[64];
    shrewd_basic_reflect_ref_t level;   int32_t level_val;
} shrewd_demo_card_t;

int  shrewd_demo_card_create(shrewd_demo_card_t *c, lv_obj_t *parent,
                             const shrewd_demo_card_params_t *p);
void shrewd_demo_card_dispose(shrewd_demo_card_t *c);   /* 就一行 lv_obj_delete(c->root) */
```

- **创建函数**:init 各 ref(槽位指向同 struct 里的字段)→ 建 widget 树 → 逐个绑定。
- **销毁函数**:按 §3 只需删 root,绑定级联自清。**不要**在里面手动 dispose 绑定
  ——那是机制的活。
- **生命周期**用 §architecture.md 6.2 的四阶段钩子(创建 / 激活 / 失活 / 销毁),
  自实现,形状与 Android Fragment 一致(§7.8)。
- **组合**:片段内建子片段,**作用域嵌套**——父作用域结束,子作用域一并自清(§3)。
- **参数**(`params_t`)走 §6.11 的 Q2:**reflect 描述、只放简单值**;复杂数据走状态系统。
- 组件**不碰 dbus**(桥层才碰),所以组件可独立单测。

### 7.3 控件:widget class

组件库可以往 LVGL 里**加新的控件类型**,与原生 widget 同等对待:

```c
typedef struct { lv_obj_t obj; int32_t value; /* … */ } mylib_gauge_t;

extern const lv_obj_class_t mylib_gauge_class;              /* 类描述符 */
extern const lv_property_ops_t mylib_gauge_properties[];    /* 属性表 */

lv_obj_t *mylib_gauge_create(lv_obj_t *parent);
void      mylib_gauge_set_value(lv_obj_t *g, int32_t v);
```

类描述符是 LVGL 的 `lv_obj_class_t`:`base_class`(继承)、`constructor_cb` /
`destructor_cb`、`event_cb`、`instance_size`、属性表。

> **⚠ 配置是硬要求,不是可选项。**
> `struct _lv_obj_class_t` 的定义**只在 lvgl 私有头** `src/core/lv_obj_class_private.h`
> (公共树只有前向声明),而属性字段在 `#if LV_USE_OBJ_PROPERTY` 里、**插在结构体中间**
> ——开关不同则 `sizeof` 不同,**ABI 不兼容**。所以:

| 配置 | 为什么必须 |
|---|---|
| **`LV_USE_PRIVATE_API`** | 才有 `lv_obj_class_t` 的完整定义 |
| **`LV_USE_OBJ_PROPERTY`** | 类型化属性(props) |
| **自带 `lv_conf.h`**(`-DLV_BUILD_CONF_PATH`) | 它会被装进 `include/lvgl/config/`,`LV_CONF_INCLUDE_SIMPLE` 也传给消费者 → **全链程一致**。`LV_CONF_SKIP` 下配置不随包走,一致性无从谈起 |

好消息:lvgl 的 `install.cmake` 已处理好分发——私有头装到 `include/lvgl_private/`
并改写相对路径。**控件作者要引四个头**(照 lvgl 自己 widget 的写法,已实测):

```c
#include "lvgl_private/lvgl_public.h"
#include "lvgl_private/core/lv_obj_private.h"          /* lv_obj_t 的完整定义 */
#include "lvgl_private/core/lv_obj_class_private.h"    /* lv_obj_class_t 的完整定义 */
```

**属性号段要自己划**:`lv_obj_property.h` 里 `LV_PROPERTY_ID_BUILTIN_LAST = 0xffff` 的
注释明写 *"ID of 0x10000 ~ 0xfffffff is reserved for user"*。所以:

```c
enum {
    LV_PROPERTY_PROBE_START = 0x10000,          /* ← 自定义号段从这里起 */
    LV_PROPERTY_PROBE_VALUE = LV_PROPERTY_PROBE_START + 0,
    LV_PROPERTY_PROBE_END,
};
static const lv_property_ops_t lv_gauge_properties[] = { … };   /* ★ 必须叫 lv_<widget>_properties */

const lv_obj_class_t mylib_gauge_class = {
    .instance_size = sizeof(mylib_gauge_t),
    .base_class = &lv_obj_class,
    .name = "mylib_gauge",
    LV_PROPERTY_CLASS_FIELDS(gauge, PROBE)      /* 展开找 lv_gauge_properties */
};
```

**四个坑**(前两个是探针实测,后两个是把 `tests/demo_gauge.c` 写进树里时撞的,
**8 条断言齐红才找到**):

| 坑 | 后果 |
|---|---|
| `LV_PROPERTY_ID(...)` 是**枚举声明**用的宏 | 不能直接写进数组初始化器 |
| `LV_PROPERTY_CLASS_FIELDS(widget, uppercase)` 找 `lv_<widget>_properties` | 名字不对就编译不过 |
| **`lv_obj_class_create_obj()` 只分配,构造在 `lv_obj_class_init_obj()` 里** | 少了第二步,constructor 不跑、属性全落在零值上 —— 要照 `lv_slider_create` 的**两步**写 |
| `lv_property_ops_t` 的 setter/getter 是 `void *`,而 **ISO C 不许函数指针与对象指针互转** | `-Wpedantic` 必叫,显式转换也压不掉;就地 `#pragma` 抑制,别改签名 |

> 前两个最初只在 `/tmp/lvcfg/probe.c` 里验过(`sizeof(lv_obj_class_t)=88`、属性号段
> `0x10000` 生效)。现在**树里有真的**:`tests/demo_gauge.{c,h}` + `tests/test_widget.c`,
> 验类真的造出新类型、props 走 property、自定义事件真发、实例与 property 读回一致。

**自带 `lv_conf.h` 与这两条开关无关,本来就该做**:面向嵌入式,内存 / 字体 / 功能
裁剪该由我们定,不该吃 lvgl 默认。

### 7.4 props、事件与组件级 v-model

| | 控件 | 片段 |
|---|---|---|
| **props** | LVGL property(`LV_PROPERTY_ID(clz, name, type, index)`,类型编码在 ID 里) | 创建函数参数(reflect 描述) |
| **事件** | LVGL event + 自定义 event id(`lv_event_register_id`) | LVGL event(绑内部 widget) |

**组件级 v-model 不需要新机制** ——它是"参数里放 ref"的自然结果:

```c
typedef struct {
    shrewd_basic_reflect_ref_t *value;   /* ← 传 ref 进来 */
    int32_t min, max;
} mylib_slider_params_t;
```

组件内部 `bind_slider_value(obj, params->value)` ——② 自动传播 + ③ 写回都在里头。
**父既不写"更新"也不听"事件"**。这比 Vue 的 `:modelValue` + `@update:modelValue`
省一层:那里是两条单向的语法糖,这里是直接把 ref 递进去。

> **写回为什么不成环**(实测):**程序化的 `lv_*_set_value()` 不发 `VALUE_CHANGED`**
> —— 它只置值 + 刷新(`lv_bar_set_value` 就没 `lv_obj_send_event`)。写回只由
> **用户交互**驱动的事件触发。于是"ref → widget"和"widget → ref"天然分得开:
> 前者是绑定写 widget,后者只在人动了 UI 时才发生。单测里模拟人是
> `lv_obj_send_event(obj, LV_EVENT_VALUE_CHANGED, NULL)`。

### 7.5 slots:构建回调 + 命名槽

底层机制只有一个类型:

```c
/* 槽:父提供内容的构建方式,子决定何时、何处调用 */
typedef void (*shrewd_lv_view_slot_fn)(lv_obj_t *slot, void *ctx);
```

**约定四条**:

1. 组件在创建时接收槽(单槽,或多槽);
2. 组件**在自己决定的时机**调用 `build(slot_container, ctx)` **一次**;
3. 槽容器是组件建的 widget,槽内容成为它的子节点 → **绑定随 widget 自清**(§3),
   生命周期自然闭环;
4. 槽内容**内部可用绑定**(响应式)。

**多槽用类型化结构**,不用字符串键——C 里编译期安全更值钱,而且结构体本身就是文档:

```c
typedef struct {
    shrewd_lv_view_slot_fn header;   void *header_ctx;
    shrewd_lv_view_slot_fn content;  void *content_ctx;
    shrewd_lv_view_slot_fn footer;   void *footer_ctx;
} mylib_panel_slots_t;

int mylib_panel_create(mylib_panel_t *p, lv_obj_t *parent,
                       const mylib_panel_params_t *params,
                       const mylib_panel_slots_t *slots);
```

**框架只提供 `shrewd_lv_view_slot_fn` 这个约定类型 + 生命周期保证**;槽名由组件库
用类型化结构定,各自自文档化又不失统一。

> **边界**:slot 是**一次创建**。槽内容要**整体替换**(不同分支建不同的树)不是 slot
> 的活,是**条件渲染 / 列表对账**(§10)的活。别混用。

### 7.6 主题

不自造主题引擎,LVGL 有:

| | 机制 |
|---|---|
| **控件** | 走 **LVGL 主题**(`lv_obj_class_t.theme_inheritable` + `lv_theme_t`),与原生 widget 一致 |
| **片段** | 暴露**可覆盖的 style 常量表**(设计 token),创建时套用 |

### 7.7 facade:三档

最早那句"消费者只 include `view.h`"要如实分档:

| 角色 | 引用什么 |
|---|---|
| **应用开发者** | 只 `shrewd-lv-view/view.h` |
| **片段作者** | `view.h` 就够 |
| **控件作者**(造新 widget 类型) | `view.h` **+ `lvgl_private/lvgl_public.h`** |

第三档确实打破了"只 include view.h"——因为 `lv_obj_class_t` 的定义在 **lvgl 的私有头**,
而那是 **lvgl 的边界,不是我们的**。复制它的私有头是维护灾难,所以如实记为**扩展点**,
不假装规则没破。

**组件库本身就是普通 C 库**:`#include "shrewd-lv-view/view.h"`,导出自己的头,
打成 CMake 包(`find_package` 导入)——与 shrewd-lv-view 被消费的方式完全一致。
命名前缀用组件库自己的,不占用 `shrewd_`。

> **已落地**:`examples/component-lib/` 就是这个包 —— **`demo-components`**,前缀
> `demo_`,内含控件 `demo/gauge`、片段 `demo/card`,自装自测,`examples/prototype`
> 用 `find_package(demo-components)` 消费它。**§7.2/§7.3 的样板随它搬了过去**,
> 框架 `tests/` 只留机制单测。
>
> **组件库的产出物不止 widget,还有它的绑定胶水**:`demo_bind_gauge_value()`。
> §6 的扩展规则(一张 target 表 + 一个 3 行便捷函数)**从框架外面用得通**,不用改框架。
>
> 只有真打包才撞得到的两点,记在 `examples/component-lib/README.md`:warnings 目标要
> `$<BUILD_INTERFACE:...>` 包住(静态库会把私有链接依赖记进导出集);**带写回的 target
> 表不能是 `const`** —— 写回挂的自定义事件 id 是运行时注册的。

### 7.8 调研依据与被堵死的路

**两条路被调研堵死**(都来自 lvgl 9.6 vendored 源码与官方文档):

| 发现 | 结论 |
|---|---|
| **`lv_fragment` 已废弃**,官方 Callout:"This module is now deprecated and will be removed in an upcoming release" | **不建在它上面**。但**采纳它的形态**——四阶段钩子(`constructor` / `attached` / `detached` / `obj_will_delete` / `destructor`)、`child_manager` 组合、`instance_size`,与我们独立推导的四阶段几乎一一对应 |
| **`lv_menu` 已废弃**:"A menu is page navigation over base widgets … **Build it directly instead**" | LVGL 反复废弃现成组件、把 UI 组合推给使用者用基础 widget 搭。**控件之外的组合是我们的活** |
| `struct _lv_obj_class_t` 在公共树只有前向声明 | 控件作者需要私有头 → facade 第三档(§7.7) |

**两条依据支撑形状**:

| 依据 | 支撑 |
|---|---|
| `lv_fragment` 文档:"View object … will be destroyed, but **instances created in class constructor will be kept**" | **状态与视图分离**——LVGL 自己也这么做,印证 §6.3 |
| Vue 组件模型(props 声明 + 类型 + 校验、emits、slots 是函数参数) | 控件用 property 属性表;slots 用构建回调 |

**Vue 组件生态的对照**(已取官方文档 `props` / `events` / `slots`):

| Vue | 我们的对应 |
|---|---|
| `defineProps({title: String})` | 控件的 property 表(`LV_PROPERTY_ID`,类型编码在 ID 里) |
| `$emit('someEvent')` + 声明式 emits | LVGL event + 自定义 event id |
| `<slot>` / 具名槽 / scoped slot | `shrewd_lv_view_slot_fn` + 类型化多槽结构 |
| Component v-model(`:modelValue` + `@update:`) | **传 ref 即双向**(§7.4),比它省一层 |

## 8. 页面管理:API 形状

机制骨架在 [architecture.md](architecture.md) §6,本节落成 C 的形状;机制依据不重复。

### 8.1 页面 = 片段 + 导航标识

四阶段钩子(创建 / 激活 / 失活 / 销毁)对片段与页面是**同一套**,差别只在:页面有
**导航标识 + 参数描述**、被注册进导航栈;片段只是被别人用。

**所以同一套描述符** —— 页面 = 片段 + `name` + `param_type`。**不搞两套生命周期。**

### 8.2 页面描述符

```c
typedef struct shrewd_lv_view_page_desc {
    const char *name;                                /* ★ 标识,恢复时要序列化 */
    const shrewd_basic_reflect_type_t *param_type;    /* 参数的 reflect 描述 */
    shrewd_lv_view_release_t release;                 /* 释放策略(architecture.md 6.5) */

    /* 四阶段 */
    int  (*create)(shrewd_lv_view_page_t *p, lv_obj_t *container, const void *params);
    int  (*enter)(shrewd_lv_view_page_t *p);
    void (*exit)(shrewd_lv_view_page_t *p);
    void (*destroy)(shrewd_lv_view_page_t *p);
} shrewd_lv_view_page_desc_t;
```

**`name` 用字符串而非指针/枚举**:恢复时要靠它把页面找回来(快照序列化后跨进程、
跨重启,指针失效);组件库各自命名,字符串天然免撞号。

### 8.3 实例:调用方持有(永久)

> **这一条是 architecture.md 6.3「状态不丢」唯一能兑现的形态。**

```c
typedef struct {
    shrewd_lv_view_page_t base;            /* 必须在首位 */
    /* 状态 */
    shrewd_basic_reflect_ref_t title;  char title_buf[32];
} shrewd_my_home_t;

static shrewd_my_home_t home;              /* 调用方持有,永久 */
```

**释放策略只管 widget 树,不管实例**:

| | 形态 | "离即销毁"销毁什么 | 状态 |
|---|---|---|---|
| **(a) 调用方持有** ✅ | 页面 struct 永久 | **只 widget 树** | 在 struct 里,**真不丢** |
| (b) 框架按 `instance_size` 分配 | 实例随页面生死 | 实例 + widget | **丢**,除非在 params |

取 (b) 则 architecture.md 6.3 的论证塌了——"敢释放"的前提正是状态不丢。

**内存账**:实例几十字节,widget 树才是大头。所以「离即销毁」省的是 widget,不是
实例 —— 这正是 6.3 说的"代价很低"。

### 8.4 导航栈:条目与操作

**条目是描述,不持 widget**:

```c
typedef struct {
    const shrewd_lv_view_page_desc_t *desc;
    void    *params;      /* ★ 拷贝进条目(可序列化,支撑恢复) */
    uint32_t seq;         /* 区分同页面多次出现(architecture.md 6.11 Q4) */
    uint64_t entered_at;  /* 进入时间:超时返回 / 诊断 */
} shrewd_lv_view_nav_entry_t;
```

**参数拷贝进条目** —— architecture.md 6.11 Q2 定的"参数可序列化",正因为它是**可恢复
状态的载体**。

**操作集**(architecture.md 6.6):

```c
/* 已实现 —— 作用在**当前栈**(模态非空时是它,否则当前分支) */
int shrewd_lv_view_nav_push(shrewd_lv_view_nav_t *, shrewd_lv_view_page_t *page, const void *params);
int shrewd_lv_view_nav_pop(shrewd_lv_view_nav_t *);
int shrewd_lv_view_nav_pop_until(shrewd_lv_view_nav_t *, shrewd_lv_view_nav_pred_fn, void *ctx);
int shrewd_lv_view_nav_replace(shrewd_lv_view_nav_t *, shrewd_lv_view_page_t *page, const void *params);

/* 已实现 —— 分支与模态(architecture.md §6.9) */
int shrewd_lv_view_nav_switch_branch(shrewd_lv_view_nav_t *, size_t branch);
int shrewd_lv_view_nav_modal_push(shrewd_lv_view_nav_t *, shrewd_lv_view_page_t *page,
                                  const void *params);

/* 已实现 —— 压栈并清掉中间若干层(architecture.md §6.6) */
int shrewd_lv_view_nav_remove_until(shrewd_lv_view_nav_t *, shrewd_lv_view_page_t *page,
                                    const void *params, shrewd_lv_view_nav_pred_fn, void *ctx);
```

**分支与模态是同一套操作作用在不同的栈上**:`switch_branch` 换分支(不产生历史);
**模态栈初始为空,`push` 进不去它** —— 所以"打开模态"是一个独立操作
(`nav_modal_push`)。此后 `push`/`pop` 都作用在模态栈上(可嵌套),关完最后一层即
回到分支。**模态压着时不允许 `switch_branch`**。

> **实现修正**:原稿 `push`/`replace` 收 `desc`,但 §8.3 定的是**实例调用方持有** ——
> 两者不一致。改为收**实例**(实例里就有 `desc`,更直接);**登记表留给恢复用**,
> 因为恢复要按 `name` 找回实例。

`pop_until` / `remove_until` **按谓词不按索引**(architecture.md 6.6 的调研修正);
谓词从栈顶往下找第一个满足的,递归场景的歧义自然消解为"取最近的那个"。

> **实现修正(`remove_until`)**:三处稿子对不齐 —— §6.6 表格写「压栈并清掉中间
> 若干层 / 产生历史=是」,§15 一句写「`pop_until` 的变体,加个锚点」,上面这个
> 签名既没有 `params` 又管新页叫 `anchor`。**按 §6.6 表格定**(它经调研对齐
> Flutter `pushAndRemoveUntil`,`route`/`predicate` 两个参数一一对应):
>
> - **压栈并清下层**:把新页压成栈顶(**产生历史**),再从它下面一层往下,
>   谓词不命中的逐个清掉,第一个命中的留下(连同它以下全部)。
> - 签名随 `push` 走:第二个参数是**新页**,并且**要有 `params`**(压页总要参数)。
> - `pred == NULL` **恒不命中** → 锚点以下全清,栈里只剩新页。这是最常见的用法
>   (登录/向导结束直达 home),不必为此写一个恒假谓词。
>
> 它与 `pop_until` 的分工:`pop_until` **回到栈里已有的那层**(它留下);
> `remove_until` **去一个可能不在栈里的新页**,并把中间几层清掉。home 不在栈里时
> `pop_until` 表达不了。

**分支与模态是同一套操作作用在不同的栈上**(architecture.md 6.9):
`nav_switch_branch()` 切分支(不产生历史),模态栈独立、释放策略可不同。

### 8.5 守卫

```c
typedef enum {
    SHREWD_LV_VIEW_GUARD_ALLOW,       /* 放行 */
    SHREWD_LV_VIEW_GUARD_CANCEL,      /* 取消 */
    SHREWD_LV_VIEW_GUARD_REDIRECT,    /* 改道到别的页面 */
    SHREWD_LV_VIEW_GUARD_CONSUME,     /* 吃掉请求(不真走) */
} shrewd_lv_view_guard_result_t;

typedef shrewd_lv_view_guard_result_t (*shrewd_lv_view_guard_fn)(
        void *ctx,
        shrewd_lv_view_nav_request_t *req);   /* req->to / req->from / req->redirect */

void shrewd_lv_view_nav_set_guard(shrewd_lv_view_nav_t *, shrewd_lv_view_guard_fn, void *ctx);
```

> **实现修正**:原稿守卫只收 `to`/`from`,但 `REDIRECT` 需要**改道目标**却无处可填。
> 改收 `shrewd_lv_view_nav_request_t *`,守卫填 `req->redirect` 即改道。这比让守卫
> 再入调用 `nav_replace` 干净——**不重入**。

**全局注册一个,而非每页一个** —— 守卫要看 `to/from` 才能判断,每页一个反而要拼装。
四种结果见 architecture.md 6.7;`CONSUME` 就是"再按一次退出"的机制,于是
"栈空返回怎么办"不另设 API。

### 8.6 栈恢复

```c
int shrewd_lv_view_nav_register(shrewd_lv_view_nav_t *, shrewd_lv_view_page_t *page);
int shrewd_lv_view_nav_save_size(const shrewd_lv_view_nav_t *, size_t *out_size);
int shrewd_lv_view_nav_save(const shrewd_lv_view_nav_t *, void *buf, size_t size);
int shrewd_lv_view_nav_restore(shrewd_lv_view_nav_t *, const void *buf, size_t size);
```

保存的是条目的 `desc->name` + params;恢复时靠 `name` 找回页面、按序重建,**只激活栈顶**
(其余是描述,回退到它们时才建 widget —— 与释放策略一致)。**「存到哪」归使用者**
(architecture.md 6.11 Q3),框架负责产出有效快照。

**登记表**:`name -> 实例` 只服务于恢复(快照里只有 name,必须能找回实例)。
`push` 顺带登记;**启动时把可恢复的页面登记一次即可**,否则恢复会 `NOT_FOUND`。

> **实现修正**:原稿是 `save_fn` / `load_fn` 回调。**回调形式让调用方不知道要读多少**,
> 而且多一层分配归属问题。改为 `save_size` + `save` + `restore` 三个缓冲接口 ——
> 调用方查询长度、自己准备缓冲、自己决定写到哪,无歧义。

## 9. 项目形态与脚手架

项目叫 lvgl-vue,使用上就该有 **vue 味**。本节定**使用者的项目长什么样、怎么开箱**。
调研依据:`create-vue` 的模板全树(192 项,已解析)与 Vue 官方 tooling 文档。

### 9.1 "vue 味"是五件事

| # | 是什么 | Vue 的形态 | 我们的落法 |
|---|---|---|---|
| **V1** | **一条命令开箱** | `npm create vue@latest` | `tools/new-app.sh <名字>` |
| **V2** | **熟悉的目录形状** | `src/components/` `views/` `composables/` `stores/` | **同名同形** |
| **V3** | **就近内聚** | SFC 三段式 | 一个组件一个目录 |
| **V4** | **入口就是装配** | `main.ts` 的 `createApp().use().mount()` | `main.c` 的 `app_init().use().mount()` |
| **V5** | **按需特性 + 最小样板** | feature flags + `--bare` | CMake option + `--bare` |

**V3 学不了 SFC**(立项文档非目标:无模板编译、无代码生成、无中间产物),但
**「就近」在 C 里有表达**——见 §9.5。

### 9.2 入口:框架不带 main

拓扑 A(独立进程)与拓扑 B(业务主进程里的一个线程)的入口不同,所以
**框架不提供 `main`,也不持有主循环**——这正是「不隐藏线程」的落法。框架只给
**单步函数**,调用方决定在哪跑:

```c
shrewd_lv_view_app_t app;
shrewd_lv_view_app_init(&app, lv_display_get_default());
shrewd_lv_view_app_use(&app, &mylib_plugin);      /* 装组件库 */
shrewd_lv_view_app_mount(&app, &home_page);       /* 挂起始页 */
while (1) { shrewd_lv_view_app_step(&app); usleep(5000); }
```

`app_step()` 干三件事:**出队跨线程投递 → 逐个 `ref_set` → 驱动 `lv_timer_handler`**。
它是**必需的调用点**——桥要在 UI 线程出队,而框架不持有循环,必须有个地方让调用方戳它。

### 9.3 两种形态:默认 main,option 可选

**开箱即用**:默认生成 `int main`,`cmake && build && run` 就能跑起来。

```cmake
option(SHREWD_APP_AS_THREAD "Build as a library with gui_thread() instead of main()" OFF)
```

| 形态 | 产物 | 入口 | 用途 |
|---|---|---|---|
| **默认 standalone** | 可执行 | `app_main.c` 的 `main()` | **开箱、调试** |
| `SHREWD_APP_AS_THREAD=ON` | 库 | `app_thread.c` 的 `gui_thread()` | 嵌入交付(拓扑 B) |

**不是用编译开关做两种库**——库是一样的,变的只是入口文件;option 只是**选建哪个
target**。调试走独立,交付走线程。

### 9.4 目录约定

```
my-gui-app/
├── CMakeLists.txt             ← option:SHREWD_APP_AS_THREAD
├── lv_conf.h                  ← 可选:覆盖框架默认(经 `tools/build-deps.sh --lv-conf`)
├── src/
│   ├── app_main.c             ← 入口 A:独立进程(默认)
│   ├── app_thread.c           ← 入口 B:线程函数(嵌入交付)
│   ├── app.c / app.h          ← 装配:init / use / mount
│   ├── stores/                ← ★ 数据模型
│   │   ├── device.h / device.c
│   │   └── app_state.h / app_state.c
│   ├── components/            ← 组件(片段 / 控件),一目录一组件
│   │   ├── card/   card.h  card.c
│   │   └── gauge/  gauge.h gauge.c
│   ├── views/                 ← 页面
│   │   ├── home.h / home.c
│   │   └── settings.h / settings.c
│   ├── composables/           ← 复用逻辑
│   └── assets/                ← 图片、字体
├── tests/
└── docs/
```

**`stores/` 放什么**:

| 放 | 不放 |
|---|---|
| 数据结构定义(**含 reflect 描述** → 可序列化,支撑栈恢复与参数) | 组件的私有状态(跟组件目录) |
| **应用级共享 ref**(设备状态、全局配置) | widget(那是 views / components) |
| sb_dbus 路径约定与读写封装 | 业务逻辑(那是 composables) |

**为什么与组件状态分开**:§6.3 的分离在目录上也成立——**数据模型跨组件共享,
组件状态是组件私有的**。混在一个目录里,看不出谁是真相。

### 9.5 就近内聚:一个组件一个目录

没有 SFC,但**一个组件一个目录**——`.h`(接口)+ `.c`(实现)+ 可选资源 + 可选测试
都在一处:

```
components/card/
├── card.h
├── card.c
├── card_assets.h      ← 可选
└── card_test.c        ← 可选
```

页面同理(`views/home/home.{h,c}`)。

### 9.6 脚手架

框架仓带 `template/app/` + `tools/new-app.sh <名字>`:

| flag | 对应 |
|---|---|
| `--pages` | 页面管理(architecture.md §6) |
| `--list` | 列表 / 条件渲染(§10) |
| `--tests` | 无头测试夹具(§13) |
| `--bare` | 最小样板 |

**默认 `main`,option 切线程** —— 生成即能跑,这才叫开箱。

## 10. 列表与条件渲染:对账原语

立项文档定的口径:**容器子项按 key 最小对账,增删 lv 对象,不做 diff 算法**。
条件渲染是它的退化形态(key 集合是 0 或 1),共用一个原语。

### 10.1 两种变化,两个机制

先分清列表的两类变化——它们**不走同一个机制**:

| 变化 | 机制 | 谁触发 |
|---|---|---|
| **内容变化**(某个子项的字段变了) | **反应式**(§2) | item 里的 ref |
| **结构变化**(增删 / 重排) | **对账**(本节) | `source` ref |

这样对账不必管"更新内容",也不必重跑 `build`(见 §10.4)。

### 10.2 `source` ref 的槽位

对齐 Vue 的 `v-for="item in list"` —— **ref 持有列表本身**:

```c
/* ref 槽位类型 */
typedef struct {
    const void *items;   /* 连续数组;抽象源为 NULL */
    uint32_t    count;
} shrewd_lv_view_list_t;
```

**结构变了**(增删 / 重排)→ `ref_set` 整个结构 → 触发对账。
**抽象源**(如 sb_dbus 的 `/devices/*`)`items = NULL`,靠 `at` 回调取数据。

**`count` 由 ref 承载,不再做回调** —— 结构的真相只有一处。

### 10.3 数据源描述

```c
typedef struct {
    /* 数据访问:at 为 NULL 时用 items + item_size 推导 */
    const void *(*at)(void *ctx, uint32_t i);
    uint32_t (*key)(void *ctx, uint32_t i);   /* 稳定标识,必需 */
    size_t item_size;

    /* 子项 */
    lv_obj_t *(*build)(void *ctx, lv_obj_t *container, const void *item);
    void (*teardown)(void *ctx, lv_obj_t *root, const void *item);   /* 可选 */
} shrewd_lv_view_list_desc_t;

> **实现修正**:`at`/`key` 的 `ctx` 原稿是 `const void *`,与 `build`/`teardown` 的
> `void *` 不一致 —— 调用方同一份 ctx 要来回强制转换。**统一为 `void *`**。

int shrewd_lv_view_bind_list(lv_obj_t *container,
                             shrewd_basic_reflect_ref_t *source,
                             const shrewd_lv_view_list_desc_t *desc,
                             void *ctx);
```

**`item_ctx` 不需要了** —— 按 §3 的模型,**子项的状态在数据源里(含 ref),widget 只是
投影**。所以子项的 root widget 就是它的句柄:

- `build` **返回子项 root**;
- `teardown` **可选**(`NULL` = 就删 widget,绑定按 §3 级联自清)。

没有"widget 之外的子项私有资源"——有状态就在数据源里,没状态就只有 widget。

### 10.4 对账规则(最小的那一种)

1. 逐个 key 查现有子项;**命中** → **复用,不重建**;
2. **未命中** → `build` 建新子项;
3. 扫一遍现有子项,数据里没有的 key → `teardown`(可选)+ `lv_obj_delete`。

> **注意第 1 条**:命中是**复用,不是"更新数据与绑定"**。`build` **只跑一次** ——
> 若让它重跑,会再挂一套绑定(泄漏)。内容要变,就得是 item 里的 ref(§10.1)。

**顺序变化**(key 集合相同但次序不同):把已有对象挪到目标位置,**不重建**。
LVGL 有 `lv_obj_move_to_index(obj, index)` 与 `lv_obj_swap(a, b)`(已确认),够用;
子项里编辑到一半的状态因此不会丢。

### 10.5 两条契约 / 要求

**契约**:

> **`at(i)` 返回的指针,在子项存活期间必须稳定。**

否则复用的 widget 绑定会指向失效内存。数据源若会重建数组,须让 key 变化(或通知)
触发重建。

**设计要求**:

> **子项的数据结构里要含 ref**,否则内容变化反映不出来。

静态列表(菜单项)也照做 —— ref 不变就是了,不影响。


### 10.6 虚拟化:只物化可视窗口

长列表不能一项一个 widget —— 嵌入式上内存吃不消。**`item_height > 0` 即开启
虚拟化**(必须等高):

```
容器(可滚,layout = NONE)
  ├─ spacer        pos(0,0) size(1, count*item_height)   ← 撑出滚动范围
  └─ 子项 × k       pos(0, index*item_height) size(容器宽, item_height)
```

窗口 = `[scroll_top/item_height - 1, (scroll_top+view)/item_height + 1]`,上下各多探
一行。内存 **O(可视)** 而不是 O(总数)。

**对账仍然按 key**,只是"该显示哪些 index"由滚动窗口决定 —— 所以结构变化(增删
重排)天然复用 §10.4 的逻辑,没有第二套规则。

**滚动时在窗口边缘上 teardown + build** —— 也就是一个子项滚出视野就拆掉它、
滚进视野的重建。**不做 widget 复用**(recycling):复用要把绑定从旧 item 重新指到
新 item,那需要"绑定组"(一次创建、整组释放)这个新原语,是下一步。现在这版解决
的是内存这个主要矛盾,滚动的 CPU 代价是边界上的一两行。

`item_height == 0` 时行为与原先完全一致(全量建、靠 `lv_obj_move_to_index` 排序),
所以小列表不必为虚拟化付代价。

## 11. 跨线程桥

立项文档的口径:

    dbus recv(分发线程)→ 快照拷贝 → lv_async_call(或 eventfd+lv_timer 批量)
      → shrewd_lv_view_set → 反应图自动推 UI

由 C1,`ref_set` 会**在调用线程同步跑 effect**,effect 里是 `lv_*` 调用。所以:

**铁律:所有 `ref_set` 必须发生在 LVGL 线程。** 桥的全部职责就是把数据从别的线程送到
这个线程,没有任何魔法。立项文档的非目标里写了"不隐藏线程",这条就是它的落地形态——
调用方看得见自己在做跨线程投递。

桥的形状:

    数据侧(任意线程):   post()  快照入队
    UI 线程(app_step):  drain() 一次性出队全部 → 逐个 ref_set
                        → effect 同步推 UI,LVGL 自行合并重绘

> **实现修正(两条)**:
>
> 1. **唤醒机制用 `app_step` 轮询,不用 `lv_async_call`**。调用方本来就在主循环里
>    每 tick 调 `shrewd_lv_view_app_step()`,轮询天然存在;`lv_async_call` 只在
>    追求"入队即刻唤醒"的低延迟场景才需要,而那要多一层线程唤醒。轮询把批量
>    变成结构保证 —— 一次 drain 整队吃掉。
> 2. **快照不带序号/版本**。队列是 FIFO、drain 全量应用、`ref_set` 覆盖写,所以
>    不会丢也不会乱序;同 ref 连投两次是"后者胜",正是想要的语义。序号/版本是
>    给"可能丢包"的通道用的,这里不需要。

批量是关键:50 个字段变化不该唤醒 UI 线程 50 次。一次 drain 吃掉整队,由 C2/effect
去重,由 LVGL 合并重绘。

桥放在**本仓**(立项文档:"桥是唯一引用 LVGL API 的层,必须留在本仓库")。

## 12. 约定

- **命名**:`shrewd_lv_view_*` / `SHREWD_LV_VIEW_*`。讨论中的临时名不落代码。
- **内存**:`shrewd_basic_platform_*`,不碰 `malloc`。所有记录都有对应的 dispose。
- **错误(跨仓约定,D4 已决)**:返回 `int`,OK=0,负值失败,数据走出参。
  命名同构 `SHREWD_<REPO>_<MODULE>_<INFO>`。**这一段是 shrewd-\* 家族的号段登记表,
  新仓照抄即可**:

  | 仓 | 号段 | 说明 |
  |---|---|---|
  | shrewd-basic | `-1..-99` 通用,模块各占 `-N00..-N99` | log=-100、reflect=-200、json=-300、bin=-400、dbus=-500 …… 整套落在 `-1..-9999` |
  | shrewd-lvgl-vue | **`-0x6000..-0x6fff`** | 仓内再按模块分 `-0x6Mxx`:M=0 通用、1 导航,以后 2 绑定、3 列表、4 桥、5 壳 |
  | 其他 shrewd-\* | `-0xN000..-0xNfff` | 一个仓一个十六进制块,先到先得,登记进本表 |

  **两套方案天然不打架**:basic 是十进制 `-N00`,整套只到 `-9999`;下游用十六进制
  `-0xN000` 起,最小也是 `-4096` 以上。所以**不需要改动 shrewd-basic**,也就谈不上
  "让它保留号段";若日后想在它那边登记一份保留表,另议。

  > 原稿这里写的是「占 -600..-699」,已作废 —— 那正好是 shrewd-basic 第 6 个模块的
  > 块(-600..-699),会撞号。改用 `-0x6000..-0x6fff` 是为了躲开它的分配法。

  **三条规则**:

  1. **shrewd-basic 的失败原样返回,不跨层重编码。** 理由是不丢信息 ——
     `DBUS_CONFLICT` 被压成 `ARG_INVALID` 就是撒谎(参数明明没错)。号段不相交,
     所以原样返回时**来源一眼可辨**,连映射表都不用维护。
  2. **本仓的码只用于两件事**:本仓自己的检查(参数、状态),以及 **LVGL 的失败**
     —— LVGL 只返回 NULL/void、没有错误码,只能自己起名,`MEM_NOMEM` 就是给它留的。
  3. **`shrewd_lv_view_error_name()` 认本仓的码,其余回落
     `shrewd_basic_error_name()`**,不丢信息。每个仓都该有这样一个函数。

  `tests/test_errors.c` 钉住这三条:塞一个宽度 `SIZE_MAX` 的 `param_type` 让上游分配
  真失败,断言回来的是 `-1` 而不是 `-0x6002`;谁把翻译加回来,那条就红。
- **computed / watch**(D5 已决):**不主动重复暴露**。`view.h` 的 API 形参是
  `shrewd_basic_reflect_ref_t *`,它**必然引入 `react.h`** —— computed / watch 就在
  那个头里,随之可用。`view.h` 只管绑定与页面管理:API 面不膨胀,能力也不缺。
- **头文件**:消费者只 include `shrewd-lv-view/view.h`;绑定层是自包含私有实现。
  LVGL 与 shrewd-basic 的类型会出现在 API 形参里(§initiation.md 门面隔离:隔离的是
  实现,不是类型)。

## 13. 测试策略

立项文档要求"绑定层可独立单测"。三层:

| 层 | 手段 | 覆盖 |
|---|---|---|
| **机制单测** | 纯 C,不碰 LVGL | `applying` 抑制、对账算法、绑定记录的分配/释放 |
| **无头集成** | `tests/lvgl_fixture.{c,h}`:lv_init + display + 小 buffer + no-op flush + 假 tick(L6) | 绑定真跑,断言走 `lv_label_get_text` / `lv_slider_get_value` 读回 |
| **事件写回** | `lv_obj_send_event` 或假 indev 投事件 | v-model 回写与回路抑制 |

夹具要点:`lv_display_set_buffers` 用一块小缓冲即可(不需要全屏),`lv_display_set_flush_cb`
给个空实现,`lv_tick_set_cb` 给个单调递增的假时钟。**不用**模拟渲染结果,断言一律
查状态(LVGL 属性读回 + ref 槽位)。

## 14. 里程碑切分

**M2(下一段)**:机制 + text/range + v-model + 组件生命周期
1. `shrewd_lv_view_target_t` / `shrewd_lv_view_bind()` 底层机制,含 `applying` 与 DELETE 清理
2. label text / slider value / textarea text 三个便捷绑定
3. 无头夹具 + 机制单测
4. 组件模型(创建/销毁)与一个 demo 组件

**M3**:列表/条件渲染 + dbus 桥
5. `bind_list` 对账原语与条件渲染退化形态
6. 跨线程桥与 sb_dbus 合流
7. 真实页面原型倒推 API 手感(立项文档里程碑 3)

### 14.1 原型倒推:API 手感(里程碑 3 第 7 项)

`examples/prototype/` 是一个**真跑起来**的页面原型:数据模型(stores/)、组件
(device_card)、三个页面(home 列表 / detail v-model / settings 文本)、数据源线程、
跨线程桥、导航(push/pop)。200 tick 跑完、退出码 0。

写它的过程暴露了下面这些**摩擦点**。这是这一项的交付物 —— 不是代码。

#### 摩擦点(按影响排序)

| # | 摩擦 | 现状 | 建议 |
|---|---|---|---|
| **1** | **列表子项的组件实例没有归属** | §10.3 说"`item_ctx` 不需要",但**真组件有实例结构**,而子项是动态的、调用方无法预先持有。**已修**:加 `desc->instance_size`,框架分配、传给 `build`、销毁时释放(照 `lv_fragment` 的形态) | ✅ |
| **2** | **框架不提供 tick 源** | **已修**:`app_init` 检测 `lv_tick_get_cb()`,空则装默认时钟,可被覆盖 | ✅ |
| **3** | **display 必须有 flush 回调** | 没有它 `lv_timer_handler()` **转不出来**(实测挂死在第 0 个 tick)。手写 `main.c` 时最容易漏 | 脚手架已有;原型手写时踩了。文档记一笔 |
| **4** | **include 路径约定** | 一目录一组件 → `views/views.h`、`components/device_card/device_card.h`,手写容易忘 | 统一约定写进 §9.4 |

> **第 1、2 条已修**(测试 + 原型都验过):`test_list.c` 的 `test_instance_owned`
> 证明框架分配的实例会送到 `build`/`teardown`;原型删掉手写 tick 后仍跑通。

#### 顺手的地方(不用改)

- **组件形态**直白:状态 struct + 创建/销毁,一眼看懂;
- **绑定 API** 一行一个:`shrewd_lv_view_bind_label_text(label, &d->name)`;
- **导航 API** 清楚:`nav_push(&app.nav, &detail.base, &id)`;
- **桥** 的 `post` / `drain` 边界清楚,数据源线程里一眼看出该用哪个。

#### 一个跨线程的确认

数据源线程**只能 `post` 不能 `ref_set`** —— 原型里那行注释就是铁律的落地形态,
写起来不需要额外思考,这条设计是成立的。

## 15. 已决汇总与下一步

D1–D5 **已全部落定**,决策写在各自的小节(不在这里堆):

| | 决策 | 落在 | 依据 |
|---|---|---|---|
| **D1** | **作用域模型**:绑定随 widget 自清,组件 dispose 只删 root | §3 | Vue `effectScope` / React cleanup / Android LiveData 同构;只有 Flutter 偏手动,未采纳 |
| **D2** | **两层**:机制单点 + 便捷函数 | §6 | 无参照,口味;可退成只给便捷函数 |
| **D3** | **两种都支持**,`char[N]` 为 v-model 默认 | §5 | LVGL 自己就是「拷贝默认 + `_static` 借用」 |
| **D4** | 自建 `-0x6000..-0x6fff`,**shrewd-basic 的码原样返回** | §12 | 号段躲开 basic 的十进制 `-N00` 分配法;不跨层重编码,不丢信息 |
| **D5** | **不重复暴露**,随 `react.h` 自然可用 | §12 | `view.h` 必然引入 `react.h` |

**D1 的契约**(状态 struct 必须活得比作用域久)由四阶段收口:销毁钩子先删 widget
(级联解绑)、再释放状态,顺序写死,契约落在一处。页面管理的对应语义见
[architecture.md](architecture.md) §6.2。

**组件生态**(§7)也已全部落定:C1 组件形态(两种产出物)、C2 props(控件走
LVGL property / 片段走创建参数)、C3 事件、C4 slots(构建回调 + 命名槽)、
C5 组件级 v-model(传 ref 即双向)、C6 主题、C7 组合与作用域、C8 分发、
C9 facade(三档)。

**页面管理的 API 形状**(§8)也已落定:页面 = 片段 + 导航标识(同一套四阶段)、
描述符、**实例调用方持有**(architecture.md 6.3「状态不丢」唯一能兑现的形态)、
导航条目与操作集、守卫(全局一个、四种结果)、栈恢复。

**列表数据源**(§10)也已落定:`source` ref 槽位 = `shrewd_lv_view_list_t`,
**两种变化走两个机制**(内容变化走反应式 / 结构变化走对账),`build` 只跑一次。
子项实例由框架按 `desc->instance_size` 分配并释放(§14.1 摩擦点 1 已修)。

**构建配置落地**也已完成:`config/lv_conf.h`(最小配置,只写差异)+
`tools/build-deps.sh --lv-conf`,开 `LV_USE_PRIVATE_API` + `LV_USE_OBJ_PROPERTY`,
`LV_BUILD_SET_CONFIG_OPTS=ON`。已实测:配置随包安装、私有头随包安装、
自定义控件 + 类型化属性编译运行通过(`sizeof(lv_obj_class_t)=88`)。

**脚手架**也已落地并实测:`template/app/` + `tools/new-app.sh`。
生成的应用**真的编译、真的跑**(默认出可执行,`--thread` 出静态库)。

顺带把**最小 app 层**实现了(`shrewd_lv_view_app_t` + `init/use/mount/step`),
否则模板引用的 API 不存在、生成的项目编不过,"开箱即用"就落空。`app_mount`
现在只做 create + enter,**导航栈是 M2 的活**。

**壳**(architecture.md §6.9)也已实现:`shrewd_lv_view_shell_create` 建标题栏 +
内容区 + 底栏,`nav->host` 指到内容区;**标题栏显当前页名是 nav 状态的绑定**,
导航变了自动跟,不手动同步。

**`remove_until`** 也已实现(architecture.md §6.6「压栈并清掉中间若干层」,
对齐 Flutter `pushAndRemoveUntil`):把新页压成栈顶,再把它下面谓词不命中的层
逐个清掉,第一个命中的留下;`pred == NULL` 则锚点以下全清。语义取舍见 §8.4 的
实现修正。

**跨仓约定**(§12)也已落定:号段登记表(shrewd-basic 十进制 `-N00`,下游各仓
十六进制 `-0xN000`,两套天然不撞)、**shrewd-basic 的失败原样透传不重编码**、
`error_name` 认本仓的其余回落。原稿写的 `-600..-699` 作废 —— 那正好是 basic 第 6
个模块的块;改用 `-0x6000..-0x6fff`。**无需改动 shrewd-basic**。

**底栏 tab 高亮**也已实现(architecture.md §6.9「底栏高亮当前 tab,全是绑定」)。
两层:

- **机制** `bind_tab_bar`:bar 的**直接子项按分支顺序排列**,第 i 个在
  `current_branch == i` 时进 `LV_STATE_CHECKED` —— **样式交给主题**,框架只表达
  "谁是当前"(§7.6);
- **便捷** `shell_add_tab`:建按钮 + 点击切分支 + 高亮跟上,一行一个 tab。

**组件生态**(§7)也已落地,两种产出物都有样板:片段 `demo/card`(v-model + 槽 +
设计 token)、控件 `demo/gauge`(`lv_obj_class_t` + 属性表 + 自定义事件)。框架只多
了一个类型 `shrewd_lv_view_slot_fn` —— 其余全是约定和 LVGL 自己的机制。
**§7.3 的四个坑是写进树里才撞出来的**,已记在该节。

组件库也**真打成了独立 CMake 包**(§7.7「普通 C 库」):`examples/component-lib/`
的 **`demo-components`**,前缀 `demo_`,`find_package` 导入,原型就是这么消费的。
样板随之搬进包里(框架 `tests/` 只留机制单测),还多出**绑定胶水**
`demo_bind_gauge_value()` —— §6 的扩展规则从框架外面用得通。

**至此 §15 的清单清空。** 已落地:依赖与构建、绑定机制(含 v-model 与回路抑制)、
组件模型与组件生态(含独立包)、列表对账、跨线程桥 + sb_dbus 合流、导航栈
(四阶段/释放策略/守卫/`pop_until`/`remove_until`)、栈恢复、分支与模态、壳
(标题栏 + 底栏 tab)、脚手架、真实页面原型、跨仓约定。
**测试:框架 10 + 组件库 2,全绿。**

**性能/内存基线**([benchmarks.md](benchmarks.md))已建,`tests/bench.c` 是活的
测量。几条定论:

- **widget 不吃 libc 堆** —— 全在 LVGL 的池里,只看 `mallinfo` 会以为界面免费;
- **虚拟化省 12 倍**(100 项:494 → 39 字节/项);
- **列表重排几乎免费**(0.38 µs/100 项)—— 只挪不重建,这正是 §10.4 的设计意图;
- **`ref_set` 传播超线性**(1/10/100 个绑定 = 0.10/1.16/41.77 µs)—— 数据面拆细
  比优化绑定层管用;
- **导航栈条目 48 字节** —— 栈深上限 32 才 1.5KB,栈不占内存。

**顺带修了一个真 bug**(ASAN 实测):列表绑定的容器删除路径 use-after-free ——
容器的 DELETE 先释放绑定,子项的 DELETE 钩子还在读它。已修:释放前先摘钩子。

**那个崩溃已定位并堵住**(benchmarks.md §4):真因是 `config/lv_conf.h` 与已安装的
lvgl **不一致** —— 它是 lvgl 的 ABI 配置,两边结构体布局对不上就会静默写坏内存。
从零重建依赖后 8/8 不复现。`CMakeLists.txt` 加了 configure 期一致性门禁,
不一致直接 FATAL。**规矩:改 `config/lv_conf.h` 就要重跑 `tools/build-deps.sh`。**

**列表虚拟化**(§10.6)已补:`item_height > 0` 即只物化可视窗口,内存 O(可视) 而
不是 O(总数);对账仍按 key,没有第二套规则。**不做 widget 复用** —— 那需要
"绑定组"原语,是下一步。

**再往下是用,不是设计**:真实控件的绘制与主题接入、props 的类型校验、组件库的
多包协作(几个组件库互相用)—— 这些要有真实组件需求再动,免得凭空设计。
