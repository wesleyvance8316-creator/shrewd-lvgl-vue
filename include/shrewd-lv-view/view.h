#ifndef SHREWD_LV_VIEW_VIEW_H
#define SHREWD_LV_VIEW_VIEW_H

/**
 * @file view.h
 * @brief The facade of shrewd-lv-view: the ONLY header consumers
 *        include. Vue-style reactive primitives, widget bindings and
 *        the bus-to-UI bridge all surface through this header; the
 *        implementation behind it is a private detail and may move
 *        between repositories without touching consumer code
 *        (docs/initiation.md).
 *
 * Naming law: every public identifier is shrewd_lv_view_* (macros
 * SHREWD_LV_VIEW_*), like shrewd-basic's shrewd_basic_*.
 */

#include <stddef.h>
#include <stdint.h>

#include "lvgl/lvgl.h"
#include "shrewd-basic/reflect/react.h"
#include "shrewd-basic/reflect/reflect.h"

/** Library version string (SemVer; 0.x = API free to change). */
#define SHREWD_LV_VIEW_VERSION_STRING "0.1.0"

/** Return the library version string. */
const char *shrewd_lv_view_version(void);

/*
 * ==========================================================================
 * 错误码
 * ==========================================================================
 *
 * 家法:返回 int,SHREWD_LV_VIEW_ERR_OK (=0) 成功、负值失败,数据走出参。
 *
 * **跨仓约定**(docs/design.md §12):
 *   - shrewd-basic 的失败**原样返回,不跨层重编码** —— 不丢信息,号段不相交
 *     所以来源一眼可辨;
 *   - 本仓的码只用于**本仓自己的检查**和 **LVGL 的失败**(LVGL 只返回 NULL/void,
 *     没有错误码,只能自己起名);
 *   - `shrewd_lv_view_error_name()` 认本仓的码,其余回落 `shrewd_basic_error_name()`。
 *
 * 号段 -0x6000..-0x6fff 是 lv_view 自己的,仓内再按 `-0x6Mxx` 分模块(M=0 通用、
 * 1 导航)。它不占用 shrewd-basic 的域分配法(那套按十进制 `-N00..-N99` 分模块,
 * 整套只到 -9999),所以**不需要改动 shrewd-basic**。
 */
typedef enum shrewd_lv_view_error {
    SHREWD_LV_VIEW_ERR_OK = 0,
    /* 通用:-0x60xx */
    SHREWD_LV_VIEW_ERR_ARG_INVALID = -0x6001,
    SHREWD_LV_VIEW_ERR_MEM_NOMEM = -0x6002,
    SHREWD_LV_VIEW_ERR_NOT_FOUND = -0x6003,
    SHREWD_LV_VIEW_ERR_FULL = -0x6004,
    SHREWD_LV_VIEW_ERR_NOT_MOUNTED = -0x6005,
    /* 导航:见 docs/architecture.md §6 */
    SHREWD_LV_VIEW_ERR_NAV_STACK_FULL = -0x6100,
    SHREWD_LV_VIEW_ERR_NAV_CANCELLED = -0x6101,
    SHREWD_LV_VIEW_ERR_NAV_EMPTY = -0x6102
} shrewd_lv_view_error_t;

/** Return the name of an error code; unknown codes fall back to shrewd-basic. */
const char *shrewd_lv_view_error_name(int code);

/*
 * ==========================================================================
 * 应用装配
 * ==========================================================================
 *
 * 框架**不提供 main、也不持有主循环**(docs/design.md §9.2)——那正是
 * 「不隐藏线程」的落法。调用方决定在哪跑:独立进程的 main,或业务主进程
 * 里的一个线程。框架只给单步函数。
 */

struct shrewd_lv_view_app; /* 前置声明:否则下面参数表里的 struct 声明是局部的 */
typedef struct shrewd_lv_view_bridge shrewd_lv_view_bridge_t; /* 前置:app 要持有它 */

/** A component library's registration entry (docs/design.md §7.7). */
typedef struct shrewd_lv_view_plugin {
    const char *name;
    int (*init)(void *ctx, struct shrewd_lv_view_app *app);
    void *ctx;
} shrewd_lv_view_plugin_t;

