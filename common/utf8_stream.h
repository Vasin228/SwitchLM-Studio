#ifndef UTF8_STREAM_H
#define UTF8_STREAM_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Legacy ring buffer interface
typedef struct {
    uint8_t *buffer;
    size_t capacity;
    size_t head;
    size_t tail;
    size_t count;
} Utf8RingBuffer;

void utf8_init(Utf8RingBuffer *rb, uint8_t *buf, size_t cap);
bool utf8_push(Utf8RingBuffer *rb, uint8_t byte);
size_t utf8_read(Utf8RingBuffer *rb, uint8_t *out, size_t max_len);

// Streaming UTF-8 multi-byte protector
// Holds incomplete 0xD0/0xD1 Cyrillic or multi-byte sequences across token chunk boundaries
typedef struct {
    uint8_t pending[8];
    size_t pending_len;
} Utf8Stream;

void utf8_stream_init(Utf8Stream *s);
void utf8_stream_reset(Utf8Stream *s);
size_t utf8_stream_push(Utf8Stream *s, const char *in_bytes, size_t in_len, char *out_buf, size_t out_max);

#ifdef __cplusplus
}
#endif

#endif // UTF8_STREAM_H
