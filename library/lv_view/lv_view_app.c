/**
 * @file lv_view_app.c
 * @brief Application assembly: page host + plugins + the one loop step.
 *
 * The framework does not own a main() or a loop (docs/design.md §9.2) -
 * that is the "do not hide threads" rule. It owns the host widget, the
 * plugin registry, and one step the caller ticks from wherever its loop
 * lives.
 */

#include <string.h>

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

int shrewd_lv_view_app_init(shrewd_lv_view_app_t *app, lv_display_t *display) {
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
    return SHREWD_LV_VIEW_ERR_OK;
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
    if ((NULL == app) || (NULL == page) || (NULL == page->desc) || (NULL == app->host)) {
        return SHREWD_LV_VIEW_ERR_ARG_INVALID;
    }
    if (NULL != app->mounted) {
        if (NULL != app->mounted->desc->exit) {
            app->mounted->desc->exit(app->mounted);
        }
        if (NULL != app->mounted->desc->destroy) {
            app->mounted->desc->destroy(app->mounted);
        }
        if (NULL != app->mounted->root) {
            lv_obj_delete(app->mounted->root);
            app->mounted->root = NULL;
        }
        app->mounted = NULL;
    }
    int status = page->desc->create(page, app->host, params);
    if (SHREWD_LV_VIEW_ERR_OK != status) {
        return status;
    }
    if (NULL != page->desc->enter) {
        status = page->desc->enter(page);
        if (SHREWD_LV_VIEW_ERR_OK != status) {
            return status;
        }
    }
    app->mounted = page;
    return SHREWD_LV_VIEW_ERR_OK;
}

int shrewd_lv_view_app_step(shrewd_lv_view_app_t *app) {
    if (NULL == app) {
        return SHREWD_LV_VIEW_ERR_ARG_INVALID;
    }
    /* M2 drains cross-thread deliveries here (docs/design.md §11). */
    lv_timer_handler();
    return SHREWD_LV_VIEW_ERR_OK;
}
