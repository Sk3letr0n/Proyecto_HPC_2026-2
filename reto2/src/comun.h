/* ============================================================================
 *  comun.h - Utilidades compartidas por los cuatro programas del Reto 2
 *            (calculo de PI por Monte Carlo: Dartboard y Buffon's Needles)
 *  Proyecto HPC 2026-2
 *
 *  Contiene:
 *    1. Reloj de pared portable (QueryPerformanceCounter / clock_gettime).
 *    2. Generador pseudoaleatorio xoshiro256** (Blackman & Vigna, 2018):
 *         - 4 palabras de 64 bits de estado (32 bytes), periodo 2^256 - 1.
 *         - ~1 ns por numero, sin divisiones ni llamadas a la biblioteca.
 *         - Funcion de SALTO (jump) que avanza 2^128 posiciones: permite dar a
 *           cada hilo un sub-flujo propio que NO se solapa con el de los demas.
 *       Por que no rand():
 *         - En MinGW/UCRT RAND_MAX = 32767: solo 15 bits de resolucion, es
 *           decir, el "tablero" se discretiza en una rejilla de 32768 x 32768.
 *         - Es una llamada a una DLL (no se puede inlinear ni vectorizar).
 *         - Su estado es global u oculto por hilo: no sirve para obtener
 *           flujos independientes y reproducibles por hilo.
 *    3. Lectura de opciones de linea de comandos comun a todos los programas.
 *    4. Impresion de resultados (legible o una linea CSV ';' con coma decimal,
 *       como los demas CSV del repositorio).
 * ========================================================================== */
#ifndef COMUN_H
#define COMUN_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Tamano de linea de cache (x86-64). Se usa para el relleno anti false sharing. */
#define LINEA_CACHE 64

/* ---------------------------------------------------------------------------
 *  1. Reloj de pared
 * ------------------------------------------------------------------------- */
#if defined(_WIN32)
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
  static double reloj_segundos(void) {
      LARGE_INTEGER f, t;
      QueryPerformanceFrequency(&f);
      QueryPerformanceCounter(&t);
      return (double)t.QuadPart / (double)f.QuadPart;
  }
#else
  static double reloj_segundos(void) {
      struct timespec ts;
      clock_gettime(CLOCK_MONOTONIC, &ts);
      return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
  }
#endif

/* ---------------------------------------------------------------------------
 *  2. xoshiro256**  (dominio publico, https://prng.di.unimi.it/)
 * ------------------------------------------------------------------------- */
typedef struct { uint64_t s[4]; } xoshiro_t;

static inline uint64_t rotl64(uint64_t x, int k) {
    return (x << k) | (x >> (64 - k));
}

