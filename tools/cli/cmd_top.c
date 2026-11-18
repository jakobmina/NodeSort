/* `nodesort top`: top-k rows of an embedding matrix by similarity to a query vector.
 * SPDX-License-Identifier: MIT */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cli.h"
#include "thread.h"

enum metric { M_COSINE, M_DOT, M_L2 };
static const char *const metric_name[] = {"cosine", "dot", "l2"};

static void usage(FILE *out) {
    fprintf(out,
            "usage: nodesort top MATRIX --query Q [-k K] [--metric M] [--ids IDS] [-o OUT.npy]\n"
            "                    [--dim D] [--type T] [--threads N]\n"
            "\n"
            "  MATRIX        N x D embeddings: 2-D .npy (float32/float64/int32/uint32), or a raw\n"
            "                file with --dim D\n"
            "  --query Q     query vector: .npy of length D (or 1 x D), or raw with D values\n"
            "  -k K          number of results (default 10)\n"
            "  --metric M    cosine (default), dot, or l2 (smaller distance = more similar)\n"
            "  --ids IDS     uint32/uint64 .npy or raw file with one ID per row (default: row "
            "number)\n"
            "  -o OUT.npy    also write the results as a structured .npy with fields id, score\n"
            "  --dim D       row length of a raw MATRIX\n"
            "  --type T      element type of raw files without a typed extension: f32, f64, i32, "
            "u32\n"
            "  --threads N   threads for scoring and ranking (default: all cores)\n");
}

/* ── Scoring ───────────────────────────────────────────────────────────── */
typedef struct {
    const array_t *m;
    const double *q;
    double qnorm;
    enum metric metric;
    size_t begin, end;
    float *scores;
    ns_thread thread;
} score_job;

/* One scoring loop per element type; values are widened to double. */
#define DEFINE_SCORE(NAME, T)                                                                      \
    static void NAME(score_job *j) {                                                               \
        const T *base = (const T *)j->m->data;                                                     \
        size_t d = j->m->cols;                                                                     \
        for (size_t r = j->begin; r < j->end; r++) {                                               \
            const T *x = base + r * d;                                                             \
            double acc = 0, xx = 0;                                                                \
            if (j->metric == M_L2) {                                                               \
                for (size_t c = 0; c < d; c++) {                                                   \
                    double diff = (double)x[c] - j->q[c];                                          \
                    acc += diff * diff;                                                            \
                }                                                                                  \
                j->scores[r] = (float)sqrt(acc);                                                   \
                continue;                                                                          \
            }                                                                                      \
            for (size_t c = 0; c < d; c++) {                                                       \
                double v = (double)x[c];                                                           \
                acc += v * j->q[c];                                                                \
                xx += v * v;                                                                       \
            }                                                                                      \
            if (j->metric == M_COSINE) acc = xx > 0 ? acc / (sqrt(xx) * j->qnorm) : (double)NAN;   \
            j->scores[r] = (float)acc;                                                             \
        }                                                                                          \
    }

DEFINE_SCORE(score_f32, float)
DEFINE_SCORE(score_f64, double)
DEFINE_SCORE(score_i32, int32_t)
DEFINE_SCORE(score_u32, uint32_t)

static void score_rows(void *arg) {
    score_job *j = (score_job *)arg;
    switch (j->m->type) {
    case DT_F32: score_f32(j); break;
    case DT_F64: score_f64(j); break;
    case DT_I32: score_i32(j); break;
    default: score_u32(j); break;
    }
}

/* Scores every row, spreading rows over `threads` threads (the caller works too). */
static void score_all(const array_t *m, const double *q, double qnorm, enum metric metric,
                      float *scores, unsigned threads) {
    size_t min_rows = 1024;
    size_t max_threads = m->rows / min_rows;
    if (threads > max_threads) threads = (unsigned)(max_threads ? max_threads : 1);
    if (threads > 256) threads = 256;
    score_job jobs[256];
    size_t per = m->rows / threads, extra = m->rows % threads, at = 0;
    for (unsigned t = 0; t < threads; t++) {
        jobs[t].m = m;
        jobs[t].q = q;
        jobs[t].qnorm = qnorm;
        jobs[t].metric = metric;
        jobs[t].scores = scores;
        jobs[t].begin = at;
        at += per + (t < extra ? 1u : 0u);
        jobs[t].end = at;
    }
    unsigned started = 1;
    for (; started < threads; started++)
        if (ns_thread_start(&jobs[started].thread, score_rows, &jobs[started]) != 0) break;
    for (unsigned t = started; t < threads; t++) score_rows(&jobs[t]); /* thread unavailable */
    score_rows(&jobs[0]);
    for (unsigned t = 1; t < started; t++) ns_thread_join(&jobs[t].thread);
}