typedef struct shrewd_lv_view_page_desc shrewd_lv_view_page_desc_t;

/** A page instance. Caller-held and permanent (docs/design.md §8.3). */
typedef struct shrewd_lv_view_page {
    const shrewd_lv_view_page_desc_t *desc;
    lv_obj_t *root; /**< widget tree; created/destroyed per release policy */
} shrewd_lv_view_page_t;

/**
 * Page descriptor. A page is a fragment plus a navigation identity
 * (docs/design.md §8.1): the four-stage hooks are the same set that a
 * plain fragment uses.
 */
/**
 * 释放策略(architecture.md §6.5):管的是 **widget 树**,不是实例
 * (实例调用方持有、永久,design.md §8.3)。
 */
typedef enum shrewd_lv_view_release {
    SHREWD_LV_VIEW_RELEASE_RESIDENT = 0,    /**< 建一次不销毁 */
    SHREWD_LV_VIEW_RELEASE_DESTROY_ON_EXIT, /**< 失活即销毁(默认) */
    SHREWD_LV_VIEW_RELEASE_CACHE            /**< 缓存 cache_count 个 */
} shrewd_lv_view_release_t;

struct shrewd_lv_view_page_desc {
    const char *name; /**< identity; serialized on restore, so a string */
    const shrewd_basic_reflect_type_t *param_type; /**< params 的 reflect 描述 */
    shrewd_lv_view_release_t release;
    size_t cache_count; /**< 仅 RELEASE_CACHE 用 */

    /** Create the widget tree and bind. @p params is the stack entry's copy. */
    int (*create)(shrewd_lv_view_page_t *p, lv_obj_t *container, const void *params);
    /** Enter: subscribe / load. */
    int (*enter)(shrewd_lv_view_page_t *p);
    /** Leave: unsubscribe / save. */
    void (*exit)(shrewd_lv_view_page_t *p);
    /** Destroy the widget tree. Bindings clean themselves (docs/design.md §3). */
    void (*destroy)(shrewd_lv_view_page_t *p);
};

/* --- 导航(architecture.md §6.6–6.7 / design.md §8.4–8.5)-------------- */

/** 守卫结果。 */
typedef enum shrewd_lv_view_guard_result {
    SHREWD_LV_VIEW_GUARD_ALLOW = 0,
    SHREWD_LV_VIEW_GUARD_CANCEL,
    SHREWD_LV_VIEW_GUARD_REDIRECT,
    SHREWD_LV_VIEW_GUARD_CONSUME /**< 请求被吃掉,不真走("再按一次退出") */
} shrewd_lv_view_guard_result_t;

/** 一次导航请求:守卫看 to/from,填 redirect 即改道。 */
typedef struct shrewd_lv_view_nav_request {
    const shrewd_lv_view_page_desc_t *to;
    const shrewd_lv_view_page_desc_t *from;
    shrewd_lv_view_page_t *redirect; /**< 守卫填它即改道 */
} shrewd_lv_view_nav_request_t;

typedef shrewd_lv_view_guard_result_t (*shrewd_lv_view_guard_fn)(void *ctx,
                                                                 shrewd_lv_view_nav_request_t *req);

/** 谓词:从栈顶往下找第一个满足的(architecture.md §6.6)。 */
typedef int (*shrewd_lv_view_nav_pred_fn)(void *ctx, const shrewd_lv_view_page_t *page);

/** 栈条目:描述,不持 widget(architecture.md §6.6)。 */
typedef struct shrewd_lv_view_nav_entry {
    shrewd_lv_view_page_t *page;
    void *params; /**< 副本,可序列化(§6.11 Q2) */
    size_t params_size;
    uint32_t seq; /**< 区分同页面多次出现(§6.11 Q4) */
    uint64_t entered_at;
    int built; /**< widget 树是否已建 */
} shrewd_lv_view_nav_entry_t;

