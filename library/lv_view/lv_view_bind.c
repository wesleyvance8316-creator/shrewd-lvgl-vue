/**
 * @file lv_view_bind.c
 * @brief 绑定机制的单点实现(design.md §2 / §3 / §4)。
 *
 * 一条绑定 = 一个 effect:ref 变了自动写 widget;双向时 widget 变了自动
 * 写回 ref。三件事都只在这里实现一次,便捷函数只是薄壳:
 *
 *   - **applying 抑制**(§4):写 widget 期间置位,写回回调看它就退,
 *     否则 ref_set → effect → 写 widget → 事件 → 写回 会成环。
 *   - **DELETE 自清**(§3):绑定记录随 widget 删除而释放,使用者不记不解绑。
 *   - **读回要拷贝**(§5):widget 的读回多是 LVGL 自持指针,写进 ref 槽位
 *     必须拷贝,所以每个绑定带一块 scratch。
 */

#include <string.h>

#include "shrewd-basic/platform/memory.h"
#include "shrewd-lv-view/view.h"

typedef struct shrewd_lv_view_binding {
    shrewd_basic_reflect_effect_t effect;
    shrewd_basic_reflect_ref_t *ref;
    const shrewd_lv_view_target_t *target;
    void *obj;
    void *scratch; /**< ref->size bytes, for target->read() */
    int applying;
} shrewd_lv_view_binding_t;

/** ref → widget。由 effect 驱动,ref_set 时同步跑(design.md §2)。 */
static void binding_apply(void *ctx) {
    shrewd_lv_view_binding_t *b = (shrewd_lv_view_binding_t *)ctx;
    const void *value = shrewd_basic_reflect_ref_get(b->ref);
    b->applying = 1;
    b->target->write(b->obj, value, b->ref->size);
    b->applying = 0;
}

/** widget → ref。applying 期间退——那是本绑定在写 widget,不是用户改动。 */
static void binding_changed(lv_event_t *e) {
    shrewd_lv_view_binding_t *b = (shrewd_lv_view_binding_t *)lv_event_get_user_data(e);
    if ((0 != b->applying) || (NULL == b->target->read)) {
        return;
    }
    if (SHREWD_LV_VIEW_ERR_OK != b->target->read(b->obj, b->scratch, b->ref->size)) {
        return;
    }
    (void)shrewd_basic_reflect_ref_set(b->ref, b->scratch, b->ref->size);
}

/** widget 删除 → 解绑 + 释放。绑定随 widget 死(§3)。 */
static void binding_delete(lv_event_t *e) {
    shrewd_lv_view_binding_t *b = (shrewd_lv_view_binding_t *)lv_event_get_user_data(e);
    shrewd_basic_reflect_effect_dispose(&b->effect);
    void *scratch = b->scratch;
    void *self = b;
    (void)shrewd_basic_platform_free(&scratch);
    (void)shrewd_basic_platform_free(&self);
}

int shrewd_lv_view_bind(void *obj,
                        const shrewd_lv_view_target_t *target,
                        shrewd_basic_reflect_ref_t *ref) {
    if ((NULL == obj) || (NULL == target) || (NULL == target->write) || (NULL == ref)) {
        return SHREWD_LV_VIEW_ERR_ARG_INVALID;
    }
    if (0 == ref->size) {
        return SHREWD_LV_VIEW_ERR_ARG_INVALID;
    }
    shrewd_lv_view_binding_t *b = NULL;
    void *slot = NULL;
    int status = shrewd_basic_platform_calloc(&slot, 1, sizeof(*b));
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    b = (shrewd_lv_view_binding_t *)slot;
    b->ref = ref;
    b->target = target;
    b->obj = obj;
    status = shrewd_basic_platform_alloc(&b->scratch, ref->size);
    if (SHREWD_BASIC_ERR_OK != status) {
        (void)shrewd_basic_platform_free(&slot);
        return status;
    }
    /* 先挂清理再建 effect:effect_init 立刻跑一次,若中途失败仍可自清。 */
    if (NULL == lv_obj_add_event_cb((lv_obj_t *)obj, binding_delete, LV_EVENT_DELETE, b)) {
        (void)shrewd_basic_platform_free(&b->scratch);
        (void)shrewd_basic_platform_free(&slot);
        return SHREWD_LV_VIEW_ERR_MEM_NOMEM;
    }
    status = shrewd_basic_reflect_effect_init(&b->effect, binding_apply, b);
    if (SHREWD_BASIC_ERR_OK != status) {
        (void)shrewd_basic_platform_free(&b->scratch);
        slot = b;
        (void)shrewd_basic_platform_free(&slot);
        return status;
    }
    if (NULL != target->read) {
        (void)lv_obj_add_event_cb((lv_obj_t *)obj, binding_changed, target->changed_event, b);
    }
    return SHREWD_LV_VIEW_ERR_OK;
}

/* --- 便捷绑定:每个 widget/属性一张 target 表(design.md §6)---------- */

static void label_text_write(void *obj, const void *value, size_t size) {
    (void)size;
    lv_label_set_text((lv_obj_t *)obj, (const char *)value);
}

static const shrewd_lv_view_target_t g_label_text = {
    .write = label_text_write,
    .read = NULL,
    .changed_event = 0,
};

int shrewd_lv_view_bind_label_text(lv_obj_t *obj, shrewd_basic_reflect_ref_t *ref) {
    return shrewd_lv_view_bind(obj, &g_label_text, ref);
}

static void slider_value_write(void *obj, const void *value, size_t size) {
    (void)size;
    lv_slider_set_value((lv_obj_t *)obj, *(const int32_t *)value, LV_ANIM_OFF);
}

static int slider_value_read(void *obj, void *out, size_t size) {
    if (sizeof(int32_t) != size) {
        return SHREWD_LV_VIEW_ERR_ARG_INVALID;
    }
    *(int32_t *)out = lv_slider_get_value((const lv_obj_t *)obj);
    return SHREWD_LV_VIEW_ERR_OK;
}

static const shrewd_lv_view_target_t g_slider_value = {
    .write = slider_value_write,
    .read = slider_value_read,
    .changed_event = LV_EVENT_VALUE_CHANGED,
};

int shrewd_lv_view_bind_slider_value(lv_obj_t *obj, shrewd_basic_reflect_ref_t *ref) {
    return shrewd_lv_view_bind(obj, &g_slider_value, ref);
}

static void textarea_text_write(void *obj, const void *value, size_t size) {
    (void)size;
    lv_textarea_set_text((lv_obj_t *)obj, (const char *)value);
}

static int textarea_text_read(void *obj, void *out, size_t size) {
    const char *text = lv_textarea_get_text((const lv_obj_t *)obj);
    if (NULL == text) {
        return SHREWD_LV_VIEW_ERR_ARG_INVALID;
    }
    /* LVGL 自持指针:必须拷贝进槽位(§5)。 */
    (void)memset(out, 0, size);
    (void)strncpy((char *)out, text, size - 1);
    return SHREWD_LV_VIEW_ERR_OK;
}

static const shrewd_lv_view_target_t g_textarea_text = {
    .write = textarea_text_write,
    .read = textarea_text_read,
    .changed_event = LV_EVENT_VALUE_CHANGED,
};

int shrewd_lv_view_bind_textarea_text(lv_obj_t *obj, shrewd_basic_reflect_ref_t *ref) {
    return shrewd_lv_view_bind(obj, &g_textarea_text, ref);
}
