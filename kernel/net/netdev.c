#include "netdev.h"
#include <stddef.h>
#include <str.h>

static NetDevice* g_devices;

void netdev_init(void) {
    g_devices = NULL;
}

int netdev_register(NetDevice* device) {
    if (!device || !device->name[0] || !device->transmit || !device->receive) return -1;
    if (netdev_find(device->name)) return -1;
    device->next = g_devices;
    g_devices = device;
    return 0;
}

NetDevice* netdev_find(const char* name) {
    if (!name) return NULL;
    for (NetDevice* device = g_devices; device; device = device->next) {
        if (strcmp(device->name, name) == 0) return device;
    }
    return NULL;
}

NetDevice* netdev_first(void) {
    return g_devices;
}

int netdev_send(NetDevice* device, const void* frame, uint32_t length) {
    if (!device || !device->transmit || !frame) return -1;
    return device->transmit(device, frame, length);
}

int netdev_receive(NetDevice* device, void* frame, uint32_t capacity, uint32_t* length) {
    if (!device || !device->receive || !frame || !length) return -1;
    return device->receive(device, frame, capacity, length);
}