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

    check(SHREWD_LV_VIEW_ERR_OK == demo_card_create(&card, lv_screen_active(), &params, NULL),
          "create");
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
    check(SHREWD_LV_VIEW_ERR_OK == demo_card_create(&card, lv_screen_active(), NULL, NULL),
          "create");

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
    check(SHREWD_LV_VIEW_ERR_OK == demo_card_create(&card, lv_screen_active(), NULL, NULL),
          "create");

    lv_slider_set_value(card_slider(&card), 12, LV_ANIM_OFF);
    (void)lv_obj_send_event(card_slider(&card), LV_EVENT_VALUE_CHANGED, NULL);
    check(12 == card.level_val, "slider writes back to state");
}

/* --- 4. 销毁:绑定级联自清 ------------------------------------------ */

static void test_dispose_cascades(void) {
    printf("test_dispose_cascades\n");
    demo_card_t card;
    check(SHREWD_LV_VIEW_ERR_OK == demo_card_create(&card, lv_screen_active(), NULL, NULL),
          "create");
    demo_card_dispose(&card);
    check(NULL == card.root, "root cleared");

    /* 绑定已随 widget 自清:再改状态不得触碰已删除的 widget。 */
    (void)strncpy(card.title_buf, "after-dispose", sizeof(card.title_buf) - 1);
    check(SHREWD_BASIC_ERR_OK
              == shrewd_basic_reflect_ref_set(&card.title, card.title_buf, sizeof(card.title_buf)),
          "ref_set after dispose");
}

/* --- 5. 槽:父给内容,子决定何时何处调用(§7.5)---------------------- */

static int g_slot_calls;
static lv_obj_t *g_slot_container;

static void build_footer(lv_obj_t *slot, void *ctx) {
    g_slot_calls += 1;
    g_slot_container = slot;
    lv_obj_t *label = lv_label_create(slot);
    lv_label_set_text(label, (const char *)ctx);
}

static void test_slots(void) {
    printf("test_slots\n");
    demo_card_t card;
    g_slot_calls = 0;
    g_slot_container = NULL;
    static const demo_card_slots_t slots = {
        .footer = build_footer,
        .footer_ctx = (void *)"slot-content",
    };
    check(SHREWD_LV_VIEW_ERR_OK == demo_card_create(&card, lv_screen_active(), NULL, &slots),
          "create with slots");
    check(1 == g_slot_calls, "slot built exactly once");
    check(NULL != g_slot_container, "slot got a container");
    check(1 == lv_obj_get_child_count(g_slot_container), "slot content is the container's child");

    /* 槽内容挂在组件的 widget 下 → 它的绑定随组件销毁级联自清(§3)。 */
    demo_card_dispose(&card);
    check(1 == g_slot_calls, "dispose does not re-run the slot");
}

/* --- 6. 组件级 v-model:传 ref 即双向(§7.4)------------------------- */

static void test_vmodel(void) {
    printf("test_vmodel\n");
    int32_t external = 10;
    shrewd_basic_reflect_ref_t ext_ref;
    check(SHREWD_BASIC_ERR_OK
              == shrewd_basic_reflect_ref_init(&ext_ref, &external, sizeof(external)),
          "ref_init");

    demo_card_params_t params = {
        .title = "vm",
        .level = 0,
        .level_ref = &ext_ref,
    };
    demo_card_t card;
    check(SHREWD_LV_VIEW_ERR_OK == demo_card_create(&card, lv_screen_active(), &params, NULL),
          "create with an external ref");
    check(10 == lv_slider_get_value(card_slider(&card)), "external ref drives the slider");

    /* 父改数据 → 组件跟着变:父没写"更新" */
    external = 55;
    check(SHREWD_BASIC_ERR_OK
              == shrewd_basic_reflect_ref_set(&ext_ref, &external, sizeof(external)),
          "ref_set");
    check(55 == lv_slider_get_value(card_slider(&card)), "slider follows the ref");

    /* 组件改 UI → 数据跟着变:父没听"事件" */
    lv_slider_set_value(card_slider(&card), 20, LV_ANIM_OFF);
    (void)lv_obj_send_event(card_slider(&card), LV_EVENT_VALUE_CHANGED, NULL);
    check(20 == external, "slider writes back through the ref");
}

/* --- 7. 设计 token:片段暴露可覆盖的 style 常量表(§7.6)------------- */

static void test_design_tokens(void) {
    printf("test_design_tokens\n");
    demo_card_style.height = 80;
    demo_card_t card;
    check(SHREWD_LV_VIEW_ERR_OK == demo_card_create(&card, lv_screen_active(), NULL, NULL),
          "create");
    check(80 == lv_obj_get_style_height(card.root, LV_PART_MAIN),
          "design token applied to the root");
    demo_card_dispose(&card);
    demo_card_style.height = 60; /* 还原默认 */
}

int main(void) {
    lvgl_fixture_init();
    test_create();
    test_state_is_source();
    test_widget_writes_back();
    test_dispose_cascades();
    test_slots();
    test_vmodel();
    test_design_tokens();
    if (0 == g_fail) {
        printf("test_component: ok\n");
        return 0;
    }
    printf("test_component: FAILED (%d)\n", g_fail);
    return 1;
}
