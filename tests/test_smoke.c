/**
 * @file test_smoke.c
 * @brief Skeleton smoke: the facade compiles and reports a version.
 */

#include <stdio.h>
#include <string.h>

#include "shrewd-lv-view/view.h"

int main(void) {
    if (0 != strcmp(shrewd_lv_view_version(), SHREWD_LV_VIEW_VERSION_STRING)) {
        printf("test_smoke: FAILED version\n");
        return 1;
    }
    printf("test_smoke: ok (%s)\n", shrewd_lv_view_version());
    return 0;
}
