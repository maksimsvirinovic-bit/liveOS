#ifndef NET_H
#define NET_H

#include <stdint.h>
#include "netdev.h"

#define NET_IPV4(a, b, c, d) \
    (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (uint32_t)(d))

typedef struct {
    uint32_t address;
    uint32_t netmask;
    uint32_t gateway;
    uint32_t dns;
    uint8_t mac[6];
    uint16_t mtu;
} NetInfo;

void net_init(void);
int net_get_info(NetInfo* info);
void net_poll(void);
int net_ping(uint32_t address, uint32_t timeout_ticks, uint32_t* elapsed_ticks);
int net_udp_send(uint32_t address, uint16_t source_port, uint16_t destination_port,
                 const void* data, uint32_t length);
int net_udp_receive(uint16_t local_port, void* data, uint32_t capacity,
                    uint32_t* length, uint32_t* source_address, uint16_t* source_port);
int net_tcp_connect(uint32_t address, uint16_t port, uint32_t timeout_ticks);
int net_tcp_send(const void* data, uint32_t length, uint32_t timeout_ticks);
int net_tcp_receive(void* data, uint32_t capacity, uint32_t* length,
                    uint32_t timeout_ticks);
void net_tcp_close(void);
uint16_t net_checksum(const void* data, uint32_t length);

#endif