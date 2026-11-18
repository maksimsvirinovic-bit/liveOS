#include "../user/tls13.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static uint8_t hex_digit(char value) {
    if (value >= '0' && value <= '9') return (uint8_t)(value - '0');
    if (value >= 'a' && value <= 'f') return (uint8_t)(value - 'a' + 10);
    if (value >= 'A' && value <= 'F') return (uint8_t)(value - 'A' + 10);
    return 0xFF;
}

static uint32_t from_hex(const char* text, uint8_t* output, uint32_t capacity) {
    uint32_t count = 0;
    while (text[0] && text[1]) {
        assert(count < capacity);
        uint8_t hi = hex_digit(text[0]);
        uint8_t lo = hex_digit(text[1]);
        assert(hi != 0xFF && lo != 0xFF);
        output[count++] = (uint8_t)((hi << 4) | lo);
        text += 2;
    }
    assert(text[0] == '\0');
    return count;
}

static void expect_hex(const uint8_t* actual, uint32_t size, const char* expected_hex) {
    uint8_t expected[2048];
    uint32_t expected_size = from_hex(expected_hex, expected, sizeof(expected));
    assert(size == expected_size);
    for (uint32_t i = 0; i < size; i++) {
        if (actual[i] != expected[i]) {
            fprintf(stderr, "hex mismatch at byte %u: got %02x expected %02x\n",
                    i, actual[i], expected[i]);
            abort();
        }
    }
}

