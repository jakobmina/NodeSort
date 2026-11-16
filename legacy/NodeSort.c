#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <stdbool.h>

// ── Estructuras Dinámicas ──────────────────────────────────
typedef struct {
    int *data;
    size_t size;
    size_t capacity;
} Bucket;

void init_bucket(Bucket *b) {
    b->capacity = 1024;
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

// ── Lógica Base ─────────────────────────────────────────────
int compare_llave_orden(const void *a, const void *b) {
    int val_a = *(const int *)a;
    int val_b = *(const int *)b;
    
    int dec_a = (val_a / 10) * 10;
    int dec_b = (val_b / 10) * 10;
    
    if (dec_a != dec_b) {
        return dec_a - dec_b;
    }
    return val_a - val_b;
}

int compare_native(const void *a, const void *b) {
    return (*(int *)a - *(int *)b);
}

Bucket* distribuir(int *lista, size_t n, int num_nodos) {
    int mn = lista[0], mx = lista[0];
    for (size_t i = 1; i < n; i++) {
        if (lista[i] < mn) mn = lista[i];
        if (lista[i] > mx) mx = lista[i];
    }

    double esp = (double)(mx - mn);
    Bucket *cubetas = (Bucket *)malloc(num_nodos * sizeof(Bucket));
    for (int i = 0; i < num_nodos; i++) {
        init_bucket(&cubetas[i]);
    }

    double step = esp / num_nodos;
    
    // Distribución optimizada (evita el bucle for anidado de Python)
    for (size_t i = 0; i < n; i++) {
        int idx = (int)((lista[i] - mn) / step);
        if (idx >= num_nodos) idx = num_nodos - 1; // Seguridad para el valor máximo
        append_bucket(&cubetas[idx], lista[i]);
    }
    return cubetas;
}

// ── Versiones ────────────────────────────────────────────────
void sort_secuencial(int *lista, size_t n, int num_nodos, int *resultado) {
    Bucket *cubetas = distribuir(lista, n, num_nodos);
    size_t offset = 0;
    
    for (int i = 0; i < num_nodos; i++) {
        qsort(cubetas[i].data, cubetas[i].size, sizeof(int), compare_llave_orden);
        memcpy(resultado + offset, cubetas[i].data, cubetas[i].size * sizeof(int));
        offset += cubetas[i].size;
        free_bucket(&cubetas[i]);
    }
    free(cubetas);
}

// Estructura para pasar argumentos a pthread
typedef struct {
    Bucket *cubeta;
} ThreadArgs;

void* ordenar_cubeta(void *args) {
    ThreadArgs *ta = (ThreadArgs *)args;
    qsort(ta->cubeta->data, ta->cubeta->size, sizeof(int), compare_llave_orden);
    return NULL;
}

void sort_threads(int *lista, size_t n, int num_nodos, int *resultado) {
    Bucket *cubetas = distribuir(lista, n, num_nodos);
    
    pthread_t *threads = (pthread_t *)malloc(num_nodos * sizeof(pthread_t));
    ThreadArgs *args = (ThreadArgs *)malloc(num_nodos * sizeof(ThreadArgs));
    
    for (int i = 0; i < num_nodos; i++) {
        args[i].cubeta = &cubetas[i];
        pthread_create(&threads[i], NULL, ordenar_cubeta, &args[i]);
    }
    
    size_t offset = 0;
    for (int i = 0; i < num_nodos; i++) {
        pthread_join(threads[i], NULL);
        memcpy(resultado + offset, cubetas[i].data, cubetas[i].size * sizeof(int));
        offset += cubetas[i].size;
        free_bucket(&cubetas[i]);
    }
    
    free(threads);
    free(args);
    free(cubetas);
}

// ── Benchmark ────────────────────────────────────────────────
double get_time_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (ts.tv_sec * 1000.0) + (ts.tv_nsec / 1000000.0);
}

bool check_sorted(int *arr, int *ref, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (arr[i] != ref[i]) return false;
    }
    return true;
}

