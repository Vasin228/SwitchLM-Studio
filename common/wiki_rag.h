#ifndef WIKI_RAG_H
#define WIKI_RAG_H

#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WIKI_MAGIC 0x4B575753 // "SWWK"

typedef struct {
    uint64_t hash;     // 64-bit FNV-1a hash
    uint32_t offset;   // Offset in .idx file
    uint16_t length;   // Byte length of text snippet
    uint16_t reserved; // Padding for explicit 16-byte alignment
} WikiEntry;

typedef struct {
    FILE *f;
    uint32_t entry_count;
    uint32_t index_start;
} WikiIndex;

int wiki_open(WikiIndex *idx, const char *path);
void wiki_close(WikiIndex *idx);
int wiki_search(WikiIndex *idx, uint64_t hash, char *out_buf, size_t buf_size);

// 64-bit FNV-1a hash calculation
uint64_t wiki_fnv1a(const char *str);
uint64_t wiki_fnv1a_64_lower(const char *str);

// Zero-RAM binary lookup directly by file path
bool wiki_rag_lookup_by_hash(const char *path, uint64_t hash, char *out_buf, size_t buf_size);

#ifdef __cplusplus
}
#endif

#endif // WIKI_RAG_H
