#ifndef LIVEOS_TLS13_H
#define LIVEOS_TLS13_H

#include <stdint.h>

#define TLS13_SHA256_SIZE 32
#define TLS13_AEAD_KEY_SIZE 32
#define TLS13_AEAD_NONCE_SIZE 12
#define TLS13_AEAD_TAG_SIZE 16
#define TLS13_RECORD_HEADER_SIZE 5
#define TLS13_RECORD_MAX_PLAINTEXT 16384
#define TLS13_RECORD_MAX_CIPHERTEXT (TLS13_RECORD_MAX_PLAINTEXT + 256)
#define TLS13_X25519_KEY_SIZE 32

typedef struct {
    uint32_t state[8];
    uint64_t bit_count;
    uint8_t block[64];
    uint32_t block_used;
} Tls13Sha256;

typedef struct {
    uint8_t key[TLS13_AEAD_KEY_SIZE];
    uint8_t iv[TLS13_AEAD_NONCE_SIZE];
    uint64_t sequence;
} Tls13TrafficKey;

typedef struct {
    uint8_t early_secret[TLS13_SHA256_SIZE];
    uint8_t handshake_secret[TLS13_SHA256_SIZE];
    uint8_t client_handshake_traffic[TLS13_SHA256_SIZE];
    uint8_t server_handshake_traffic[TLS13_SHA256_SIZE];
    uint8_t master_secret[TLS13_SHA256_SIZE];
    uint8_t client_application_traffic[TLS13_SHA256_SIZE];
    uint8_t server_application_traffic[TLS13_SHA256_SIZE];
} Tls13KeySchedule;

typedef struct {
    Tls13Sha256 hash;
    uint8_t active;
} Tls13Transcript;

void tls13_sha256_init(Tls13Sha256* context);
void tls13_sha256_update(Tls13Sha256* context, const void* data, uint32_t length);
void tls13_sha256_final(Tls13Sha256* context, uint8_t digest[TLS13_SHA256_SIZE]);
void tls13_sha256(const void* data, uint32_t length, uint8_t digest[TLS13_SHA256_SIZE]);
int tls13_hkdf_extract(const uint8_t* salt, uint32_t salt_length,
                       const uint8_t* input, uint32_t input_length,
                       uint8_t output[TLS13_SHA256_SIZE]);
int tls13_hkdf_expand(const uint8_t secret[TLS13_SHA256_SIZE],
                      const uint8_t* info, uint32_t info_length,
                      uint8_t* output, uint32_t output_length);
int tls13_hkdf_expand_label(const uint8_t secret[TLS13_SHA256_SIZE],
                            const char* label, const uint8_t* context,
                            uint32_t context_length, uint8_t* output,
                            uint32_t output_length);
int tls13_x25519(uint8_t shared_secret[TLS13_X25519_KEY_SIZE],
                       const uint8_t private_key[TLS13_X25519_KEY_SIZE],
                       const uint8_t public_key[TLS13_X25519_KEY_SIZE]);
int tls13_key_schedule_handshake(Tls13KeySchedule* schedule,
                                 const uint8_t* psk, uint32_t psk_length,
                                 const uint8_t shared_secret[TLS13_X25519_KEY_SIZE],
                                 const uint8_t transcript_hash[TLS13_SHA256_SIZE]);
int tls13_key_schedule_application(Tls13KeySchedule* schedule,
                                const uint8_t transcript_hash[TLS13_SHA256_SIZE]);
int tls13_traffic_key_init(Tls13TrafficKey* traffic,
                                       const uint8_t traffic_secret[TLS13_SHA256_SIZE]);
int tls13_finished_verify_data(const uint8_t handshake_traffic_secret[TLS13_SHA256_SIZE],
                                           const uint8_t transcript_hash[TLS13_SHA256_SIZE],
                                           uint8_t verify_data[TLS13_SHA256_SIZE]);
int tls13_finished_verify(const uint8_t handshake_traffic_secret[TLS13_SHA256_SIZE],
                                      const uint8_t transcript_hash[TLS13_SHA256_SIZE],
                                      const uint8_t verify_data[TLS13_SHA256_SIZE]);
void tls13_transcript_init(Tls13Transcript* transcript);
int tls13_transcript_update(Tls13Transcript* transcript, const void* data, uint32_t length);
int tls13_transcript_digest(const Tls13Transcript* transcript,
                                        uint8_t digest[TLS13_SHA256_SIZE]);
int tls13_traffic_key_update(uint8_t traffic_secret[TLS13_SHA256_SIZE],
                                         Tls13TrafficKey* traffic);
int tls13_random_bytes(void* output, uint32_t length);
int tls13_chacha20_poly1305_encrypt(const uint8_t key[TLS13_AEAD_KEY_SIZE],
                                    const uint8_t nonce[TLS13_AEAD_NONCE_SIZE],
                                    const uint8_t* aad, uint32_t aad_length,
                                    const uint8_t* plaintext, uint32_t plaintext_length,
                                    uint8_t* ciphertext, uint8_t tag[TLS13_AEAD_TAG_SIZE]);
int tls13_chacha20_poly1305_decrypt(const uint8_t key[TLS13_AEAD_KEY_SIZE],
                                    const uint8_t nonce[TLS13_AEAD_NONCE_SIZE],
                                    const uint8_t* aad, uint32_t aad_length,
                                    const uint8_t* ciphertext, uint32_t ciphertext_length,
                                    const uint8_t tag[TLS13_AEAD_TAG_SIZE], uint8_t* plaintext);
int tls13_record_seal(Tls13TrafficKey* traffic, uint8_t content_type,
                      const uint8_t* plaintext, uint32_t plaintext_length,
                      uint8_t* record, uint32_t record_capacity,
                      uint32_t* record_length);
int tls13_record_open(Tls13TrafficKey* traffic, const uint8_t* record,
                      uint32_t record_length, uint8_t* content_type,
                      uint8_t* plaintext, uint32_t plaintext_capacity,
                      uint32_t* plaintext_length);

#endif
