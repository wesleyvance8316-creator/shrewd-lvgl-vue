# 使用教程

从零到一个能跑的应用。API 速查见 [api.md](api.md),设计取舍见 [design.md](design.md)。

> 读者假定会 C、会一点 LVGL。**不需要懂 Vue,也不需要懂浏览器内核** ——
> 下面每一步都直接给写法。

---

## 0. 先花 30 秒看懂它在干什么

写 LVGL 的日常是:改了数据 → 手动 `lv_label_set_text` → 忘了一处 → 界面和数据
对不上。这个框架把"手动同步"这件事干掉了:

```c
shrewd_basic_reflect_ref_t title;
char title_buf[32] = "hello";
shrewd_basic_reflect_ref_init(&title, title_buf, sizeof(title_buf));

shrewd_lv_view_bind_label_text(my_label, &title);   /* 绑一次 */

strncpy(title_buf, "world", sizeof(title_buf) - 1);
shrewd_basic_reflect_ref_set(&title, title_buf, sizeof(title_buf));
/* my_label 现在自己就是 "world" —— 你没写 set_text */
```

三件事:

1. **`ref` 是数据格** —— 一块槽位 + 一份"谁在看我"的记录。
2. **`bind_*` 是一条声明** —— "这个 widget 显示这个 ref"。之后不用管。
3. **写 ref 就是改数据** —— 自动传播到所有绑了它的地方。

还有一条铁律,别的都好说:**所有 `ref_set` 必须发生在跑 `lv_timer_handler()`
的那个线程**。别的线程要送数据进来,走桥(§6)。

---

## 1. 起步

```sh
# 1. 依赖:lvgl 9.6.0 + shrewd-basic,按 deps/versions.lock 钉住
tools/fetch-deps.sh && tools/build-deps.sh

# 2. 框架自己
cmake -S . -B build -DCMAKE_PREFIX_PATH=build/prefix
cmake --build build && ctest --test-dir build
cmake --install build --prefix build/prefix     # ← 把框架装进同一前缀
```

**最后一步不能漏** —— 消费者靠 `find_package(shrewd-lv-view CONFIG)` 找它。

要新建应用,用脚手架:

```sh
tools/new-app.sh my-app
```

它给的是一个开箱能跑的骨架(默认 main + 可切的线程形态)。下面从手写讲起,
这样每个 API 是干什么的都看得见。

---

## 2. 第一个页面

页面 = **状态 struct + 四阶段钩子**,状态调用方持有、永久。

```c
#include "shrewd-lv-view/view.h"

typedef struct {
    shrewd_lv_view_page_t base;      /* 必须是第一个成员 */
    lv_obj_t *label;
} home_page_t;

static int home_create(shrewd_lv_view_page_t *p, lv_obj_t *container, const void *params) {
    (void)params;
    home_page_t *h = (home_page_t *)p;
    p->root = lv_obj_create(container);
    h->label = lv_label_create(p->root);
    lv_label_set_text(h->label, "hello");
    return SHREWD_LV_VIEW_ERR_OK;
}

static const shrewd_lv_view_page_desc_t home_desc = {
    .name = "home",                          /* 恢复时按它找回实例 */
    .release = SHREWD_LV_VIEW_RELEASE_DESTROY_ON_EXIT,
    .create = home_create,
    /* enter / exit / destroy 都可选 */
};
```

**四阶段**:创建(建树)→ 激活(进来了)→ 失活(被盖住/退出)→ 销毁(拆树)。
顺序确定、一次不多一次不少。**销毁只删 widget 树**,状态不丢 —— 这就是
"状态与视图分离",所以敢释放。

`release` 决定 widget 树什么时候拆:`RESIDENT`(常驻)/ `DESTROY_ON_EXIT`(失活
即拆,默认)/ `CACHE`(留 N 个)。**它管的是 widget,不是实例**。

---

## 3. 让数据驱动 UI

状态放在页面自己的 struct 里,**ref 指向同 struct 的槽位**:

```c
typedef struct {
    shrewd_lv_view_page_t base;
    shrewd_basic_reflect_ref_t title;
    char title_buf[32];
    shrewd_basic_reflect_ref_t level;
    int32_t level_val;
    lv_obj_t *label, *slider;
} home_page_t;
```

`create` 里建树 + 逐个绑定:

