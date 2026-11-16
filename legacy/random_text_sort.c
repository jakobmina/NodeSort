#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <stdbool.h>
#include <ctype.h>

// ── Estructuras ──────────────────────────────────────────────
typedef struct {
    int *data;
    size_t size;
    size_t capacity;
} Bucket;

typedef struct {
    char *str;
    int value;  // valor numérico para ordenar
} TextItem;

void init_bucket(Bucket *b, size_t cap) {
    b->capacity = cap > 0 ? cap : 1024;
    b->size = 0;
    b->data = (int *)malloc(b->capacity * sizeof(int));
}

void append_bucket(Bucket *b, int val) {
    if (b->size >= b->capacity) {
        b->capacity *= 2;
        b->data = (int *)realloc(b->data, b->capacity * sizeof(int));
    }
    b->data[b->size++] = val;
}

void free_bucket(Bucket *b) {
    free(b->data);
    b->size = 0;
    b->capacity = 0;
}

// ── Generador de texto aleatorio ──────────────────────────────
void generate_random_text(TextItem *items, size_t count) {
    const char *words[] = {
        "alpha", "beta", "gamma", "delta", "epsilon",
        "zeta", "eta", "theta", "iota", "kappa",
        "lambda", "mu", "nu", "xi", "omicron"
    };
    int num_words = sizeof(words) / sizeof(words[0]);

    for (size_t i = 0; i < count; i++) {
        // Generar string aleatorio basado en palabras
        int word_idx = rand() % num_words;
        int suffix = rand() % 1000;  // número del 0-999
        
        items[i].str = (char *)malloc(64);
        snprintf(items[i].str, 64, "%s_%03d", words[word_idx], suffix);
        
        // Valor numérico: suma de caracteres ASCII mod 100
        items[i].value = suffix % 100;
    }
}

void print_text_items(TextItem *items, size_t count, const char *label) {
    printf("\n%s:\n", label);
    for (size_t i = 0; i < count; i++) {
        if (i < 20)  // mostrar primeros 20
            printf("  [%zu] %s (value: %d)\n", i, items[i].str, items[i].value);
        else if (i == 20)
            printf("  ... [%zu items more]\n", count - 20);
    }
}

// ── Comparadores ─────────────────────────────────────────────
int compare_value(const void *a, const void *b) {
    int va = *(const int *)a;
    int vb = *(const int *)b;
    return va - vb;
}

// ── Timing ───────────────────────────────────────────────────
double get_time_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

// ════════════════════════════════════════════════════════════
// NodeSort v2 adaptado para TextItem
// ════════════════════════════════════════════════════════════
typedef struct {
    int      *values;
    size_t    start, end;
    int       mn;
    double    step;
    int       num_nodos;
    Bucket   *local_buckets;
} DistArgs;

void* thread_distribute(void *arg) {
    DistArgs *da = (DistArgs *)arg;
    for (size_t i = da->start; i < da->end; i++) {
        int idx = (int)((da->values[i] - da->mn) / da->step);
        if (idx >= da->num_nodos) idx = da->num_nodos - 1;
        append_bucket(&da->local_buckets[idx], da->values[i]);
    }
    return NULL;
}

typedef struct {
    Bucket *cubeta;
} SortArgs;

void* thread_sort(void *arg) {
    SortArgs *sa = (SortArgs *)arg;
    qsort(sa->cubeta->data, sa->cubeta->size, sizeof(int), compare_value);
    return NULL;
}

void nodesort_text(int *values, size_t n, int num_nodos, int *resultado) {
    // 1. Min/Max
    int mn = values[0], mx = values[0];
    for (size_t i = 1; i < n; i++) {
        if (values[i] < mn) mn = values[i];
        if (values[i] > mx) mx = values[i];
    }
    double step = (double)(mx - mn + 1) / num_nodos;

    // 2. Distribución paralela
    pthread_t *dt = malloc(num_nodos * sizeof(pthread_t));
    DistArgs  *da = malloc(num_nodos * sizeof(DistArgs));
    size_t chunk = n / num_nodos;

    for (int t = 0; t < num_nodos; t++) {
        da[t].values = values;
        da[t].start = t * chunk;
        da[t].end = (t == num_nodos - 1) ? n : (t + 1) * chunk;
        da[t].mn = mn;
        da[t].step = step;
        da[t].num_nodos = num_nodos;
        da[t].local_buckets = malloc(num_nodos * sizeof(Bucket));
        size_t local_cap = chunk / num_nodos + 64;
        for (int b = 0; b < num_nodos; b++)
            init_bucket(&da[t].local_buckets[b], local_cap);

        pthread_create(&dt[t], NULL, thread_distribute, &da[t]);
    }
    for (int t = 0; t < num_nodos; t++)
        pthread_join(dt[t], NULL);

    // 3. Merge de cubetas locales
    Bucket *global = malloc(num_nodos * sizeof(Bucket));
    for (int b = 0; b < num_nodos; b++) {
        size_t total_size = 0;
        for (int t = 0; t < num_nodos; t++)
            total_size += da[t].local_buckets[b].size;
        init_bucket(&global[b], total_size + 1);
        for (int t = 0; t < num_nodos; t++) {
            Bucket *lb = &da[t].local_buckets[b];
            memcpy(global[b].data + global[b].size, lb->data,
                   lb->size * sizeof(int));
            global[b].size += lb->size;
            free_bucket(lb);
        }
    }

    for (int t = 0; t < num_nodos; t++)
        free(da[t].local_buckets);

    // 4. Sort paralelo
    pthread_t *st = malloc(num_nodos * sizeof(pthread_t));
    SortArgs  *sa = malloc(num_nodos * sizeof(SortArgs));
    for (int b = 0; b < num_nodos; b++) {
        sa[b].cubeta = &global[b];
        pthread_create(&st[b], NULL, thread_sort, &sa[b]);
    }

    size_t offset = 0;
    for (int b = 0; b < num_nodos; b++) {
        pthread_join(st[b], NULL);
        memcpy(resultado + offset, global[b].data,
               global[b].size * sizeof(int));
        offset += global[b].size;
        free_bucket(&global[b]);
    }

    free(dt); free(da); free(st); free(sa); free(global);
}

