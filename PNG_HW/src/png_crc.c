#include "png_crc.h"
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

uint32_t crc_table[256];

int crc_table_computed = 0;

void make_crc_table(void)
{
    uint32_t c;

    for (uint32_t n = 0; n < 256; n++) {
        c = n;
        for (size_t k = 0; k < 8; k++) {
            if (c & 1)
                c = 0xedb88320 ^ (c >> 1);
            else
                c = c >> 1;
        }
        crc_table[n] = c;
    }
    crc_table_computed = 1;
}

uint32_t update_crc(unsigned long crc, const uint8_t *buf, int len)
{
    uint32_t c = crc;
    int n;

    if (!crc_table_computed)
        make_crc_table();
    for (n = 0; n < len; n++) {
        c = crc_table[(c ^ buf[n]) & 0xff] ^ (c >> 8);
    }
    return c;
}

uint32_t png_crc(const uint8_t *buf, size_t len)
{
    return update_crc(0xFFFFFFFF, buf, len) ^ 0xFFFFFFFF;
}
