/**
 * @file device.c
 */
#include "stores/device.h"

#include <stdio.h>
#include <string.h>

const shrewd_basic_reflect_type_t prototype_i32_type = {
    .sb_refl_kind = SHREWD_BASIC_REFLECT_KIND_INT32,
    .sb_refl_name = "int32",
    .sb_refl_size = sizeof(int32_t),
};

void device_init(device_t *d, int32_t id, const char *name, int32_t battery) {
    memset(d, 0, sizeof(*d));
    d->id = id;
    (void)shrewd_basic_reflect_ref_init(&d->name, d->name_buf, sizeof(d->name_buf));
    (void)shrewd_basic_reflect_ref_init(&d->battery, &d->battery_val, sizeof(d->battery_val));
    (void)snprintf(d->name_buf, sizeof(d->name_buf), "%s", name);
    d->battery_val = battery;
}