```c
shrewd_basic_reflect_ref_init(&h->title, h->title_buf, sizeof(h->title_buf));
shrewd_basic_reflect_ref_init(&h->level, &h->level_val, sizeof(h->level_val));

h->label  = lv_label_create(p->root);
h->slider = lv_slider_create(p->root);

shrewd_lv_view_bind_label_text(h->label, &h->title);
shrewd_lv_view_bind_slider_value(h->slider, &h->level);
```

之后**只改数据**:

```c
h->level_val = 42;
shrewd_basic_reflect_ref_set(&h->level, &h->level_val, sizeof(h->level_val));
/* slider 自己变 42 */
```

**销毁时不要手动解绑** —— 绑定挂在 widget 上,`lv_obj_delete(root)` 时级联自清。
这是"widget 即作用域"。

> 参数(`params`)走 reflect 描述、只放简单值(可序列化);复杂数据一律走状态系统。

---

## 4. 双向:v-model

给组件传 `ref`,它内部绑上,就自动双向:

```c
typedef struct {
    const char *title;
    int32_t level;
    shrewd_basic_reflect_ref_t *level_ref;   /* ← 传 ref 进来 */
} card_params_t;
```

父**既不写"更新"也不听"事件"**。比 Vue 的 `:modelValue` + `@update:modelValue`
省一层。

**为什么不成环**:程序化的 `lv_*_set_value()` 不发 `VALUE_CHANGED`,写回只由
**用户交互**驱动的事件触发。所以"ref → widget"和"widget → ref"天然分得开。

---

## 5. 列表

**内容变化走反应式,结构变化走对账** —— 两个机制,别混。

```c
static const void *at(void *ctx, uint32_t i) { return &devices[i]; }
static uint32_t  key(void *ctx, uint32_t i)  { return devices[i].id; }

static lv_obj_t *build(void *ctx, lv_obj_t *container, const void *item, void *instance) {
    device_row_t *row = (device_row_t *)instance;
    row->root = lv_obj_create(container);
    row->name = lv_label_create(row->root);
    /* 绑 item 里的 ref —— 内容变了自动跟 */
    shrewd_lv_view_bind_label_text(row->name, &((const device_t *)item)->name_ref);
    return row->root;
}

static const shrewd_lv_view_list_desc_t desc = {
    .at = at,
    .key = key,
    .item_size = sizeof(device_t),
    .instance_size = sizeof(device_row_t),   /* 0 = 不要实例 */
    .build = build,
    .teardown = NULL,
};

shrewd_lv_view_list_t slot = { .items = devices, .count = n };
shrewd_basic_reflect_ref_init(&slot_ref, &slot, sizeof(slot));
shrewd_lv_view_bind_list(container, &slot_ref, &desc, NULL);

/* 增删改结构:改 slot 再 ref_set,对账自动做增删复用 */
```

三条要点:

- **`build` 只跑一次** —— 再跑会挂第二套绑定(泄漏)。内容要变就用 item 里的 ref。
- **`key` 必须稳定** —— 对账靠它。顺序变化只挪不重建。
- **子项实例由框架分配**(声明 `instance_size`),你不用碰 `calloc`/`free`。

key 集合只有 0 或 1 个时,它自动退化成**条件渲染**。

### 列表很长?开虚拟化

```c
static const shrewd_lv_view_list_desc_t desc = {
    /* …同上… */
    .item_height = 48,      /* > 0 即虚拟化,行高必须等高 */
};
```

1000 项也只物化**可视窗口**那几个 widget,内存 O(可视) 而不是 O(总数)。
滚动时窗口边缘上 teardown + build。框架把控件摆到 `(容器宽, item_height)`,
并在容器里放一根撑高度的 spacer —— **容器的 layout 会被设成 NONE**,别在它上面
再套 flex。

> 一个小坑:`lv_obj_scroll_by(obj, 0, +dy)` 是**把内容往下推**(往回滚),
> 想往下翻内容要传负值,而且要用 `lv_obj_scroll_by_bounded` 才会被滚动范围夹住。

---

## 6. 数据从别的线程来

铁律:所有 `ref_set` 都得在 UI 线程。数据侧只管 `post`:

