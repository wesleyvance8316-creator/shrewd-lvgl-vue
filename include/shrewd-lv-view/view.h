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
struct shrewd_lv_view_page_desc {
    const char *name;       /**< identity; serialized on restore, so a string */
    const void *param_type; /**< reflect type of params (docs/design.md §8.2) */

    /** Create the widget tree and bind. @p params is the stack entry's copy. */
    int (*create)(shrewd_lv_view_page_t *p, lv_obj_t *container, const void *params);
    /** Enter: subscribe / load. */
    int (*enter)(shrewd_lv_view_page_t *p);
    /** Leave: unsubscribe / save. */
    void (*exit)(shrewd_lv_view_page_t *p);
    /** Destroy the widget tree. Bindings clean themselves (docs/design.md §3). */
    void (*destroy)(shrewd_lv_view_page_t *p);
};

#define SHREWD_LV_VIEW_MAX_PLUGINS 8

/** Application: page host + configuration + plugins. */
typedef struct shrewd_lv_view_app {
    lv_display_t *display;
    lv_obj_t *host; /**< page container (M2: shell content area) */
    shrewd_lv_view_page_t *mounted;
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
 * The binding layer lands here (docs/initiation.md milestone 2): text /
 * range / v-model bindings and the component lifecycle. The reactive
 * core is not duplicated in this facade - it lives in shrewd-basic
 * (shrewd_basic_reflect_ref_t / _effect / _computed / _watch) and its
 * public header travels with this package.
 */

#endif /* SHREWD_LV_VIEW_VIEW_H */
