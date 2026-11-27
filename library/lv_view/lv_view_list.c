/**
 * @file lv_view_list.c
 * @brief 列表与条件渲染的对账原语(design.md §10)。
 *
 * **两种变化走两个机制**:
 *   - 内容变化(item 里的 ref 变了)→ 反应式,自动更新 widget;
 *   - 结构变化(增删 / 重排)      → 对账(source ref 触发)。
 *
 * 所以 `build` **只跑一次**:命中即复用、不重建 —— 若让它重跑会再挂一套绑定
 * (泄漏)。顺序用 `lv_obj_move_to_index` 挪,不重建,子项里编辑到一半的状态因此
 * 不丢。
 */

#include <string.h>

#include "shrewd-basic/platform/memory.h"
#include "shrewd-lv-view/view.h"

#define LIST_MIN_CAPACITY 8

typedef struct {
    uint32_t key;
    lv_obj_t *child;
    void *instance; /**< 框架按 desc->instance_size 分配,随子项释放 */
} shrewd_lv_view_list_slot_t;

typedef struct {
    shrewd_basic_reflect_effect_t effect;
    shrewd_basic_reflect_ref_t *source;
    const shrewd_lv_view_list_desc_t *desc;
    void *ctx;
    lv_obj_t *container;
    shrewd_lv_view_list_slot_t *slots;
    size_t count;
    size_t capacity;
} shrewd_lv_view_list_binding_t;

/* --- 槽位表 ---------------------------------------------------------- */

static shrewd_lv_view_list_slot_t *list_find(shrewd_lv_view_list_binding_t *b, uint32_t key) {
    for (size_t i = 0; i < b->count; ++i) {
        if ((NULL != b->slots[i].child) && (b->slots[i].key == key)) {
            return &b->slots[i];
        }
    }
    return NULL;
}

static void slot_release(shrewd_lv_view_list_slot_t *slot, const shrewd_lv_view_list_desc_t *desc) {
    if (NULL != slot->instance) {
        void *p = slot->instance;
        (void)shrewd_basic_platform_free(&p);
        slot->instance = NULL;
    }
    slot->child = NULL;
    (void)desc;
}

static int list_add(shrewd_lv_view_list_binding_t *b,
                    uint32_t key,
                    lv_obj_t *child,
                    void *instance) {
    if (b->count >= b->capacity) {
        size_t cap = (0 == b->capacity) ? LIST_MIN_CAPACITY : b->capacity * 2;
        void *slot = NULL;
        if (SHREWD_BASIC_ERR_OK
            != shrewd_basic_platform_realloc(&slot, b->slots, cap * sizeof(*b->slots))) {
            return SHREWD_LV_VIEW_ERR_MEM_NOMEM;
        }
        b->slots = (shrewd_lv_view_list_slot_t *)slot;
        b->capacity = cap;
    }
    b->slots[b->count].key = key;
    b->slots[b->count].child = child;
    b->slots[b->count].instance = instance;
    b->count += 1;
    return SHREWD_LV_VIEW_ERR_OK;
}

/* --- 清理 ------------------------------------------------------------- */

/**
 * 子项被外部删除:清掉槽位,免得下次对账拿到悬垂指针。
 *
 * user_data 是**绑定**,不是槽位指针 —— list_add 可能 realloc slots,
 * 存槽位地址会悬垂。按 widget 查槽位才是稳的。
 */
static void list_child_delete(lv_event_t *e) {
    shrewd_lv_view_list_binding_t *b = (shrewd_lv_view_list_binding_t *)lv_event_get_user_data(e);
    lv_obj_t *child = (lv_obj_t *)lv_event_get_current_target(e);
    for (size_t i = 0; i < b->count; ++i) {
        if (b->slots[i].child == child) {
            slot_release(&b->slots[i], b->desc);
        }
    }
}

/** 容器被删:整条绑定随它死(design.md §3)。 */
static void list_binding_delete(lv_event_t *e) {
    shrewd_lv_view_list_binding_t *b = (shrewd_lv_view_list_binding_t *)lv_event_get_user_data(e);
    shrewd_basic_reflect_effect_dispose(&b->effect);
    /* 子项随容器一起被 LVGL 删掉;这里释放框架持有的实例。 */
    for (size_t i = 0; i < b->count; ++i) {
        slot_release(&b->slots[i], b->desc);
    }
    void *scratch = b->slots;
    void *self = b;
    (void)shrewd_basic_platform_free(&scratch);
    (void)shrewd_basic_platform_free(&self);
}

/** 取第 i 项的数据指针。 */
static const void *list_item_at(shrewd_lv_view_list_binding_t *b,
                                const shrewd_lv_view_list_t *list,
                                uint32_t i) {
    if (NULL != b->desc->at) {
        return b->desc->at(b->ctx, i);
    }
    return (const char *)list->items + ((size_t)i * b->desc->item_size);
}