/** 一个栈:分支或模态各一份。 */
typedef struct shrewd_lv_view_nav_stack {
    shrewd_lv_view_nav_entry_t *entries;
    size_t count;
    size_t capacity;
} shrewd_lv_view_nav_stack_t;

/** 默认栈深上限:安全底线,不是语义(architecture.md §6.6)。 */
#define SHREWD_LV_VIEW_NAV_DEFAULT_LIMIT 32

/** 分支数上限(tab 数)。 */
#define SHREWD_LV_VIEW_NAV_BRANCHES 8

/**
 * 导航:**分层的栈**(architecture.md §6.9)。
 *
 *   - **分支**:tab 各持独立栈,`switch_branch` 换分支、不产生历史;
 *   - **模态**:独立的一层,叠在 LVGL top layer 上。
 *
 * 操作集只有一套,作用在**当前栈**上:模态栈非空时是它,否则是当前分支。
 * 这样模态天然叠在最上、还能嵌套。
 */
typedef struct shrewd_lv_view_nav {
    lv_obj_t *host;       /**< 分支页的容器(壳的内容区) */
    lv_obj_t *modal_host; /**< 模态页的容器(LVGL top layer) */
    size_t limit;
    uint32_t next_seq;
    shrewd_lv_view_guard_fn guard;
    void *guard_ctx;
    /* 页面登记表:name -> 实例。**只服务于恢复**(§8.6 按 name 找回实例),
       所以要恢复的页面必须先登记;push 会顺带登记。 */
    shrewd_lv_view_page_t **registry;
    size_t registry_count;
    size_t registry_capacity;
    shrewd_lv_view_nav_stack_t branches[SHREWD_LV_VIEW_NAV_BRANCHES];
    size_t branch_count;
    size_t current;
    shrewd_lv_view_nav_stack_t modal;

    /**
     * **可绑定的导航状态**(architecture.md §6.4):壳的 UI 是这些 ref 的绑定 ——
     * 标题栏显当前页名、底栏高亮当前 tab,不用手动同步。框架在导航时更新它们。
     */
    shrewd_basic_reflect_ref_t current_name;
    char current_name_buf[64];
    shrewd_basic_reflect_ref_t current_branch;
    uint32_t current_branch_val;
} shrewd_lv_view_nav_t;

/**
 * 初始化导航。@p host 是分支页的容器,@p modal_host 是模态页的容器
 * (通常 `lv_display_get_layer_top()`)。@p limit 0 表示用默认上限。
 */
int shrewd_lv_view_nav_init(shrewd_lv_view_nav_t *nav,
                            lv_obj_t *host,
                            lv_obj_t *modal_host,
                            size_t limit);

/** 释放导航(含未销毁的 widget 树与参数副本)。 */
void shrewd_lv_view_nav_dispose(shrewd_lv_view_nav_t *nav);

void shrewd_lv_view_nav_set_guard(shrewd_lv_view_nav_t *nav,
                                  shrewd_lv_view_guard_fn guard,
                                  void *ctx);

/**
 * 登记页面,让 `name -> 实例` 可查。**恢复要用** —— 快照里只有 name,
 * 必须能找回实例。push 会顺带登记;启动时把所有可恢复的页面登记一次即可。
 */
int shrewd_lv_view_nav_register(shrewd_lv_view_nav_t *nav, shrewd_lv_view_page_t *page);

/**
 * 切换当前分支(不产生历史,architecture.md §6.9)。
 * @p branch 从 0 起;分支会按需登记。
 */
int shrewd_lv_view_nav_switch_branch(shrewd_lv_view_nav_t *nav, size_t branch);

/** 前进。@p params 拷进栈条目。作用在当前栈。 */
int shrewd_lv_view_nav_push(shrewd_lv_view_nav_t *nav,
                            shrewd_lv_view_page_t *page,
                            const void *params);

/**
 * 打开模态:压进**模态栈**。模态栈初始为空,push 进不去它,所以"打开模态"
 * 是一个独立操作(architecture.md §6.9)。此后 push/pop 都作用在模态栈上
 * (可嵌套);关完最后一层即回到分支。
 */
