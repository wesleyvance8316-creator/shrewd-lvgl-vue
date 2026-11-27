/**
 * @file main.c
 * @brief 真实页面原型:把六件机制都用上,倒推 API 手感。
 */
#include <stdio.h>
#include <unistd.h>

#include "feed.h"
#include "views/views.h"

#define DEVICES 4

/*
 * ★ 踩坑记录:display **必须**有 flush 回调。没有它 lv_timer_handler() 会转
 * 不出来(实测挂在第 0 个 tick)—— LVGL 的刷新等不到 flush_ready 就空转。
 */
static void disp_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px) {
    (void)area;
    (void)px;
    lv_display_flush_ready(disp);
}

int main(void) {
    /* 1. 数据模型(stores/) */
    static device_t devices[DEVICES];
    static device_t *device_ptrs[DEVICES];
    device_init(&devices[0], 1, "gateway", 90);
    device_init(&devices[1], 2, "sensor-a", 64);
    device_init(&devices[2], 3, "sensor-b", 33);
    device_init(&devices[3], 4, "sensor-c", 12);
    for (int i = 0; i < DEVICES; ++i) {
        device_ptrs[i] = &devices[i];
    }

    /* 2. 装配 */
    /* tick 源交给 app_init 的默认实现(design.md §14.1 摩擦点 2 已修) */
    lv_init();
    lv_display_t *disp = lv_display_create(480, 320);
    lv_display_set_flush_cb(disp, disp_flush);
    (void)disp;
    static lv_color_t buf[480 * 10];
    lv_display_set_buffers(disp, buf, NULL, sizeof(buf), LV_DISPLAY_RENDER_MODE_PARTIAL);

    shrewd_lv_view_app_t app;
    if (SHREWD_LV_VIEW_ERR_OK != shrewd_lv_view_app_init(&app, disp)) {
        return 1;
    }

    /* 2.5 壳:标题栏 + 底栏 + 内容区(architecture.md §6.9)。
     *     标题栏显当前页名 —— 是 nav 状态的绑定,不是手动同步。 */
    static shrewd_lv_view_shell_t shell;
    if (SHREWD_LV_VIEW_ERR_OK != shrewd_lv_view_shell_create(&shell, disp, &app.nav, 32, 24)) {
        return 1;
    }

    /* 3. 页面:调用方持有,永久(design.md §8.3) */
    static home_page_t home;
    static detail_page_t detail;
    static settings_page_t settings;
    home_page_init(&home, device_ptrs, DEVICES);
    detail_page_init(&detail, device_ptrs, DEVICES);
    settings_page_init(&settings);

    /* 4. 数据源线程 → 桥 → UI 线程 */
    shrewd_lv_view_bridge_t bridge;
    if (SHREWD_LV_VIEW_ERR_OK != shrewd_lv_view_bridge_init(&bridge)) {
        return 1;
    }
    shrewd_lv_view_app_set_bridge(&app, &bridge);
    (void)feed_start(&bridge, &devices[1]);

    if (SHREWD_LV_VIEW_ERR_OK != shrewd_lv_view_app_mount(&app, &home.base, NULL)) {
        return 1;
    }

    /* 6. 主循环:框架不持有它(design.md §9.2) */
    for (int tick = 0; tick < 200; ++tick) {
        (void)shrewd_lv_view_app_step(&app);
        /* 演示导航:50 tick 后进详情,再 50 tick 回去 */
        if (50 == tick) {
            int32_t id = 2;
            (void)shrewd_lv_view_nav_push(&app.nav, &detail.base, &id);
        }
        if (100 == tick) {
            (void)shrewd_lv_view_nav_pop(&app.nav);
        }
    }

    feed_stop();
    shrewd_lv_view_bridge_dispose(&bridge);
    shrewd_lv_view_nav_dispose(&app.nav);
    printf("prototype: ok\n");
    return 0;
}
