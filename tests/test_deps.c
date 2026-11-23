/**
 * @file test_deps.c
 * @brief Dependency wiring: the packages shrewd-lv-view imports through
 *        find_package really are there - the headers compile against the
 *        installed layout and the symbols link. One smoke assertion per
 *        dependency.
 *
 * This is a build-system test, not a behavioural one, so it reaches past
 * the view.h facade on purpose: what it checks is that the imported
 * targets carry usable include paths and archives at all. Milestone 2
 * exercises the bindings themselves.
 */

#include <stdint.h>
#include <stdio.h>

/* "lvgl/lvgl.h", not "lvgl.h": only this spelling resolves both against
 * the installed package (include/lvgl/lvgl.h) and against an in-tree lvgl.
 * The bare "lvgl.h" is a source-tree shim that never gets installed. */
#include "lvgl/lvgl.h"
#include "shrewd-basic/reflect/react.h"

int main(void) {
    /* lvgl: a linked entry point, plus the version versions.lock pins. */
    lv_init();
    if ((9 != lv_version_major()) || (6 != lv_version_minor())) {
        printf("test_deps: FAILED lvgl version %d.%d (expected 9.6)\n",
               lv_version_major(),
               lv_version_minor());
        return 1;
    }

    /* shrewd-basic: a linked entry point, plus a value that survives a
     * round trip through a ref cell. */
    int32_t storage = 0;
    int32_t next = 42;
    shrewd_basic_reflect_ref_t ref;
    if (SHREWD_BASIC_ERR_OK != shrewd_basic_reflect_ref_init(&ref, &storage, sizeof(storage))) {
        printf("test_deps: FAILED ref_init\n");
        return 1;
    }
    if (SHREWD_BASIC_ERR_OK != shrewd_basic_reflect_ref_set(&ref, &next, sizeof(next))) {
        printf("test_deps: FAILED ref_set\n");
        return 1;
    }
    if (42 != storage) {
        printf("test_deps: FAILED ref round-trip (%d)\n", (int)storage);
        return 1;
    }
    if (42 != *(const int32_t *)shrewd_basic_reflect_ref_get(&ref)) {
        printf("test_deps: FAILED ref_get\n");
        return 1;
    }

    printf("test_deps: ok (lvgl %d.%d, shrewd-basic ref round-trip)\n",
           lv_version_major(),
           lv_version_minor());
    return 0;
}
