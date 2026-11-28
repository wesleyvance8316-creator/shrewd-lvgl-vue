/**
 * @file devices.c
 * @brief 设备数据 + 一条采集线程。
 *
 * 采集线程**只 post 不碰 UI**(design.md §11 的铁律):改动经桥送到 UI 线程,
 * 由 `app_step` 里的 drain 统一落 ref。
 */
#include "devices.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

demo_device_t demo_devices[DEMO_DEVICE_COUNT];

static pthread_t g_thread;
static int g_running;
static shrewd_lv_view_bridge_t *g_bridge;

void demo_devices_init(void) {
    static const char *names[DEMO_DEVICE_COUNT] = {
        "gateway",
        "sensor-a",
        "sensor-b",
        "sensor-c",
        "relay-1",
        "relay-2",
        "meter-1",
        "meter-2",
    };
    for (int i = 0; i < DEMO_DEVICE_COUNT; ++i) {
        demo_device_t *d = &demo_devices[i];
        d->id = i + 1;
        (void)snprintf(d->name_buf, sizeof(d->name_buf), "%s", names[i]);
        d->battery_val = 20 + (17 * i);
        (void)shrewd_basic_reflect_ref_init(&d->name, d->name_buf, sizeof(d->name_buf));
        (void)shrewd_basic_reflect_ref_init(&d->battery, &d->battery_val, sizeof(d->battery_val));
    }
}

static void *feed_main(void *arg) {
    (void)arg;
    int round = 0;
    while (0 != g_running) {
        int i = round % DEMO_DEVICE_COUNT;
        int32_t next = demo_devices[i].battery_val + 3;
        if (100 < next) {
            next = 10;
        }
        /* 数据侧只投递:UI 线程的 drain 会去 ref_set。 */
        (void)shrewd_lv_view_bridge_post(g_bridge, &demo_devices[i].battery, &next, sizeof(next));
        round += 1;
        usleep(200 * 1000);
    }
    return NULL;
}

int demo_feed_start(shrewd_lv_view_bridge_t *bridge) {
    if (NULL == bridge) {
        return SHREWD_LV_VIEW_ERR_ARG_INVALID;
    }
    g_bridge = bridge;
    g_running = 1;
    return (0 == pthread_create(&g_thread, NULL, feed_main, NULL)) ? SHREWD_LV_VIEW_ERR_OK
                                                                   : SHREWD_LV_VIEW_ERR_FULL;
}

void demo_feed_stop(void) {
    if (0 == g_running) {
        return;
    }
    g_running = 0;
    (void)pthread_join(g_thread, NULL);
    g_bridge = NULL;
}