int shrewd_lv_view_nav_modal_push(shrewd_lv_view_nav_t *nav,
                                  shrewd_lv_view_page_t *page,
                                  const void *params);

/** 返回一层。作用在当前栈。 */
int shrewd_lv_view_nav_pop(shrewd_lv_view_nav_t *nav);

/** 按谓词连续 pop,中间逐个 exit + 按策略销毁。 */
int shrewd_lv_view_nav_pop_until(shrewd_lv_view_nav_t *nav,
                                 shrewd_lv_view_nav_pred_fn pred,
                                 void *ctx);

/**
 * 压栈并清掉中间若干层(architecture.md §6.6,对齐 Flutter `pushAndRemoveUntil`)。
 *
 * 把 @p page 压成新栈顶(**产生历史**),再从它下面一层往下,谓词**不命中**的
 * 逐个清掉,第一个**命中**的留下(连同它以下全部)。
 *
 * @code
 *   [home, w1, w2, w3] -- remove_until(&done, NULL, is_home, ...) --> [home, done]
 * @endcode
 *
 * 典型:登录 / 向导结束直达 home,流程页一层不留 —— home 不在栈里时
 * `pop_until` 做不到,这个能。
 *
 * @param pred  NULL 表示恒不命中:锚点以下全清,栈里只剩 @p page。
 */
int shrewd_lv_view_nav_remove_until(shrewd_lv_view_nav_t *nav,
                                    shrewd_lv_view_page_t *page,
                                    const void *params,
                                    shrewd_lv_view_nav_pred_fn pred,
                                    void *ctx);

/** 同级跳转:不产生历史。作用在当前栈。 */
int shrewd_lv_view_nav_replace(shrewd_lv_view_nav_t *nav,
                               shrewd_lv_view_page_t *page,
                               const void *params);

/** 当前栈的栈顶;空栈返回 NULL。 */
shrewd_lv_view_page_t *shrewd_lv_view_nav_top(shrewd_lv_view_nav_t *nav);

/** 当前栈的深度。 */
size_t shrewd_lv_view_nav_depth(const shrewd_lv_view_nav_t *nav);

/** 当前栈是否是模态栈。 */
int shrewd_lv_view_nav_in_modal(const shrewd_lv_view_nav_t *nav);

/**
 * 登记页面,让 `name -> 实例` 可查。**恢复要用** —— 快照里只有 name,
 * 必须能找回实例。push 会顺带登记;启动时把所有可恢复的页面登记一次即可。
 */
int shrewd_lv_view_nav_register(shrewd_lv_view_nav_t *nav, shrewd_lv_view_page_t *page);

/**
 * 栈快照(§8.6):把所有条目的 `name + params` 交给调用方。
 * **「存到哪」归使用者**(flash / 文件 / shm),框架只产出有效快照。
 *
 * 用法:先 `nav_save_size` 拿长度,再 `nav_save` 写进自己准备的缓冲。
 */
int shrewd_lv_view_nav_save_size(const shrewd_lv_view_nav_t *nav, size_t *out_size);

/** 把快照写进 @p buf(@p size 由 nav_save_size 给出)。 */
int shrewd_lv_view_nav_save(const shrewd_lv_view_nav_t *nav, void *buf, size_t size);

/**
 * 从快照重建栈:靠 `name` 在登记表里找回实例,按序重建条目,**只激活栈顶**
 * (其余条目是描述,等回退到它们时才建 widget —— 与释放策略一致)。
 * 当前栈会被先清空(逐个 exit + 按策略销毁)。
 */
int shrewd_lv_view_nav_restore(shrewd_lv_view_nav_t *nav, const void *buf, size_t size);

#define SHREWD_LV_VIEW_MAX_PLUGINS 8

/** Application: page host + configuration + plugins. */
typedef struct shrewd_lv_view_app {
    lv_display_t *display;
    lv_obj_t *host;                  /**< page container (M2: shell content area) */
    shrewd_lv_view_nav_t nav;        /**< 导航栈 */
    shrewd_lv_view_bridge_t *bridge; /**< 可选:每 tick drain 它 */
    const shrewd_lv_view_plugin_t *plugins[SHREWD_LV_VIEW_MAX_PLUGINS];
    size_t plugin_count;
} shrewd_lv_view_app_t;