void shuffle(int *array, size_t n) {
    for (size_t i = 0; i < n - 1; i++) {
        size_t j = i + rand() / (RAND_MAX / (n - i) + 1);
        int t = array[j];
        array[j] = array[i];
        array[i] = t;
    }
}

typedef void (*SortFunc)(int *, size_t, int, int *);

typedef struct {
    char label[30];
    SortFunc fn;
    int nodos;
} Config;

void benchmark(int *lista, size_t n, Config *configs, int num_configs, int reps) {
    int *ref = (int *)malloc(n * sizeof(int));
    memcpy(ref, lista, n * sizeof(int));
    qsort(ref, n, sizeof(int), compare_native); // Referencia correcta

    printf("\n%s\n", "──────────────────────────────────────────────────────────────");
    printf("  n=%zu   repeticiones=%d\n", n, reps);
    printf("%s\n", "──────────────────────────────────────────────────────────────");
    printf("  %-28s %6s %8s %9s %4s\n", "Modo", "Nodos", "ms", "speedup", "✓");
    printf("  %-28s %6s %8s %9s %4s\n", "────────────────────────────", "──────", "────────", "─────────", "────");

    double base_ms = -1.0;
    int *buffer = (int *)malloc(n * sizeof(int));

    for (int c = 0; c < num_configs; c++) {
        double min_ms = -1.0;
        bool correcto = false;

        for (int r = 0; r < reps; r++) {
            double t0 = get_time_ms();
            configs[c].fn(lista, n, configs[c].nodos, buffer);
            double t1 = get_time_ms();
            double elapsed = t1 - t0;

            if (min_ms < 0 || elapsed < min_ms) {
                min_ms = elapsed;
            }
        }
        
        // Verifica con la última corrida
        correcto = check_sorted(buffer, ref, n);

        char speedup_str[16];
        if (base_ms < 0) {
            base_ms = min_ms;
            strcpy(speedup_str, "─");
        } else {
            snprintf(speedup_str, sizeof(speedup_str), "%.2fx", base_ms / min_ms);
        }

        printf("  %-28s %6d %8.2f %9s %4s\n", configs[c].label, configs[c].nodos, min_ms, speedup_str, correcto ? "✓" : "✗");
    }

    // Timsort/Qsort nativo como referencia
    double min_ms_nativo = -1.0;
    for (int r = 0; r < reps; r++) {
        memcpy(buffer, lista, n * sizeof(int));
        double t0 = get_time_ms();
        qsort(buffer, n, sizeof(int), compare_native);
        double t1 = get_time_ms();
        double elapsed = t1 - t0;
        if (min_ms_nativo < 0 || elapsed < min_ms_nativo) min_ms_nativo = elapsed;
    }
    printf("  %-28s %6s %8.2f %9s %4s\n", "── C nativo (qsort) ──", "─", min_ms_nativo, "", check_sorted(buffer, ref, n) ? "✓" : "✗");

    free(ref);
    free(buffer);
}

int main() {
    srand(7); // Semilla pseudoaleatoria

    size_t sizes[] = {10000, 100000, 500000};
    int num_sizes = sizeof(sizes) / sizeof(sizes[0]);

    Config configs[] = {
        {"Secuencial", sort_secuencial, 2},
        {"Threads  2 nodos", sort_threads, 2},
        {"Threads  4 nodos", sort_threads, 4},
        {"Threads  8 nodos", sort_threads, 8}
    };
    int num_configs = sizeof(configs) / sizeof(configs[0]);

    for (int s = 0; s < num_sizes; s++) {
        size_t n = sizes[s];
        int *lista = (int *)malloc(n * sizeof(int));
        
        // Llenar con valores y barajar
        for (size_t i = 0; i < n; i++) {
            lista[i] = (rand() % (n * 10)) + 1;
        }
        
        benchmark(lista, n, configs, num_configs, 3);
        free(lista);
    }

    return 0;
}