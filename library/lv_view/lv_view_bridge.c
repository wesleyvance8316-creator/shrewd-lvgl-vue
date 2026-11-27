/**
 * @file lv_view_bridge.c
 * @brief 跨线程桥(design.md §11)。
 *
 * **铁律:所有 ref_set 必须发生在跑 lv_timer_handler() 的那个线程** —— effect
 * 同步重跑、里面是 lv_* 调用。桥只做投递:数据侧 `post` 入队,UI 线程 `drain`
 * 一次性出队再逐个 ref_set。
 *
 * 批量由结构保证:post 只是入队,drain 整队吃掉,50 个字段变化不会唤醒 UI 线程
 * 50 次。effect 去重、LVGL 合并重绘。
 */

#include <pthread.h>
#include <string.h>

#include "shrewd-basic/platform/memory.h"
#include "shrewd-lv-view/view.h"

typedef struct bridge_delivery {
    shrewd_basic_reflect_ref_t *ref;
    void *value;
    size_t size;
    struct bridge_delivery *next;
} bridge_delivery_t;

typedef struct {
    pthread_mutex_t lock;
    bridge_delivery_t *head;
    bridge_delivery_t *tail;
} bridge_state_t;

int shrewd_lv_view_bridge_init(shrewd_lv_view_bridge_t *b) {
    if (NULL == b) {
        return SHREWD_LV_VIEW_ERR_ARG_INVALID;
    }
    memset(b, 0, sizeof(*b));
    bridge_state_t *state = NULL;
    void *slot = NULL;
    if (SHREWD_BASIC_ERR_OK != shrewd_basic_platform_calloc(&slot, 1, sizeof(*state))) {
        return SHREWD_LV_VIEW_ERR_MEM_NOMEM;
    }
    state = (bridge_state_t *)slot;
    (void)pthread_mutex_init(&state->lock, NULL);
    b->state = state;
    return SHREWD_LV_VIEW_ERR_OK;
}

void shrewd_lv_view_bridge_dispose(shrewd_lv_view_bridge_t *b) {
    if ((NULL == b) || (NULL == b->state)) {
        return;
    }
    bridge_state_t *state = (bridge_state_t *)b->state;
    bridge_delivery_t *it = state->head;
    while (NULL != it) {
        bridge_delivery_t *next = it->next;
        void *value = it->value;
        void *self = it;
        (void)shrewd_basic_platform_free(&value);
        (void)shrewd_basic_platform_free(&self);
        it = next;
    }
    (void)pthread_mutex_destroy(&state->lock);
    void *self = state;
    (void)shrewd_basic_platform_free(&self);
    b->state = NULL;
}

int shrewd_lv_view_bridge_post(shrewd_lv_view_bridge_t *b,
                               shrewd_basic_reflect_ref_t *ref,
                               const void *value,
                               size_t size) {
    if ((NULL == b) || (NULL == b->state) || (NULL == ref) || (NULL == value)) {
        return SHREWD_LV_VIEW_ERR_ARG_INVALID;
    }
    if (ref->size != size) {
        return SHREWD_LV_VIEW_ERR_ARG_INVALID;
    }
    bridge_delivery_t *d = NULL;
    void *slot = NULL;
    if (SHREWD_BASIC_ERR_OK != shrewd_basic_platform_calloc(&slot, 1, sizeof(*d))) {
        return SHREWD_LV_VIEW_ERR_MEM_NOMEM;
    }
    d = (bridge_delivery_t *)slot;
    if (SHREWD_BASIC_ERR_OK != shrewd_basic_platform_alloc(&d->value, size)) {
        slot = d;
        (void)shrewd_basic_platform_free(&slot);
        return SHREWD_LV_VIEW_ERR_MEM_NOMEM;
    }
    memcpy(d->value, value, size);
    d->ref = ref;
    d->size = size;

    bridge_state_t *state = (bridge_state_t *)b->state;
    (void)pthread_mutex_lock(&state->lock);
    if (NULL != state->tail) {
        state->tail->next = d;
    } else {
        state->head = d;
    }
    state->tail = d;
    (void)pthread_mutex_unlock(&state->lock);
    return SHREWD_LV_VIEW_ERR_OK;
}

int shrewd_lv_view_bridge_drain(shrewd_lv_view_bridge_t *b) {
    if ((NULL == b) || (NULL == b->state)) {
        return SHREWD_LV_VIEW_ERR_ARG_INVALID;
    }
    bridge_state_t *state = (bridge_state_t *)b->state;
    /* 一次性摘走整队:锁只护指针交换,ref_set 在锁外跑(effect 可能很贵)。 */
    (void)pthread_mutex_lock(&state->lock);
    bridge_delivery_t *first = state->head;
    state->head = NULL;
    state->tail = NULL;
    (void)pthread_mutex_unlock(&state->lock);

    while (NULL != first) {
        bridge_delivery_t *next = first->next;
        (void)shrewd_basic_reflect_ref_set(first->ref, first->value, first->size);
        void *value = first->value;
        void *self = first;
        (void)shrewd_basic_platform_free(&value);
        (void)shrewd_basic_platform_free(&self);
        first = next;
    }
    return SHREWD_LV_VIEW_ERR_OK;
}

/* --- sb_dbus 合流 ----------------------------------------------------- */

static int bridge_dbus_recv(void *ctx, const shrewd_basic_dbus_note_t *note) {
    shrewd_lv_view_dbus_bind_t *bind = (shrewd_lv_view_dbus_bind_t *)ctx;
    if ((NULL == note) || (NULL == note->value)) {
        return SHREWD_LV_VIEW_ERR_OK;
    }
    /* 回调跑在 sb_dbus 的分发线程 —— 正是"数据侧"。 */
    return shrewd_lv_view_bridge_post(bind->bridge, bind->ref, note->value, note->size);
}

int shrewd_lv_view_bridge_bind_dbus(shrewd_lv_view_bridge_t *b,
                                    shrewd_basic_dbus_t *bus,
                                    const char *pattern,
                                    shrewd_basic_reflect_ref_t *ref,
                                    shrewd_lv_view_dbus_bind_t *bind,
                                    shrewd_basic_dbus_handle_t **handle) {
    if ((NULL == b) || (NULL == bus) || (NULL == pattern) || (NULL == ref) || (NULL == bind)
        || (NULL == handle)) {
        return SHREWD_LV_VIEW_ERR_ARG_INVALID;
    }
    /* 两个字段都由这里填好 —— 让调用方预填结构体必然会漏一个(实测漏过)。 */
    bind->bridge = b;
    bind->ref = ref;
    if (SHREWD_BASIC_ERR_OK
        != shrewd_basic_dbus_subscribe(bus, pattern, bridge_dbus_recv, bind, handle)) {
        return SHREWD_LV_VIEW_ERR_ARG_INVALID;
    }
    return SHREWD_LV_VIEW_ERR_OK;
}