/** Bind the app to a display. Creates the page host on the active screen. */
int shrewd_lv_view_app_init(shrewd_lv_view_app_t *app, lv_display_t *display);

/** Register a component library; its init runs immediately. */
int shrewd_lv_view_app_use(shrewd_lv_view_app_t *app, const shrewd_lv_view_plugin_t *plugin);

/**
 * Mount a page: run its create() then enter(). The container is the app's
 * host (M2: the shell's content area).
 */
int shrewd_lv_view_app_mount(shrewd_lv_view_app_t *app,
                             shrewd_lv_view_page_t *page,
                             const void *params);

/**
 * One step of the UI thread's loop: drain cross-thread deliveries, then
 * drive lv_timer_handler(). Call it from wherever the loop lives.
 */
int shrewd_lv_view_app_step(shrewd_lv_view_app_t *app);

/** 给 app_step 一个桥:此后每 tick 都会 drain 它。 */
void shrewd_lv_view_app_set_bridge(shrewd_lv_view_app_t *app, shrewd_lv_view_bridge_t *bridge);

/*
 * ==========================================================================
 * 壳(architecture.md §6.9)
 * ==========================================================================
 *
 * 标题栏 + 底栏是**持久层**:不进页面栈、一直活着。页面渲染在中间的内容区。
 * 壳的 UI 是**导航状态的绑定** —— 标题栏显当前页名、底栏高亮当前 tab,全是绑定。
 */

typedef struct shrewd_lv_view_shell {
    lv_obj_t *root;
    lv_obj_t *header;
    lv_obj_t *title;   /**< 标题栏的文本,绑 nav->current_name */
    lv_obj_t *content; /**< 页面容器 = nav->host */
    lv_obj_t *footer;
    shrewd_lv_view_nav_t *nav; /**< 壳的 UI 是它的绑定(§6.9) */
    uint32_t tab_count;        /**< 已加的 tab 数,即下一个 tab 的分支号 */
} shrewd_lv_view_shell_t;

/**
 * 建壳并接上导航:@p nav 的 host 指到壳的内容区,标题绑上当前页名。
 * @p footer_height 0 表示不要底栏。
 */
int shrewd_lv_view_shell_create(shrewd_lv_view_shell_t *shell,
                                lv_display_t *display,
                                shrewd_lv_view_nav_t *nav,
                                int32_t header_height,
                                int32_t footer_height);

/**
 * 往底栏加一个 tab:建按钮、点它切分支、高亮跟 `nav->current_branch`。
 *
 * 第 n 次调用对应**分支 n**。高亮是一个绑定(`bind_tab_bar`),不是手动同步。
 * 需要建壳时给了底栏(`footer_height > 0`),且 @p shell 活得比它的 widget 久。
 */
int shrewd_lv_view_shell_add_tab(shrewd_lv_view_shell_t *shell, const char *label);

/*
 * ==========================================================================
 * 绑定:把 ref 接到 widget 上
 * ==========================================================================
 *
 * 一条绑定就是一个 effect(docs/design.md §2):ref 变了自动写 widget,
 * 反之(双向)widget 变了自动写回 ref。绑定随 widget 删除自清(design.md §3),
 * 使用者不记不解绑。
 */

/**
 * 属性适配器:描述"怎么写进 widget"与"怎么从 widget 读回"。
 * `read` 为 NULL 即单向;非 NULL 即双向,并在 `changed_event` 上写回。
 */
typedef struct shrewd_lv_view_target {
    /** ref → widget。@p value 指向 ref 的槽位,@p size 是其宽度。 */
    void (*write)(void *obj, const void *value, size_t size);
    /** widget → ref;返回 shrewd_lv_view_* 错误码。NULL = 单向。 */
    int (*read)(void *obj, void *out, size_t size);
    /** 写回触发的 LVGL 事件码(read 为 NULL 时忽略)。 */
    uint32_t changed_event;
} shrewd_lv_view_target_t;

