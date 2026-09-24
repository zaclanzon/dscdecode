/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * Deterministic sanitizer smoke campaign, not a coverage-guided fuzzer.
 * Invoke with seed files made by concatenating each PPS and its payload.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int LLVMFuzzerTestOneInput(const uint8_t *, size_t);
static uint32_t state = 0x9e3779b9;

static uint32_t random32(void)
{
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

int main(int argc, char **argv)
{
    uint8_t seed[65536], work[65536];
    size_t total = 0;
    int arg;
    for (arg = 1; arg < argc; arg++) {
        FILE *f = fopen(argv[arg], "rb");
        size_t n, i;
        if (!f) {
            perror(argv[arg]);
            return 1;
        }
        n = fread(seed, 1, sizeof(seed), f);
        fclose(f);
        if (n < 128) return 2;
        for (i = 0; i < 40000; i++) {
            unsigned k, changes = 1 + random32() % 16;
            size_t len = n;
            memcpy(work, seed, n);
            if (i % 8 == 0)
                len = random32() % (n + 1);
            else if (i % 8 == 1) {
                for (k = 0; k < n; k++) {
                    work[k] = (uint8_t)random32();
                }
            } else
                for (k = 0; k < changes; k++) {
                    size_t base = i % 3 ? 128 : 0;
                    if (n <= base) base = 0;
                    size_t at = base + random32() % (n - base);
                    work[at] ^= (uint8_t)(1u << (random32() % 8));
                }
            LLVMFuzzerTestOneInput(work, len);
            total++;
        }
    }
    printf("%zu deterministic mutated-input executions completed\n", total);
    return 0;
}
