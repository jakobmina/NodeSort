#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <stdbool.h>

// ── Estructuras ──────────────────────────────────────────────
typedef struct {
    int *data;
    size_t size;
    size_t capacity;
} Bucket;

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

// ── Comparadores ─────────────────────────────────────────────
int compare_llave(const void *a, const void *b) {
    int va = *(const int *)a;
    int vb = *(const int *)b;
    int da = (va / 10) * 10;
    int db = (vb / 10) * 10;
    return (da != db) ? (da - db) : (va - vb);
}

int compare_native(const void *a, const void *b) {
    return *(int *)a - *(int *)b;
}

// ── Timing ───────────────────────────────────────────────────
double get_time_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

bool check_sorted(int *arr, int *ref, size_t n) {
    for (size_t i = 0; i < n; i++)
        if (arr[i] != ref[i]) return false;
    return true;
}

// ════════════════════════════════════════════════════════════
// VERSIÓN 1 — Distribución secuencial + sort paralelo (v1)
// ════════════════════════════════════════════════════════════
Bucket* distribuir_seq(int *lista, size_t n, int mn, int mx, int num_nodos) {
    double step = (double)(mx - mn) / num_nodos;
    Bucket *cubetas = (Bucket *)malloc(num_nodos * sizeof(Bucket));
    for (int i = 0; i < num_nodos; i++)
        init_bucket(&cubetas[i], n / num_nodos + 64);

    for (size_t i = 0; i < n; i++) {
        int idx = (int)((lista[i] - mn) / step);
        if (idx >= num_nodos) idx = num_nodos - 1;
        append_bucket(&cubetas[idx], lista[i]);
    }
    return cubetas;
}

typedef struct {
    Bucket *cubeta;
} SortArgs;

void* thread_sort(void *arg) {
    SortArgs *sa = (SortArgs *)arg;
    qsort(sa->cubeta->data, sa->cubeta->size, sizeof(int), compare_llave);
    return NULL;
}

void sort_v1(int *lista, size_t n, int num_nodos, int *resultado) {
    int mn = lista[0], mx = lista[0];
    for (size_t i = 1; i < n; i++) {
        if (lista[i] < mn) mn = lista[i];
        if (lista[i] > mx) mx = lista[i];
    }

    Bucket *cubetas = distribuir_seq(lista, n, mn, mx, num_nodos);

    pthread_t *threads = malloc(num_nodos * sizeof(pthread_t));
    SortArgs  *sargs   = malloc(num_nodos * sizeof(SortArgs));

    for (int i = 0; i < num_nodos; i++) {
        sargs[i].cubeta = &cubetas[i];
        pthread_create(&threads[i], NULL, thread_sort, &sargs[i]);
    }

    size_t offset = 0;
    for (int i = 0; i < num_nodos; i++) {
        pthread_join(threads[i], NULL);
        memcpy(resultado + offset, cubetas[i].data, cubetas[i].size * sizeof(int));
        offset += cubetas[i].size;
        free_bucket(&cubetas[i]);
    }
    free(threads); free(sargs); free(cubetas);
}

// ════════════════════════════════════════════════════════════
// VERSIÓN 2 — Distribución paralela + sort paralelo (v2)
//
// Idea: cada thread escanea su PROPIO segmento de la lista
// original y llena cubetas locales. Luego se fusionan las
// cubetas locales por nodo antes de ordenar.
// ════════════════════════════════════════════════════════════
typedef struct {
    // Entrada
    int      *lista;
    size_t    start, end;       // segmento de lista que este thread procesa
    int       mn;
    double    step;
    int       num_nodos;
    // Salida: cubetas locales de este thread (una por nodo)
    Bucket   *local_buckets;    // [num_nodos]
} DistArgs;

void* thread_distribute(void *arg) {
    DistArgs *da = (DistArgs *)arg;
    for (size_t i = da->start; i < da->end; i++) {
        int idx = (int)((da->lista[i] - da->mn) / da->step);
        if (idx >= da->num_nodos) idx = da->num_nodos - 1;
        append_bucket(&da->local_buckets[idx], da->lista[i]);
    }
    return NULL;
}

