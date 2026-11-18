#ifndef NETDEV_H
#define NETDEV_H

#include <stdint.h>

#define NETDEV_NAME_MAX 16
#define NETDEV_FRAME_MAX 1518

typedef struct NetDevice NetDevice;

struct NetDevice {
    char name[NETDEV_NAME_MAX];
    uint8_t mac[6];
    uint16_t mtu;
    void* private_data;
    int (*transmit)(NetDevice* device, const void* frame, uint32_t length);
    int (*receive)(NetDevice* device, void* frame, uint32_t capacity, uint32_t* length);
    NetDevice* next;
};

void netdev_init(void);
int netdev_register(NetDevice* device);
NetDevice* netdev_find(const char* name);
NetDevice* netdev_first(void);
int netdev_send(NetDevice* device, const void* frame, uint32_t length);
int netdev_receive(NetDevice* device, void* frame, uint32_t capacity, uint32_t* length);

#endif