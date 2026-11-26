/**
 * @file main.c
 * @brief 入口 A:独立进程(默认)。开箱即用 —— build 完直接跑。
 *
 * 若 GUI 是业务主进程里的一个线程(拓扑 B),改用 app_thread.c 并把
 * CMake 的 SHREWD_APP_AS_THREAD 打开。框架不持有主循环,见 design.md §9.2。
 */
#include "app.h"

int main(void) {
    shrewd_lv_view_app_t app;
    if (SHREWD_LV_VIEW_ERR_OK != app_boot(&app)) {
        return 1;
    }
    for (;;) {
        app_step(&app);
    }
    return 0;
}
