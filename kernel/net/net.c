#include "net.h"
#include <pit/pit.h>
#include <stddef.h>
#include <str.h>

#define ETH_TYPE_IPV4 0x0800
#define ETH_TYPE_ARP 0x0806
#define IP_PROTOCOL_ICMP 1
#define IP_PROTOCOL_TCP 6
#define IP_PROTOCOL_UDP 17
#define ARP_CACHE_SIZE 8
#define UDP_QUEUE_SIZE 8
#define NET_PACKET_MAX 1500
#define NET_PING_ID 0x4C4F
#define TCP_RX_CAPACITY 4096
#define TCP_FLAG_FIN 0x01
#define TCP_FLAG_SYN 0x02
#define TCP_FLAG_RST 0x04
#define TCP_FLAG_PSH 0x08
#define TCP_FLAG_ACK 0x10
#define TCP_STATE_CLOSED 0
#define TCP_STATE_SYN_SENT 1
#define TCP_STATE_ESTABLISHED 2

typedef struct __attribute__((packed)) {
    uint8_t destination[6];
    uint8_t source[6];
    uint16_t type;
} EthernetHeader;

typedef struct __attribute__((packed)) {
    uint16_t hardware_type;
    uint16_t protocol_type;
    uint8_t hardware_size;
    uint8_t protocol_size;
    uint16_t operation;
    uint8_t sender_mac[6];
    uint32_t sender_ip;
    uint8_t target_mac[6];
    uint32_t target_ip;
} ArpPacket;

typedef struct __attribute__((packed)) {
    uint8_t version_ihl;
    uint8_t tos;
    uint16_t total_length;
    uint16_t identification;
    uint16_t fragment;
    uint8_t ttl;
    uint8_t protocol;
    uint16_t checksum;
    uint32_t source;
    uint32_t destination;
} Ipv4Header;

typedef struct __attribute__((packed)) {
    uint8_t type;
    uint8_t code;
    uint16_t checksum;
    uint16_t identifier;
    uint16_t sequence;
} IcmpHeader;

typedef struct __attribute__((packed)) {
    uint16_t source_port;
    uint16_t destination_port;
    uint16_t length;
    uint16_t checksum;
} UdpHeader;

typedef struct __attribute__((packed)) {
    uint16_t source_port;
    uint16_t destination_port;
    uint32_t sequence;
    uint32_t acknowledgement;
    uint8_t data_offset;
    uint8_t flags;
    uint16_t window;
    uint16_t checksum;
    uint16_t urgent;
} TcpHeader;

typedef struct {
    uint32_t address;
    uint8_t mac[6];
    uint32_t updated;
    int valid;
} ArpCacheEntry;

typedef struct {
    uint32_t source_address;
    uint16_t source_port;
    uint16_t destination_port;
    uint16_t length;
    uint8_t data[NET_PACKET_MAX];
} UdpDatagram;

static NetDevice* g_device;
static NetInfo g_info;
static ArpCacheEntry g_arp[ARP_CACHE_SIZE];
static UdpDatagram g_udp_queue[UDP_QUEUE_SIZE];
static uint32_t g_udp_head;
static uint32_t g_udp_count;
static uint16_t g_ip_id;
static uint16_t g_ping_sequence;
static uint16_t g_pending_ping_sequence;
static uint32_t g_pending_ping_address;
static uint32_t g_pending_ping_tick;
static int g_ping_pending;
static int g_ping_done;
static struct {
    uint32_t remote_address;
    uint32_t send_sequence;
    uint32_t receive_sequence;
    uint32_t last_acknowledged;
    uint32_t initial_sequence;
    uint16_t local_port;
    uint16_t remote_port;
    uint8_t state;
    uint8_t receive_data[TCP_RX_CAPACITY];
    uint32_t receive_length;
} g_tcp;

static void zero_bytes(void* memory, uint32_t size) {
    uint8_t* bytes = (uint8_t*)memory;
    for (uint32_t i = 0; i < size; i++) bytes[i] = 0;
}

static uint16_t read_be16(const void* pointer) {
    const uint8_t* bytes = (const uint8_t*)pointer;
    return (uint16_t)(((uint16_t)bytes[0] << 8) | bytes[1]);
}

static uint32_t read_be32(const void* pointer) {
    const uint8_t* bytes = (const uint8_t*)pointer;
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) | bytes[3];
}

