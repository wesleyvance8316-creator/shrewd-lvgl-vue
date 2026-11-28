/**
 * @file bench.c
 * @brief 性能/内存基线(design.md §16)。
 *
 * **打印绝对数,只断言相对关系** —— 绝对时间随机器变,关系不变。所以断言都是
 * "虚拟化比全量省"这类,数值本身只是打印出来供抄录,见 docs/benchmarks.md。
 *
 * **内存有两块,必须分开量**:
 *   - **LVGL 的池**(`LV_MEM_SIZE`)—— widget 树在这里,**不经过 libc 堆**;
 *   - **libc 堆** —— 框架自己的 struct(ref / effect / binding / 槽位)在这里。
 *
 * 合起来才是真实占用。每测完一块就把 widget 删掉 —— 池只有 64KB,留着会把
 * 后面的测量挤成空操作(实测踩过)。
 *
 * **例外:最后一节(ref_set 传播)的 box 不删** —— 见文末"已知问题"。
 */
#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "lvgl_fixture.h"
#include "shrewd-lv-view/view.h"

static int g_fail;

static void check(int ok, const char *what) {
    if (!ok) {
        printf("  FAIL %s\n", what);
        g_fail += 1;
    }
}

/* --- 两块内存 ------------------------------------------------------- */

typedef struct {
    size_t lv_bytes; /**< LVGL 池里在用的字节(widget 树) */
    size_t lv_blocks;
    size_t heap; /**< libc 堆(框架的 struct) */
} mem_t;

static mem_t mem_now(void) {
    mem_t m;
    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    m.lv_bytes = mon.total_size - mon.free_size;
    m.lv_blocks = (size_t)mon.used_cnt;
    m.heap = (size_t)mallinfo2().uordblks;
    return m;
}

static double now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ((double)ts.tv_sec * 1e6) + ((double)ts.tv_nsec / 1e3);
}

static void mem_report(const char *what, mem_t before, mem_t after, int units) {
    double lv
        = (double)((after.lv_bytes > before.lv_bytes) ? (after.lv_bytes - before.lv_bytes) : 0);
    double hp = (double)((after.heap > before.heap) ? (after.heap - before.heap) : 0);
    printf("  %-28s %7.0f + %6.0f = %7.0f 字节  (%.0f/个, %d 个, %zu 块)\n",
           what,
           lv,
           hp,
           lv + hp,
           (0 < units) ? ((lv + hp) / (double)units) : 0.0,
           units,
           after.lv_blocks);
}

/* --- 1. 静态尺寸 ---------------------------------------------------- */

static void bench_sizes(void) {
    printf("== 尺寸(字节,纯 sizeof)==\n");
    printf("  %-34s %4zu\n", "shrewd_basic_reflect_ref_t", sizeof(shrewd_basic_reflect_ref_t));
    printf(
        "  %-34s %4zu\n", "shrewd_basic_reflect_effect_t", sizeof(shrewd_basic_reflect_effect_t));
    printf("  %-34s %4zu\n", "shrewd_lv_view_page_t", sizeof(shrewd_lv_view_page_t));
    printf("  %-34s %4zu\n", "shrewd_lv_view_page_desc_t", sizeof(shrewd_lv_view_page_desc_t));
    printf("  %-34s %4zu\n", "shrewd_lv_view_nav_entry_t", sizeof(shrewd_lv_view_nav_entry_t));
    printf("  %-34s %4zu\n", "shrewd_lv_view_nav_stack_t", sizeof(shrewd_lv_view_nav_stack_t));
    printf("  %-34s %4zu\n", "shrewd_lv_view_nav_t", sizeof(shrewd_lv_view_nav_t));
    printf("  %-34s %4zu\n", "shrewd_lv_view_target_t", sizeof(shrewd_lv_view_target_t));
    printf("  %-34s %4zu\n", "shrewd_lv_view_list_t", sizeof(shrewd_lv_view_list_t));
    printf("  %-34s %4zu\n", "shrewd_lv_view_list_desc_t", sizeof(shrewd_lv_view_list_desc_t));
    printf("  %-34s %4zu\n", "shrewd_lv_view_shell_t", sizeof(shrewd_lv_view_shell_t));
    printf("  %-34s %4zu\n", "shrewd_lv_view_bridge_t", sizeof(shrewd_lv_view_bridge_t));
}

/* --- 2. 内存基线 ---------------------------------------------------- */

#define N_ITEMS 100

static int32_t g_items[N_ITEMS];

static const void *src_at(void *ctx, uint32_t i) {
    (void)ctx;
    return &g_items[i];
}

static uint32_t src_key(void *ctx, uint32_t i) {
    (void)ctx;
    return (uint32_t)g_items[i];
}

static lv_obj_t *src_build(void *ctx, lv_obj_t *container, const void *item, void *instance) {
    (void)ctx;
    (void)item;
    (void)instance;
    lv_obj_t *child = lv_obj_create(container);
    lv_obj_t *label = lv_label_create(child);
    lv_label_set_text(label, "x");
    return child;
}

