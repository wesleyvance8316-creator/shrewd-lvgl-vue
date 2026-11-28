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
    lv_obj_t *spacer; /**< 虚拟化:撑出滚动范围,不是子项 */
    int applying;     /**< 重入护栏:物化会动容器,可能带回 scroll 事件 */
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
        int status = shrewd_basic_platform_realloc(&slot, b->slots, cap * sizeof(*b->slots));
        if (SHREWD_BASIC_ERR_OK != status) {
            return status;
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
    /*
     * **先把子项上的清理钩子摘掉**。LVGL 删容器时,容器与子项的 DELETE 先后
     * 不由我们控制;若容器这条先跑、子项那条后跑,`list_child_delete` 就会去读
     * 已经释放的绑定 —— use-after-free,后面是堆损坏(ASAN 实测)。
     * 摘钩子时子项还活着;若顺序反过来,子项已经把自己那格置空了,这里跳过。
     */
    for (size_t i = 0; i < b->count; ++i) {
        if (NULL != b->slots[i].child) {
            (void)lv_obj_remove_event_cb_with_user_data(b->slots[i].child, list_child_delete, b);
        }
    }
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

/* --- 虚拟化:只物化可视窗口(item_height > 0)---------------------- */

/**
 * 该物化的 `[first, last]`。
 *
 * 不虚拟化时就是全量;虚拟化时按滚动位置取窗口,上下各多探一行 —— 滚动时
 * 边缘上是 teardown + build,多探一行能少抖。
 */
static void list_window(shrewd_lv_view_list_binding_t *b,
                        const shrewd_lv_view_list_t *list,
                        uint32_t *first,
                        uint32_t *last) {
    uint32_t count = list->count;
    *first = 0;
    *last = (0 == count) ? 0 : (count - 1);
    if ((0 >= b->desc->item_height) || (0 == count)) {
        return;
    }
    /* 坐标是 layout 算出来的,先让它落地(无头环境尤其重要)。 */
    lv_obj_update_layout(b->container);
    int32_t ih = b->desc->item_height;
    /* 用 scroll_top(顶部被隐藏的高度)而不是 get_scroll_y —— 后者向下滚是负的。 */
    int32_t top = lv_obj_get_scroll_top(b->container);
    if (0 > top) {
        top = 0;
    }
    int32_t view = lv_obj_get_content_height(b->container);
    if (0 >= view) {
        view = lv_obj_get_height(b->container);
    }
    uint32_t f = (uint32_t)(top / ih);
    uint32_t l = (uint32_t)((top + view) / ih);
    if (0 < f) {
        f -= 1;
    }
    if (l + 1 < count) {
        l += 1;
    }
    if (l >= count) {
        l = count - 1;
    }
    if (f > l) {
        f = l;
    }
    *first = f;
    *last = l;
}

/* --- 对账:最小的那一种(design.md §10.4)-------------------------- */

/** key 是否落在 `[first, last]` 对应的那些项里。 */
static int list_key_in_window(shrewd_lv_view_list_binding_t *b,
                              const shrewd_lv_view_list_t *list,
                              uint32_t key,
                              uint32_t first,
                              uint32_t last) {
    for (uint32_t i = first; (i <= last) && (i < list->count); ++i) {
        if (b->desc->key(b->ctx, i) == key) {
            return 1;
        }
    }
    return 0;
}

static void list_reconcile(shrewd_lv_view_list_binding_t *b,
                           const shrewd_lv_view_list_t *list,
                           uint32_t first,
                           uint32_t last) {
    /* 1. 命中 → 复用,不重建;未命中 → build。
       `i < list->count` 是空列表的护栏:count 为 0 时 last 会落到 0。 */
    for (uint32_t i = first; (i <= last) && (i < list->count); ++i) {
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

    /* 2. 窗口里没有的 key → teardown + 删(不虚拟化时即"数据里没有")。 */
    for (size_t i = 0; i < b->count; ++i) {
        lv_obj_t *child = b->slots[i].child;
        if (NULL == child) {
            continue;
        }
        if (0 != list_key_in_window(b, list, b->slots[i].key, first, last)) {
            continue;
        }
        if (NULL != b->desc->teardown) {
            b->desc->teardown(b->ctx, b->slots[i].instance);
        }
        lv_obj_delete(child);
        slot_release(&b->slots[i], b->desc);
    }

    /* 3. 定位。不虚拟化:挪到目标索引,不重建(design.md §10.4);
     *    虚拟化:按行高摆到绝对位置 —— 顺序由坐标表达,不靠 child 顺序。 */
    for (uint32_t i = first; (i <= last) && (i < list->count); ++i) {
        shrewd_lv_view_list_slot_t *slot = list_find(b, b->desc->key(b->ctx, i));
        if ((NULL == slot) || (NULL == slot->child)) {
            continue;
        }
        if (0 < b->desc->item_height) {
            lv_obj_set_pos(slot->child, 0, (int32_t)i * b->desc->item_height);
            lv_obj_set_size(
                slot->child, lv_obj_get_content_width(b->container), b->desc->item_height);
        } else {
            lv_obj_move_to_index(slot->child, (int32_t)i);
        }
    }
}

static void list_apply(void *ctx) {
    shrewd_lv_view_list_binding_t *b = (shrewd_lv_view_list_binding_t *)ctx;
    if (0 != b->applying) {
        return; /* 物化会动容器,可能带回 scroll 事件 —— 别绕回来 */
    }
    b->applying = 1;
    const shrewd_lv_view_list_t *list
        = (const shrewd_lv_view_list_t *)shrewd_basic_reflect_ref_get(b->source);
    if (NULL != b->spacer) {
        lv_obj_set_size(b->spacer, 1, (int32_t)list->count * b->desc->item_height);
    }
    uint32_t first = 0;
    uint32_t last = 0;
    list_window(b, list, &first, &last);
    list_reconcile(b, list, first, last);
    b->applying = 0;
}

/** 滚动 → 窗口滑动 → 重新物化。虚拟化模式下才挂。 */
static void list_scroll(lv_event_t *e) {
    shrewd_lv_view_list_binding_t *b = (shrewd_lv_view_list_binding_t *)lv_event_get_user_data(e);
    list_apply(b);
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
    int status = shrewd_basic_platform_calloc(&slot, 1, sizeof(*b));
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    b = (shrewd_lv_view_list_binding_t *)slot;
    b->source = source;
    b->desc = desc;
    b->ctx = ctx;
    b->container = container;
    /* 虚拟化:item_height > 0。容器改成无布局 + 一根撑高度的 spacer,
       滚动事件驱动窗口滑动。 */
    if (0 < desc->item_height) {
        lv_obj_set_style_layout(container, LV_LAYOUT_NONE, 0);
        b->spacer = lv_obj_create(container);
        if (NULL == b->spacer) {
            void *self = b;
            (void)shrewd_basic_platform_free(&self);
            return SHREWD_LV_VIEW_ERR_MEM_NOMEM;
        }
        lv_obj_set_pos(b->spacer, 0, 0);
        lv_obj_set_size(b->spacer, 1, 0);
        lv_obj_set_style_bg_opa(b->spacer, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(b->spacer, 0, 0);
        lv_obj_set_style_pad_all(b->spacer, 0, 0);
        (void)lv_obj_add_event_cb(container, list_scroll, LV_EVENT_SCROLL, b);
    }

    /* 先挂清理再建 effect:effect_init 立刻跑一次,中途失败仍可自清。 */
    if (NULL == lv_obj_add_event_cb(container, list_binding_delete, LV_EVENT_DELETE, b)) {
        slot = b;
        (void)shrewd_basic_platform_free(&slot);
        return SHREWD_LV_VIEW_ERR_MEM_NOMEM;
    }
    status = shrewd_basic_reflect_effect_init(&b->effect, list_apply, b);
    if (SHREWD_BASIC_ERR_OK != status) {
        slot = b;
        (void)shrewd_basic_platform_free(&slot);
        return status;
    }
    return SHREWD_LV_VIEW_ERR_OK;
}
