#include "driver.h"
#include <stdio.h>
#include <str.h>
#include <stddef.h>

static Driver* g_drivers = NULL;
static int     g_driver_count = 0;
static CharDriverOps* g_primary_char = NULL;

void driver_subsystem_init(void) {
    g_drivers = NULL;
    g_driver_count = 0;
    g_primary_char = NULL;
}

int driver_register(Driver* drv) {
    if (!drv || !drv->name || !drv->init) return -1;

    for (Driver* d = g_drivers; d; d = d->next) {
        if (strcmp(d->name, drv->name) == 0) return -1;
    }

    drv->next = NULL;
    if (!g_drivers) {
        g_drivers = drv;
    } else {
        Driver* d = g_drivers;
        while (d->next) d = d->next;
        d->next = drv;
    }
    g_driver_count++;

    if (drv->type == DRIVER_TYPE_CHAR && !g_primary_char && drv->private_data) {
        g_primary_char = (CharDriverOps*)drv->private_data;
    }
    return 0;
}

int driver_unregister(Driver* drv) {
    if (!drv) return -1;

    Driver* prev = NULL;
    for (Driver* d = g_drivers; d; prev = d, d = d->next) {
        if (d != drv) continue;

        if (prev) prev->next = d->next;
        else      g_drivers = d->next;

        if (d->deinit) d->deinit(d);

        if (d->type == DRIVER_TYPE_CHAR && d->private_data == (void*)g_primary_char) {
            g_primary_char = NULL;
        }
        g_driver_count--;
        return 0;
    }
    return -1;
}

Driver* driver_find(const char* name) {
    if (!name) return NULL;
    for (Driver* d = g_drivers; d; d = d->next) {
        if (strcmp(d->name, name) == 0) return d;
    }
    return NULL;
}

void driver_init_all(void) {
    for (int level = 0; level <= DRIVER_INIT_LATE; level++) {
        for (Driver* d = g_drivers; d; d = d->next) {
            if (d->init_level != level) continue;
            int r = d->init(d);
            if (r != 0) {
                printf("[driver] init failed: %s (err %d)\n", d->name, r);
            }
        }
    }
}

void driver_list(void) {
    printf("Loaded drivers (%d):\n", g_driver_count);
    for (Driver* d = g_drivers; d; d = d->next) {
        printf("  %s (type=%d level=%d)\n", d->name, (int)d->type, (int)d->init_level);
    }
}

CharDriverOps* char_driver_get_primary(void) {
    return g_primary_char;
}