static unsigned long long id_at(const array_t *ids, size_t i) {
    if (ids->type == DT_U32) return ((const uint32_t *)ids->data)[i];
    return (unsigned long long)((const uint64_t *)ids->data)[i];
}

static double element_as_double(const array_t *a, size_t i) {
    switch (a->type) {
    case DT_F32: return (double)((const float *)a->data)[i];
    case DT_F64: return ((const double *)a->data)[i];
    case DT_I32: return (double)((const int32_t *)a->data)[i];
    case DT_U32: return (double)((const uint32_t *)a->data)[i];
    default: return (double)((const uint64_t *)a->data)[i];
    }
}

/* ── Command ───────────────────────────────────────────────────────────── */
int cmd_top(int argc, char **argv) {
    const char *matrix_path = NULL, *query_path = NULL, *ids_path = NULL, *out_path = NULL;
    uint64_t k = 10, dim = 0, x;
    enum metric metric = M_COSINE;
    dtype raw_type = DT_NONE;
    unsigned threads = 0;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *v;
        if (strcmp(a, "--help") == 0 || strcmp(a, "-h") == 0) {
            usage(stdout);
            return EXIT_OK;
        } else if (strcmp(a, "--query") == 0 || strcmp(a, "-q") == 0) {
            if (!(query_path = option_value(argc, argv, &i))) return EXIT_USAGE;
        } else if (strcmp(a, "--ids") == 0) {
            if (!(ids_path = option_value(argc, argv, &i))) return EXIT_USAGE;
        } else if (strcmp(a, "-o") == 0 || strcmp(a, "--output") == 0) {
            if (!(out_path = option_value(argc, argv, &i))) return EXIT_USAGE;
        } else if (strcmp(a, "-k") == 0) {
            if (!(v = option_value(argc, argv, &i))) return EXIT_USAGE;
            if (!parse_count(v, &k) || k == 0) {
                cli_error("-k must be a positive number (got '%s')", v);
                return EXIT_USAGE;
            }
        } else if (strcmp(a, "--dim") == 0) {
            if (!(v = option_value(argc, argv, &i))) return EXIT_USAGE;
            if (!parse_count(v, &dim) || dim == 0) {
                cli_error("--dim must be a positive number (got '%s')", v);
                return EXIT_USAGE;
            }
        } else if (strcmp(a, "--metric") == 0) {
            if (!(v = option_value(argc, argv, &i))) return EXIT_USAGE;
            if (strcmp(v, "cosine") == 0)
                metric = M_COSINE;
            else if (strcmp(v, "dot") == 0)
                metric = M_DOT;
            else if (strcmp(v, "l2") == 0)
                metric = M_L2;
            else {
                cli_error("--metric must be cosine, dot or l2 (got '%s')", v);
                return EXIT_USAGE;
            }
        } else if (strcmp(a, "--type") == 0) {
            if (!(v = option_value(argc, argv, &i))) return EXIT_USAGE;
            if (!dtype_parse(v, &raw_type) || raw_type == DT_U64) {
                cli_error("--type must be f32, f64, i32 or u32 (got '%s')", v);
                return EXIT_USAGE;
            }
        } else if (strcmp(a, "--threads") == 0) {
            if (!(v = option_value(argc, argv, &i))) return EXIT_USAGE;
            if (!parse_count(v, &x) || x > 4096) {
                cli_error("--threads must be a number between 0 and 4096 (got '%s')", v);
                return EXIT_USAGE;
            }
            threads = (unsigned)x;
        } else if (a[0] == '-' && a[1] != '\0') {
            cli_error("top: unknown option '%s'", a);
            usage(stderr);
            return EXIT_USAGE;
        } else if (!matrix_path) {
            matrix_path = a;
        } else {
            cli_error("top: only one matrix file is accepted ('%s')", a);
            return EXIT_USAGE;
        }
    }
    if (!matrix_path || !query_path) {
        if (matrix_path) cli_error("top: --query is required");
        usage(stderr);
        return EXIT_USAGE;
    }

    char err[ERR_LEN];
    int rc = EXIT_FILE;
    array_t m, q, ids;
    memset(&m, 0, sizeof m);
    memset(&q, 0, sizeof q);
    memset(&ids, 0, sizeof ids);
    double *qd = NULL;
    float *scores = NULL;

    if (array_read(matrix_path, raw_type, (size_t)dim, &m, err) != 0) {
        cli_error("%s", err);
        goto done;
    }
    if (m.ndim != 2) {
        cli_error("%s: expected an N x D matrix (raw files need --dim D)", matrix_path);
        goto done;
    }
    if (m.type == DT_U64) {
        cli_error("%s: uint64 embeddings are not supported", matrix_path);
        goto done;
    }
    if (m.from_npy && dim != 0 && dim != m.cols) {
        cli_error("%s: --dim %llu does not match the matrix (%zu columns)", matrix_path,
                  (unsigned long long)dim, m.cols);
        goto done;
    }
    if (array_read(query_path, raw_type, 0, &q, err) != 0) {
        cli_error("%s", err);
        goto done;
    }
    if (q.count != m.cols || (q.ndim == 2 && q.rows != 1)) {
        cli_error("%s: query has %zu values but the matrix rows have %zu", query_path, q.count,
                  m.cols);
        goto done;
    }
    if (ids_path) {
        if (array_read(ids_path, DT_NONE, 0, &ids, err) != 0) {
            cli_error("%s", err);
            goto done;
        }
        if (ids.type != DT_U32 && ids.type != DT_U64) {
            cli_error("%s: IDs must be uint32 or uint64 (got %s)", ids_path, dtype_name(ids.type));
            goto done;
        }
        if (ids.ndim != 1 || ids.count != m.rows) {
            cli_error("%s: %zu IDs for %zu matrix rows", ids_path, ids.count, m.rows);
            goto done;
        }
    } else {
        ids.type = m.rows > UINT32_MAX ? DT_U64 : DT_U32;
        ids.ndim = 1;
        ids.rows = ids.count = m.rows;
        ids.cols = 1;
        ids.data = malloc((m.rows ? m.rows : 1) * dtype_size(ids.type));
        if (!ids.data) {
            cli_error("out of memory");
            rc = EXIT_SORT;
            goto done;
        }
        for (size_t r = 0; r < m.rows; r++) {
            if (ids.type == DT_U32)
                ((uint32_t *)ids.data)[r] = (uint32_t)r;
            else
                ((uint64_t *)ids.data)[r] = (uint64_t)r;
        }
    }

    qd = (double *)malloc((m.cols ? m.cols : 1) * sizeof *qd);
    scores = (float *)malloc((m.rows ? m.rows : 1) * sizeof *scores);
    if (!qd || !scores) {
        cli_error("out of memory");
        rc = EXIT_SORT;
        goto done;
    }
    double qq = 0;
    for (size_t c = 0; c < m.cols; c++) {
        qd[c] = element_as_double(&q, c);
        qq += qd[c] * qd[c];
    }
    if (metric == M_COSINE && !(qq > 0)) {
        cli_error("%s: the query vector has zero length; cosine similarity is undefined",
                  query_path);
        goto done;
    }

    unsigned nthreads = threads ? threads : cpu_count();
    double t0 = now_ms();
    score_all(&m, qd, sqrt(qq), metric, scores, nthreads);
    double t1 = now_ms();

    nodesort_options opt;
    nodesort_options_init(&opt);
    opt.threads = threads;
    opt.descending = metric != M_L2;
    nodesort_status st = nodesort_kv_f32(scores, ids.data, dtype_size(ids.type), m.rows, &opt);
    double t2 = now_ms();
    if (st != NODESORT_OK) {
        cli_error("%s", nodesort_strerror(st));
        rc = EXIT_SORT;
        goto done;
    }

    size_t shown = (size_t)(k < m.rows ? k : m.rows);
    char cnt[32];
    printf("top %zu of %s rows (%zu dims), metric %s: scored in %.2f ms, ranked in %.2f ms\n\n",
           shown, fmt_count(m.rows, cnt, sizeof cnt), m.cols, metric_name[metric], t1 - t0,
           t2 - t1);
    printf("  %6s  %20s  %12s\n", "rank", "id", metric == M_L2 ? "distance" : "score");
    for (size_t r = 0; r < shown; r++) {
        printf("  %6zu  %20llu  %12.6f\n", r + 1, id_at(&ids, r), (double)scores[r]);
    }
    rc = EXIT_OK;

    if (out_path) {
        if (npy_write_id_score(out_path, ids.data, ids.type, scores, shown, err) != 0) {
            cli_error("%s", err);
            rc = EXIT_FILE;
        } else {
            printf("\nwrote %s\n", out_path);
        }
    }

done:
    free(qd);
    free(scores);
    array_free(&m);
    array_free(&q);
    array_free(&ids);
    return rc;
}
