#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint64_t rng_state;

static uint64_t next_random(void) {
    uint64_t x = rng_state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    rng_state = x;
    return x * UINT64_C(2685821657736338717);
}

static void *checked_calloc(size_t count, size_t size) {
    if (size != 0 && count > SIZE_MAX / size) {
        fprintf(stderr, "Error: matriz demasiado grande.\n");
        exit(EXIT_FAILURE);
    }
    void *p = calloc(count, size);
    if (!p) {
        perror("calloc");
        exit(EXIT_FAILURE);
    }
    return p;
}

static int parse_int(const char *s, const char *name, int min_value) {
    char *end = NULL;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (errno || !end || *end || v < min_value || v > INT_MAX) {
        fprintf(stderr, "Valor no valido para %s: %s\n", name, s);
        exit(EXIT_FAILURE);
    }
    return (int)v;
}

static int *read_matrix(const char *path, int *n_out) {
    FILE *f = fopen(path, "r");
    if (!f) {
        perror(path);
        exit(EXIT_FAILURE);
    }
    int n;
    if (fscanf(f, "%d", &n) != 1 || n <= 0) {
        fprintf(stderr, "Formato invalido en %s\n", path);
        fclose(f);
        exit(EXIT_FAILURE);
    }
    size_t cells = (size_t)n * (size_t)n;
    if (cells / (size_t)n != (size_t)n) {
        fprintf(stderr, "Matriz demasiado grande en %s\n", path);
        fclose(f);
        exit(EXIT_FAILURE);
    }
    int *m = checked_calloc(cells, sizeof(*m));
    for (size_t i = 0; i < cells; ++i) {
        if (fscanf(f, "%d", &m[i]) != 1) {
            fprintf(stderr, "Faltan datos en %s (elemento %zu)\n", path, i);
            free(m);
            fclose(f);
            exit(EXIT_FAILURE);
        }
    }
    fclose(f);
    *n_out = n;
    return m;
}

static void write_random_matrix(const char *path, int n, int limit) {
    FILE *f = fopen(path, "w");
    if (!f) {
        perror(path);
        exit(EXIT_FAILURE);
    }
    fprintf(f, "%d\n", n);
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j) {
            int value = (int)(next_random() % ((uint64_t)limit + 1));
            fprintf(f, "%d%c", value, j + 1 == n ? '\n' : ' ');
        }
    }
    if (fclose(f) != 0) {
        perror("fclose");
        exit(EXIT_FAILURE);
    }
}

static void make_path(char *dst, size_t cap, const char *dir, const char *name) {
    int written = snprintf(dst, cap, "%s/%s", dir, name);
    if (written < 0 || (size_t)written >= cap) {
        fprintf(stderr, "Ruta demasiado larga.\n");
        exit(EXIT_FAILURE);
    }
}

