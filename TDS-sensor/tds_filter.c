#include "tds_filter.h"

uint16_t tds_filter_median_u16(const uint16_t *src, uint16_t len, uint16_t *work)
{
    uint16_t i;
    uint16_t j;
    uint16_t key;

    if (src == 0 || work == 0 || len == 0) {
        return 0;
    }

    /* Copy input to work buffer */
    for (i = 0; i < len; i++) {
        work[i] = src[i];
    }

    /* Insertion sort (len is typically small: 10..50) */
    for (i = 1; i < len; i++) {
        key = work[i];
        j = i;
        while (j > 0 && work[(uint16_t)(j - 1)] > key) {
            work[j] = work[(uint16_t)(j - 1)];
            j--;
        }
        work[j] = key;
    }

    /* Median */
    if ((len & 1U) != 0U) {
        return work[(uint16_t)(len / 2U)];
    }

    return (uint16_t)((uint32_t)work[(uint16_t)(len / 2U)] +
                      (uint32_t)work[(uint16_t)(len / 2U - 1U)]) / 2U;
}
