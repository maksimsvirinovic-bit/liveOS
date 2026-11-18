#include "tls13.h"

#define SHA256_BLOCK_SIZE 64
#define TLS13_INNER_CONTENT_TYPE 23
#define TLS13_OUTER_CONTENT_TYPE 23
#define TLS13_LEGACY_VERSION_MAJOR 3
#define TLS13_LEGACY_VERSION_MINOR 3

static uint32_t rotate_right(uint32_t value, uint32_t count) {
    return (value >> count) | (value << (32 - count));
}

static uint32_t read_be32(const uint8_t* bytes) {
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) | bytes[3];
}

static void write_be32(uint8_t* bytes, uint32_t value) {
    bytes[0] = (uint8_t)(value >> 24);
    bytes[1] = (uint8_t)(value >> 16);
    bytes[2] = (uint8_t)(value >> 8);
    bytes[3] = (uint8_t)value;
}

static uint32_t read_le32(const uint8_t* bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static void write_le32(uint8_t* bytes, uint32_t value) {
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static void sha256_transform(Tls13Sha256* context, const uint8_t block[64]) {
    static const uint32_t constants[64] = {
        0x428A2F98,0x71374491,0xB5C0FBCF,0xE9B5DBA5,0x3956C25B,0x59F111F1,0x923F82A4,0xAB1C5ED5,
        0xD807AA98,0x12835B01,0x243185BE,0x550C7DC3,0x72BE5D74,0x80DEB1FE,0x9BDC06A7,0xC19BF174,
        0xE49B69C1,0xEFBE4786,0x0FC19DC6,0x240CA1CC,0x2DE92C6F,0x4A7484AA,0x5CB0A9DC,0x76F988DA,
        0x983E5152,0xA831C66D,0xB00327C8,0xBF597FC7,0xC6E00BF3,0xD5A79147,0x06CA6351,0x14292967,
        0x27B70A85,0x2E1B2138,0x4D2C6DFC,0x53380D13,0x650A7354,0x766A0ABB,0x81C2C92E,0x92722C85,
        0xA2BFE8A1,0xA81A664B,0xC24B8B70,0xC76C51A3,0xD192E819,0xD6990624,0xF40E3585,0x106AA070,
        0x19A4C116,0x1E376C08,0x2748774C,0x34B0BCB5,0x391C0CB3,0x4ED8AA4A,0x5B9CCA4F,0x682E6FF3,
        0x748F82EE,0x78A5636F,0x84C87814,0x8CC70208,0x90BEFFFA,0xA4506CEB,0xBEF9A3F7,0xC67178F2
    };
    uint32_t words[64];
    for (uint32_t i = 0; i < 16; i++) words[i] = read_be32(block + i * 4);
    for (uint32_t i = 16; i < 64; i++) {
        uint32_t s0 = rotate_right(words[i - 15], 7) ^ rotate_right(words[i - 15], 18) ^ (words[i - 15] >> 3);
        uint32_t s1 = rotate_right(words[i - 2], 17) ^ rotate_right(words[i - 2], 19) ^ (words[i - 2] >> 10);
        words[i] = words[i - 16] + s0 + words[i - 7] + s1;
    }

    uint32_t a = context->state[0], b = context->state[1];
    uint32_t c = context->state[2], d = context->state[3];
    uint32_t e = context->state[4], f = context->state[5];
    uint32_t g = context->state[6], h = context->state[7];
    for (uint32_t i = 0; i < 64; i++) {
        uint32_t s1 = rotate_right(e, 6) ^ rotate_right(e, 11) ^ rotate_right(e, 25);
        uint32_t choice = (e & f) ^ (~e & g);
        uint32_t first = h + s1 + choice + constants[i] + words[i];
        uint32_t s0 = rotate_right(a, 2) ^ rotate_right(a, 13) ^ rotate_right(a, 22);
        uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        uint32_t second = s0 + majority;
        h = g; g = f; f = e; e = d + first;
        d = c; c = b; b = a; a = first + second;
    }
    context->state[0] += a; context->state[1] += b;
    context->state[2] += c; context->state[3] += d;
    context->state[4] += e; context->state[5] += f;
    context->state[6] += g; context->state[7] += h;
}

void tls13_sha256_init(Tls13Sha256* context) {
    static const uint32_t initial[8] = {
        0x6A09E667,0xBB67AE85,0x3C6EF372,0xA54FF53A,
        0x510E527F,0x9B05688C,0x1F83D9AB,0x5BE0CD19
    };
    if (!context) return;
    for (uint32_t i = 0; i < 8; i++) context->state[i] = initial[i];
    context->bit_count = 0;
    context->block_used = 0;
}

void tls13_sha256_update(Tls13Sha256* context, const void* input, uint32_t length) {
    if (!context || (!input && length)) return;
    const uint8_t* bytes = (const uint8_t*)input;
    context->bit_count += (uint64_t)length * 8;
    while (length) {
        uint32_t available = SHA256_BLOCK_SIZE - context->block_used;
        uint32_t take = length < available ? length : available;
        for (uint32_t i = 0; i < take; i++) context->block[context->block_used + i] = bytes[i];
        context->block_used += take;
        bytes += take;
        length -= take;
        if (context->block_used == SHA256_BLOCK_SIZE) {
            sha256_transform(context, context->block);
            context->block_used = 0;
        }
    }
}

void tls13_sha256_final(Tls13Sha256* context, uint8_t digest[TLS13_SHA256_SIZE]) {
    if (!context || !digest) return;
    uint64_t bit_count = context->bit_count;
    context->block[context->block_used++] = 0x80;
    if (context->block_used > 56) {
        while (context->block_used < 64) context->block[context->block_used++] = 0;
        sha256_transform(context, context->block);
        context->block_used = 0;
    }
    while (context->block_used < 56) context->block[context->block_used++] = 0;
    for (uint32_t i = 0; i < 8; i++) context->block[63 - i] = (uint8_t)(bit_count >> (i * 8));
    sha256_transform(context, context->block);
    for (uint32_t i = 0; i < 8; i++) write_be32(digest + i * 4, context->state[i]);
    for (uint32_t i = 0; i < sizeof(*context); i++) ((uint8_t*)context)[i] = 0;
}

void tls13_sha256(const void* data, uint32_t length, uint8_t digest[TLS13_SHA256_SIZE]) {
    Tls13Sha256 context;
    tls13_sha256_init(&context);
    tls13_sha256_update(&context, data, length);
    tls13_sha256_final(&context, digest);
}

static void hmac_sha256(const uint8_t* key, uint32_t key_length,
                        const uint8_t* data, uint32_t data_length,
                        uint8_t output[TLS13_SHA256_SIZE]) {
    uint8_t normalized[SHA256_BLOCK_SIZE] = {0};
    if (key_length > SHA256_BLOCK_SIZE) tls13_sha256(key, key_length, normalized);
    else for (uint32_t i = 0; i < key_length; i++) normalized[i] = key[i];
    uint8_t inner_pad[SHA256_BLOCK_SIZE];
    uint8_t outer_pad[SHA256_BLOCK_SIZE];
    for (uint32_t i = 0; i < SHA256_BLOCK_SIZE; i++) {
        inner_pad[i] = normalized[i] ^ 0x36;
        outer_pad[i] = normalized[i] ^ 0x5C;
    }
    Tls13Sha256 context;
    uint8_t inner[TLS13_SHA256_SIZE];
    tls13_sha256_init(&context);
    tls13_sha256_update(&context, inner_pad, sizeof(inner_pad));
    tls13_sha256_update(&context, data, data_length);
    tls13_sha256_final(&context, inner);
    tls13_sha256_init(&context);
    tls13_sha256_update(&context, outer_pad, sizeof(outer_pad));
    tls13_sha256_update(&context, inner, sizeof(inner));
    tls13_sha256_final(&context, output);
    for (uint32_t i = 0; i < sizeof(normalized); i++) normalized[i] = inner_pad[i] = outer_pad[i] = 0;
    for (uint32_t i = 0; i < sizeof(inner); i++) inner[i] = 0;
}

int tls13_hkdf_extract(const uint8_t* salt, uint32_t salt_length,
                       const uint8_t* input, uint32_t input_length,
                       uint8_t output[TLS13_SHA256_SIZE]) {
    if (!output || (!salt && salt_length) || (!input && input_length)) return -1;
    uint8_t zero_salt[TLS13_SHA256_SIZE] = {0};
    if (!salt || !salt_length) {
        salt = zero_salt;
        salt_length = sizeof(zero_salt);
    }
    hmac_sha256(salt, salt_length, input, input_length, output);
    return 0;
}

int tls13_hkdf_expand(const uint8_t prk[TLS13_SHA256_SIZE], const uint8_t* info,
                      uint32_t info_length, uint8_t* output, uint32_t output_length) {
    if (!prk || (!info && info_length) || (!output && output_length) || output_length > 255u * 32u) return -1;
    uint8_t previous[TLS13_SHA256_SIZE];
    uint32_t previous_length = 0;
    uint32_t written = 0;
    uint8_t input[32 + 256 + 1];
    if (info_length > 256) return -1;
    for (uint32_t counter = 1; written < output_length; counter++) {
        for (uint32_t i = 0; i < previous_length; i++) input[i] = previous[i];
        for (uint32_t i = 0; i < info_length; i++) input[previous_length + i] = info[i];
        input[previous_length + info_length] = (uint8_t)counter;
        hmac_sha256(prk, 32, input, previous_length + info_length + 1, previous);
        previous_length = 32;
        uint32_t take = output_length - written;
        if (take > 32) take = 32;
        for (uint32_t i = 0; i < take; i++) output[written + i] = previous[i];
        written += take;
    }
    for (uint32_t i = 0; i < sizeof(input); i++) input[i] = 0;
    for (uint32_t i = 0; i < sizeof(previous); i++) previous[i] = 0;
    return 0;
}

int tls13_hkdf_expand_label(const uint8_t secret[TLS13_SHA256_SIZE],
                            const char* label, const uint8_t* context,
                            uint32_t context_length, uint8_t* output,
                            uint32_t output_length) {
    static const char prefix[] = "tls13 ";
    if (!secret || !label || (!context && context_length) || (!output && output_length) ||
        output_length > 65535 || context_length > 255) return -1;
    uint32_t label_length = 0;
    while (label[label_length]) label_length++;
    if (label_length + sizeof(prefix) - 1 > 255) return -1;
    uint8_t info[2 + 1 + 255 + 1 + 255];
    uint32_t position = 0;
    info[position++] = (uint8_t)(output_length >> 8);
    info[position++] = (uint8_t)output_length;
    info[position++] = (uint8_t)(label_length + sizeof(prefix) - 1);
    for (uint32_t i = 0; i < sizeof(prefix) - 1; i++) info[position++] = (uint8_t)prefix[i];
    for (uint32_t i = 0; i < label_length; i++) info[position++] = (uint8_t)label[i];
    info[position++] = (uint8_t)context_length;
    for (uint32_t i = 0; i < context_length; i++) info[position++] = context[i];
    int result = tls13_hkdf_expand(secret, info, position, output, output_length);
    for (uint32_t i = 0; i < sizeof(info); i++) info[i] = 0;
    return result;
}

typedef struct { uint64_t limb[5]; } X25519Field;

#define X25519_MASK ((1ULL << 51) - 1)

static uint64_t x25519_load64(const uint8_t* input) {
    uint64_t value = 0;
    for (uint32_t i = 0; i < 8; i++) value |= (uint64_t)input[i] << (8 * i);
    return value;
}

static void x25519_cswap(uint64_t swap, X25519Field* left, X25519Field* right) {
    uint64_t mask = (uint64_t)0 - swap;
    for (uint32_t i = 0; i < 5; i++) {
        uint64_t difference = mask & (left->limb[i] ^ right->limb[i]);
        left->limb[i] ^= difference;
        right->limb[i] ^= difference;
    }
}

static void x25519_field_from_bytes(X25519Field* output, const uint8_t input[32]) {
    output->limb[0] = x25519_load64(input) & X25519_MASK;
    output->limb[1] = (x25519_load64(input + 6) >> 3) & X25519_MASK;
    output->limb[2] = (x25519_load64(input + 12) >> 6) & X25519_MASK;
    output->limb[3] = (x25519_load64(input + 19) >> 1) & X25519_MASK;
    output->limb[4] = (x25519_load64(input + 24) >> 12) & X25519_MASK;
}

static void x25519_field_reduce(X25519Field* value) {
    uint64_t carry = value->limb[0] >> 51; value->limb[0] &= X25519_MASK; value->limb[1] += carry;
    carry = value->limb[1] >> 51; value->limb[1] &= X25519_MASK; value->limb[2] += carry;
    carry = value->limb[2] >> 51; value->limb[2] &= X25519_MASK; value->limb[3] += carry;
    carry = value->limb[3] >> 51; value->limb[3] &= X25519_MASK; value->limb[4] += carry;
    carry = value->limb[4] >> 51; value->limb[4] &= X25519_MASK; value->limb[0] += carry * 19;
    carry = value->limb[0] >> 51; value->limb[0] &= X25519_MASK; value->limb[1] += carry;
}

static void x25519_field_add(X25519Field* output, const X25519Field* left,
                             const X25519Field* right) {
    for (uint32_t i = 0; i < 5; i++) output->limb[i] = left->limb[i] + right->limb[i];
}

static void x25519_field_sub(X25519Field* output, const X25519Field* left,
                             const X25519Field* right) {
    output->limb[0] = left->limb[0] + 2 * (X25519_MASK - 18) - right->limb[0];
    for (uint32_t i = 1; i < 5; i++) output->limb[i] = left->limb[i] + 2 * X25519_MASK - right->limb[i];
}

static void x25519_field_mul(X25519Field* output, const X25519Field* left,
                             const X25519Field* right) {
    __uint128_t t0 = (__uint128_t)left->limb[0] * right->limb[0] +
        19 * ((__uint128_t)left->limb[1] * right->limb[4] + (__uint128_t)left->limb[2] * right->limb[3] +
              (__uint128_t)left->limb[3] * right->limb[2] + (__uint128_t)left->limb[4] * right->limb[1]);
    __uint128_t t1 = (__uint128_t)left->limb[0] * right->limb[1] + (__uint128_t)left->limb[1] * right->limb[0] +
        19 * ((__uint128_t)left->limb[2] * right->limb[4] + (__uint128_t)left->limb[3] * right->limb[3] +
              (__uint128_t)left->limb[4] * right->limb[2]);
    __uint128_t t2 = (__uint128_t)left->limb[0] * right->limb[2] + (__uint128_t)left->limb[1] * right->limb[1] +
        (__uint128_t)left->limb[2] * right->limb[0] + 19 * ((__uint128_t)left->limb[3] * right->limb[4] +
              (__uint128_t)left->limb[4] * right->limb[3]);
    __uint128_t t3 = (__uint128_t)left->limb[0] * right->limb[3] + (__uint128_t)left->limb[1] * right->limb[2] +
        (__uint128_t)left->limb[2] * right->limb[1] + (__uint128_t)left->limb[3] * right->limb[0] +
        19 * (__uint128_t)left->limb[4] * right->limb[4];
    __uint128_t t4 = (__uint128_t)left->limb[0] * right->limb[4] + (__uint128_t)left->limb[1] * right->limb[3] +
        (__uint128_t)left->limb[2] * right->limb[2] + (__uint128_t)left->limb[3] * right->limb[1] +
        (__uint128_t)left->limb[4] * right->limb[0];
    uint64_t carry = (uint64_t)(t0 >> 51); output->limb[0] = (uint64_t)t0 & X25519_MASK; t1 += carry;
    carry = (uint64_t)(t1 >> 51); output->limb[1] = (uint64_t)t1 & X25519_MASK; t2 += carry;
    carry = (uint64_t)(t2 >> 51); output->limb[2] = (uint64_t)t2 & X25519_MASK; t3 += carry;
    carry = (uint64_t)(t3 >> 51); output->limb[3] = (uint64_t)t3 & X25519_MASK; t4 += carry;
    carry = (uint64_t)(t4 >> 51); output->limb[4] = (uint64_t)t4 & X25519_MASK;
    output->limb[0] += carry * 19;
    x25519_field_reduce(output);
}

static void x25519_field_square(X25519Field* output, const X25519Field* value) {
    x25519_field_mul(output, value, value);
}

static void x25519_field_mul_small(X25519Field* output, const X25519Field* value, uint64_t multiplier) {
    __uint128_t carry = 0;
    for (uint32_t i = 0; i < 5; i++) {
        __uint128_t product = (__uint128_t)value->limb[i] * multiplier + carry;
        output->limb[i] = (uint64_t)product & X25519_MASK;
        carry = product >> 51;
    }
    output->limb[0] += (uint64_t)carry * 19;
    x25519_field_reduce(output);
}

static void x25519_field_inverse(X25519Field* output, const X25519Field* value) {
    X25519Field result = {{1, 0, 0, 0, 0}};
    for (int bit = 254; bit >= 0; bit--) {
        x25519_field_square(&result, &result);
        int set = bit >= 5 || ((0xEBu >> bit) & 1u);
        if (set) x25519_field_mul(&result, &result, value);
    }
    *output = result;
}

static void x25519_field_to_bytes(uint8_t output[32], X25519Field* value) {
    x25519_field_reduce(value);
    static const uint64_t modulus[5] = {
        X25519_MASK - 18, X25519_MASK, X25519_MASK, X25519_MASK, X25519_MASK
    };
    uint64_t reduced[5];
    uint64_t borrow = 0;
    for (uint32_t i = 0; i < 5; i++) {
        uint64_t subtrahend = modulus[i] + borrow;
        uint64_t current = value->limb[i];
        reduced[i] = (current - subtrahend) & X25519_MASK;
        borrow = current < subtrahend;
    }
    uint64_t use_reduced = (uint64_t)0 - (borrow ^ 1u);
    for (uint32_t i = 0; i < 5; i++) {
        value->limb[i] = (reduced[i] & use_reduced) | (value->limb[i] & ~use_reduced);
        reduced[i] = 0;
    }
    for (uint32_t i = 0; i < 32; i++) output[i] = 0;
    for (uint32_t bit = 0; bit < 255; bit++) {
        uint32_t limb = bit / 51;
        uint32_t offset = bit % 51;
        output[bit / 8] |= (uint8_t)(((value->limb[limb] >> offset) & 1u) << (bit % 8));
    }
}

int tls13_x25519(uint8_t shared_secret[32], const uint8_t private_key[32],
                 const uint8_t public_key[32]) {
    if (!shared_secret || !private_key || !public_key) return -1;
    uint8_t scalar[32], point[32];
    for (uint32_t i = 0; i < 32; i++) {
        scalar[i] = private_key[i];
        point[i] = public_key[i];
    }
    scalar[0] &= 248;
    scalar[31] &= 127;
    scalar[31] |= 64;
    point[31] &= 127;

    X25519Field x1, x2 = {{1, 0, 0, 0, 0}}, z2 = {{0, 0, 0, 0, 0}}, x3, z3 = {{1, 0, 0, 0, 0}};
    x25519_field_from_bytes(&x1, point);
    x3 = x1;
    uint64_t swap = 0;
    for (int bit = 254; bit >= 0; bit--) {
        uint64_t current = (scalar[bit >> 3] >> (bit & 7)) & 1;
        swap ^= current;
        x25519_cswap(swap, &x2, &x3);
        x25519_cswap(swap, &z2, &z3);
        swap = current;

        X25519Field a, aa, b, bb, e, c, d, da, cb, temporary;
        x25519_field_add(&a, &x2, &z2);
        x25519_field_square(&aa, &a);
        x25519_field_sub(&b, &x2, &z2);
        x25519_field_square(&bb, &b);
        x25519_field_sub(&e, &aa, &bb);
        x25519_field_add(&c, &x3, &z3);
        x25519_field_sub(&d, &x3, &z3);
        x25519_field_mul(&da, &d, &a);
        x25519_field_mul(&cb, &c, &b);
        x25519_field_add(&temporary, &da, &cb);
        x25519_field_square(&x3, &temporary);
        x25519_field_sub(&temporary, &da, &cb);
        x25519_field_square(&temporary, &temporary);
        x25519_field_mul(&z3, &x1, &temporary);
        x25519_field_mul(&x2, &aa, &bb);
        x25519_field_mul_small(&temporary, &e, 121665);
        x25519_field_add(&temporary, &aa, &temporary);
        x25519_field_mul(&z2, &e, &temporary);
    }
    x25519_cswap(swap, &x2, &x3);
    x25519_cswap(swap, &z2, &z3);
    X25519Field inverse, result;
    x25519_field_inverse(&inverse, &z2);
    x25519_field_mul(&result, &x2, &inverse);
    x25519_field_to_bytes(shared_secret, &result);

    uint8_t nonzero = 0;
    for (uint32_t i = 0; i < 32; i++) nonzero |= shared_secret[i];
    for (uint32_t i = 0; i < sizeof(scalar); i++) scalar[i] = point[i] = 0;
    return nonzero ? 0 : -1;
}

static void tls13_derive_secret(const uint8_t secret[32], const char* label,
                                const uint8_t transcript_hash[32], uint8_t output[32]) {
    (void)tls13_hkdf_expand_label(secret, label, transcript_hash, 32, output, 32);
}

int tls13_key_schedule_handshake(Tls13KeySchedule* schedule,
                                 const uint8_t* psk, uint32_t psk_length,
                                 const uint8_t shared_secret[32],
                                 const uint8_t transcript_hash[32]) {
    if (!schedule || (!psk && psk_length) || !shared_secret || !transcript_hash) return -1;
    uint8_t zero[32] = {0};
    uint8_t empty_hash[32];
    tls13_sha256(0, 0, empty_hash);
    if (!psk || !psk_length) {
        psk = zero;
        psk_length = sizeof(zero);
    }
    if (tls13_hkdf_extract(zero, sizeof(zero), psk, psk_length, schedule->early_secret) != 0) return -1;
    uint8_t derived[32];
    tls13_derive_secret(schedule->early_secret, "derived", empty_hash, derived);
    if (tls13_hkdf_extract(derived, sizeof(derived), shared_secret, 32,
                           schedule->handshake_secret) != 0) return -1;
    tls13_derive_secret(schedule->handshake_secret, "c hs traffic", transcript_hash,
                        schedule->client_handshake_traffic);
    tls13_derive_secret(schedule->handshake_secret, "s hs traffic", transcript_hash,
                        schedule->server_handshake_traffic);
    tls13_derive_secret(schedule->handshake_secret, "derived", empty_hash, derived);
    if (tls13_hkdf_extract(derived, sizeof(derived), zero, sizeof(zero),
                           schedule->master_secret) != 0) return -1;
    for (uint32_t i = 0; i < sizeof(derived); i++) derived[i] = empty_hash[i] = 0;
    return 0;
}

int tls13_key_schedule_application(Tls13KeySchedule* schedule,
                                   const uint8_t transcript_hash[32]) {
    if (!schedule || !transcript_hash) return -1;
    tls13_derive_secret(schedule->master_secret, "c ap traffic", transcript_hash,
                        schedule->client_application_traffic);
    tls13_derive_secret(schedule->master_secret, "s ap traffic", transcript_hash,
                        schedule->server_application_traffic);
    return 0;
}

int tls13_traffic_key_init(Tls13TrafficKey* traffic, const uint8_t traffic_secret[32]) {
    if (!traffic || !traffic_secret) return -1;
    if (tls13_hkdf_expand_label(traffic_secret, "key", 0, 0,
                                traffic->key, sizeof(traffic->key)) != 0 ||
        tls13_hkdf_expand_label(traffic_secret, "iv", 0, 0,
                                traffic->iv, sizeof(traffic->iv)) != 0) {
        return -1;
    }
    traffic->sequence = 0;
    return 0;
}

int tls13_finished_verify_data(const uint8_t handshake_traffic_secret[32],
                               const uint8_t transcript_hash[32], uint8_t verify_data[32]) {
    if (!handshake_traffic_secret || !transcript_hash || !verify_data) return -1;
    uint8_t finished_key[32];
    if (tls13_hkdf_expand_label(handshake_traffic_secret, "finished", 0, 0,
                                finished_key, sizeof(finished_key)) != 0) return -1;
    hmac_sha256(finished_key, sizeof(finished_key), transcript_hash, 32, verify_data);
    for (uint32_t i = 0; i < sizeof(finished_key); i++) finished_key[i] = 0;
    return 0;
}

int tls13_finished_verify(const uint8_t handshake_traffic_secret[32],
                          const uint8_t transcript_hash[32], const uint8_t verify_data[32]) {
    if (!verify_data) return -1;
    uint8_t expected[32];
    if (tls13_finished_verify_data(handshake_traffic_secret, transcript_hash, expected) != 0) return -1;
    uint8_t difference = 0;
    for (uint32_t i = 0; i < sizeof(expected); i++) difference |= expected[i] ^ verify_data[i];
    for (uint32_t i = 0; i < sizeof(expected); i++) expected[i] = 0;
    return difference == 0 ? 0 : -1;
}

void tls13_transcript_init(Tls13Transcript* transcript) {
    if (!transcript) return;
    tls13_sha256_init(&transcript->hash);
    transcript->active = 1;
}

int tls13_transcript_update(Tls13Transcript* transcript, const void* data, uint32_t length) {
    if (!transcript || !transcript->active || (!data && length)) return -1;
    tls13_sha256_update(&transcript->hash, data, length);
    return 0;
}

int tls13_transcript_digest(const Tls13Transcript* transcript, uint8_t digest[32]) {
    if (!transcript || !transcript->active || !digest) return -1;
    Tls13Sha256 copy = transcript->hash;
    tls13_sha256_final(&copy, digest);
    return 0;
}

int tls13_traffic_key_update(uint8_t traffic_secret[32], Tls13TrafficKey* traffic) {
    if (!traffic_secret || !traffic) return -1;
    uint8_t next_secret[32];
    if (tls13_hkdf_expand_label(traffic_secret, "traffic upd", 0, 0,
                                next_secret, sizeof(next_secret)) != 0) return -1;
    for (uint32_t i = 0; i < sizeof(next_secret); i++) traffic_secret[i] = next_secret[i];
    int result = tls13_traffic_key_init(traffic, traffic_secret);
    for (uint32_t i = 0; i < sizeof(next_secret); i++) next_secret[i] = 0;
    return result;
}

int tls13_random_bytes(void* output, uint32_t length) {
    if (!output && length) return -1;
    uint32_t eax = 1, ebx, ecx, edx;
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                     : "a"(eax), "c"(0));
    (void)ebx;
    (void)edx;
    if (!(ecx & (1u << 30))) return -1;
    uint8_t* bytes = (uint8_t*)output;
    while (length) {
        uint64_t random_value = 0;
        uint8_t success = 0;
        for (uint32_t retry = 0; retry < 10 && !success; retry++) {
            __asm__ volatile("rdrand %0; setc %1" : "=r"(random_value), "=qm"(success));
        }
        if (!success) return -1;
        uint32_t take = length < sizeof(random_value) ? length : sizeof(random_value);
        for (uint32_t i = 0; i < take; i++) bytes[i] = (uint8_t)(random_value >> (i * 8));
        bytes += take;
        length -= take;
    }
    return 0;
}

static void chacha_quarter(uint32_t* a, uint32_t* b, uint32_t* c, uint32_t* d) {
    *a += *b; *d ^= *a; *d = (*d << 16) | (*d >> 16);
    *c += *d; *b ^= *c; *b = (*b << 12) | (*b >> 20);
    *a += *b; *d ^= *a; *d = (*d << 8) | (*d >> 24);
    *c += *d; *b ^= *c; *b = (*b << 7) | (*b >> 25);
}

static void chacha_block(const uint8_t key[32], uint32_t counter,
                         const uint8_t nonce[12], uint8_t output[64]) {
    uint32_t state[16] = {
        0x61707865,0x3320646E,0x79622D32,0x6B206574,
        0,0,0,0,0,0,0,0,counter,0,0,0
    };
    for (uint32_t i = 0; i < 8; i++) state[4 + i] = read_le32(key + i * 4);
    for (uint32_t i = 0; i < 3; i++) state[13 + i] = read_le32(nonce + i * 4);
    uint32_t working[16];
    for (uint32_t i = 0; i < 16; i++) working[i] = state[i];
    for (uint32_t round = 0; round < 10; round++) {
        chacha_quarter(&working[0], &working[4], &working[8], &working[12]);
        chacha_quarter(&working[1], &working[5], &working[9], &working[13]);
        chacha_quarter(&working[2], &working[6], &working[10], &working[14]);
        chacha_quarter(&working[3], &working[7], &working[11], &working[15]);
        chacha_quarter(&working[0], &working[5], &working[10], &working[15]);
        chacha_quarter(&working[1], &working[6], &working[11], &working[12]);
        chacha_quarter(&working[2], &working[7], &working[8], &working[13]);
        chacha_quarter(&working[3], &working[4], &working[9], &working[14]);
    }
    for (uint32_t i = 0; i < 16; i++) write_le32(output + i * 4, working[i] + state[i]);
    for (uint32_t i = 0; i < 16; i++) working[i] = state[i] = 0;
}

static int chacha_xor(const uint8_t key[32], const uint8_t nonce[12], uint32_t counter,
                      const uint8_t* input, uint32_t length, uint8_t* output) {
    if (length && (!input || !output)) return -1;
    uint64_t blocks = ((uint64_t)length + 63) / 64;
    if (blocks > (uint64_t)0xFFFFFFFFu - counter + 1) return -1;
    uint8_t block[64];
    uint32_t offset = 0;
    while (offset < length) {
        chacha_block(key, counter++, nonce, block);
        uint32_t take = length - offset;
        if (take > 64) take = 64;
        for (uint32_t i = 0; i < take; i++) output[offset + i] = input[offset + i] ^ block[i];
        offset += take;
    }
    for (uint32_t i = 0; i < sizeof(block); i++) block[i] = 0;
    return 0;
}

static void poly1305_auth(const uint8_t* message, uint32_t length,
                          const uint8_t key[32], uint8_t tag[16]) {
    uint32_t r0 = read_le32(key + 0) & 0x3FFFFFF;
    uint32_t r1 = (read_le32(key + 3) >> 2) & 0x3FFFF03;
    uint32_t r2 = (read_le32(key + 6) >> 4) & 0x3FFC0FF;
    uint32_t r3 = (read_le32(key + 9) >> 6) & 0x3F03FFF;
    uint32_t r4 = (read_le32(key + 12) >> 8) & 0x00FFFFF;
    uint32_t s1 = r1 * 5, s2 = r2 * 5, s3 = r3 * 5, s4 = r4 * 5;
    uint32_t h0 = 0, h1 = 0, h2 = 0, h3 = 0, h4 = 0;

    while (length >= 16) {
        h0 += read_le32(message + 0) & 0x3FFFFFF;
        h1 += (read_le32(message + 3) >> 2) & 0x3FFFFFF;
        h2 += (read_le32(message + 6) >> 4) & 0x3FFFFFF;
        h3 += (read_le32(message + 9) >> 6) & 0x3FFFFFF;
        h4 += (read_le32(message + 12) >> 8) | (1u << 24);
        uint64_t d0 = (uint64_t)h0*r0 + (uint64_t)h1*s4 + (uint64_t)h2*s3 + (uint64_t)h3*s2 + (uint64_t)h4*s1;
        uint64_t d1 = (uint64_t)h0*r1 + (uint64_t)h1*r0 + (uint64_t)h2*s4 + (uint64_t)h3*s3 + (uint64_t)h4*s2;
        uint64_t d2 = (uint64_t)h0*r2 + (uint64_t)h1*r1 + (uint64_t)h2*r0 + (uint64_t)h3*s4 + (uint64_t)h4*s3;
        uint64_t d3 = (uint64_t)h0*r3 + (uint64_t)h1*r2 + (uint64_t)h2*r1 + (uint64_t)h3*r0 + (uint64_t)h4*s4;
        uint64_t d4 = (uint64_t)h0*r4 + (uint64_t)h1*r3 + (uint64_t)h2*r2 + (uint64_t)h3*r1 + (uint64_t)h4*r0;
        uint32_t carry = (uint32_t)(d0 >> 26); h0 = (uint32_t)d0 & 0x3FFFFFF; d1 += carry;
        carry = (uint32_t)(d1 >> 26); h1 = (uint32_t)d1 & 0x3FFFFFF; d2 += carry;
        carry = (uint32_t)(d2 >> 26); h2 = (uint32_t)d2 & 0x3FFFFFF; d3 += carry;
        carry = (uint32_t)(d3 >> 26); h3 = (uint32_t)d3 & 0x3FFFFFF; d4 += carry;
        carry = (uint32_t)(d4 >> 26); h4 = (uint32_t)d4 & 0x3FFFFFF;
        h0 += carry * 5; carry = h0 >> 26; h0 &= 0x3FFFFFF; h1 += carry;
        message += 16; length -= 16;
    }
    if (length) {
        uint8_t final_block[16] = {0};
        for (uint32_t i = 0; i < length; i++) final_block[i] = message[i];
        final_block[length] = 1;
        h0 += read_le32(final_block + 0) & 0x3FFFFFF;
        h1 += (read_le32(final_block + 3) >> 2) & 0x3FFFFFF;
        h2 += (read_le32(final_block + 6) >> 4) & 0x3FFFFFF;
        h3 += (read_le32(final_block + 9) >> 6) & 0x3FFFFFF;
        h4 += read_le32(final_block + 12) >> 8;
        uint64_t d0 = (uint64_t)h0*r0 + (uint64_t)h1*s4 + (uint64_t)h2*s3 + (uint64_t)h3*s2 + (uint64_t)h4*s1;
        uint64_t d1 = (uint64_t)h0*r1 + (uint64_t)h1*r0 + (uint64_t)h2*s4 + (uint64_t)h3*s3 + (uint64_t)h4*s2;
        uint64_t d2 = (uint64_t)h0*r2 + (uint64_t)h1*r1 + (uint64_t)h2*r0 + (uint64_t)h3*s4 + (uint64_t)h4*s3;
        uint64_t d3 = (uint64_t)h0*r3 + (uint64_t)h1*r2 + (uint64_t)h2*r1 + (uint64_t)h3*r0 + (uint64_t)h4*s4;
        uint64_t d4 = (uint64_t)h0*r4 + (uint64_t)h1*r3 + (uint64_t)h2*r2 + (uint64_t)h3*r1 + (uint64_t)h4*r0;
        uint32_t carry = (uint32_t)(d0 >> 26); h0 = (uint32_t)d0 & 0x3FFFFFF; d1 += carry;
        carry = (uint32_t)(d1 >> 26); h1 = (uint32_t)d1 & 0x3FFFFFF; d2 += carry;
        carry = (uint32_t)(d2 >> 26); h2 = (uint32_t)d2 & 0x3FFFFFF; d3 += carry;
        carry = (uint32_t)(d3 >> 26); h3 = (uint32_t)d3 & 0x3FFFFFF; d4 += carry;
        carry = (uint32_t)(d4 >> 26); h4 = (uint32_t)d4 & 0x3FFFFFF;
        h0 += carry * 5; carry = h0 >> 26; h0 &= 0x3FFFFFF; h1 += carry;
    }

    uint32_t carry = h1 >> 26; h1 &= 0x3FFFFFF; h2 += carry;
    carry = h2 >> 26; h2 &= 0x3FFFFFF; h3 += carry;
    carry = h3 >> 26; h3 &= 0x3FFFFFF; h4 += carry;
    carry = h4 >> 26; h4 &= 0x3FFFFFF; h0 += carry * 5;
    carry = h0 >> 26; h0 &= 0x3FFFFFF; h1 += carry;

    uint32_t g0 = h0 + 5; carry = g0 >> 26; g0 &= 0x3FFFFFF;
    uint32_t g1 = h1 + carry; carry = g1 >> 26; g1 &= 0x3FFFFFF;
    uint32_t g2 = h2 + carry; carry = g2 >> 26; g2 &= 0x3FFFFFF;
    uint32_t g3 = h3 + carry; carry = g3 >> 26; g3 &= 0x3FFFFFF;
    uint32_t g4 = h4 + carry - (1u << 26);
    uint32_t mask = (g4 >> 31) - 1;
    uint32_t inverse = ~mask;
    h0 = (h0 & inverse) | (g0 & mask); h1 = (h1 & inverse) | (g1 & mask);
    h2 = (h2 & inverse) | (g2 & mask); h3 = (h3 & inverse) | (g3 & mask);
    h4 = (h4 & inverse) | (g4 & mask);

    uint32_t word0 = h0 | (h1 << 26);
    uint32_t word1 = (h1 >> 6) | (h2 << 20);
    uint32_t word2 = (h2 >> 12) | (h3 << 14);
    uint32_t word3 = (h3 >> 18) | (h4 << 8);
    uint64_t f0 = (uint64_t)word0 + read_le32(key + 16);
    uint64_t f1 = (uint64_t)word1 + read_le32(key + 20) + (f0 >> 32);
    uint64_t f2 = (uint64_t)word2 + read_le32(key + 24) + (f1 >> 32);
    uint64_t f3 = (uint64_t)word3 + read_le32(key + 28) + (f2 >> 32);
    write_le32(tag + 0, (uint32_t)f0); write_le32(tag + 4, (uint32_t)f1);
    write_le32(tag + 8, (uint32_t)f2); write_le32(tag + 12, (uint32_t)f3);
}

static void poly1305_aead_tag(const uint8_t key[32], const uint8_t* aad, uint32_t aad_length,
                              const uint8_t* ciphertext, uint32_t ciphertext_length,
                              uint8_t tag[16]) {
    uint8_t message[2 * (TLS13_RECORD_MAX_CIPHERTEXT + 16) + 32];
    uint32_t length = 0;
    for (uint32_t i = 0; i < aad_length; i++) message[length++] = aad[i];
    while (length & 15u) message[length++] = 0;
    for (uint32_t i = 0; i < ciphertext_length; i++) message[length++] = ciphertext[i];
    while (length & 15u) message[length++] = 0;
    uint64_t aad_bits = (uint64_t)aad_length;
    uint64_t ciphertext_bits = (uint64_t)ciphertext_length;
    for (uint32_t i = 0; i < 8; i++) message[length++] = (uint8_t)(aad_bits >> (i * 8));
    for (uint32_t i = 0; i < 8; i++) message[length++] = (uint8_t)(ciphertext_bits >> (i * 8));
    poly1305_auth(message, length, key, tag);
    for (uint32_t i = 0; i < sizeof(message); i++) message[i] = 0;
}

int tls13_chacha20_poly1305_encrypt(const uint8_t key[32], const uint8_t nonce[12],
                                    const uint8_t* aad, uint32_t aad_length,
                                    const uint8_t* plaintext, uint32_t plaintext_length,
                                    uint8_t* ciphertext, uint8_t tag[16]) {
    if (!key || !nonce || (!aad && aad_length) || (!plaintext && plaintext_length) ||
        (!ciphertext && plaintext_length) || !tag || plaintext_length > TLS13_RECORD_MAX_CIPHERTEXT ||
        aad_length > TLS13_RECORD_MAX_CIPHERTEXT) return -1;
    uint8_t first_block[64];
    chacha_block(key, 0, nonce, first_block);
    if (chacha_xor(key, nonce, 1, plaintext, plaintext_length, ciphertext) != 0) return -1;
    poly1305_aead_tag(first_block, aad, aad_length, ciphertext, plaintext_length, tag);
    for (uint32_t i = 0; i < sizeof(first_block); i++) first_block[i] = 0;
    return 0;
}

int tls13_chacha20_poly1305_decrypt(const uint8_t key[32], const uint8_t nonce[12],
                                    const uint8_t* aad, uint32_t aad_length,
                                    const uint8_t* ciphertext, uint32_t ciphertext_length,
                                    const uint8_t tag[16], uint8_t* plaintext) {
    if (!key || !nonce || (!aad && aad_length) || (!ciphertext && ciphertext_length) ||
        (!plaintext && ciphertext_length) || !tag || ciphertext_length > TLS13_RECORD_MAX_CIPHERTEXT ||
        aad_length > TLS13_RECORD_MAX_CIPHERTEXT) return -1;
    uint8_t first_block[64], expected[16];
    chacha_block(key, 0, nonce, first_block);
    poly1305_aead_tag(first_block, aad, aad_length, ciphertext, ciphertext_length, expected);
    uint8_t difference = 0;
    for (uint32_t i = 0; i < 16; i++) difference |= expected[i] ^ tag[i];
    if (difference) {
        for (uint32_t i = 0; i < sizeof(first_block); i++) first_block[i] = 0;
        for (uint32_t i = 0; i < sizeof(expected); i++) expected[i] = 0;
        return -1;
    }
    int result = chacha_xor(key, nonce, 1, ciphertext, ciphertext_length, plaintext);
    for (uint32_t i = 0; i < sizeof(first_block); i++) first_block[i] = 0;
    for (uint32_t i = 0; i < sizeof(expected); i++) expected[i] = 0;
    return result;
}

static void traffic_nonce(const Tls13TrafficKey* traffic, uint8_t nonce[12]) {
    for (uint32_t i = 0; i < 12; i++) nonce[i] = traffic->iv[i];
    uint64_t sequence = traffic->sequence;
    for (uint32_t i = 0; i < 8; i++) nonce[11 - i] ^= (uint8_t)(sequence >> (i * 8));
}

int tls13_record_seal(Tls13TrafficKey* traffic, uint8_t content_type,
                      const uint8_t* plaintext, uint32_t plaintext_length,
                      uint8_t* record, uint32_t record_capacity,
                      uint32_t* record_length) {
    if (!traffic || (!plaintext && plaintext_length) || !record || !record_length ||
        plaintext_length > TLS13_RECORD_MAX_PLAINTEXT || traffic->sequence == UINT64_MAX) return -1;
    uint32_t inner_length = plaintext_length + 1;
    uint32_t encrypted_length = inner_length + TLS13_AEAD_TAG_SIZE;
    uint32_t total_length = TLS13_RECORD_HEADER_SIZE + encrypted_length;
    if (total_length > record_capacity || encrypted_length > 0xFFFF) return -1;

    record[0] = TLS13_OUTER_CONTENT_TYPE;
    record[1] = TLS13_LEGACY_VERSION_MAJOR;
    record[2] = TLS13_LEGACY_VERSION_MINOR;
    record[3] = (uint8_t)(encrypted_length >> 8);
    record[4] = (uint8_t)encrypted_length;
    uint8_t inner[TLS13_RECORD_MAX_PLAINTEXT + 1];
    for (uint32_t i = 0; i < plaintext_length; i++) inner[i] = plaintext[i];
    inner[plaintext_length] = content_type;
    uint8_t nonce[12], tag[16];
    traffic_nonce(traffic, nonce);
    if (tls13_chacha20_poly1305_encrypt(traffic->key, nonce, record, TLS13_RECORD_HEADER_SIZE,
                                       inner, inner_length, record + TLS13_RECORD_HEADER_SIZE, tag) != 0) return -1;
    for (uint32_t i = 0; i < sizeof(tag); i++) record[TLS13_RECORD_HEADER_SIZE + inner_length + i] = tag[i];
    traffic->sequence++;
    *record_length = total_length;
    for (uint32_t i = 0; i < sizeof(inner); i++) inner[i] = 0;
    for (uint32_t i = 0; i < sizeof(nonce); i++) nonce[i] = 0;
    for (uint32_t i = 0; i < sizeof(tag); i++) tag[i] = 0;
    return 0;
}

int tls13_record_open(Tls13TrafficKey* traffic, const uint8_t* record,
                      uint32_t record_length, uint8_t* content_type,
                      uint8_t* plaintext, uint32_t plaintext_capacity,
                      uint32_t* plaintext_length) {
    if (!traffic || !record || !content_type || !plaintext || !plaintext_length ||
        record_length < TLS13_RECORD_HEADER_SIZE + TLS13_AEAD_TAG_SIZE + 1 ||
        traffic->sequence == UINT64_MAX || record[0] != TLS13_OUTER_CONTENT_TYPE ||
        record[1] != TLS13_LEGACY_VERSION_MAJOR || record[2] != TLS13_LEGACY_VERSION_MINOR) return -1;
    uint32_t encrypted_length = ((uint32_t)record[3] << 8) | record[4];
    if (encrypted_length != record_length - TLS13_RECORD_HEADER_SIZE ||
        encrypted_length > TLS13_RECORD_MAX_CIPHERTEXT + TLS13_AEAD_TAG_SIZE) return -1;
    uint32_t inner_length = encrypted_length - TLS13_AEAD_TAG_SIZE;
    if (inner_length > plaintext_capacity + 1 || inner_length > TLS13_RECORD_MAX_PLAINTEXT + 1) return -1;
    uint8_t inner[TLS13_RECORD_MAX_PLAINTEXT + 1];
    uint8_t nonce[12];
    traffic_nonce(traffic, nonce);
    if (tls13_chacha20_poly1305_decrypt(traffic->key, nonce, record, TLS13_RECORD_HEADER_SIZE,
                                       record + TLS13_RECORD_HEADER_SIZE, inner_length,
                                       record + TLS13_RECORD_HEADER_SIZE + inner_length, inner) != 0) return -1;
    uint32_t content_length = inner_length;
    while (content_length && inner[content_length - 1] == 0) content_length--;
    if (!content_length) return -1;
    *content_type = inner[content_length - 1];
    if (*content_type < 20 || *content_type > 23) return -1;
    content_length--;
    if (content_length > plaintext_capacity) return -1;
    for (uint32_t i = 0; i < content_length; i++) plaintext[i] = inner[i];
    *plaintext_length = content_length;
    traffic->sequence++;
    for (uint32_t i = 0; i < sizeof(inner); i++) inner[i] = 0;
    for (uint32_t i = 0; i < sizeof(nonce); i++) nonce[i] = 0;
    return 0;
}