static const shrewd_lv_view_list_desc_t g_desc_full = {
    .at = src_at,
    .key = src_key,
    .item_size = sizeof(int32_t),
    .instance_size = 0,
    .item_height = 0,
    .build = src_build,
};

static const shrewd_lv_view_list_desc_t g_desc_win = {
    .at = src_at,
    .key = src_key,
    .item_size = sizeof(int32_t),
    .instance_size = 0,
    .item_height = 20,
    .build = src_build,
};

static void items_fill(uint32_t n) {
    for (uint32_t i = 0; i < n; ++i) {
        g_items[i] = (int32_t)(1000 + i);
    }
}

static size_t list_measure(const shrewd_lv_view_list_desc_t *desc, uint32_t n, const char *what) {
    items_fill(n);
    lv_obj_t *container = lv_obj_create(lv_screen_active());
    lv_obj_set_size(container, 200, 100);
    shrewd_lv_view_list_t slot;
    memset(&slot, 0, sizeof(slot));
    shrewd_basic_reflect_ref_t ref;
    (void)shrewd_basic_reflect_ref_init(&ref, &slot, sizeof(slot));

    mem_t before = mem_now();
    (void)shrewd_lv_view_bind_list(container, &ref, desc, NULL);
    slot.items = g_items;
    slot.count = n;
    (void)shrewd_basic_reflect_ref_set(&ref, &slot, sizeof(slot));
    mem_t after = mem_now();
    mem_report(what, before, after, (int)n);

    size_t total = (after.lv_bytes > before.lv_bytes ? after.lv_bytes - before.lv_bytes : 0)
                   + (after.heap > before.heap ? after.heap - before.heap : 0);
    lv_obj_delete(container);
    return total;
}

static void bench_memory(void) {
    printf("== 内存基线(LVGL 池 + libc 堆)==\n");

    {
        static char buf[32];
        shrewd_basic_reflect_ref_t ref;
        (void)shrewd_basic_reflect_ref_init(&ref, buf, sizeof(buf));
        lv_obj_t *box = lv_obj_create(lv_screen_active());
        mem_t before = mem_now();
        for (int i = 0; i < 100; ++i) {
            lv_obj_t *l = lv_label_create(box);
            (void)shrewd_lv_view_bind_label_text(l, &ref);
        }
        mem_t after = mem_now();
        mem_report("绑定(bind_label_text)", before, after, 100);
        check(after.heap > before.heap, "a binding costs heap");
        lv_obj_delete(box);
    }

    size_t full = list_measure(&g_desc_full, N_ITEMS, "列表 100 项(全量)");
    size_t win = list_measure(&g_desc_win, N_ITEMS, "列表 100 项(虚拟化)");
    check(win < full, "virtualized list costs less than the full list");
    if (0 < win) {
        printf("  %-28s %.2f×\n", "虚拟化 / 全量", (double)win / (double)full);
    }

    {
        shrewd_lv_view_bridge_t bridge;
        (void)shrewd_lv_view_bridge_init(&bridge);
        static int32_t val;
        shrewd_basic_reflect_ref_t ref;
        (void)shrewd_basic_reflect_ref_init(&ref, &val, sizeof(val));
        mem_t before = mem_now();
        for (int i = 0; i < 1000; ++i) {
            int32_t v = i;
            (void)shrewd_lv_view_bridge_post(&bridge, &ref, &v, sizeof(v));
        }
        mem_t after = mem_now();
        mem_report("桥:1000 条待投递", before, after, 1000);
        (void)shrewd_lv_view_bridge_drain(&bridge);
        shrewd_lv_view_bridge_dispose(&bridge);
    }
}

/* --- 3. 时间基线 ---------------------------------------------------- */

#define REPEAT 5

