#include "net/net.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#define ETH_ARP 0x0806
#define ETH_IPV4 0x0800
#define IP_ICMP 1
#define IP_UDP 17
#define IP_TCP 6

static uint32_t fake_ticks;
static uint8_t rx_frame[NETDEV_FRAME_MAX];
static uint32_t rx_length;
static uint8_t tx_frame[NETDEV_FRAME_MAX];
static uint32_t tx_length;
static int rx_ready;
static int tcp_peer_enabled;
static int tcp_syn_seen;
static NetDevice fake_device;

static void inject_frame(const uint8_t* frame, uint32_t length);
static void ethernet_header(uint8_t* frame, const uint8_t destination[6], uint16_t type);

uint32_t pit_get_tick(void) {
    return fake_ticks++;
}

static uint16_t get16(const uint8_t* data) {
    return (uint16_t)(((uint16_t)data[0] << 8) | data[1]);
}

static uint32_t get32(const uint8_t* data) {
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) | data[3];
}

static void put16(uint8_t* data, uint16_t value) {
    data[0] = (uint8_t)(value >> 8);
    data[1] = (uint8_t)value;
}

static void put32(uint8_t* data, uint32_t value) {
    data[0] = (uint8_t)(value >> 24);
    data[1] = (uint8_t)(value >> 16);
    data[2] = (uint8_t)(value >> 8);
    data[3] = (uint8_t)value;
}

