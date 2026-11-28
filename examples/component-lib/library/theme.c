/**
 * @file theme.c
 * @brief 主题接线的实现。
 *
 * 要**定义**一个 `lv_theme_t` 得看得到它的结构,那是 lvgl 的私有头 ——
 * 和写控件一样属于 §7.7 的第三档。用它的人不需要。
 */
#include "demo/theme.h"

#include <string.h>

#include "demo/gauge.h"
#include "lvgl_private/core/lv_obj_private.h"
#include "lvgl_private/lvgl_public.h"
#include "lvgl_private/themes/lv_theme_private.h"

const demo_tokens_t demo_tokens_default = {
    .pad_small = 6,
    .pad_def = 12,
    .radius = 8,
    .border_width = 1,
    .card_height = 60,
    .gauge_height = 16,
};

demo_tokens_t demo_tokens;

typedef struct {
    lv_theme_t base;
    const demo_tokens_t *tokens;
    lv_style_t gauge;
} demo_theme_t;

/**
 * 主题的 apply:**只管自己控件的样子**,别的 widget 交给父主题。
 * LVGL 会先把父主题套上,再调我们这层。
 */
static void demo_theme_apply(lv_theme_t *th, lv_obj_t *obj) {
    demo_theme_t *t = (demo_theme_t *)th;
    if (lv_obj_check_type(obj, &demo_gauge_class)) {
        lv_obj_add_style(obj, &t->gauge, LV_PART_MAIN);
    }
}

int demo_theme_init(lv_display_t *disp, const demo_tokens_t *tokens) {
    if (NULL == disp) {
        return SHREWD_LV_VIEW_ERR_ARG_INVALID;
    }
    static demo_theme_t th; /* 一块屏一份,够用 */
    memset(&th, 0, sizeof(th));
    th.tokens = (NULL != tokens) ? tokens : &demo_tokens_default;
    demo_tokens = *th.tokens; /* 片段从同一份 token 取样式 */

    lv_style_init(&th.gauge);
    lv_style_set_height(&th.gauge, th.tokens->gauge_height);
    lv_style_set_radius(&th.gauge, th.tokens->radius);
    lv_style_set_border_width(&th.gauge, th.tokens->border_width);
    lv_style_set_pad_all(&th.gauge, th.tokens->pad_small);

    /* 父主题 = 这块屏当前的主题(通常是 LVGL 默认),只在其上加自己那层。 */
    lv_theme_set_parent(&th.base, lv_display_get_theme(disp));
    lv_theme_set_apply_cb(&th.base, demo_theme_apply);
    th.base.disp = disp;
    lv_display_set_theme(disp, &th.base);
    return SHREWD_LV_VIEW_ERR_OK;
}