/* splitmix64: expande una semilla de 64 bits en el estado de 256 bits. */
static inline uint64_t splitmix64(uint64_t *x) {
    uint64_t z = (*x += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

static inline void xo_semilla(xoshiro_t *r, uint64_t semilla) {
    for (int i = 0; i < 4; i++) r->s[i] = splitmix64(&semilla);
}

static inline uint64_t xo_siguiente(xoshiro_t *r) {
    uint64_t *s = r->s;
    const uint64_t resultado = rotl64(s[1] * 5, 7) * 9;
    const uint64_t t = s[1] << 17;
    s[2] ^= s[0];
    s[3] ^= s[1];
    s[1] ^= s[2];
    s[0] ^= s[3];
    s[2] ^= t;
    s[3] = rotl64(s[3], 45);
    return resultado;
}

/* Double uniforme en [0, 1) con 53 bits de mantisa. */
static inline double xo_double(xoshiro_t *r) {
    return (double)(xo_siguiente(r) >> 11) * 0x1.0p-53;
}

/* Avanza el generador 2^128 posiciones (sub-flujos no solapados por hilo). */
static inline void xo_salto(xoshiro_t *r) {
    static const uint64_t SALTO[] = { 0x180ec6d33cfd0abaULL, 0xd5a61266f0c9392cULL,
                                      0xa9582618e03fc9aaULL, 0x39abdc4529b1661cULL };
    uint64_t s0 = 0, s1 = 0, s2 = 0, s3 = 0;
    for (int i = 0; i < 4; i++)
        for (int b = 0; b < 64; b++) {
            if (SALTO[i] & (UINT64_C(1) << b)) {
                s0 ^= r->s[0]; s1 ^= r->s[1]; s2 ^= r->s[2]; s3 ^= r->s[3];
            }
            xo_siguiente(r);
        }
    r->s[0] = s0; r->s[1] = s1; r->s[2] = s2; r->s[3] = s3;
}

/* Estado del hilo `id`: misma semilla base + `id` saltos de 2^128. */
static inline void xo_semilla_hilo(xoshiro_t *r, uint64_t semilla, int id) {
    xo_semilla(r, semilla);
    for (int i = 0; i < id; i++) xo_salto(r);
}

/* ---------------------------------------------------------------------------
 *  3. Opciones de linea de comandos
 *
 *    -n <muestras>   numero de puntos/agujas (acepta 1e8, 2.5e7, ...)
 *    -s <semilla>    semilla del generador              (defecto 42)
 *    -t <hilos>      hilos OpenMP (solo versiones omp)  (defecto: OMP)
 *    -v <variante>   variante del algoritmo a ejecutar
 *    -c              salida CSV en una linea
 * ------------------------------------------------------------------------- */
typedef struct {
    long long   n;
    uint64_t    semilla;
    int         hilos;
    const char *variante;
    int         csv;
} opciones_t;

static void uso(const char *prog, const char *variantes, const char *defecto) {
    fprintf(stderr,
        "Uso: %s -n <muestras> [-s <semilla>] [-t <hilos>] [-v <variante>] [-c]\n"
        "  -n  numero de muestras (admite notacion 1e8)\n"
        "  -s  semilla (defecto 42)\n"
        "  -t  hilos OpenMP (solo versiones paralelas)\n"
        "  -v  variante: %s (defecto %s)\n"
        "  -c  salida CSV: programa;variante;hilos;N;pi;error_abs;tiempo_s\n",
        prog, variantes, defecto);
}

static int leer_opciones(int argc, char **argv, opciones_t *op,
                         const char *variantes, const char *defecto) {
    op->n = 100000000LL;
    op->semilla = 42;
    op->hilos = 0;
    op->variante = defecto;
    op->csv = 0;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "-c")) { op->csv = 1; continue; }
        if (!strcmp(a, "-h") || !strcmp(a, "--help")) { uso(argv[0], variantes, defecto); exit(0); }
        if (i + 1 >= argc) { uso(argv[0], variantes, defecto); return 0; }
        const char *v = argv[++i];
        if      (!strcmp(a, "-n")) op->n = (long long)strtod(v, NULL);
        else if (!strcmp(a, "-s")) op->semilla = strtoull(v, NULL, 10);
        else if (!strcmp(a, "-t")) op->hilos = atoi(v);
        else if (!strcmp(a, "-v")) op->variante = v;
        else { uso(argv[0], variantes, defecto); return 0; }
    }
    if (op->n <= 0) { fprintf(stderr, "Error: -n debe ser > 0\n"); return 0; }
    return 1;
}

/* ---------------------------------------------------------------------------
 *  4. Impresion de resultados
 * ------------------------------------------------------------------------- */
static void coma_decimal(char *buf) {
    for (; *buf; buf++) if (*buf == '.') *buf = ',';
}

static void imprimir_resultado(const char *programa, const opciones_t *op,
                               int hilos, double pi, double t) {
    double err = fabs(pi - M_PI);
    if (op->csv) {
        char spi[32], serr[32], st[32];
        snprintf(spi, sizeof spi, "%.10f", pi);
        snprintf(serr, sizeof serr, "%.3e", err);
        snprintf(st, sizeof st, "%.6f", t);
        coma_decimal(spi); coma_decimal(serr); coma_decimal(st);
        printf("%s;%s;%d;%lld;%s;%s;%s\n", programa, op->variante, hilos,
               op->n, spi, serr, st);
    } else {
        printf("Programa   : %s\n", programa);
        printf("Variante   : %s\n", op->variante);
        printf("Hilos      : %d\n", hilos);
        printf("Muestras   : %lld\n", op->n);
        printf("PI estimado: %.10f\n", pi);
        printf("Error abs  : %.3e   (1/sqrt(N) = %.3e)\n", err, 1.0 / sqrt((double)op->n));
        printf("Tiempo (s) : %.6f\n", t);
        printf("Muestras/s : %.3e\n", (double)op->n / t);
    }
}

#endif /* COMUN_H */