static void bench_time(void) {
    printf("== 时间基线(取 %d 次里的最小值)==\n", REPEAT);

    {
        lv_obj_t *container = lv_obj_create(lv_screen_active());
        lv_obj_set_size(container, 200, 100);
        items_fill(N_ITEMS);
        shrewd_lv_view_list_t slot;
        memset(&slot, 0, sizeof(slot));
        shrewd_basic_reflect_ref_t ref;
        (void)shrewd_basic_reflect_ref_init(&ref, &slot, sizeof(slot));
        (void)shrewd_lv_view_bind_list(container, &ref, &g_desc_full, NULL);

        double best = 1e18;
        for (int r = 0; r < REPEAT; ++r) {
            slot.items = g_items;
            slot.count = 0;
            (void)shrewd_basic_reflect_ref_set(&ref, &slot, sizeof(slot));
            double t0 = now_us();
            slot.count = N_ITEMS;
            (void)shrewd_basic_reflect_ref_set(&ref, &slot, sizeof(slot));
            double t1 = now_us();
            if (t1 - t0 < best) {
                best = t1 - t0;
            }
        }
        printf("  %-28s %8.2f µs  (%d 项建满)\n", "列表对账:建", best, N_ITEMS);

        best = 1e18;
        for (int r = 0; r < REPEAT; ++r) {
            for (uint32_t i = 0; i < N_ITEMS / 2; ++i) {
                int32_t tmp = g_items[i];
                g_items[i] = g_items[N_ITEMS - 1 - i];
                g_items[N_ITEMS - 1 - i] = tmp;
            }
            double t0 = now_us();
            (void)shrewd_basic_reflect_ref_set(&ref, &slot, sizeof(slot));
            double t1 = now_us();
            if (t1 - t0 < best) {
                best = t1 - t0;
            }
        }
        printf("  %-28s %8.2f µs  (%d 项倒序)\n", "列表对账:重排", best, N_ITEMS);
        lv_obj_delete(container);
    }

    {
        lv_obj_t *container = lv_obj_create(lv_screen_active());
        lv_obj_set_size(container, 200, 100);
        items_fill(N_ITEMS);
        shrewd_lv_view_list_t slot;
        memset(&slot, 0, sizeof(slot));
        shrewd_basic_reflect_ref_t ref;
        (void)shrewd_basic_reflect_ref_init(&ref, &slot, sizeof(slot));
        (void)shrewd_lv_view_bind_list(container, &ref, &g_desc_win, NULL);
        slot.items = g_items;
        slot.count = N_ITEMS;
        (void)shrewd_basic_reflect_ref_set(&ref, &slot, sizeof(slot));

        double t0 = now_us();
        for (int i = 0; i < 50; ++i) {
            lv_obj_scroll_by_bounded(container, 0, -20, LV_ANIM_OFF);
        }
        double t1 = now_us();
        printf("  %-28s %8.2f µs  (50 行)\n", "虚拟化滚动", t1 - t0);
        lv_obj_delete(container);
    }

    {
        shrewd_lv_view_bridge_t bridge;
        (void)shrewd_lv_view_bridge_init(&bridge);
        static int32_t val;
        shrewd_basic_reflect_ref_t ref;
        (void)shrewd_basic_reflect_ref_init(&ref, &val, sizeof(val));
        double t0 = now_us();
        for (int i = 0; i < 1000; ++i) {
            int32_t v = i;
            (void)shrewd_lv_view_bridge_post(&bridge, &ref, &v, sizeof(v));
        }
        double t1 = now_us();
        (void)shrewd_lv_view_bridge_drain(&bridge);
        double t2 = now_us();
        printf("  %-28s %8.2f µs  (1000 次)\n", "bridge post", t1 - t0);
        printf("  %-28s %8.2f µs  (1000 条一次 drain)\n", "bridge drain", t2 - t1);
        shrewd_lv_view_bridge_dispose(&bridge);
    }
}

/* --- 4. ref_set 传播(放最后,box 不删)------------------------------- */

/*
 * **已知问题(design.md §16)**:大批绑定 widget 被删掉之后,再新建 widget 会在
 * `lv_theme_apply → lv_obj_add_style` 里读 NULL 崩掉。已排除:不是 LVGL 池耗尽
 * (64KB→512KB 一样崩)、纯 LVGL 同样模式(DELETE 钩子里 free + effect_dispose)
 * 不崩、ASAN 对我们的堆没有任何报告。**所以这里的 box 不删**,免得把后面的
 * 测量和整个用例拖垮;这条路径要单独查。
 */
static void bench_ref_set(void) {
    printf("== ref_set 传播(绑定越多越贵)==\n");
    for (int n = 1; n <= 100; n *= 10) {
        lv_obj_t *box = lv_obj_create(lv_screen_active());
        static char buf[32];
        shrewd_basic_reflect_ref_t ref;
        (void)shrewd_basic_reflect_ref_init(&ref, buf, sizeof(buf));
        for (int i = 0; i < n; ++i) {
            lv_obj_t *l = lv_label_create(box);
            (void)shrewd_lv_view_bind_label_text(l, &ref);
        }
        double best = 1e18;
        for (int r = 0; r < REPEAT; ++r) {
            (void)snprintf(buf, sizeof(buf), "tick-%d", r);
            double t0 = now_us();
            (void)shrewd_basic_reflect_ref_set(&ref, buf, sizeof(buf));
            double t1 = now_us();
            if (t1 - t0 < best) {
                best = t1 - t0;
            }
        }
        printf("  %-28s %8.2f µs  (%d 个绑定)\n", "ref_set 传播", best, n);
        lv_obj_delete(box);
    }
}

int main(void) {
    /* 无缓冲:万一中途崩,前面的数不能跟着丢。 */
    (void)setvbuf(stdout, NULL, _IONBF, 0);
    lvgl_fixture_init();
    bench_sizes();
    bench_memory();
    bench_time();
    bench_ref_set();
    if (0 == g_fail) {
        printf("bench: ok\n");
        return 0;
    }
    printf("bench: FAILED (%d)\n", g_fail);
    return 1;
}
