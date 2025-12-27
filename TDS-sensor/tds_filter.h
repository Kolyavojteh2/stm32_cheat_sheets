#ifndef TDS_FILTER_H
#define TDS_FILTER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Returns median of src[] using work[] as scratch buffer (len elements). */
uint16_t tds_filter_median_u16(const uint16_t *src, uint16_t len, uint16_t *work);

#ifdef __cplusplus
}
#endif

#endif /* TDS_FILTER_H */