static void write_be16(void* pointer, uint16_t value) {
    uint8_t* bytes = (uint8_t*)pointer;
    bytes[0] = (uint8_t)(value >> 8);
    bytes[1] = (uint8_t)value;
}

static void write_be32(void* pointer, uint32_t value) {
    uint8_t* bytes = (uint8_t*)pointer;
    bytes[0] = (uint8_t)(value >> 24);
    bytes[1] = (uint8_t)(value >> 16);
    bytes[2] = (uint8_t)(value >> 8);
    bytes[3] = (uint8_t)value;
}

uint16_t net_checksum(const void* data, uint32_t length) {
    const uint8_t* bytes = (const uint8_t*)data;
    uint32_t sum = 0;
    while (length >= 2) {
        sum += ((uint32_t)bytes[0] << 8) | bytes[1];
        bytes += 2;
        length -= 2;
    }
    if (length) sum += (uint32_t)bytes[0] << 8;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

static int mac_equal(const uint8_t left[6], const uint8_t right[6]) {
    for (uint32_t i = 0; i < 6; i++) if (left[i] != right[i]) return 0;
    return 1;
}

static void mac_copy(uint8_t destination[6], const uint8_t source[6]) {
    for (uint32_t i = 0; i < 6; i++) destination[i] = source[i];
}

static void arp_update(uint32_t address, const uint8_t mac[6]) {
    uint32_t slot = 0;
    for (uint32_t i = 0; i < ARP_CACHE_SIZE; i++) {
        if (g_arp[i].valid && g_arp[i].address == address) {
            slot = i;
            goto update;
        }
        if (!g_arp[i].valid) slot = i;
    }
    for (uint32_t i = 0; i < ARP_CACHE_SIZE; i++) {
        if (g_arp[i].updated < g_arp[slot].updated) slot = i;
    }
update:
    g_arp[slot].address = address;
    mac_copy(g_arp[slot].mac, mac);
    g_arp[slot].updated = pit_get_tick();
    g_arp[slot].valid = 1;
}

static int arp_lookup(uint32_t address, uint8_t mac[6]) {
    for (uint32_t i = 0; i < ARP_CACHE_SIZE; i++) {
        if (!g_arp[i].valid || g_arp[i].address != address) continue;
        if ((uint32_t)(pit_get_tick() - g_arp[i].updated) > 60000) {
            g_arp[i].valid = 0;
            return 0;
        }
        mac_copy(mac, g_arp[i].mac);
        return 1;
    }
    return 0;
}

static int send_ethernet(uint16_t type, const uint8_t destination[6],
                         const void* payload, uint32_t length) {
    uint8_t frame[NETDEV_FRAME_MAX];
    if (!g_device || length + sizeof(EthernetHeader) > sizeof(frame)) return -1;
    EthernetHeader* header = (EthernetHeader*)frame;
    mac_copy(header->destination, destination);
    mac_copy(header->source, g_info.mac);
    write_be16(&header->type, type);
    const uint8_t* source = (const uint8_t*)payload;
    for (uint32_t i = 0; i < length; i++) frame[sizeof(*header) + i] = source[i];
    return netdev_send(g_device, frame, sizeof(*header) + length);
}

static int send_arp_request(uint32_t target_ip) {
    static const uint8_t broadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    ArpPacket packet;
    packet.hardware_type = 0;
    packet.protocol_type = 0;
    packet.hardware_size = 6;
    packet.protocol_size = 4;
    write_be16(&packet.hardware_type, 1);
    write_be16(&packet.protocol_type, ETH_TYPE_IPV4);
    write_be16(&packet.operation, 1);
    mac_copy(packet.sender_mac, g_info.mac);
    write_be32(&packet.sender_ip, g_info.address);
    zero_bytes(packet.target_mac, sizeof(packet.target_mac));
    write_be32(&packet.target_ip, target_ip);
    return send_ethernet(ETH_TYPE_ARP, broadcast, &packet, sizeof(packet));
}

static int resolve_mac(uint32_t address, uint8_t mac[6]) {
    static const uint8_t broadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    if (address == 0xFFFFFFFFu ||
        (((address & g_info.netmask) == (g_info.address & g_info.netmask)) &&
         ((address | g_info.netmask) == 0xFFFFFFFFu))) {
        mac_copy(mac, broadcast);
        return 0;
    }
    if (address == g_info.address) {
        mac_copy(mac, g_info.mac);
        return 0;
    }
    if (arp_lookup(address, mac)) return 0;
    send_arp_request(address);
    uint32_t start = pit_get_tick();
    while ((uint32_t)(pit_get_tick() - start) < 1000) {
        net_poll();
        if (arp_lookup(address, mac)) return 0;
        __asm__ volatile("sti; hlt; cli" ::: "memory");
    }
    return -1;
}

static int send_ipv4(uint32_t destination, uint8_t protocol, const void* payload,
                     uint32_t payload_length) {
    uint8_t destination_mac[6];
    uint32_t next_hop = (destination & g_info.netmask) == (g_info.address & g_info.netmask)
        ? destination : g_info.gateway;
    if (!next_hop || resolve_mac(next_hop, destination_mac) != 0) return -1;
    if (payload_length + sizeof(Ipv4Header) > g_device->mtu) return -1;

    uint8_t packet[NET_PACKET_MAX];
    Ipv4Header* header = (Ipv4Header*)packet;
    header->version_ihl = 0x45;
    header->tos = 0;
    write_be16(&header->total_length, (uint16_t)(sizeof(*header) + payload_length));
    write_be16(&header->identification, ++g_ip_id);
    write_be16(&header->fragment, 0x4000);
    header->ttl = 64;
    header->protocol = protocol;
    header->checksum = 0;
    write_be32(&header->source, g_info.address);
    write_be32(&header->destination, destination);
    write_be16(&header->checksum, net_checksum(header, sizeof(*header)));
    const uint8_t* source = (const uint8_t*)payload;
    for (uint32_t i = 0; i < payload_length; i++) packet[sizeof(*header) + i] = source[i];
    return send_ethernet(ETH_TYPE_IPV4, destination_mac, packet,
                         sizeof(*header) + payload_length);
}

static uint16_t tcp_checksum(uint32_t source, uint32_t destination,
                             const void* segment, uint32_t length) {
    uint32_t sum = 0;
    sum += (source >> 16) & 0xFFFF;
    sum += source & 0xFFFF;
    sum += (destination >> 16) & 0xFFFF;
    sum += destination & 0xFFFF;
    sum += IP_PROTOCOL_TCP;
    sum += length;
    const uint8_t* bytes = (const uint8_t*)segment;
    uint32_t remaining = length;
    while (remaining >= 2) {
        sum += ((uint32_t)bytes[0] << 8) | bytes[1];
        bytes += 2;
        remaining -= 2;
    }
    if (remaining) sum += (uint32_t)bytes[0] << 8;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

static int tcp_send_segment(uint8_t flags, const void* data, uint32_t length) {
    uint8_t segment[NET_PACKET_MAX];
    if (length + sizeof(TcpHeader) > sizeof(segment)) return -1;
    TcpHeader* header = (TcpHeader*)segment;
    write_be16(&header->source_port, g_tcp.local_port);
    write_be16(&header->destination_port, g_tcp.remote_port);
    write_be32(&header->sequence, g_tcp.send_sequence);
    write_be32(&header->acknowledgement, g_tcp.receive_sequence);
    header->data_offset = 5u << 4;
    header->flags = flags;
    write_be16(&header->window, TCP_RX_CAPACITY - g_tcp.receive_length);
    header->checksum = 0;
    header->urgent = 0;
    const uint8_t* bytes = (const uint8_t*)data;
    for (uint32_t i = 0; i < length; i++) segment[sizeof(*header) + i] = bytes[i];
    write_be16(&header->checksum,
               tcp_checksum(g_info.address, g_tcp.remote_address,
                            segment, sizeof(*header) + length));
    if (send_ipv4(g_tcp.remote_address, IP_PROTOCOL_TCP, segment,
                  sizeof(*header) + length) != 0) return -1;
    if (flags & TCP_FLAG_SYN) g_tcp.send_sequence++;
    g_tcp.send_sequence += length;
    if (flags & TCP_FLAG_FIN) g_tcp.send_sequence++;
    return 0;
}

int net_tcp_connect(uint32_t address, uint16_t port, uint32_t timeout_ticks) {
    if (!g_device || !address || !port || !timeout_ticks || g_tcp.state != TCP_STATE_CLOSED) return -1;
    zero_bytes(&g_tcp, sizeof(g_tcp));
    g_tcp.remote_address = address;
    g_tcp.remote_port = port;
    g_tcp.local_port = (uint16_t)(49152 + (pit_get_tick() % 16384));
    g_tcp.send_sequence = (pit_get_tick() << 12) ^ address ^ port;
    g_tcp.initial_sequence = g_tcp.send_sequence;
    g_tcp.state = TCP_STATE_SYN_SENT;

    uint32_t start = pit_get_tick();
    uint32_t retry_at = start;
    uint32_t retries = 0;
    while ((uint32_t)(pit_get_tick() - start) < timeout_ticks) {
        if (retries == 0 || (uint32_t)(pit_get_tick() - retry_at) >= 500) {
            g_tcp.send_sequence = g_tcp.initial_sequence;
            if (tcp_send_segment(TCP_FLAG_SYN, NULL, 0) != 0) return -1;
            retries++;
            retry_at = pit_get_tick();
            if (retries > 4) break;
        }
        net_poll();
        if (g_tcp.state == TCP_STATE_ESTABLISHED) return 0;
        __asm__ volatile("sti; hlt; cli" ::: "memory");
    }
    g_tcp.state = TCP_STATE_CLOSED;
    return -1;
}

int net_tcp_send(const void* data, uint32_t length, uint32_t timeout_ticks) {
    if (!data || !length || length > g_device->mtu - sizeof(Ipv4Header) - sizeof(TcpHeader) ||
        g_tcp.state != TCP_STATE_ESTABLISHED) return -1;
    uint32_t sequence = g_tcp.send_sequence;
    uint32_t start = pit_get_tick();
    for (uint32_t attempt = 0; attempt < 4; attempt++) {
        g_tcp.send_sequence = sequence;
        if (tcp_send_segment(TCP_FLAG_ACK | TCP_FLAG_PSH, data, length) != 0) return -1;
        uint32_t retry_start = pit_get_tick();
        while ((uint32_t)(pit_get_tick() - retry_start) < 500 &&
               (uint32_t)(pit_get_tick() - start) < timeout_ticks) {
            net_poll();
            if (g_tcp.last_acknowledged >= sequence + length) return (int)length;
            __asm__ volatile("sti; hlt; cli" ::: "memory");
        }
    }
    return -1;
}

int net_tcp_receive(void* data, uint32_t capacity, uint32_t* length,
                    uint32_t timeout_ticks) {
    if (!data || !length || !capacity || g_tcp.state != TCP_STATE_ESTABLISHED) return -1;
    uint32_t start = pit_get_tick();
    while (!g_tcp.receive_length && (uint32_t)(pit_get_tick() - start) < timeout_ticks) {
        net_poll();
        if (!g_tcp.receive_length) __asm__ volatile("sti; hlt; cli" ::: "memory");
    }
    if (!g_tcp.receive_length) return 0;
    uint32_t count = g_tcp.receive_length < capacity ? g_tcp.receive_length : capacity;
    uint8_t* destination = (uint8_t*)data;
    for (uint32_t i = 0; i < count; i++) destination[i] = g_tcp.receive_data[i];
    for (uint32_t i = count; i < g_tcp.receive_length; i++) {
        g_tcp.receive_data[i - count] = g_tcp.receive_data[i];
    }
    g_tcp.receive_length -= count;
    *length = count;
    tcp_send_segment(TCP_FLAG_ACK, NULL, 0);
    return 1;
}

void net_tcp_close(void) {
    if (g_tcp.state == TCP_STATE_ESTABLISHED) tcp_send_segment(TCP_FLAG_FIN | TCP_FLAG_ACK, NULL, 0);
    g_tcp.state = TCP_STATE_CLOSED;
}

static int send_icmp_echo(uint32_t address, uint16_t sequence, uint32_t tick) {
    uint8_t packet[16];
    IcmpHeader* header = (IcmpHeader*)packet;
    header->type = 8;
    header->code = 0;
    header->checksum = 0;
    write_be16(&header->identifier, NET_PING_ID);
    write_be16(&header->sequence, sequence);
    write_be32(packet + sizeof(*header), tick);
    write_be16(&header->checksum, net_checksum(packet, sizeof(packet)));
    return send_ipv4(address, IP_PROTOCOL_ICMP, packet, sizeof(packet));
}

int net_ping(uint32_t address, uint32_t timeout_ticks, uint32_t* elapsed_ticks) {
    if (!address || !timeout_ticks) return -1;
    g_ping_sequence++;
    g_pending_ping_sequence = g_ping_sequence;
    g_pending_ping_address = address;
    g_pending_ping_tick = pit_get_tick();
    g_ping_pending = 1;
    g_ping_done = 0;
    if (send_icmp_echo(address, g_pending_ping_sequence, g_pending_ping_tick) != 0) {
        g_ping_pending = 0;
        return -1;
    }

    uint32_t start = pit_get_tick();
    while ((uint32_t)(pit_get_tick() - start) < timeout_ticks) {
        net_poll();
        if (g_ping_done) {
            if (elapsed_ticks) *elapsed_ticks = pit_get_tick() - g_pending_ping_tick;
            g_ping_pending = 0;
            return 0;
        }
        __asm__ volatile("sti; hlt; cli" ::: "memory");
    }
    g_ping_pending = 0;
    return -1;
}

int net_udp_send(uint32_t address, uint16_t source_port, uint16_t destination_port,
                 const void* data, uint32_t length) {
    if (!data || length > NET_PACKET_MAX - sizeof(Ipv4Header) - sizeof(UdpHeader)) return -1;
    uint8_t packet[NET_PACKET_MAX];
    UdpHeader* header = (UdpHeader*)packet;
    write_be16(&header->source_port, source_port);
    write_be16(&header->destination_port, destination_port);
    write_be16(&header->length, (uint16_t)(sizeof(*header) + length));
    header->checksum = 0;
    const uint8_t* source = (const uint8_t*)data;
    for (uint32_t i = 0; i < length; i++) packet[sizeof(*header) + i] = source[i];
    return send_ipv4(address, IP_PROTOCOL_UDP, packet, sizeof(*header) + length);
}

int net_udp_receive(uint16_t local_port, void* data, uint32_t capacity,
                    uint32_t* length, uint32_t* source_address, uint16_t* source_port) {
    if (!data || !length) return -1;
    for (uint32_t i = 0; i < g_udp_count; i++) {
        uint32_t index = (g_udp_head + i) % UDP_QUEUE_SIZE;
        UdpDatagram* datagram = &g_udp_queue[index];
        if (datagram->destination_port != local_port) continue;
        if (datagram->length > capacity) return -1;
        uint8_t* destination = (uint8_t*)data;
        for (uint32_t j = 0; j < datagram->length; j++) destination[j] = datagram->data[j];
        *length = datagram->length;
        if (source_address) *source_address = datagram->source_address;
        if (source_port) *source_port = datagram->source_port;
        for (uint32_t j = i; j + 1 < g_udp_count; j++) {
            uint32_t to = (g_udp_head + j) % UDP_QUEUE_SIZE;
            uint32_t from = (g_udp_head + j + 1) % UDP_QUEUE_SIZE;
            g_udp_queue[to].source_address = g_udp_queue[from].source_address;
            g_udp_queue[to].source_port = g_udp_queue[from].source_port;
            g_udp_queue[to].destination_port = g_udp_queue[from].destination_port;
            g_udp_queue[to].length = g_udp_queue[from].length;
            for (uint32_t byte = 0; byte < g_udp_queue[from].length; byte++) {
                g_udp_queue[to].data[byte] = g_udp_queue[from].data[byte];
            }
        }
        g_udp_count--;
        return 1;
    }
    return 0;
}

static void handle_arp(const uint8_t* payload, uint32_t length) {
    if (length < sizeof(ArpPacket)) return;
    const ArpPacket* request = (const ArpPacket*)payload;
    if (read_be16(&request->hardware_type) != 1 ||
        read_be16(&request->protocol_type) != ETH_TYPE_IPV4 ||
        request->hardware_size != 6 || request->protocol_size != 4) return;

    uint32_t sender_ip = read_be32(&request->sender_ip);
    uint32_t target_ip = read_be32(&request->target_ip);
    if (sender_ip) arp_update(sender_ip, request->sender_mac);
    if (read_be16(&request->operation) != 1 || target_ip != g_info.address) return;

    ArpPacket reply;
    write_be16(&reply.hardware_type, 1);
    write_be16(&reply.protocol_type, ETH_TYPE_IPV4);
    reply.hardware_size = 6;
    reply.protocol_size = 4;
    write_be16(&reply.operation, 2);
    mac_copy(reply.sender_mac, g_info.mac);
    write_be32(&reply.sender_ip, g_info.address);
    mac_copy(reply.target_mac, request->sender_mac);
    write_be32(&reply.target_ip, sender_ip);
    send_ethernet(ETH_TYPE_ARP, request->sender_mac, &reply, sizeof(reply));
}

static void handle_icmp(uint32_t source, const uint8_t* payload, uint32_t length) {
    if (length < sizeof(IcmpHeader)) return;
    const IcmpHeader* request = (const IcmpHeader*)payload;
    if (net_checksum(payload, length) != 0) return;
    if (request->type == 8 && request->code == 0) {
        uint8_t reply[NET_PACKET_MAX];
        for (uint32_t i = 0; i < length; i++) reply[i] = payload[i];
        IcmpHeader* header = (IcmpHeader*)reply;
        header->type = 0;
        header->checksum = 0;
        write_be16(&header->checksum, net_checksum(reply, length));
        send_ipv4(source, IP_PROTOCOL_ICMP, reply, length);
    } else if (request->type == 0 && request->code == 0 && g_ping_pending &&
               source == g_pending_ping_address &&
               read_be16(&request->identifier) == NET_PING_ID &&
               read_be16(&request->sequence) == g_pending_ping_sequence) {
        g_ping_done = 1;
    }
}

static void handle_udp(uint32_t source, const uint8_t* payload, uint32_t length) {
    if (length < sizeof(UdpHeader)) return;
    const UdpHeader* header = (const UdpHeader*)payload;
    uint32_t udp_length = read_be16(&header->length);
    if (udp_length < sizeof(*header) || udp_length > length) return;
    if (g_udp_count == UDP_QUEUE_SIZE) {
        g_udp_head = (g_udp_head + 1) % UDP_QUEUE_SIZE;
        g_udp_count--;
    }
    uint32_t index = (g_udp_head + g_udp_count) % UDP_QUEUE_SIZE;
    UdpDatagram* datagram = &g_udp_queue[index];
    datagram->source_address = source;
    datagram->source_port = read_be16(&header->source_port);
    datagram->destination_port = read_be16(&header->destination_port);
    datagram->length = (uint16_t)(udp_length - sizeof(*header));
    const uint8_t* source_data = payload + sizeof(*header);
    for (uint32_t i = 0; i < datagram->length; i++) datagram->data[i] = source_data[i];
    g_udp_count++;
}

static void handle_tcp(uint32_t source, const uint8_t* payload, uint32_t length) {
    if (length < sizeof(TcpHeader)) return;
    const TcpHeader* header = (const TcpHeader*)payload;
    uint32_t header_length = (header->data_offset >> 4) * 4;
    if (header_length < sizeof(*header) || header_length > length ||
        tcp_checksum(source, g_info.address, payload, length) != 0) return;
    if (g_tcp.state == TCP_STATE_CLOSED || source != g_tcp.remote_address ||
        read_be16(&header->source_port) != g_tcp.remote_port ||
        read_be16(&header->destination_port) != g_tcp.local_port) return;

    uint32_t sequence = read_be32(&header->sequence);
    uint32_t acknowledgement = read_be32(&header->acknowledgement);
    uint8_t flags = header->flags;
    if (flags & TCP_FLAG_RST) {
        g_tcp.state = TCP_STATE_CLOSED;
        return;
    }
    if (g_tcp.state == TCP_STATE_SYN_SENT && (flags & (TCP_FLAG_SYN | TCP_FLAG_ACK)) ==
        (TCP_FLAG_SYN | TCP_FLAG_ACK) && acknowledgement == g_tcp.send_sequence) {
        g_tcp.receive_sequence = sequence + 1;
        g_tcp.last_acknowledged = acknowledgement;
        g_tcp.state = TCP_STATE_ESTABLISHED;
        tcp_send_segment(TCP_FLAG_ACK, NULL, 0);
        return;
    }
    if (g_tcp.state != TCP_STATE_ESTABLISHED) return;
    if ((flags & TCP_FLAG_ACK) && acknowledgement > g_tcp.last_acknowledged &&
        acknowledgement <= g_tcp.send_sequence) {
        g_tcp.last_acknowledged = acknowledgement;
    }

    uint32_t payload_length = length - header_length;
    if (payload_length && sequence == g_tcp.receive_sequence) {
        uint32_t available = TCP_RX_CAPACITY - g_tcp.receive_length;
        if (payload_length > available) payload_length = available;
        const uint8_t* data = payload + header_length;
        for (uint32_t i = 0; i < payload_length; i++) {
            g_tcp.receive_data[g_tcp.receive_length + i] = data[i];
        }
        g_tcp.receive_length += payload_length;
        g_tcp.receive_sequence += payload_length;
        tcp_send_segment(TCP_FLAG_ACK, NULL, 0);
    }
    if (flags & TCP_FLAG_FIN) {
        g_tcp.receive_sequence++;
        tcp_send_segment(TCP_FLAG_ACK, NULL, 0);
        g_tcp.state = TCP_STATE_CLOSED;
    }
}

static void handle_ipv4(const uint8_t* payload, uint32_t length) {
    if (length < sizeof(Ipv4Header)) return;
    const Ipv4Header* header = (const Ipv4Header*)payload;
    uint32_t header_length = (header->version_ihl & 0x0F) * 4;
    uint32_t total_length = read_be16(&header->total_length);
    if ((header->version_ihl >> 4) != 4 || header_length < sizeof(*header) ||
        header_length > length || total_length < header_length || total_length > length ||
        net_checksum(payload, header_length) != 0) return;
    uint16_t fragment = read_be16(&header->fragment);
    if (fragment & 0x3FFF) return;
    uint32_t destination = read_be32(&header->destination);
    if (destination != g_info.address && destination != 0xFFFFFFFFu) return;
    uint32_t source = read_be32(&header->source);
    arp_update(source, ((const EthernetHeader*)(payload - sizeof(EthernetHeader)))->source);
    const uint8_t* data = payload + header_length;
    uint32_t data_length = total_length - header_length;
    if (header->protocol == IP_PROTOCOL_ICMP) handle_icmp(source, data, data_length);
    else if (header->protocol == IP_PROTOCOL_UDP) handle_udp(source, data, data_length);
    else if (header->protocol == IP_PROTOCOL_TCP) handle_tcp(source, data, data_length);
}

void net_init(void) {
    g_device = netdev_find("en0");
    zero_bytes(&g_info, sizeof(g_info));
    zero_bytes(g_arp, sizeof(g_arp));
    zero_bytes(g_udp_queue, sizeof(g_udp_queue));
    g_udp_head = 0;
    g_udp_count = 0;
    g_ip_id = 0;
    g_ping_pending = 0;
    g_ping_done = 0;
    if (!g_device) return;

    g_info.address = NET_IPV4(10, 0, 2, 15);
    g_info.netmask = NET_IPV4(255, 255, 255, 0);
    g_info.gateway = NET_IPV4(10, 0, 2, 2);
    g_info.dns = NET_IPV4(10, 0, 2, 3);
    g_info.mtu = g_device->mtu;
    mac_copy(g_info.mac, g_device->mac);
}

int net_get_info(NetInfo* info) {
    if (!g_device || !info) return -1;
    info->address = g_info.address;
    info->netmask = g_info.netmask;
    info->gateway = g_info.gateway;
    info->dns = g_info.dns;
    info->mtu = g_info.mtu;
    mac_copy(info->mac, g_info.mac);
    return 0;
}

void net_poll(void) {
    if (!g_device) return;
    uint8_t frame[NETDEV_FRAME_MAX];
    for (uint32_t count = 0; count < 8; count++) {
        uint32_t length = 0;
        int result = netdev_receive(g_device, frame, sizeof(frame), &length);
        if (result <= 0) break;
        if (length < sizeof(EthernetHeader)) continue;
        const EthernetHeader* header = (const EthernetHeader*)frame;
        uint16_t type = read_be16(&header->type);
        if (!mac_equal(header->destination, g_info.mac) &&
            !(header->destination[0] == 0xFF && header->destination[1] == 0xFF &&
              header->destination[2] == 0xFF && header->destination[3] == 0xFF &&
              header->destination[4] == 0xFF && header->destination[5] == 0xFF)) continue;
        if (type == ETH_TYPE_ARP) handle_arp(frame + sizeof(*header), length - sizeof(*header));
        else if (type == ETH_TYPE_IPV4) {
            handle_ipv4(frame + sizeof(*header), length - sizeof(*header));
        }
    }
}