```c
shrewd_lv_view_bridge_t bridge;
shrewd_lv_view_bridge_init(&bridge);

/* 数据线程 / sb_dbus 的分发线程: */
int32_t v = read_sensor();
shrewd_lv_view_bridge_post(&bridge, &battery_ref, &v, sizeof(v));   /* 只入队 */

/* UI 线程主循环里: */
shrewd_lv_view_app_step(&app);   /* 内部先 drain 再 lv_timer_handler */
```

`post` 只入队、`drain` 一次性吃掉整队 —— **50 个字段变化不会唤醒 UI 线程 50 次**。

用 sb_dbus 的话一行接上:

```c
shrewd_lv_view_dbus_bind_t bind;
shrewd_basic_dbus_handle_t *h;
shrewd_lv_view_bridge_bind_dbus(&bridge, bus, "/device/battery", &battery_ref, &bind, &h);
```

---

## 7. 页面跳转

### 7.1 栈

```c
shrewd_lv_view_nav_push(&nav, &detail.base, &device_id);   /* 前进 */
shrewd_lv_view_nav_pop(&nav);                              /* 返回一层 */
```

**栈条目是"描述"不是"对象"**,所以**栈深 ≠ 实例数**。递归天然支持:

```
a.仓库页 → b.货架页 → c.货品(详情里有仓库链接)→ a.仓库页
```

第二次到 `a` 是**前进**,不是返回。框架**不自动去重** —— "前进"还是"返回"由操作
本身表达:点链接是 `push`,按返回键是 `pop`,点面包屑是 `pop_until`。

### 7.2 跨级返回

```c
static int is_home(void *ctx, const shrewd_lv_view_page_t *p) {
    return p == (const shrewd_lv_view_page_t *)ctx;
}
shrewd_lv_view_nav_pop_until(&nav, is_home, &home.base);      /* 回到 home,留下它 */
shrewd_lv_view_nav_remove_until(&nav, &done.base, NULL, is_home, &home.base);
/*   [home, w1, w2, w3]  →  [home, done]   压栈并清掉中间若干层 */
```

**按谓词不按索引** —— 递归场景下"回到哪个 `a`"的歧义自然消解为"取最近的那个"。
`remove_until` 的 `pred` 传 `NULL` = 锚点以下全清(登录/向导结束直达 home 就靠它)。

### 7.3 tab 与模态

```c
shrewd_lv_view_nav_switch_branch(&nav, 1);     /* 换分支,不产生历史 */
shrewd_lv_view_nav_modal_push(&nav, &dialog.base, NULL);   /* 模态,独立一层 */
```

分支各持独立栈;模态叠在 LVGL top layer 上,关完最后一层即回到分支。
**模态压着时不能 `switch_branch`**。

### 7.4 守卫

```c
static shrewd_lv_view_guard_result_t my_guard(void *ctx, shrewd_lv_view_nav_request_t *req) {
    if (dirty && (0 == strcmp("home", req->to->name))) {
        return SHREWD_LV_VIEW_GUARD_CANCEL;      /* 还没保存,不让走 */
    }
    return SHREWD_LV_VIEW_GUARD_ALLOW;
}
shrewd_lv_view_nav_set_guard(&nav, my_guard, NULL);
```

四种结果:`ALLOW` / `CANCEL` / `REDIRECT`(改道)/ `CONSUME`(吃掉请求,不真走 ——
"再按一次退出"就是它)。

### 7.5 断电恢复

GUI 是常驻进程,**没有"重开"这个逃生口**,所以栈要能存能取:

```c
shrewd_lv_view_nav_register(&nav, &home.base);   /* 要恢复的页面先登记 */
size_t n;
shrewd_lv_view_nav_save_size(&nav, &n);
shrewd_lv_view_nav_save(&nav, buf, n);           /* 存到哪归你:flash/文件/shm */
/* …断电… */
shrewd_lv_view_nav_restore(&nav, buf, n);
```

快照里只有 `name` + 参数,所以**必须先登记**才能找回实例。

---

## 8. 壳:标题栏与 tab

```c
shrewd_lv_view_shell_t shell;
shrewd_lv_view_shell_create(&shell, display, &nav, 32, 24);   /* 24 = 底栏高,0 = 不要 */
shrewd_lv_view_shell_add_tab(&shell, "设备");
shrewd_lv_view_shell_add_tab(&shell, "设置");
```

