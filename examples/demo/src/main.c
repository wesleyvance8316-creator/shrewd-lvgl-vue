/**
 * @file main.c
 * @brief 可交互 demo:壳 + 三个 tab + 虚拟化列表 + 守卫 + 跨线程数据。
 *
 * 跑起来是**一个 SDL 窗口**,鼠标当触摸:
 *   - 底栏点 tab 切分支(高亮自动跟 `nav->current_branch`);
 *   - 设备列表点一行 → 详情(gauge 是第三方控件、slider 是 v-model);
 *   - 改了滑块再按"返回" → **守卫吃掉第一次请求**,再按一次才走;
 *   - 电量由后台线程持续推上来,列表里的数字自己跳。
 *
 * 用法:
 *     demo [--ticks N]     跑 N 帧后自己退(无头/CI 用;省略则跑到关窗)
 *
 * 框架不持有主循环(design.md §9.2)—— 也不持有窗口,显示/输入接入在
 * `platform/sdl_port.c`,那正是接自己板子时要写的那层。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "demo/gauge.h"
#include "demo/theme.h"
#include "devices.h"
#include "platform/sdl_port.h"
#include "views.h"

#define DEMO_W 480
#define DEMO_H 320

static demo_home_page_t g_home;
static demo_detail_page_t g_detail;
static demo_settings_page_t g_settings;

/**
 * 守卫(§8.5):离开"改过但没保存"的详情页时,**吃掉第一次请求**。
 * 这就是 `GUARD_CONSUME` 的用法 —— 请求被处理了,但没真的走。
 */
static shrewd_lv_view_guard_result_t demo_guard(void *ctx, shrewd_lv_view_nav_request_t *req) {
    demo_detail_page_t *d = (demo_detail_page_t *)ctx;
    if ((0 == demo_detail_is_dirty(d)) || (req->from != d->base.desc)) {
        return SHREWD_LV_VIEW_GUARD_ALLOW;
    }
    /* 消费掉这一次,并把"已看过提示"记成新的基准 —— 下次就放行。 */
    d->level_at_open = d->level_val;
    if (NULL != d->hint) {
        lv_label_set_text(d->hint, "改动未保存 —— 再按一次返回");
    }
    printf("[guard] 吃掉请求:改动未保存,再按一次返回\n");
    return SHREWD_LV_VIEW_GUARD_CONSUME;
}

int main(int argc, char **argv) {
    long ticks = -1;
    for (int i = 1; i < argc; ++i) {
        if ((0 == strcmp(argv[i], "--ticks")) && (i + 1 < argc)) {
            ticks = strtol(argv[++i], NULL, 10);
        }
    }

    /* LVGL 先起来,再建 display —— 反过来会在 lv_display_create 里空指针崩。 */
    lv_init();

    sdl_port_t port;
    if (0 != sdl_port_init(&port, DEMO_W, DEMO_H, "shrewd-lv-view demo")) {
        fprintf(stderr, "SDL 起不来(无显示环境可设 SDL_VIDEODRIVER=dummy)\n");
        return 1;
    }

    /* 组件库的主题:控件走 LVGL 主题,片段用同一份 token(§7.6)。 */
    if (SHREWD_LV_VIEW_ERR_OK != demo_theme_init(port.display, NULL)) {
        return 1;
    }

    shrewd_lv_view_app_t app;
    if (SHREWD_LV_VIEW_ERR_OK != shrewd_lv_view_app_init(&app, port.display)) {
        return 1;
    }

    /* 壳:标题栏 + 底栏 + 内容区。标题与 tab 高亮都是导航状态的绑定。 */
    static shrewd_lv_view_shell_t shell;
    if (SHREWD_LV_VIEW_ERR_OK
        != shrewd_lv_view_shell_create(&shell, port.display, &app.nav, 32, 32)) {
        return 1;
    }
    if ((SHREWD_LV_VIEW_ERR_OK != shrewd_lv_view_shell_add_tab(&shell, "设备"))
        || (SHREWD_LV_VIEW_ERR_OK != shrewd_lv_view_shell_add_tab(&shell, "设置"))) {
        return 1;
    }

    demo_devices_init();
    demo_home_init(&g_home, &app.nav, &g_detail.base);
    demo_detail_init(&g_detail, &app.nav);
    demo_settings_init(&g_settings, &app.nav);
    shrewd_lv_view_nav_set_guard(&app.nav, demo_guard, &g_detail);

    /* 跨线程:采集线程只 post,UI 线程在 app_step 里 drain。 */
    shrewd_lv_view_bridge_t bridge;
    if (SHREWD_LV_VIEW_ERR_OK != shrewd_lv_view_bridge_init(&bridge)) {
        return 1;
    }
    shrewd_lv_view_app_set_bridge(&app, &bridge);
    (void)demo_feed_start(&bridge);

    /* 两个分支各持独立栈(§6.9)。 */
    if (SHREWD_LV_VIEW_ERR_OK != shrewd_lv_view_nav_switch_branch(&app.nav, 1)) {
        return 1;
    }
    if (SHREWD_LV_VIEW_ERR_OK != shrewd_lv_view_nav_push(&app.nav, &g_settings.base, NULL)) {
        return 1;
    }
    if (SHREWD_LV_VIEW_ERR_OK != shrewd_lv_view_nav_switch_branch(&app.nav, 0)) {
        return 1;
    }
    if (SHREWD_LV_VIEW_ERR_OK != shrewd_lv_view_app_mount(&app, &g_home.base, NULL)) {
        return 1;
    }

    long n = 0;
    while (0 == sdl_port_wants_quit(&port)) {
        sdl_port_poll(&port);
        (void)shrewd_lv_view_app_step(&app);
        n += 1;
        if ((0 <= ticks) && (ticks <= n)) {
            break;
        }
        sdl_port_delay(16); /* 约 60fps */
    }

    demo_feed_stop();
    shrewd_lv_view_bridge_dispose(&bridge);
    shrewd_lv_view_nav_dispose(&app.nav);
    sdl_port_dispose(&port);
    printf("demo: ok (%ld 帧)\n", n);
    return 0;
}
