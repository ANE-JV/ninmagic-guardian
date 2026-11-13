#ifndef NINMAGIC_GUARDIAN_SHA256_H
#define NINMAGIC_GUARDIAN_SHA256_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
  uint32_t h[8];
  uint64_t bytes;
  unsigned used;
  unsigned char block[64];
} gc_sha256;
void gc_sha256_init(gc_sha256 *);
void gc_sha256_update(gc_sha256 *, const void *, size_t);
void gc_sha256_final(gc_sha256 *, unsigned char out[32]);
void gc_sha256_buffer(const void *, size_t, unsigned char out[32]);
#ifdef __cplusplus
}
#endif
#endif
