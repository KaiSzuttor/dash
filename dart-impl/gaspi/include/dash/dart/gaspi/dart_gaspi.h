#ifndef DART_GASPI_H
#define DART_GASPI_H

#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
#include <GASPI.h>
#include <dash/dart/if/dart.h>
#include <dash/dart/if/dart_types.h>
#include "gaspi_utils.h"

#define DART_MAX_TEAM_NUMBER (256)
#define DART_INTERFACE_ON
#define DART_GASPI_BUFFER_SIZE 1 << 13

extern gaspi_rank_t dart_gaspi_rank_num;
extern gaspi_rank_t dart_gaspi_rank;
extern const gaspi_segment_id_t dart_gaspi_buffer_id;
extern gaspi_pointer_t dart_gaspi_buffer_ptr;
extern const gaspi_segment_id_t dart_mempool_seg_localalloc;

#define DART_INTERFACE_OFF

#ifdef __cplusplus
}
#endif

#endif /* DART_GASPI_H */
