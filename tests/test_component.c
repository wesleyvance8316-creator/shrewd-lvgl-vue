/**
 * @file test_component.c
 * @brief 组件模型的单测(design.md §7.2)。
 *
 * 验三件事:状态是真相(widget 只是投影)、双向绑定、销毁级联自清。
 */
#include <stdio.h>
#include <string.h>

#include "demo_card.h"
#include "lvgl_fixture.h"

static int g_fail;

static void check(int ok, const char *what) {
    if (!ok) {
        printf("  FAIL %s\n", what);
        g_fail += 1;
    }
}

static lv_obj_t *card_label(demo_card_t *c) {
    return lv_obj_get_child(c->root, 0);
}

static lv_obj_t *card_slider(demo_card_t *c) {
    return lv_obj_get_child(c->root, 1);
}

/* --- 1. 创建:参数落到状态,状态落到 widget -------------------------- */

static void test_create(void) {
    printf("test_create\n");
    demo_card_params_t params = {.title = "hello", .level = 40};
    demo_card_t card;

    check(SHREWD_LV_VIEW_ERR_OK == demo_card_create(&card, lv_screen_active(), &params), "create");
    check(NULL != card.root, "root is set");
    check(0 == strcmp("hello", card.title_buf), "params land in state");
    check(40 == card.level_val, "params land in state (level)");

    lv_obj_t *label = card_label(&card);
    lv_obj_t *slider = card_slider(&card);
    check(0 == strcmp("hello", lv_label_get_text(label)), "state projects onto label");
    check(40 == lv_slider_get_value(slider), "state projects onto slider");
}

/* --- 2. 状态是真相:改状态 → widget 跟着变 -------------------------- */

static void test_state_is_source(void) {
    printf("test_state_is_source\n");
    demo_card_t card;
    check(SHREWD_LV_VIEW_ERR_OK == demo_card_create(&card, lv_screen_active(), NULL), "create");

    (void)strncpy(card.title_buf, "changed", sizeof(card.title_buf) - 1);
    check(SHREWD_BASIC_ERR_OK
              == shrewd_basic_reflect_ref_set(&card.title, card.title_buf, sizeof(card.title_buf)),
          "ref_set title");
    check(0 == strcmp("changed", lv_label_get_text(card_label(&card))), "label follows state");

    card.level_val = 90;
    check(SHREWD_BASIC_ERR_OK
              == shrewd_basic_reflect_ref_set(&card.level, &card.level_val, sizeof(card.level_val)),
          "ref_set level");
    check(90 == lv_slider_get_value(card_slider(&card)), "slider follows state");
}

/* --- 3. 双向:改 widget → 状态跟着变 --------------------------------- */

static void test_widget_writes_back(void) {
    printf("test_widget_writes_back\n");
    demo_card_t card;
    check(SHREWD_LV_VIEW_ERR_OK == demo_card_create(&card, lv_screen_active(), NULL), "create");

    lv_slider_set_value(card_slider(&card), 12, LV_ANIM_OFF);
    (void)lv_obj_send_event(card_slider(&card), LV_EVENT_VALUE_CHANGED, NULL);
    check(12 == card.level_val, "slider writes back to state");
}

/* --- 4. 销毁:绑定级联自清 ------------------------------------------ */

static void test_dispose_cascades(void) {
    printf("test_dispose_cascades\n");
    demo_card_t card;
    check(SHREWD_LV_VIEW_ERR_OK == demo_card_create(&card, lv_screen_active(), NULL), "create");
    demo_card_dispose(&card);
    check(NULL == card.root, "root cleared");

    /* 绑定已随 widget 自清:再改状态不得触碰已删除的 widget。 */
    (void)strncpy(card.title_buf, "after-dispose", sizeof(card.title_buf) - 1);
    check(SHREWD_BASIC_ERR_OK
              == shrewd_basic_reflect_ref_set(&card.title, card.title_buf, sizeof(card.title_buf)),
          "ref_set after dispose");
}

int main(void) {
    lvgl_fixture_init();
    test_create();
    test_state_is_source();
    test_widget_writes_back();
    test_dispose_cascades();
    if (0 == g_fail) {
        printf("test_component: ok\n");
        return 0;
    }
    printf("test_component: FAILED (%d)\n", g_fail);
    return 1;
}
