#pragma once

#include <cstdint>
#include <vector>

class XXTEA {
public:
    static void decrypt(uint32_t *v, int n, uint32_t const k[4]) {
        uint32_t y, z, sum;
        unsigned p, rounds, e;
        if (n < 2) return;
        rounds = 6 + 52 / n;
        sum = rounds * 0x9E3779B9;
        y = v[0];
        do {
            e = (sum >> 2) & 3;
            for (p = n - 1; p > 0; p--) {
                z = v[p - 1];
                y = v[p] -= ((((z >> 5) ^ (y << 2)) + ((y >> 3) ^ (z << 4))) ^ ((sum ^ y) + (k[(p & 3) ^ e] ^ z)));
            }
            z = v[n - 1];
            y = v[0] -= ((((z >> 5) ^ (y << 2)) + ((y >> 3) ^ (z << 4))) ^ ((sum ^ y) + (k[(p & 3) ^ e] ^ z)));
            sum -= 0x9E3779B9;
        } while (--rounds);
    }
    
    static void encrypt(uint32_t *v, int n, uint32_t const k[4]) {
        uint32_t y, z, sum;
        unsigned p, rounds, e;
        if (n < 2) return;
        rounds = 6 + 52 / n;
        sum = 0;
        z = v[n - 1];
        do {
            sum += 0x9E3779B9;
            e = (sum >> 2) & 3;
            for (p = 0; p < (unsigned)n - 1; p++) {
                y = v[p + 1];
                z = v[p] += ((((z >> 5) ^ (y << 2)) + ((y >> 3) ^ (z << 4))) ^ ((sum ^ y) + (k[(p & 3) ^ e] ^ z)));
            }
            y = v[0];
            z = v[n - 1] += ((((z >> 5) ^ (y << 2)) + ((y >> 3) ^ (z << 4))) ^ ((sum ^ y) + (k[(p & 3) ^ e] ^ z)));
        } while (--rounds);
    }
};