标题栏自动显当前页名、底栏自动高亮当前 tab —— **都是导航状态的绑定,不是手动
同步**。页面渲染在中间的内容区(`nav->host` 已指过去)。

---

## 9. 写组件

### 9.1 片段:可复用的 UI 部分

```c
typedef struct {
    lv_obj_t *root;
    shrewd_basic_reflect_ref_t level;
    int32_t level_val;
} my_card_t;

int  my_card_create(my_card_t *c, lv_obj_t *parent, const my_card_params_t *p);
void my_card_dispose(my_card_t *c);      /* 就一行 lv_obj_delete(c->root) */
```

**销毁函数不要手动 dispose 绑定** —— 那是机制的活。可以再加:

- **槽**:`shrewd_lv_view_slot_fn`,父给内容、子决定何时何处调用一次;
- **设计 token**:一张可覆盖的 style 常量表,创建时套用(不自造主题引擎)。

### 9.2 控件:新的 `lv_obj` 类型

```c
typedef struct { lv_obj_t obj; int32_t value; /* … */ } my_gauge_t;
extern const lv_obj_class_t my_gauge_class;
```

要引 **lvgl 的私有头**(facade 第三档)。四个坑记在 `docs/design.md` §7.3,其中
最容易踩的是:**`lv_obj_class_create_obj()` 只分配,构造在 `lv_obj_class_init_obj()`**
—— 两步都要。

### 9.3 打成包

组件库就是**普通 C 库**:`find_package` 导入,**前缀用自己的,不占用 `shrewd_`**。
完整样板见 `examples/component-lib/`(`demo-components`)—— 含控件、片段、
以及**绑定胶水**(`demo_bind_gauge_value`,§6 的扩展规则从框架外面用得通)。

---

## 10. 常见坑

| 现象 | 原因 |
|---|---|
| 一建 display 就崩在 `lv_display_create` | **`lv_init()` 还没调**。框架的 `app_init` 不替你调(和原型一样,起 LVGL 是应用的事),顺序是 `lv_init()` → 建 display → `app_init` |
| 界面完全不动、卡在第一个 tick | **没有 tick 源**。`app_init` 会装默认的,自己给要在它之前 `lv_tick_set_cb()` |
| `lv_timer_handler()` 一直不返回 | display **没有 flush 回调**。即使无头测试也要给个空实现 |
| 列表 `build` 跑了两次 | 它**只该跑一次**。内容变化要用 item 里的 ref,不要改 `build` |
| 改了数据界面没变 | 忘了 `ref_set` —— 直接写槽位不会通知 |
| 别的线程改数据崩了 | 违反铁律。走桥 `post`,UI 线程 `drain` |
| 销毁页面后崩 | 多半是手动解绑了 —— 绑定随 widget 自清,别碰 |
| `push` 报 `NAV_STACK_FULL` | 栈深超上限(默认 32)。这是**安全底线**,GUI 是常驻进程没有逃生口 |

---

### 可交互的样本:`examples/demo/`

上面每一条在一个 SDL 窗口里都能点到底:底栏切 tab、列表点一行进详情、
滑块改了再按"返回"会被**守卫拦一次**、电量由后台线程持续推上来。

```sh
cmake -S examples/demo -B examples/demo/build -DCMAKE_PREFIX_PATH=<prefix>
cmake --build examples/demo/build && ./examples/demo/build/demo
SDL_VIDEODRIVER=dummy ./examples/demo/build/demo --ticks 120   # 无头跑一截
```

**框架不碰 SDL** —— 窗口、像素搬运、事件读取都在 `src/platform/sdl_port.c`。
那正是接自己的屏和触摸时要写的那层,所以它留在 demo 里当样本,不进框架;
换到板子上就是把 SDL 换成 fbdev + evdev,外面的应用代码一行不动。

## 11. 构建与包

```cmake
find_package(shrewd-lv-view CONFIG REQUIRED)
target_link_libraries(app PRIVATE shrewd-lv-view::shrewd-lv-view)
```

一个 `CMAKE_PREFIX_PATH` 解掉整套(lvgl / shrewd-basic / 框架 / 组件库都装在
同一个前缀)。**外部库一律 `find_package`,不 `add_subdirectory`。**

自检:`tools/check-style.sh`(clang-format / clang-tidy / 条件括号),
`ctest --test-dir build`。
