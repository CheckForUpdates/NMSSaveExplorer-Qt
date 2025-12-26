#pragma once

#include <cstdint>
#include <cstddef>

class SpookyHash {
public:
    static void Hash128(const void *message, size_t length, uint64_t *hash1, uint64_t *hash2) {
        uint64_t a, b, c, d;
        a = b = c = d = 0xdeadbeefdeadbeefULL;
        a += *hash1; b += *hash2;

        if (length < 192) {
            Short(message, length, &a, &b, &c, &d);
            *hash1 = a; *hash2 = b;
            return;
        }

        const uint64_t *data = static_cast<const uint64_t *>(message);
        size_t nblocks = length / 96;
        for (size_t i = 0; i < nblocks; ++i) {
            a += data[0]; b += data[1]; c += data[2]; d += data[3]; Mix(&a, &b, &c, &d); data += 4;
            a += data[0]; b += data[1]; c += data[2]; d += data[3]; Mix(&a, &b, &c, &d); data += 4;
            a += data[0]; b += data[1]; c += data[2]; d += data[3]; Mix(&a, &b, &c, &d); data += 4;
        }

        *hash1 = a; *hash2 = b;
    }

private:
    static inline uint64_t Rot64(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }
    static inline void Mix(uint64_t *a, uint64_t *b, uint64_t *c, uint64_t *d) {
        *a -= *b; *a ^= Rot64(*b, 44); *a += *d; *b -= *c; *b ^= Rot64(*c, 15); *b += *a;
        *c -= *d; *c ^= Rot64(*d, 8);  *c += *b; *d -= *a; *d ^= Rot64(*a, 32); *d += *c;
    }
    static inline void End(uint64_t *a, uint64_t *b, uint64_t *c, uint64_t *d) {
        *d ^= *c; *c = Rot64(*c, 15); *d += *c; *a ^= *d; *d = Rot64(*d, 52); *a += *d;
        *b ^= *a; *a = Rot64(*a, 26); *b += *a; *c ^= *b; *b = Rot64(*b, 51); *c += *b;
        *d ^= *c; *c = Rot64(*c, 28); *d += *c; *a ^= *d; *d = Rot64(*d, 9);  *a += *d;
        *b ^= *a; *a = Rot64(*a, 47); *b += *a; *c ^= *b; *b = Rot64(*b, 54); *c += *b;
    }

    static void Short(const void *message, size_t length, uint64_t *a, uint64_t *b, uint64_t *c, uint64_t *d) {
        const uint64_t *data = static_cast<const uint64_t *>(message);
        size_t remainder = length % 32;
        size_t nblocks = length / 32;
        for (size_t i = 0; i < nblocks; ++i) {
            *a += data[0]; *b += data[1]; *c += data[2]; *d += data[3];
            *a = Rot64(*a, 11); *a += *b; *b = Rot64(*b, 25); *b += *c;
            *c = Rot64(*c, 16); *c += *d; *d = Rot64(*d, 4);  *d += *a;
            data += 4;
        }
        *d += (uint64_t)length << 56;
        End(a, b, c, d);
    }
};
