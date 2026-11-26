/**
 * @file app_thread.c
 * @brief 入口 B:GUI 作为业务主进程的一个线程(拓扑 B)。
 *
 * 由业务主程序起这条线程并调用 gui_thread()。框架不持有主循环,
 * 见 design.md §9.2。
 */
#include "app.h"

void *gui_thread(void *arg) {
    (void)arg;
    static shrewd_lv_view_app_t app;
    if (SHREWD_LV_VIEW_ERR_OK != app_boot(&app)) {
        return NULL;
    }
    for (;;) {
        app_step(&app);
    }
    return NULL;
}