static uint16_t tcp_checksum(const uint8_t* segment, uint32_t length) {
    uint32_t sum = 0;
    sum += 0x0A00;
    sum += 0x0202;
    sum += 0x0A00;
    sum += 0x020F;
    sum += IP_TCP;
    sum += length;
    for (uint32_t i = 0; i + 1 < length; i += 2) sum += get16(segment + i);
    if (length & 1) sum += (uint32_t)segment[length - 1] << 8;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

static void inject_tcp_syn_ack(const uint8_t* outgoing) {
    uint8_t frame[14 + 20 + 20] = {0};
    static const uint8_t device_mac[6] = {0x52, 0x54, 0, 0x12, 0x34, 0x56};
    ethernet_header(frame, device_mac, ETH_IPV4);
    uint8_t* ip = frame + 14;
    ip[0] = 0x45;
    put16(ip + 2, sizeof(frame) - 14);
    put16(ip + 6, 0x4000);
    ip[8] = 64;
    ip[9] = IP_TCP;
    put32(ip + 12, NET_IPV4(10, 0, 2, 2));
    put32(ip + 16, NET_IPV4(10, 0, 2, 15));
    put16(ip + 10, net_checksum(ip, 20));
    const uint8_t* outgoing_tcp = outgoing + 14 + 20;
    uint8_t* tcp = ip + 20;
    put16(tcp, get16(outgoing_tcp + 2));
    put16(tcp + 2, get16(outgoing_tcp));
    put32(tcp + 4, 700);
    put32(tcp + 8, get32(outgoing_tcp + 4) + 1);
    tcp[12] = 5u << 4;
    tcp[13] = 0x12;
    put16(tcp + 14, 4096);
    put16(tcp + 16, tcp_checksum(tcp, 20));
    inject_frame(frame, sizeof(frame));
}

static int mock_transmit(NetDevice* device, const void* frame, uint32_t length) {
    (void)device;
    assert(length <= sizeof(tx_frame));
    const uint8_t* bytes = (const uint8_t*)frame;
    for (uint32_t i = 0; i < length; i++) tx_frame[i] = bytes[i];
    tx_length = length;
    if (tcp_peer_enabled && length >= 14 + 20 + 20 &&
        get16(tx_frame + 12) == ETH_IPV4 && tx_frame[14 + 9] == IP_TCP) {
        uint8_t flags = tx_frame[14 + 20 + 13];
        if ((flags & 0x12) == 0x02) {
            tcp_syn_seen = 1;
            inject_tcp_syn_ack(tx_frame);
        }
    }
    return 0;
}

static int mock_receive(NetDevice* device, void* frame, uint32_t capacity, uint32_t* length) {
    (void)device;
    if (!rx_ready) return 0;
    assert(rx_length <= capacity);
    uint8_t* out = (uint8_t*)frame;
    for (uint32_t i = 0; i < rx_length; i++) out[i] = rx_frame[i];
    *length = rx_length;
    rx_ready = 0;
    return 1;
}

static void inject_frame(const uint8_t* frame, uint32_t length) {
    assert(length <= sizeof(rx_frame));
    for (uint32_t i = 0; i < length; i++) rx_frame[i] = frame[i];
    rx_length = length;
    rx_ready = 1;
}

static void ethernet_header(uint8_t* frame, const uint8_t destination[6], uint16_t type) {
    for (uint32_t i = 0; i < 6; i++) {
        frame[i] = destination[i];
        frame[i + 6] = 0x10 + i;
    }
    put16(frame + 12, type);
}

static void test_checksum(void) {
    const uint8_t sample[] = {0x00, 0x01, 0xF2, 0x03, 0xF4, 0xF5, 0xF6, 0xF7};
    assert(net_checksum(sample, sizeof(sample)) == 0x220D);
    uint8_t checked[20] = {0x45, 0x00, 0x00, 0x14, 0, 0, 0x40, 0,
                           64, IP_ICMP, 0, 0, 10, 0, 2, 2, 10, 0, 2, 15};
    put16(checked + 10, net_checksum(checked, sizeof(checked)));
    assert(net_checksum(checked, sizeof(checked)) == 0);
}

static void test_arp_reply(void) {
    static const uint8_t broadcast[6] = {255, 255, 255, 255, 255, 255};
    uint8_t frame[42] = {0};
    ethernet_header(frame, broadcast, ETH_ARP);
    uint8_t* arp = frame + 14;
    put16(arp, 1);
    put16(arp + 2, ETH_IPV4);
    arp[4] = 6;
    arp[5] = 4;
    put16(arp + 6, 1);
    for (uint32_t i = 0; i < 6; i++) arp[8 + i] = 0x10 + i;
    put32(arp + 14, NET_IPV4(10, 0, 2, 2));
    put32(arp + 24, NET_IPV4(10, 0, 2, 15));
    inject_frame(frame, sizeof(frame));
    net_poll();
    assert(tx_length == 42);
    assert(get16(tx_frame + 20) == 2);
    assert(get32(tx_frame + 28) == NET_IPV4(10, 0, 2, 15));
    assert(get32(tx_frame + 38) == NET_IPV4(10, 0, 2, 2));
}

static void test_icmp_echo(void) {
    uint8_t frame[14 + 20 + 8 + 4] = {0};
    static const uint8_t device_mac[6] = {0x52, 0x54, 0, 0x12, 0x34, 0x56};
    ethernet_header(frame, device_mac, ETH_IPV4);
    uint8_t* ip = frame + 14;
    ip[0] = 0x45;
    put16(ip + 2, sizeof(frame) - 14);
    put16(ip + 6, 0x4000);
    ip[8] = 64;
    ip[9] = IP_ICMP;
    put32(ip + 12, NET_IPV4(10, 0, 2, 2));
    put32(ip + 16, NET_IPV4(10, 0, 2, 15));
    put16(ip + 10, net_checksum(ip, 20));
    uint8_t* icmp = ip + 20;
    icmp[0] = 8;
    put16(icmp + 4, 1);
    put16(icmp + 6, 2);
    put32(icmp + 8, 0x12345678);
    put16(icmp + 2, net_checksum(icmp, 12));
    inject_frame(frame, sizeof(frame));
    net_poll();
    assert(get16(tx_frame + 12) == ETH_IPV4);
    assert(tx_frame[34] == 0);
    assert(net_checksum(tx_frame + 34, 12) == 0);
}

static void test_udp_delivery(void) {
    uint8_t frame[14 + 20 + 8 + 5] = {0};
    static const uint8_t device_mac[6] = {0x52, 0x54, 0, 0x12, 0x34, 0x56};
    ethernet_header(frame, device_mac, ETH_IPV4);
    uint8_t* ip = frame + 14;
    ip[0] = 0x45;
    put16(ip + 2, sizeof(frame) - 14);
    put16(ip + 6, 0x4000);
    ip[8] = 64;
    ip[9] = IP_UDP;
    put32(ip + 12, NET_IPV4(10, 0, 2, 2));
    put32(ip + 16, NET_IPV4(10, 0, 2, 15));
    put16(ip + 10, net_checksum(ip, 20));
    uint8_t* udp = ip + 20;
    put16(udp, 12345);
    put16(udp + 2, 9000);
    put16(udp + 4, 13);
    udp[8] = 'n'; udp[9] = 'e'; udp[10] = 't'; udp[11] = '-'; udp[12] = 'o';
    inject_frame(frame, sizeof(frame));
    net_poll();

    char data[8] = {0};
    uint32_t length = 0;
    uint32_t source = 0;
    uint16_t source_port = 0;
    assert(net_udp_receive(9000, data, sizeof(data), &length, &source, &source_port) == 1);
    assert(length == 5 && data[0] == 'n' && data[4] == 'o');
    assert(source == NET_IPV4(10, 0, 2, 2) && source_port == 12345);
}

static void test_tcp_handshake(void) {
    tcp_peer_enabled = 1;
    tcp_syn_seen = 0;
    assert(net_tcp_connect(NET_IPV4(10, 0, 2, 2), 443, 1000) == 0);
    assert(tcp_syn_seen);
    assert(get16(tx_frame + 12) == ETH_IPV4);
    assert(tx_frame[14 + 20 + 13] == 0x10);
    net_tcp_close();
    tcp_peer_enabled = 0;
}

int main(void) {
    netdev_init();
    fake_device.name[0] = 'e'; fake_device.name[1] = 'n'; fake_device.name[2] = '0';
    fake_device.mtu = 1500;
    fake_device.mac[0] = 0x52; fake_device.mac[1] = 0x54;
    fake_device.mac[2] = 0; fake_device.mac[3] = 0x12;
    fake_device.mac[4] = 0x34; fake_device.mac[5] = 0x56;
    fake_device.transmit = mock_transmit;
    fake_device.receive = mock_receive;
    assert(netdev_register(&fake_device) == 0);
    net_init();

    test_checksum();
    test_arp_reply();
    test_icmp_echo();
    test_udp_delivery();
    test_tcp_handshake();
    puts("network protocol tests passed");
    return 0;
}
