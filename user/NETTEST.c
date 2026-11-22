#include "sys.h"

static int parse_ipv4(const char* text, uint32_t* address) {
    uint32_t octets[4] = {0, 0, 0, 0};
    uint32_t octet = 0;
    uint32_t count = 0;
    int digits = 0;
    for (uint32_t i = 0;; i++) {
        char ch = text[i];
        if (ch >= '0' && ch <= '9') {
            octet = octet * 10 + (uint32_t)(ch - '0');
            if (octet > 255) return -1;
            digits++;
            continue;
        }
        if ((ch == '.' || ch == '\0') && digits && count < 4) {
            octets[count++] = octet;
            octet = 0;
            digits = 0;
            if (ch == '\0') break;
            continue;
        }
        return -1;
    }
    if (count != 4) return -1;
    *address = (octets[0] << 24) | (octets[1] << 16) | (octets[2] << 8) | octets[3];
    return 0;
}

static void print_ipv4(uint32_t address) {
    char text[16];
    uint32_t octets[4] = {
        (address >> 24) & 255, (address >> 16) & 255,
        (address >> 8) & 255, address & 255
    };
    uint32_t pos = 0;
    for (uint32_t i = 0; i < 4; i++) {
        uint32_t value = octets[i];
        char digits[3];
        uint32_t count = 0;
        do {
            digits[count++] = (char)('0' + value % 10);
            value /= 10;
        } while (value);
        while (count) text[pos++] = digits[--count];
        if (i != 3) text[pos++] = '.';
    }
    text[pos] = '\0';
    print(text);
}

int main(int argc, char** argv, char** envp) {
    (void)envp;
    NetInfo info;
    if (sys_net_info(&info) != 0) {
        print("nettest: no network interface\n");
        return 1;
    }

    print("interface en0 address ");
    print_ipv4(info.address);
    print(" gateway ");
    print_ipv4(info.gateway);
    print("\n");

    const char* target = argc > 1 ? argv[1] : "10.0.2.2";
    uint32_t address;
    if (parse_ipv4(target, &address) != 0) {
        print("usage: nettest [IPv4-address]\n");
        return 2;
    }

    NetPingRequest request = { .address = address, .timeout_ticks = 5000 };
    print("ping ");
    print_ipv4(address);
    print(" ... ");
    if (sys_net_ping(&request) != 0) {
        print("timeout or unresolved route\n");
        return 1;
    }
    print("reply in ");
    char ticks[16];
    uint32_t value = request.elapsed_ticks;
    uint32_t count = 0;
    do { ticks[count++] = (char)('0' + value % 10); value /= 10; } while (value);
    while (count) {
        char digit = ticks[--count];
        sys_write(1, &digit, 1);
    }
    print(" ticks\n");
    return 0;
}