/**
 * @file app.c
 * @brief 装配。显示的创建是平台相关点,见 disp_create()。
 */
#include <unistd.h>

#include "app.h"
#include "views/home.h"

/* ★ 平台相关点:分辨率按显示驱动调整。LV_HOR_RES 是函数调用宏,
   不能用作文件作用域的数组长度,所以这里是常量。 */
#define APP_DISPLAY_HOR_RES 800
#define APP_DISPLAY_VER_RES 480
#define APP_DRAW_BUF_LINES 10

static lv_color_t s_buf[APP_DISPLAY_HOR_RES * APP_DRAW_BUF_LINES];

static void disp_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    (void)area;
    (void)px_map;
    lv_display_flush_ready(disp);
}

/* ★ 平台相关点:真实板子上按显示驱动替换。 */
static lv_display_t *disp_create(void) {
    lv_display_t *disp = lv_display_create(APP_DISPLAY_HOR_RES, APP_DISPLAY_VER_RES);
    lv_display_set_flush_cb(disp, disp_flush);
    lv_display_set_buffers(disp, s_buf, NULL, sizeof(s_buf), LV_DISPLAY_RENDER_MODE_PARTIAL);
    return disp;
}

int app_boot(shrewd_lv_view_app_t *app) {
    lv_init();
    lv_display_t *disp = disp_create();
    if (NULL == disp) {
        return SHREWD_LV_VIEW_ERR_ARG_INVALID;
    }
    int status = shrewd_lv_view_app_init(app, disp);
    if (SHREWD_LV_VIEW_ERR_OK != status) {
        return status;
    }
    static @APP_PREFIX @_home_t home;
    @APP_PREFIX @_home_init(&home);
    return shrewd_lv_view_app_mount(app, &home.base, NULL);
}

int app_step(shrewd_lv_view_app_t *app) {
    int status = shrewd_lv_view_app_step(app);
    usleep(5000);
    return status;
}
