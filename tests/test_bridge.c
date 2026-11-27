/**
 * @file test_bridge.c
 * @brief 跨线程桥的单测(design.md §11)。
 *
 * 验三件事:post/drain 的批量语义、**多线程 post + UI 线程 drain**、
 * 以及 sb_dbus 合流(回调跑在分发线程 → 入队 → UI 线程 ref_set)。
 */
#include <pthread.h>
#include <stdio.h>
#include <string.h>

#include "lvgl_fixture.h"
#include "shrewd-lv-view/view.h"

static int g_fail;

static void check(int ok, const char *what) {
    if (!ok) {
        printf("  FAIL %s\n", what);
        g_fail += 1;
    }
}

/* --- 1. 批量:post N 次,drain 一次全落地 ---------------------------- */

static void test_batch(void) {
    printf("test_batch\n");
    shrewd_lv_view_bridge_t bridge;
    check(SHREWD_LV_VIEW_ERR_OK == shrewd_lv_view_bridge_init(&bridge), "bridge_init");

    int32_t a = 0;
    int32_t b = 0;
    shrewd_basic_reflect_ref_t ra;
    shrewd_basic_reflect_ref_t rb;
    check(SHREWD_BASIC_ERR_OK == shrewd_basic_reflect_ref_init(&ra, &a, sizeof(a)), "ref_init a");
    check(SHREWD_BASIC_ERR_OK == shrewd_basic_reflect_ref_init(&rb, &b, sizeof(b)), "ref_init b");

    int32_t va = 11;
    int32_t vb = 22;
    check(SHREWD_LV_VIEW_ERR_OK == shrewd_lv_view_bridge_post(&bridge, &ra, &va, sizeof(va)),
          "post a");
    check(SHREWD_LV_VIEW_ERR_OK == shrewd_lv_view_bridge_post(&bridge, &rb, &vb, sizeof(vb)),
          "post b");
    check(0 == a, "not applied before drain");
    check(0 == b, "not applied before drain (b)");

    check(SHREWD_LV_VIEW_ERR_OK == shrewd_lv_view_bridge_drain(&bridge), "drain");
    check(11 == a, "a applied");
    check(22 == b, "b applied");

    /* 二次 drain 无副作用 */
    check(SHREWD_LV_VIEW_ERR_OK == shrewd_lv_view_bridge_drain(&bridge), "drain again");
    check((11 == a) && (22 == b), "no double apply");

    shrewd_lv_view_bridge_dispose(&bridge);
}

/* --- 2. 多线程 post + UI 线程 drain ---------------------------------- */

#define THREADS 4
#define POSTS_PER_THREAD 200

typedef struct {
    shrewd_lv_view_bridge_t *bridge;
    shrewd_basic_reflect_ref_t *ref;
    int32_t base;
} poster_ctx_t;

static void *poster_main(void *arg) {
    poster_ctx_t *c = (poster_ctx_t *)arg;
    for (int i = 0; i < POSTS_PER_THREAD; ++i) {
        int32_t v = c->base + i;
        (void)shrewd_lv_view_bridge_post(c->bridge, c->ref, &v, sizeof(v));
    }
    return NULL;
}

static void test_multithreaded(void) {
    printf("test_multithreaded\n");
    shrewd_lv_view_bridge_t bridge;
    check(SHREWD_LV_VIEW_ERR_OK == shrewd_lv_view_bridge_init(&bridge), "bridge_init");

    int32_t value = 0;
    shrewd_basic_reflect_ref_t ref;
    check(SHREWD_BASIC_ERR_OK == shrewd_basic_reflect_ref_init(&ref, &value, sizeof(value)),
          "ref_init");

    pthread_t threads[THREADS];
    poster_ctx_t ctxs[THREADS];
    for (int i = 0; i < THREADS; ++i) {
        ctxs[i].bridge = &bridge;
        ctxs[i].ref = &ref;
        ctxs[i].base = i * 1000;
        check(0 == pthread_create(&threads[i], NULL, poster_main, &ctxs[i]), "pthread_create");
    }
    for (int i = 0; i < THREADS; ++i) {
        (void)pthread_join(threads[i], NULL);
    }
    /* 所有投递都还在队列里(没 drain 过) */
    check(0 == value, "nothing applied until drain");

    check(SHREWD_LV_VIEW_ERR_OK == shrewd_lv_view_bridge_drain(&bridge), "drain all");
    /* 最后一次投递赢(ref_set 覆盖) */
    check(0 != value, "something applied");

    shrewd_lv_view_bridge_dispose(&bridge);
}

/* --- 3. sb_dbus 合流:分发线程 → 入队 → UI 线程 ---------------------- */

static void test_dbus_integration(void) {
    printf("test_dbus_integration\n");
    shrewd_lv_view_bridge_t bridge;
    check(SHREWD_LV_VIEW_ERR_OK == shrewd_lv_view_bridge_init(&bridge), "bridge_init");

    shrewd_basic_dbus_t *bus = NULL;
    if (SHREWD_BASIC_ERR_OK != shrewd_basic_dbus_create(NULL, &bus)) {
        printf("  SKIP dbus create failed\n");
        shrewd_lv_view_bridge_dispose(&bridge);
        return;
    }

    int32_t value = 0;
    shrewd_basic_reflect_ref_t ref;
    check(SHREWD_BASIC_ERR_OK == shrewd_basic_reflect_ref_init(&ref, &value, sizeof(value)),
          "ref_init");

    /* 挂一个节点,给它一个 int32 的类型 */
    static const shrewd_basic_reflect_type_t g_i32 = {
        .sb_refl_kind = SHREWD_BASIC_REFLECT_KIND_INT32,
        .sb_refl_name = "int32",
        .sb_refl_size = sizeof(int32_t),
    };
    shrewd_basic_dbus_handle_t *mounted = NULL;
    const shrewd_basic_dbus_mount_t mount = {
        .type = &g_i32,
        .store = SHREWD_BASIC_DBUS_STORE_OWNED,
    };
    check(SHREWD_BASIC_ERR_OK == shrewd_basic_dbus_mount(bus, "/t/val", &mount, &mounted), "mount");

    static shrewd_lv_view_dbus_bind_t bind;
    shrewd_basic_dbus_handle_t *handle = NULL;
    check(SHREWD_LV_VIEW_ERR_OK
              == shrewd_lv_view_bridge_bind_dbus(&bridge, bus, "/t/val", &ref, &bind, &handle),
          "bind_dbus");

    int32_t v = 77;
    check(SHREWD_BASIC_ERR_OK == shrewd_basic_dbus_set(bus, "/t/val", &v, sizeof(v), NULL), "set");
    (void)shrewd_basic_dbus_drain(bus); /* 等分发线程把通知吐完 */

    check(0 == value, "not applied before bridge drain");
    check(SHREWD_LV_VIEW_ERR_OK == shrewd_lv_view_bridge_drain(&bridge), "bridge drain");
    check(77 == value, "dbus value reached the ref through the bridge");

    if (NULL != handle) {
        (void)shrewd_basic_dbus_unsubscribe(bus, handle);
    }
    if (NULL != mounted) {
        (void)shrewd_basic_dbus_unmount(bus, mounted);
    }
    shrewd_basic_dbus_destroy(bus);
    shrewd_lv_view_bridge_dispose(&bridge);
}

int main(void) {
    lvgl_fixture_init();
    test_batch();
    test_multithreaded();
    test_dbus_integration();
    if (0 == g_fail) {
        printf("test_bridge: ok\n");
        return 0;
    }
    printf("test_bridge: FAILED (%d)\n", g_fail);
    return 1;
}