/* --- 对账:最小的那一种(design.md §10.4)-------------------------- */

static void list_reconcile(shrewd_lv_view_list_binding_t *b, const shrewd_lv_view_list_t *list) {
    /* 1. 命中 → 复用,不重建;未命中 → build。 */
    for (uint32_t i = 0; i < list->count; ++i) {
        uint32_t key = b->desc->key(b->ctx, i);
        if (NULL != list_find(b, key)) {
            continue; /* 复用:内容变化由 item 里的 ref 自动传播 */
        }
        if (NULL == b->desc->build) {
            return;
        }
        const void *item = list_item_at(b, list, i);
        /* 实例由框架分配:使用者不碰 calloc/free(§14.1 摩擦点 1)。 */
        void *instance = NULL;
        if (0 < b->desc->instance_size) {
            void *slot = NULL;
            if (SHREWD_BASIC_ERR_OK
                != shrewd_basic_platform_calloc(&slot, 1, b->desc->instance_size)) {
                return;
            }
            instance = slot;
        }
        lv_obj_t *child = b->desc->build(b->ctx, b->container, item, instance);
        if (NULL == child) {
            if (NULL != instance) {
                void *p = instance;
                (void)shrewd_basic_platform_free(&p);
            }
            return;
        }
        if (SHREWD_LV_VIEW_ERR_OK != list_add(b, key, child, instance)) {
            lv_obj_delete(child);
            if (NULL != instance) {
                void *p = instance;
                (void)shrewd_basic_platform_free(&p);
            }
            return;
        }
        /* 外部删它时钩子按 widget 清槽位(见 list_child_delete)。 */
        (void)lv_obj_add_event_cb(child, list_child_delete, LV_EVENT_DELETE, b);
    }

    /* 2. 数据里没有的 key → teardown + 删。 */
    for (size_t i = 0; i < b->count; ++i) {
        lv_obj_t *child = b->slots[i].child;
        if (NULL == child) {
            continue;
        }
        int keep = 0;
        for (uint32_t j = 0; j < list->count; ++j) {
            if (b->desc->key(b->ctx, j) == b->slots[i].key) {
                keep = 1;
                break;
            }
        }
        if (0 != keep) {
            continue;
        }
        if (NULL != b->desc->teardown) {
            b->desc->teardown(b->ctx, b->slots[i].instance);
        }
        lv_obj_delete(child);
        slot_release(&b->slots[i], b->desc);
    }

    /* 3. 顺序:挪到目标位置,不重建(design.md §10.4)。 */
    for (uint32_t i = 0; i < list->count; ++i) {
        shrewd_lv_view_list_slot_t *slot = list_find(b, b->desc->key(b->ctx, i));
        if ((NULL != slot) && (NULL != slot->child)) {
            lv_obj_move_to_index(slot->child, (int32_t)i);
        }
    }
}

static void list_apply(void *ctx) {
    shrewd_lv_view_list_binding_t *b = (shrewd_lv_view_list_binding_t *)ctx;
    const shrewd_lv_view_list_t *list
        = (const shrewd_lv_view_list_t *)shrewd_basic_reflect_ref_get(b->source);
    list_reconcile(b, list);
}

int shrewd_lv_view_bind_list(lv_obj_t *container,
                             shrewd_basic_reflect_ref_t *source,
                             const shrewd_lv_view_list_desc_t *desc,
                             void *ctx) {
    if ((NULL == container) || (NULL == source) || (NULL == desc) || (NULL == desc->key)) {
        return SHREWD_LV_VIEW_ERR_ARG_INVALID;
    }
    if (sizeof(shrewd_lv_view_list_t) != source->size) {
        return SHREWD_LV_VIEW_ERR_ARG_INVALID;
    }
    shrewd_lv_view_list_binding_t *b = NULL;
    void *slot = NULL;
    if (SHREWD_BASIC_ERR_OK != shrewd_basic_platform_calloc(&slot, 1, sizeof(*b))) {
        return SHREWD_LV_VIEW_ERR_MEM_NOMEM;
    }
    b = (shrewd_lv_view_list_binding_t *)slot;
    b->source = source;
    b->desc = desc;
    b->ctx = ctx;
    b->container = container;
    /* 先挂清理再建 effect:effect_init 立刻跑一次,中途失败仍可自清。 */
    if (NULL == lv_obj_add_event_cb(container, list_binding_delete, LV_EVENT_DELETE, b)) {
        slot = b;
        (void)shrewd_basic_platform_free(&slot);
        return SHREWD_LV_VIEW_ERR_MEM_NOMEM;
    }
    if (SHREWD_BASIC_ERR_OK != shrewd_basic_reflect_effect_init(&b->effect, list_apply, b)) {
        slot = b;
        (void)shrewd_basic_platform_free(&slot);
        return SHREWD_LV_VIEW_ERR_MEM_NOMEM;
    }
    return SHREWD_LV_VIEW_ERR_OK;
}