void sort_v2(int *lista, size_t n, int num_nodos, int *resultado) {
    // 1. Min/Max (paralelo sería posible, aquí lo dejamos rápido en O(n))
    int mn = lista[0], mx = lista[0];
    for (size_t i = 1; i < n; i++) {
        if (lista[i] < mn) mn = lista[i];
        if (lista[i] > mx) mx = lista[i];
    }
    double step = (double)(mx - mn) / num_nodos;

    // 2. Distribución paralela — cada thread llena sus cubetas locales
    pthread_t *dt = malloc(num_nodos * sizeof(pthread_t));
    DistArgs  *da = malloc(num_nodos * sizeof(DistArgs));
    size_t chunk  = n / num_nodos;

    for (int t = 0; t < num_nodos; t++) {
        da[t].lista       = lista;
        da[t].start       = t * chunk;
        da[t].end         = (t == num_nodos - 1) ? n : (t + 1) * chunk;
        da[t].mn          = mn;
        da[t].step        = step;
        da[t].num_nodos   = num_nodos;
        da[t].local_buckets = malloc(num_nodos * sizeof(Bucket));
        size_t local_cap  = chunk / num_nodos + 64;
        for (int b = 0; b < num_nodos; b++)
            init_bucket(&da[t].local_buckets[b], local_cap);

        pthread_create(&dt[t], NULL, thread_distribute, &da[t]);
    }
    for (int t = 0; t < num_nodos; t++)
        pthread_join(dt[t], NULL);

    // 3. Merge de cubetas locales por nodo → cubeta global por nodo
    //    Cada nodo acumula los fragmentos de todos los threads
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
    // Liberar local_buckets de cada thread correctamente
    for (int t = 0; t < num_nodos; t++)
        free(da[t].local_buckets);

    // 4. Sort paralelo de cada cubeta global
    pthread_t *st  = malloc(num_nodos * sizeof(pthread_t));
    SortArgs  *sa  = malloc(num_nodos * sizeof(SortArgs));
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
void sort_seq(int *lista, size_t n, int num_nodos, int *resultado) {
    int mn = lista[0], mx = lista[0];
    for (size_t i = 1; i < n; i++) {
        if (lista[i] < mn) mn = lista[i];
        if (lista[i] > mx) mx = lista[i];
    }
    Bucket *cubetas = distribuir_seq(lista, n, mn, mx, num_nodos);
    size_t offset = 0;
    for (int i = 0; i < num_nodos; i++) {
        qsort(cubetas[i].data, cubetas[i].size, sizeof(int), compare_llave);
        memcpy(resultado + offset, cubetas[i].data,
               cubetas[i].size * sizeof(int));
        offset += cubetas[i].size;
        free_bucket(&cubetas[i]);
    }
    free(cubetas);
}

// ── Benchmark ────────────────────────────────────────────────
typedef void (*SortFunc)(int *, size_t, int, int *);
typedef struct { char label[36]; SortFunc fn; int nodos; } Config;

void benchmark(int *lista, size_t n, Config *cfgs, int nc, int reps) {
    int *ref = malloc(n * sizeof(int));
    int *buf = malloc(n * sizeof(int));
    memcpy(ref, lista, n * sizeof(int));
    qsort(ref, n, sizeof(int), compare_native);

    printf("\n%s\n", "──────────────────────────────────────────────────────────────");
    printf("  n=%zu   reps=%d\n", n, reps);
    printf("%s\n", "──────────────────────────────────────────────────────────────");
    printf("  %-34s %5s %8s %9s %3s\n",
           "Modo", "Nodos", "ms", "speedup", "✓");
    printf("  %-34s %5s %8s %9s %3s\n",
           "──────────────────────────────────", "─────",
           "────────", "─────────", "───");

    double base_ms = -1;
    for (int c = 0; c < nc; c++) {
        double best = -1;
        for (int r = 0; r < reps; r++) {
            double t0 = get_time_ms();
            cfgs[c].fn(lista, n, cfgs[c].nodos, buf);
            double t1 = get_time_ms();
            double el = t1 - t0;
            if (best < 0 || el < best) best = el;
        }
        bool ok = check_sorted(buf, ref, n);
        char sp[16];
        if (base_ms < 0) { base_ms = best; strcpy(sp, "─"); }
        else snprintf(sp, sizeof(sp), "%.2fx", base_ms / best);
        printf("  %-34s %5d %8.2f %9s %3s\n",
               cfgs[c].label, cfgs[c].nodos, best, sp, ok ? "✓" : "✗");
    }

    // qsort nativo
    double best_q = -1;
    for (int r = 0; r < reps; r++) {
        memcpy(buf, lista, n * sizeof(int));
        double t0 = get_time_ms();
        qsort(buf, n, sizeof(int), compare_native);
        double t1 = get_time_ms();
        double el = t1 - t0;
        if (best_q < 0 || el < best_q) best_q = el;
    }
    printf("  %-34s %5s %8.2f %9s %3s\n",
           "── C nativo qsort ──", "─", best_q, "", "✓");
    free(ref); free(buf);
}

int main() {
    srand(7);
    size_t sizes[] = {10000, 100000, 500000, 1000000};
    int ns = sizeof(sizes) / sizeof(sizes[0]);

    Config cfgs[] = {
        {"Secuencial            (dist+sort)", sort_seq, 2},
        {"v1: dist-seq  + sort-par  2t",      sort_v1,  2},
        {"v1: dist-seq  + sort-par  4t",      sort_v1,  4},
        {"v1: dist-seq  + sort-par  8t",      sort_v1,  8},
        {"v2: dist-par  + sort-par  2t",      sort_v2,  2},
        {"v2: dist-par  + sort-par  4t",      sort_v2,  4},
        {"v2: dist-par  + sort-par  8t",      sort_v2,  8},
    };
    int nc = sizeof(cfgs) / sizeof(cfgs[0]);

    for (int s = 0; s < ns; s++) {
        size_t n = sizes[s];
        int *lista = malloc(n * sizeof(int));
        for (size_t i = 0; i < n; i++)
            lista[i] = (rand() % (n * 10)) + 1;
        benchmark(lista, n, cfgs, nc, 3);
        free(lista);
    }
    return 0;
}
