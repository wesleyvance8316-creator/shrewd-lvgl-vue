/**
 * @file lv_view_app.c
 * @brief Application assembly: page host + plugins + the one loop step.
 *
 * The framework does not own a main() or a loop (docs/design.md §9.2) -
 * that is the "do not hide threads" rule. It owns the host widget, the
 * plugin registry, and one step the caller ticks from wherever its loop
 * lives.
 */

#define _POSIX_C_SOURCE 200809L

#include <string.h>
#include <time.h>

#include "shrewd-lv-view/view.h"

const char *shrewd_lv_view_error_name(int code) {
    switch (code) {
    case SHREWD_LV_VIEW_ERR_OK:
        return "SHREWD_LV_VIEW_ERR_OK";
    case SHREWD_LV_VIEW_ERR_ARG_INVALID:
        return "SHREWD_LV_VIEW_ERR_ARG_INVALID";
    case SHREWD_LV_VIEW_ERR_MEM_NOMEM:
        return "SHREWD_LV_VIEW_ERR_MEM_NOMEM";
    case SHREWD_LV_VIEW_ERR_NOT_FOUND:
        return "SHREWD_LV_VIEW_ERR_NOT_FOUND";
    case SHREWD_LV_VIEW_ERR_FULL:
        return "SHREWD_LV_VIEW_ERR_FULL";
    case SHREWD_LV_VIEW_ERR_NOT_MOUNTED:
        return "SHREWD_LV_VIEW_ERR_NOT_MOUNTED";
    case SHREWD_LV_VIEW_ERR_NAV_STACK_FULL:
        return "SHREWD_LV_VIEW_ERR_NAV_STACK_FULL";
    case SHREWD_LV_VIEW_ERR_NAV_CANCELLED:
        return "SHREWD_LV_VIEW_ERR_NAV_CANCELLED";
    case SHREWD_LV_VIEW_ERR_NAV_EMPTY:
        return "SHREWD_LV_VIEW_ERR_NAV_EMPTY";
    default:
        break;
    }
    /* shrewd-basic's failures are passed through unmapped (docs/design.md §12). */
    return shrewd_basic_error_name(code);
}

/**
 * 默认 tick 源。**没有 tick 源 lv_timer_handler() 里定时器永不推进** —— 实测
 * 会挂在第一个 tick(design.md §14.1 摩擦点 2)。调用方可在此前
 * `lv_tick_set_cb()` 覆盖。
 */
static uint32_t app_default_tick(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((ts.tv_sec * 1000u) + (uint32_t)(ts.tv_nsec / 1000000));
}

int shrewd_lv_view_app_init(shrewd_lv_view_app_t *app, lv_display_t *display) {
    if (NULL == lv_tick_get_cb()) {
        lv_tick_set_cb(app_default_tick);
    }
    if (NULL == app) {
        return SHREWD_LV_VIEW_ERR_ARG_INVALID;
    }
    memset(app, 0, sizeof(*app));
    app->display = (NULL != display) ? display : lv_display_get_default();
    if (NULL == app->display) {
        return SHREWD_LV_VIEW_ERR_ARG_INVALID;
    }
    lv_obj_t *screen = lv_display_get_screen_active(app->display);
    if (NULL == screen) {
        return SHREWD_LV_VIEW_ERR_ARG_INVALID;
    }
    /* The page host. M2 turns this into the shell's content area. */
    app->host = lv_obj_create(screen);
    if (NULL == app->host) {
        return SHREWD_LV_VIEW_ERR_MEM_NOMEM;
    }
    lv_obj_set_style_pad_all(app->host, 0, 0);
    lv_obj_set_style_border_width(app->host, 0, 0);
    lv_obj_set_size(app->host,
                    lv_display_get_horizontal_resolution(app->display),
                    lv_display_get_vertical_resolution(app->display));
    return shrewd_lv_view_nav_init(&app->nav,
                                   app->host,
                                   lv_display_get_layer_top(app->display),
                                   SHREWD_LV_VIEW_NAV_DEFAULT_LIMIT);
}

int shrewd_lv_view_app_use(shrewd_lv_view_app_t *app, const shrewd_lv_view_plugin_t *plugin) {
    if ((NULL == app) || (NULL == plugin) || (NULL == plugin->init)) {
        return SHREWD_LV_VIEW_ERR_ARG_INVALID;
    }
    if (SHREWD_LV_VIEW_MAX_PLUGINS <= app->plugin_count) {
        return SHREWD_LV_VIEW_ERR_FULL;
    }
    int status = plugin->init(plugin->ctx, app);
    if (SHREWD_LV_VIEW_ERR_OK != status) {
        return status;
    }
    app->plugins[app->plugin_count] = plugin;
    app->plugin_count += 1;
    return SHREWD_LV_VIEW_ERR_OK;
}

int shrewd_lv_view_app_mount(shrewd_lv_view_app_t *app,
                             shrewd_lv_view_page_t *page,
                             const void *params) {
    if ((NULL == app) || (NULL == page) || (NULL == page->desc)) {
        return SHREWD_LV_VIEW_ERR_ARG_INVALID;
    }
    /* 挂载 = 压进导航栈。四阶段由栈驱动(architecture.md §6.2)。 */
    return shrewd_lv_view_nav_push(&app->nav, page, params);
}

void shrewd_lv_view_app_set_bridge(shrewd_lv_view_app_t *app, shrewd_lv_view_bridge_t *bridge) {
    if (NULL != app) {
        app->bridge = bridge;
    }
}

int shrewd_lv_view_app_step(shrewd_lv_view_app_t *app) {
    if (NULL == app) {
        return SHREWD_LV_VIEW_ERR_ARG_INVALID;
    }
    /* 跨线程投递在这一步落地:先出队 ref_set,effect 同步推 UI,再走 LVGL。 */
    if (NULL != app->bridge) {
        (void)shrewd_lv_view_bridge_drain(app->bridge);
    }
    lv_timer_handler();
    return SHREWD_LV_VIEW_ERR_OK;
}