/**
 * 建一条绑定。底层机制单点:applying 抑制、LV_EVENT_DELETE 清理都在这里。
 * 上层的便捷函数只是薄壳。
 */
int shrewd_lv_view_bind(void *obj,
                        const shrewd_lv_view_target_t *target,
                        shrewd_basic_reflect_ref_t *ref);

/* --- 便捷绑定(design.md §6)------------------------------------------ */

/** label 的文本:单向。ref 槽位是 `char[N]`(design.md §5)。 */
int shrewd_lv_view_bind_label_text(lv_obj_t *obj, shrewd_basic_reflect_ref_t *ref);

/** slider 的值:双向。ref 槽位是 `int32_t`。 */
int shrewd_lv_view_bind_slider_value(lv_obj_t *obj, shrewd_basic_reflect_ref_t *ref);

/** textarea 的文本:双向(v-model)。ref 槽位是 `char[N]`。 */
int shrewd_lv_view_bind_textarea_text(lv_obj_t *obj, shrewd_basic_reflect_ref_t *ref);

/**
 * tab 栏的高亮(architecture.md §6.9)。ref 槽位是 `uint32_t`
 * (`nav->current_branch` 正是它)。
 *
 * 约定:bar 的**直接子项按分支顺序排列**,第 i 个子项在当前分支是 i 时进
 * `LV_STATE_CHECKED`。**样式交给主题** —— 框架只表达"谁是当前"。
 */
int shrewd_lv_view_bind_tab_bar(lv_obj_t *bar, shrewd_basic_reflect_ref_t *ref);

/*
 * ==========================================================================
 * 列表与条件渲染:对账原语(design.md §10)
 * ==========================================================================
 *
 * **两种变化走两个机制**:
 *   - 内容变化(某个子项的字段变了)→ 反应式:item 里的 ref;
 *   - 结构变化(增删 / 重排)     → 对账:source ref。
 *
 * 所以 `build` **只跑一次** —— 若让它重跑会再挂一套绑定(泄漏)。内容要变,
 * 就得是 item 里的 ref。
 */

/** ref 槽位类型:列表本身(design.md §10.2)。 */
typedef struct shrewd_lv_view_list {
    const void *items; /**< 连续数组;抽象源为 NULL */
    uint32_t count;
} shrewd_lv_view_list_t;

/** 数据源 + 子项描述(design.md §10.3)。 */
typedef struct shrewd_lv_view_list_desc {
    /** 取第 i 项数据。为 NULL 时用 `items + i * item_size`。 */
    const void *(*at)(void *ctx, uint32_t i);
    /** 稳定标识,必需(对账靠它)。 */
    uint32_t (*key)(void *ctx, uint32_t i);
    size_t item_size;

    /**
     * 子项实例的宽度。**0 = 无实例**。
     *
     * 组件本该"调用方内嵌"(design.md §7.2),但列表子项是动态的、调用方无法
     * 预先持有 —— 所以框架按这个宽度分配、传给 build、销毁时释放(照
     * lv_fragment 的 instance_size 形态)。这让 §14.1 摩擦点 1 的记账消失。
     */
    size_t instance_size;

    /**
     * 建子项,返回它的 root widget。**只跑一次**。
     * @p instance 是框架分配的实例(instance_size 为 0 时是 NULL)。
     */
    lv_obj_t *(*build)(void *ctx, lv_obj_t *container, const void *item, void *instance);
    /**
     * 拆子项前的收尾,可选。框架随后删 widget、释放 instance。
     * 绑定随 widget 自清(design.md §3),这里不用手动解绑。
     */
    void (*teardown)(void *ctx, void *instance);
} shrewd_lv_view_list_desc_t;

