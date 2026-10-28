#include "utf8_stream.h"
#include <string.h>

void utf8_init(Utf8RingBuffer *rb, uint8_t *buf, size_t cap) {
    rb->buffer = buf;
    rb->capacity = cap;
    rb->head = 0;
    rb->tail = 0;
    rb->count = 0;
}

bool utf8_push(Utf8RingBuffer *rb, uint8_t byte) {
    if (rb->count == rb->capacity) return false;
    rb->buffer[rb->tail] = byte;
    rb->tail = (rb->tail + 1) % rb->capacity;
    rb->count++;
    return true;
}

size_t utf8_read(Utf8RingBuffer *rb, uint8_t *out, size_t max_len) {
    size_t read = 0;
    while (read < max_len && rb->count > 0) {
        uint8_t head_byte = rb->buffer[rb->head];
        
        // Handle invalid bytes or continuation bytes at start
        if ((head_byte & 0xC0) == 0x80) {
            rb->head = (rb->head + 1) % rb->capacity;
            rb->count--;
            continue;
        }

        int seq_len = 0;
        if ((head_byte & 0x80) == 0x00) seq_len = 1;
        else if ((head_byte & 0xE0) == 0xC0) seq_len = 2;
        else if ((head_byte & 0xF0) == 0xE0) seq_len = 3;
        else if ((head_byte & 0xF8) == 0xF0) seq_len = 4;
        else {
            rb->head = (rb->head + 1) % rb->capacity;
            rb->count--;
            continue;
        }
        
        if (rb->count < (size_t)seq_len) break;
        
        for (int i = 0; i < seq_len; i++) {
            if (read >= max_len) break;
            out[read++] = rb->buffer[rb->head];
            rb->head = (rb->head + 1) % rb->capacity;
            rb->count--;
        }
    }
    return read;
}

void utf8_stream_init(Utf8Stream *s) {
    if (!s) return;
    s->pending_len = 0;
    memset(s->pending, 0, sizeof(s->pending));
}

void utf8_stream_reset(Utf8Stream *s) {
    if (!s) return;
    s->pending_len = 0;
}

size_t utf8_stream_push(Utf8Stream *s, const char *in_bytes, size_t in_len, char *out_buf, size_t out_max) {
    if (!s || !out_buf || out_max <= 1) return 0;

    // Fast path: if no pending bytes and empty input
    if (s->pending_len == 0 && (in_bytes == NULL || in_len == 0)) {
        out_buf[0] = '\0';
        return 0;
    }

    // Combine pending + incoming bytes into temporary scratch buffer
    uint8_t temp[512];
    size_t total = s->pending_len;
    if (total > sizeof(temp)) total = sizeof(temp);
    memcpy(temp, s->pending, total);

    if (in_bytes && in_len > 0) {
        size_t copy_len = in_len;
        if (total + copy_len > sizeof(temp)) {
            copy_len = sizeof(temp) - total;
        }
        memcpy(temp + total, in_bytes, copy_len);
        total += copy_len;
    }

    s->pending_len = 0;

    size_t in_pos = 0;
    size_t out_pos = 0;
    size_t out_limit = out_max - 1;

    while (in_pos < total && out_pos < out_limit) {
        uint8_t b = temp[in_pos];
        size_t seq_len = 0;

        if ((b & 0x80) == 0x00) {
            seq_len = 1;
        } else if ((b & 0xE0) == 0xC0) {
            seq_len = 2; // e.g. 0xD0 or 0xD1 for Cyrillic
        } else if ((b & 0xF0) == 0xE0) {
            seq_len = 3;
        } else if ((b & 0xF8) == 0xF0) {
            seq_len = 4;
        } else {
            // Stray continuation or invalid byte, skip
            in_pos++;
            continue;
        }

        // Check if full sequence is present
        if (in_pos + seq_len > total) {
            // Sequence is incomplete at end of buffer: hold into pending
            size_t remaining = total - in_pos;
            if (remaining > sizeof(s->pending)) remaining = sizeof(s->pending);
            memcpy(s->pending, temp + in_pos, remaining);
            s->pending_len = remaining;
            break;
        }

        // Check if output buffer has space for full sequence
        if (out_pos + seq_len > out_limit) {
            // Buffer full, keep remaining into pending
            size_t remaining = total - in_pos;
            if (remaining > sizeof(s->pending)) remaining = sizeof(s->pending);
            memcpy(s->pending, temp + in_pos, remaining);
            s->pending_len = remaining;
            break;
        }

        // Copy sequence
        for (size_t i = 0; i < seq_len; i++) {
            out_buf[out_pos++] = (char)temp[in_pos++];
        }
    }

    out_buf[out_pos] = '\0';
    return out_pos;
}
