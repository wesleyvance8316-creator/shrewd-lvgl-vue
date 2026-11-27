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
 * shrewd-basic 的失败原样返回、不跨层重编码(见 docs/design.md §12)。
 *
 * 号段 -0x6000..-0x6fff 是 lv_view 自己的,不占用 shrewd-basic 的域分配法
 * (它按 -N00..-N99 分模块),所以**不需要改动 shrewd-basic**。
 */
typedef enum shrewd_lv_view_error {
    SHREWD_LV_VIEW_ERR_OK = 0,
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

/** 默认栈深上限:安全底线,不是语义(architecture.md §6.6)。 */
#define SHREWD_LV_VIEW_NAV_DEFAULT_LIMIT 32

/** 导航栈。 */
typedef struct shrewd_lv_view_nav {
    lv_obj_t *host;
    shrewd_lv_view_nav_entry_t *entries;
    size_t count;
    size_t capacity;
    size_t limit;
    uint32_t next_seq;
    shrewd_lv_view_guard_fn guard;
    void *guard_ctx;
    /* 页面登记表:name -> 实例。**只服务于恢复**(§8.6 按 name 找回实例),
       所以要恢复的页面必须先登记;push 会顺带登记。 */
    shrewd_lv_view_page_t **registry;
    size_t registry_count;
    size_t registry_capacity;
} shrewd_lv_view_nav_t;

/** 初始化导航栈,@p host 是页面容器。@p limit 0 表示用默认上限。 */
int shrewd_lv_view_nav_init(shrewd_lv_view_nav_t *nav, lv_obj_t *host, size_t limit);

/** 释放导航栈(含未销毁的 widget 树与参数副本)。 */
void shrewd_lv_view_nav_dispose(shrewd_lv_view_nav_t *nav);

void shrewd_lv_view_nav_set_guard(shrewd_lv_view_nav_t *nav,
                                  shrewd_lv_view_guard_fn guard,
                                  void *ctx);

/** 前进。@p params 拷进栈条目。 */
int shrewd_lv_view_nav_push(shrewd_lv_view_nav_t *nav,
                            shrewd_lv_view_page_t *page,
                            const void *params);

/** 返回一层。 */
int shrewd_lv_view_nav_pop(shrewd_lv_view_nav_t *nav);

/** 按谓词连续 pop,中间逐个 exit + 按策略销毁。 */
int shrewd_lv_view_nav_pop_until(shrewd_lv_view_nav_t *nav,
                                 shrewd_lv_view_nav_pred_fn pred,
                                 void *ctx);

/** 同级跳转 / tab 切换:不产生历史。 */
int shrewd_lv_view_nav_replace(shrewd_lv_view_nav_t *nav,
                               shrewd_lv_view_page_t *page,
                               const void *params);

/** 栈顶;空栈返回 NULL。 */
shrewd_lv_view_page_t *shrewd_lv_view_nav_top(shrewd_lv_view_nav_t *nav);

size_t shrewd_lv_view_nav_depth(const shrewd_lv_view_nav_t *nav);

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
    lv_obj_t *host;           /**< page container (M2: shell content area) */
    shrewd_lv_view_nav_t nav; /**< 导航栈 */
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

/*
 * The binding layer lands here (docs/initiation.md milestone 2): text /
 * range / v-model bindings and the component lifecycle. The reactive
 * core is not duplicated in this facade - it lives in shrewd-basic
 * (shrewd_basic_reflect_ref_t / _effect / _computed / _watch) and its
 * public header travels with this package.
 */

#endif /* SHREWD_LV_VIEW_VIEW_H */