/**
 * 把 @p source(槽位是 shrewd_lv_view_list_t)接到 @p container 上。
 *
 * 契约(design.md §10.5):
 *   - `at(i)` 返回的指针在子项存活期间必须**稳定**;
 *   - 子项的数据结构里要含 ref,否则内容变化反映不出来。
 *
 * 容器**只放列表子项**(顺序按 index 对齐)。
 *
 * **子项实例由框架管理**:声明 `instance_size`,框架分配、传给 `build`、
 * 子项销毁时释放 —— 使用者不碰 `calloc`/`free`。
 */
int shrewd_lv_view_bind_list(lv_obj_t *container,
                             shrewd_basic_reflect_ref_t *source,
                             const shrewd_lv_view_list_desc_t *desc,
                             void *ctx);

/*
 * ==========================================================================
 * 跨线程桥(design.md §11)
 * ==========================================================================
 *
 * **铁律:所有 `ref_set` 必须发生在跑 `lv_timer_handler()` 的那个线程。**
 * 因为 `ref_set` 会同步重跑 effect,而 effect 里是 `lv_*` 调用 —— 在别的线程写
 * ref = 在别的线程碰 UI 树 = 未定义行为。
 *
 * 桥的全部职责就是**把数据从别的线程送到这个线程**,不藏线程:
 *
 *     数据侧(任意线程):  post()  快照入队
 *     UI 线程(app_step): drain()  一次性出队全部 → 逐个 ref_set
 *
 * 批量是关键:50 个字段变化不该唤醒 UI 线程 50 次。一次 drain 吃掉整队,
 * 由 effect 去重、LVGL 合并重绘。
 */

#include "shrewd-basic/dbus/dbus.h"

/**
 * 桥。`state` 是私有的(队列 + 锁),由 init 分配 —— 不把 pthread 漏进公共头,
 * 与 shrewd-basic 的做法一致。
 */
struct shrewd_lv_view_bridge {
    void *state;
};

int shrewd_lv_view_bridge_init(shrewd_lv_view_bridge_t *b);
void shrewd_lv_view_bridge_dispose(shrewd_lv_view_bridge_t *b);

/**
 * 入队一份快照。**任意线程可调**;@p value 被拷贝,调用方随后可释放。
 * @p size 必须与 @p ref 的宽度一致。
 */
int shrewd_lv_view_bridge_post(shrewd_lv_view_bridge_t *b,
                               shrewd_basic_reflect_ref_t *ref,
                               const void *value,
                               size_t size);

/**
 * 出队并逐个 `ref_set`。**只能在 UI 线程调** —— 由 `shrewd_lv_view_app_step()`
 * 在每个 tick 里戳它。
 */
int shrewd_lv_view_bridge_drain(shrewd_lv_view_bridge_t *b);

/** sb_dbus 绑定的上下文:**调用方内嵌,须活过订阅**(家法:不隐藏分配)。 */
typedef struct shrewd_lv_view_dbus_bind {
    shrewd_lv_view_bridge_t *bridge;
    shrewd_basic_reflect_ref_t *ref;
} shrewd_lv_view_dbus_bind_t;

/**
 * 合流:把一个 sb_dbus 节点接到 ref 上。节点变化时(回调跑在 sb_dbus 的
 * **分发线程**,即"数据侧")→ 快照入队 → UI 线程 drain → `ref_set`。
 *
 * @p bind 由调用方持有(填好 bridge/ref 两个字段);@p handle 收回订阅句柄,
 * 交给 `shrewd_basic_dbus_unsubscribe` 退订。
 */
int shrewd_lv_view_bridge_bind_dbus(shrewd_lv_view_bridge_t *b,
                                    shrewd_basic_dbus_t *bus,
                                    const char *pattern,
                                    shrewd_basic_reflect_ref_t *ref,
                                    shrewd_lv_view_dbus_bind_t *bind,
                                    shrewd_basic_dbus_handle_t **handle);

/*
 * The binding layer lands here (docs/initiation.md milestone 2): text /
 * range / v-model bindings and the component lifecycle. The reactive
 * core is not duplicated in this facade - it lives in shrewd-basic
 * (shrewd_basic_reflect_ref_t / _effect / _computed / _watch) and its
 * public header travels with this package.
 */

#endif /* SHREWD_LV_VIEW_VIEW_H */