static void test_sha256(void) {
    uint8_t digest[32];
    tls13_sha256("abc", 3, digest);
    expect_hex(digest, sizeof(digest),
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

static void test_x25519_rfc7748(void) {
    uint8_t alice_private[32], alice_public[32], bob_private[32], bob_public[32];
    uint8_t output[32], alice_shared[32], bob_shared[32], zero[32] = {0};
    from_hex("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a",
             alice_private, sizeof(alice_private));
    from_hex("5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb",
             bob_private, sizeof(bob_private));
    from_hex("0900000000000000000000000000000000000000000000000000000000000000",
             output, sizeof(output));
    assert(tls13_x25519(alice_public, alice_private, output) == 0);
    expect_hex(alice_public, sizeof(alice_public),
        "8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a");
    assert(tls13_x25519(bob_public, bob_private, output) == 0);
    expect_hex(bob_public, sizeof(bob_public),
        "de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f");
    assert(tls13_x25519(alice_shared, alice_private, bob_public) == 0);
    assert(tls13_x25519(bob_shared, bob_private, alice_public) == 0);
    expect_hex(alice_shared, sizeof(alice_shared),
        "4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742");
    expect_hex(bob_shared, sizeof(bob_shared),
        "4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742");
    int zero_result = tls13_x25519(output, alice_private, zero);
    if (zero_result == 0) {
        uint8_t combined = 0;
        for (uint32_t i = 0; i < sizeof(output); i++) combined |= output[i];
        fprintf(stderr, "X25519 low-order result: %02x (return %d)\n", combined, zero_result);
    }
    assert(zero_result != 0);
}

static void test_key_schedule_rfc8448(void) {
    Tls13KeySchedule schedule;
    uint8_t shared_secret[32], transcript_hash[32];
    from_hex("8bd4054fb55b9d63fdfbacf9f04b9f0d35e6d63f537563efd46272900f89492d",
             shared_secret, sizeof(shared_secret));
    from_hex("860c06edc07858ee8e78f0e7428c58edd6b43f2ca3e6e95f02ed063cf0e1cad8",
             transcript_hash, sizeof(transcript_hash));
    assert(tls13_key_schedule_handshake(&schedule, NULL, 0, shared_secret,
                                        transcript_hash) == 0);
    expect_hex(schedule.early_secret, sizeof(schedule.early_secret),
        "33ad0a1c607ec03b09e6cd9893680ce210adf300aa1f2660e1b22e10f170f92a");
    expect_hex(schedule.handshake_secret, sizeof(schedule.handshake_secret),
        "1dc826e93606aa6fdc0aadc12f741b01046aa6b99f691ed221a9f0ca043fbeac");
    expect_hex(schedule.client_handshake_traffic, sizeof(schedule.client_handshake_traffic),
        "b3eddb126e067f35a780b3abf45e2d8f3b1a950738f52e9600746a0e27a55a21");
    expect_hex(schedule.server_handshake_traffic, sizeof(schedule.server_handshake_traffic),
        "b67b7d690cc16c4e75e54213cb2d37b4e9c912bcded9105d42befd59d391ad38");
    expect_hex(schedule.master_secret, sizeof(schedule.master_secret),
        "18df06843d13a08bf2a449844c5f8a478001bc4d4c627984d5a41da8d0402919");

    uint8_t server_hello_hash[32], verify_data[32];
    from_hex("860c06edc07858ee8e78f0e7428c58edd6b43f2ca3e6e95f02ed063cf0e1cad8",
             server_hello_hash, sizeof(server_hello_hash));
    assert(tls13_finished_verify_data(schedule.server_handshake_traffic,
                                      server_hello_hash, verify_data) == 0);
    expect_hex(verify_data, sizeof(verify_data),
        "2b473488b0e9d7085d0bff61acdd4efe3b04054b306c3996352155ba24b50387");
    assert(tls13_finished_verify(schedule.server_handshake_traffic,
                                 server_hello_hash, verify_data) == 0);
    verify_data[0] ^= 1;
    assert(tls13_finished_verify(schedule.server_handshake_traffic,
                                 server_hello_hash, verify_data) != 0);

    from_hex("9608102a0f1ccc6db6250b7b7e417b1a000eaada3daae4777a7686c9ff83df13",
             transcript_hash, sizeof(transcript_hash));
    assert(tls13_key_schedule_application(&schedule, transcript_hash) == 0);
    expect_hex(schedule.client_application_traffic, sizeof(schedule.client_application_traffic),
        "9e40646ce79a7f9dc05af8889bce6552875afa0b06df0087f792ebb7c17504a5");
    expect_hex(schedule.server_application_traffic, sizeof(schedule.server_application_traffic),
        "a11af9f05531f856ad47116b45a950328204b4f44bfb6b3a4b4f1f3fcb631643");
    Tls13TrafficKey traffic;
    assert(tls13_traffic_key_init(&traffic, schedule.client_application_traffic) == 0);
    expect_hex(traffic.key, sizeof(traffic.key),
        "c8afd24f48952725381a54085e8d8e3856d8d89e3019243b30a9db54809a3732");
    expect_hex(traffic.iv, sizeof(traffic.iv), "5b78923dee08579033e523d9");

    uint8_t updated_traffic[32];
    for (uint32_t i = 0; i < sizeof(updated_traffic); i++) updated_traffic[i] = schedule.client_application_traffic[i];
    traffic.sequence = 7;
    assert(tls13_traffic_key_update(updated_traffic, &traffic) == 0);
    assert(traffic.sequence == 0);
    expect_hex(updated_traffic, sizeof(updated_traffic),
        "fcdfcc72725aaee48bf64e4fd8b749cdbdbab39d90da0b26e2245ca6ea167207");
}

static void test_transcript_and_rng(void) {
    Tls13Transcript transcript;
    uint8_t digest[32], random_bytes[32];
    tls13_transcript_init(&transcript);
    assert(tls13_transcript_update(&transcript, "abc", 3) == 0);
    assert(tls13_transcript_digest(&transcript, digest) == 0);
    expect_hex(digest, sizeof(digest),
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    int random_result = tls13_random_bytes(random_bytes, sizeof(random_bytes));
    assert(random_result == 0 || random_result == -1);
}

static void test_hkdf_rfc5869_case1(void) {
    uint8_t salt[13], input[22], info[10], prk[32], output[42];
    for (uint32_t i = 0; i < sizeof(salt); i++) salt[i] = 0x00 + i;
    for (uint32_t i = 0; i < sizeof(input); i++) input[i] = 0x0B;
    for (uint32_t i = 0; i < sizeof(info); i++) info[i] = 0xF0 + i;
    assert(tls13_hkdf_extract(salt, sizeof(salt), input, sizeof(input), prk) == 0);
    expect_hex(prk, sizeof(prk),
        "077709362c2e32df0ddc3f0dc47bba6390b6c73bb50f9c3122ec844ad7c2b3e5");
    assert(tls13_hkdf_expand(prk, info, sizeof(info), output, sizeof(output)) == 0);
    expect_hex(output, sizeof(output),
        "3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf34007208d5b887185865");
}

static void test_hkdf_expand_label_rfc8448(void) {
    uint8_t zero[32] = {0}, secret[32], transcript_hash[32], output[32];
    tls13_sha256(NULL, 0, transcript_hash);
    assert(tls13_hkdf_extract(zero, sizeof(zero), zero, sizeof(zero), secret) == 0);
    assert(tls13_hkdf_expand_label(secret, "derived", transcript_hash,
                                   sizeof(transcript_hash), output, sizeof(output)) == 0);
    expect_hex(output, sizeof(output),
        "6f2615a108c702c5678f54fc9dbab69716c076189c48250cebeac3576c3611ba");
}

static void test_chacha20_poly1305_rfc8439(void) {
    uint8_t key[32], nonce[12], aad[12], plaintext[128], expected_ciphertext[128];
    uint8_t ciphertext[128], tag[16], expected_tag[16], decrypted[128];
    uint32_t plaintext_length = from_hex(
        "4c616469657320616e642047656e746c656d656e206f662074686520636c617373206f66202739393a204966204920636f756c64206f6666657220796f75206f6e6c79206f6e652074697020666f7220746865206675747572652c2073756e73637265656e20776f756c642062652069742e",
        plaintext, sizeof(plaintext));
    from_hex("808182838485868788898a8b8c8d8e8f909192939495969798999a9b9c9d9e9f", key, sizeof(key));
    from_hex("070000004041424344454647", nonce, sizeof(nonce));
    from_hex("50515253c0c1c2c3c4c5c6c7", aad, sizeof(aad));
    uint32_t expected_length = from_hex(
        "d31a8d34648e60db7b86afbc53ef7ec2a4aded51296e08fea9e2b5a736ee62d63dbea45e8ca9671282fafb69da92728b1a71de0a9e060b2905d6a5b67ecd3b3692ddbd7f2d778b8c9803aee328091b58fab324e4fad675945585808b4831d7bc3ff4def08e4b7a9de576d26586cec64b6116",
        expected_ciphertext, sizeof(expected_ciphertext));
    from_hex("1ae10b594f09e26a7e902ecbd0600691", expected_tag, sizeof(expected_tag));
    assert(tls13_chacha20_poly1305_encrypt(key, nonce, aad, sizeof(aad), plaintext,
                                            plaintext_length, ciphertext, tag) == 0);
    expect_hex(ciphertext, expected_length,
        "d31a8d34648e60db7b86afbc53ef7ec2a4aded51296e08fea9e2b5a736ee62d63dbea45e8ca9671282fafb69da92728b1a71de0a9e060b2905d6a5b67ecd3b3692ddbd7f2d778b8c9803aee328091b58fab324e4fad675945585808b4831d7bc3ff4def08e4b7a9de576d26586cec64b6116");
    expect_hex(tag, sizeof(tag), "1ae10b594f09e26a7e902ecbd0600691");
    assert(tls13_chacha20_poly1305_decrypt(key, nonce, aad, sizeof(aad), ciphertext,
                                            expected_length, tag, decrypted) == 0);
    for (uint32_t i = 0; i < plaintext_length; i++) assert(decrypted[i] == plaintext[i]);
    tag[0] ^= 1;
    assert(tls13_chacha20_poly1305_decrypt(key, nonce, aad, sizeof(aad), ciphertext,
                                            expected_length, tag, decrypted) != 0);
}

static void test_record_protection(void) {
    Tls13TrafficKey sender = {0};
    Tls13TrafficKey receiver = {0};
    for (uint32_t i = 0; i < TLS13_AEAD_KEY_SIZE; i++) sender.key[i] = receiver.key[i] = (uint8_t)i;
    for (uint32_t i = 0; i < TLS13_AEAD_NONCE_SIZE; i++) sender.iv[i] = receiver.iv[i] = (uint8_t)(0xA0 + i);
    static const uint8_t message[] = "record payload";
    uint8_t record[TLS13_RECORD_MAX_CIPHERTEXT + TLS13_RECORD_HEADER_SIZE];
    uint8_t plaintext[TLS13_RECORD_MAX_PLAINTEXT];
    uint32_t record_length = 0, plaintext_length = 0;
    uint8_t content_type = 0;
    assert(tls13_record_seal(&sender, 23, message, sizeof(message) - 1,
                             record, sizeof(record), &record_length) == 0);
    assert(sender.sequence == 1);
    assert(tls13_record_open(&receiver, record, record_length, &content_type,
                             plaintext, sizeof(plaintext), &plaintext_length) == 0);
    assert(receiver.sequence == 1 && content_type == 23);
    assert(plaintext_length == sizeof(message) - 1);
    for (uint32_t i = 0; i < plaintext_length; i++) assert(plaintext[i] == message[i]);

    record[record_length - 1] ^= 0x80;
    assert(tls13_record_open(&receiver, record, record_length, &content_type,
                             plaintext, sizeof(plaintext), &plaintext_length) != 0);
    assert(receiver.sequence == 1);
}

int main(void) {
    test_sha256();
    test_x25519_rfc7748();
    test_key_schedule_rfc8448();
    test_transcript_and_rng();
    test_hkdf_rfc5869_case1();
    test_hkdf_expand_label_rfc8448();
    test_chacha20_poly1305_rfc8439();
    test_record_protection();
    puts("TLS 1.3 primitive tests passed");
    return 0;
}