static void command_init(int argc, char **argv) {
    if (argc != 6) {
        fprintf(stderr, "Uso: %s init N LIMITE SEMILLA DIRECTORIO\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    int n = parse_int(argv[2], "N", 1);
    int limit = parse_int(argv[3], "LIMITE", 0);
    char *end = NULL;
    errno = 0;
    uint64_t seed = strtoull(argv[4], &end, 10);
    if (errno || !end || *end) {
        fprintf(stderr, "Semilla no valida: %s\n", argv[4]);
        exit(EXIT_FAILURE);
    }
    rng_state = seed ? seed : UINT64_C(0x9e3779b97f4a7c15);

    char a_path[PATH_MAX], b_path[PATH_MAX];
    make_path(a_path, sizeof(a_path), argv[5], "A.txt");
    make_path(b_path, sizeof(b_path), argv[5], "B.txt");
    write_random_matrix(a_path, n, limit);
    write_random_matrix(b_path, n, limit);
    printf("Matrices A y B creadas: N=%d, limite=%d, semilla=%" PRIu64 "\n",
           n, limit, seed);
}

static void command_worker(int argc, char **argv) {
    if (argc != 6) {
        fprintf(stderr, "Uso: %s worker DIRECTORIO FILA_INICIAL FILA_FINAL ARCHIVO_SALIDA\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    const char *dir = argv[2];
    int start = parse_int(argv[3], "FILA_INICIAL", 0);
    int end = parse_int(argv[4], "FILA_FINAL", 0);
    const char *output = argv[5];

    char a_path[PATH_MAX], b_path[PATH_MAX];
    make_path(a_path, sizeof(a_path), dir, "A.txt");
    make_path(b_path, sizeof(b_path), dir, "B.txt");

    int na, nb;
    int *a = read_matrix(a_path, &na);
    int *b = read_matrix(b_path, &nb);
    if (na != nb || start > end || end > na) {
        fprintf(stderr, "Rango o dimensiones invalidas.\n");
        free(a); free(b);
        exit(EXIT_FAILURE);
    }

    FILE *f = fopen(output, "w");
    if (!f) {
        perror(output);
        free(a); free(b);
        exit(EXIT_FAILURE);
    }

    fprintf(f, "%d %d %d\n", start, end, na);
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    for (int i = start; i < end; ++i) {
        for (int j = 0; j < na; ++j) {
            __int128 sum = 0;
            for (int k = 0; k < na; ++k)
                sum += (__int128)a[(size_t)i * na + k] * b[(size_t)k * na + j];

            if (sum > LLONG_MAX || sum < LLONG_MIN) {
                fprintf(stderr, "Desbordamiento en C[%d][%d].\n", i, j);
                fclose(f); free(a); free(b);
                exit(EXIT_FAILURE);
            }
            fprintf(f, "%lld%c", (long long)sum, j + 1 == na ? '\n' : ' ');
        }
    }

    if (fclose(f) != 0) {
        perror("fclose");
        free(a); free(b);
        exit(EXIT_FAILURE);
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double elapsed = (double)(t1.tv_sec - t0.tv_sec) +
                     (double)(t1.tv_nsec - t0.tv_nsec) / 1e9;
    printf("Worker: filas [%d, %d), tiempo de calculo: %.6f s\n",
           start, end, elapsed);
    free(a);
    free(b);
}

static void command_merge(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "Uso: %s merge DIRECTORIO ARCHIVO_C_FINAL\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    const char *dir = argv[2];
    const char *output = argv[3];
    const char *parts[] = {"C_wn1.txt", "C_wn2.txt", "C_wn3.txt"};

    char first_path[PATH_MAX];
    make_path(first_path, sizeof(first_path), dir, parts[0]);
    FILE *first = fopen(first_path, "r");
    if (!first) {
        perror(first_path);
        exit(EXIT_FAILURE);
    }
    int s0, e0, n;
    if (fscanf(first, "%d %d %d", &s0, &e0, &n) != 3 || n <= 0) {
        fprintf(stderr, "Cabecera invalida en %s\n", first_path);
        fclose(first);
        exit(EXIT_FAILURE);
    }
    fclose(first);

    size_t cells = (size_t)n * (size_t)n;
    if (cells / (size_t)n != (size_t)n) {
        fprintf(stderr, "Matriz demasiado grande.\n");
        exit(EXIT_FAILURE);
    }
    long long *c = checked_calloc(cells, sizeof(*c));
    unsigned char *seen = checked_calloc((size_t)n, sizeof(*seen));

    for (int p = 0; p < 3; ++p) {
        char path[PATH_MAX];
        make_path(path, sizeof(path), dir, parts[p]);
        FILE *f = fopen(path, "r");
        if (!f) {
            perror(path);
            free(c); free(seen);
            exit(EXIT_FAILURE);
        }
        int start, end, file_n;
        if (fscanf(f, "%d %d %d", &start, &end, &file_n) != 3 ||
            file_n != n || start < 0 || end < start || end > n) {
            fprintf(stderr, "Cabecera o rango invalido en %s\n", path);
            fclose(f); free(c); free(seen);
            exit(EXIT_FAILURE);
        }
        for (int i = start; i < end; ++i) {
            if (seen[i]) {
                fprintf(stderr, "La fila %d aparece mas de una vez.\n", i);
                fclose(f); free(c); free(seen);
                exit(EXIT_FAILURE);
            }
            seen[i] = 1;
            for (int j = 0; j < n; ++j) {
                if (fscanf(f, "%lld", &c[(size_t)i * n + j]) != 1) {
                    fprintf(stderr, "Faltan datos para la fila %d en %s\n", i, path);
                    fclose(f); free(c); free(seen);
                    exit(EXIT_FAILURE);
                }
            }
        }
        fclose(f);
    }

    for (int i = 0; i < n; ++i) {
        if (!seen[i]) {
            fprintf(stderr, "Falta la fila %d: no se puede ensamblar C.\n", i);
            free(c); free(seen);
            exit(EXIT_FAILURE);
        }
    }

    FILE *out = fopen(output, "w");
    if (!out) {
        perror(output);
        free(c); free(seen);
        exit(EXIT_FAILURE);
    }
    fprintf(out, "%d\n", n);
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j)
            fprintf(out, "%lld%c", c[(size_t)i * n + j],
                    j + 1 == n ? '\n' : ' ');
    }
    fclose(out);
    printf("Matriz C ensamblada correctamente en %s\n", output);
    free(c);
    free(seen);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr,
            "Comandos:\n"
            "  %s init N LIMITE SEMILLA DIRECTORIO\n"
            "  %s worker DIRECTORIO FILA_INICIAL FILA_FINAL ARCHIVO_SALIDA\n"
            "  %s merge DIRECTORIO ARCHIVO_C_FINAL\n",
            argv[0], argv[0], argv[0]);
        return EXIT_FAILURE;
    }
    if (strcmp(argv[1], "init") == 0) command_init(argc, argv);
    else if (strcmp(argv[1], "worker") == 0) command_worker(argc, argv);
    else if (strcmp(argv[1], "merge") == 0) command_merge(argc, argv);
    else {
        fprintf(stderr, "Comando desconocido: %s\n", argv[1]);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
