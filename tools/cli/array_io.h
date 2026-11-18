/* Reading and writing numeric arrays: NumPy .npy and raw little-endian files.
 * SPDX-License-Identifier: MIT */
#ifndef NODESORT_CLI_ARRAY_IO_H
#define NODESORT_CLI_ARRAY_IO_H

#include <stddef.h>
#include <stdint.h>

typedef enum { DT_NONE, DT_F32, DT_F64, DT_I32, DT_U32, DT_U64 } dtype;

typedef struct {
    dtype type;
    size_t ndim;     /* 1 or 2 */
    size_t rows;     /* shape[0] */
    size_t cols;     /* shape[1]; 1 for 1-D arrays */
    size_t count;    /* rows * cols */
    void *data;      /* malloc'd, `count` elements of `type` */
    int from_npy;    /* 1 = read from .npy, 0 = raw */
} array_t;

#define ERR_LEN 256

size_t dtype_size(dtype t);
const char *dtype_name(dtype t); /* "float32", "uint64", ... */

/* "f32", "f64", "i32", "u32", "u64" -> type; returns 0 if unknown. */
int dtype_parse(const char *s, dtype *out);

/* 1 if `path` ends in .npy (case-insensitive). */
int path_is_npy(const char *path);

/* Raw file type from its extension (.f32 .f64 .i32 .u32 .u64); returns 0 if none. */
int dtype_from_extension(const char *path, dtype *out);

/* Reads `path`. For raw files, `raw_type` gives the element type (DT_NONE = from the extension) and
 * `raw_cols` the row length (0 = 1-D). Returns 0 on success; otherwise fills `err`. */
int array_read(const char *path, dtype raw_type, size_t raw_cols, array_t *out, char *err);

/* Writes `a` as .npy (if `as_npy`) or raw. Returns 0 on success; otherwise fills `err`. */
int array_write(const char *path, const array_t *a, int as_npy, char *err);

/* Writes a structured .npy of `n` records {id, score}: ids are uint32 or uint64, scores float32. */
int npy_write_id_score(const char *path, const void *ids, dtype id_type, const float *scores,
                       size_t n, char *err);

void array_free(array_t *a);

#endif /* NODESORT_CLI_ARRAY_IO_H */