// ── Secuencial pura (baseline) ───────────────────────────────
void nodesort_seq(int *values, size_t n, int num_nodos, int *resultado) {
    int mn = values[0], mx = values[0];
    for (size_t i = 1; i < n; i++) {
        if (values[i] < mn) mn = values[i];
        if (values[i] > mx) mx = values[i];
    }
    double step = (double)(mx - mn + 1) / num_nodos;

    Bucket *cubetas = malloc(num_nodos * sizeof(Bucket));
    for (int i = 0; i < num_nodos; i++)
        init_bucket(&cubetas[i], n / num_nodos + 64);

    for (size_t i = 0; i < n; i++) {
        int idx = (int)((values[i] - mn) / step);
        if (idx >= num_nodos) idx = num_nodos - 1;
        append_bucket(&cubetas[idx], values[i]);
    }

    size_t offset = 0;
    for (int i = 0; i < num_nodos; i++) {
        qsort(cubetas[i].data, cubetas[i].size, sizeof(int), compare_value);
        memcpy(resultado + offset, cubetas[i].data,
               cubetas[i].size * sizeof(int));
        offset += cubetas[i].size;
        free_bucket(&cubetas[i]);
    }
    free(cubetas);
}

int main() {
    srand(time(NULL));
    
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║      Random Text Generator + NodeSort Ordering             ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n");

    size_t n = 100000;
    printf("\nGenerating %zu random text items...\n", n);
    
    // Generar items de texto
    TextItem *items = malloc(n * sizeof(TextItem));
    generate_random_text(items, n);

    // Extraer valores para ordenar
    int *values = malloc(n * sizeof(int));
    for (size_t i = 0; i < n; i++)
        values[i] = items[i].value;

    print_text_items(items, n, "Original (first 20)");

    // Benchmark
    int *sorted_seq = malloc(n * sizeof(int));
    int *sorted_v2 = malloc(n * sizeof(int));
    int *sorted_qsort = malloc(n * sizeof(int));

    printf("\n%s\n", "═══════════════════════════════════════════════════════════");
    printf("Running benchmarks (3 repetitions, best time)...\n");
    printf("%s\n", "═══════════════════════════════════════════════════════════");

    // Secuencial
    double best_seq = -1;
    for (int r = 0; r < 3; r++) {
        double t0 = get_time_ms();
        nodesort_seq(values, n, 4, sorted_seq);
        double t1 = get_time_ms();
        if (best_seq < 0 || (t1 - t0) < best_seq)
            best_seq = t1 - t0;
    }
    printf("  NodeSort (Sequential, 4 nodes):      %.2f ms\n", best_seq);

    // Paralelo v2
    double best_v2 = -1;
    for (int r = 0; r < 3; r++) {
        double t0 = get_time_ms();
        nodesort_text(values, n, 4, sorted_v2);
        double t1 = get_time_ms();
        if (best_v2 < 0 || (t1 - t0) < best_v2)
            best_v2 = t1 - t0;
    }
    printf("  NodeSort (Parallel v2, 4 threads):  %.2f ms\n", best_v2);

    // qsort nativo
    double best_qsort = -1;
    for (int r = 0; r < 3; r++) {
        memcpy(sorted_qsort, values, n * sizeof(int));
        double t0 = get_time_ms();
        qsort(sorted_qsort, n, sizeof(int), compare_value);
        double t1 = get_time_ms();
        if (best_qsort < 0 || (t1 - t0) < best_qsort)
            best_qsort = t1 - t0;
    }
    printf("  C qsort (native):                    %.2f ms\n", best_qsort);

    printf("\n%s\n", "═══════════════════════════════════════════════════════════");
    printf("  Speedup (vs qsort):\n");
    printf("    Sequential: %.2fx\n", best_qsort / best_seq);
    printf("    Parallel:   %.2fx\n", best_qsort / best_v2);
    printf("%s\n", "═══════════════════════════════════════════════════════════");

    // Mostrar resultados ordenados
    printf("\nSorted values (first 20):\n");
    for (int i = 0; i < 20 && i < (int)n; i++) {
        printf("  [%d] %d\n", i, sorted_v2[i]);
    }

    // Verificar correctitud
    memcpy(sorted_qsort, values, n * sizeof(int));
    qsort(sorted_qsort, n, sizeof(int), compare_value);
    
    bool correct = true;
    for (size_t i = 0; i < n; i++) {
        if (sorted_v2[i] != sorted_qsort[i]) {
            correct = false;
            break;
        }
    }
    printf("\n✓ Ordering correctness: %s\n", correct ? "PASSED" : "FAILED");

    // Liberación de memoria
    for (size_t i = 0; i < n; i++)
        free(items[i].str);
    free(items);
    free(values);
    free(sorted_seq);
    free(sorted_v2);
    free(sorted_qsort);

    printf("\n✓ Done!\n");
    return 0;
}
