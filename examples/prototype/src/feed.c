/**
 * @file feed.c
 */
#include "feed.h"

#include <pthread.h>
#include <unistd.h>

static pthread_t g_thread;
static int g_running;
static shrewd_lv_view_bridge_t *g_bridge;
static device_t *g_device;

static void *feed_main(void *arg) {
    (void)arg;
    int32_t v = 50;
    while (0 != g_running) {
        v += 7;
        if (100 < v) {
            v = 0;
        }
        /* 铁律:别的线程不许 ref_set,只能 post。 */
        (void)shrewd_lv_view_bridge_post(g_bridge, &g_device->battery, &v, sizeof(v));
        usleep(20 * 1000);
    }
    return NULL;
}

int feed_start(shrewd_lv_view_bridge_t *bridge, device_t *device) {
    g_bridge = bridge;
    g_device = device;
    g_running = 1;
    return (0 == pthread_create(&g_thread, NULL, feed_main, NULL)) ? SHREWD_LV_VIEW_ERR_OK
                                                                   : SHREWD_LV_VIEW_ERR_MEM_NOMEM;
}

void feed_stop(void) {
    g_running = 0;
    (void)pthread_join(g_thread, NULL);
}
