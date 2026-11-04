/* ============================================================================
 *  dartboard_serial.c - PI por Monte Carlo, metodo DARTBOARD, VERSION SERIAL
 *  Proyecto HPC 2026-2 - Reto 2
 *
 *  pi ~= 4 * (dardos dentro del cuarto de circulo) / N
 *
 *  Variantes (-v), todas con el mismo N y la misma semilla:
 *
 *    rand     Linea base "de libro": libc rand(), 2 llamadas por dardo.
 *    double   xoshiro256** con dos doubles por dardo (nucleo de referencia
 *             que usa tambien la version OpenMP).
 *    entero   OPTIMIZACION DE CPU: un solo aleatorio de 64 bits -> dos
 *             coordenadas de 31 bits y comparacion entera (ver nucleos.h).
 *    arreglo  ANTI-OPTIMIZACION DE MEMORIA: primero genera los N puntos en
 *             dos arreglos (16 bytes * N) y luego los cuenta. Convierte un
 *             problema de computo puro en uno limitado por ancho de banda y
 *             capacidad de RAM. Sirve para medir el coste de la memoria.
 *    bloques  OPTIMIZACION DE MEMORIA/CPU: genera B = 1024 puntos en un
 *             buffer de 16 KiB (cabe en L1d de 32 KiB) y los cuenta con un
 *             bucle sin dependencias que el compilador puede vectorizar.
 *
 *  Compilacion:
 *    gcc -O2 -Wall -Wextra -std=c11 -o bin/dartboard_serial src/dartboard_serial.c -lm
 * ========================================================================== */
#include "nucleos.h"

#define VARIANTES "rand | double | entero | arreglo | bloques"
#define BLOQUE 1024

static long long contar_rand(long long n, uint64_t semilla) {
    srand((unsigned)semilla);
    long long dentro = 0;
    for (long long i = 0; i < n; i++) dentro += dardo_rand();
    return dentro;
}

static long long contar_double(long long n, uint64_t semilla) {
    xoshiro_t r; xo_semilla(&r, semilla);
    long long dentro = 0;
    for (long long i = 0; i < n; i++) dentro += dardo_double(&r);
    return dentro;
}

static long long contar_entero(long long n, uint64_t semilla) {
    xoshiro_t r; xo_semilla(&r, semilla);
    long long dentro = 0;
    for (long long i = 0; i < n; i++) dentro += dardo_entero(&r);
    return dentro;
}

static long long contar_arreglo(long long n, uint64_t semilla) {
    double *xs = malloc((size_t)n * sizeof *xs);
    double *ys = malloc((size_t)n * sizeof *ys);
    if (!xs || !ys) {
        fprintf(stderr, "Error: sin memoria para %lld puntos (%.1f MiB)\n",
                n, 2.0 * n * sizeof(double) / (1024.0 * 1024.0));
        exit(1);
    }
    xoshiro_t r; xo_semilla(&r, semilla);
    for (long long i = 0; i < n; i++) {           /* fase 1: escribir 16N B */
        xs[i] = xo_double(&r);
        ys[i] = xo_double(&r);
    }
    long long dentro = 0;
    for (long long i = 0; i < n; i++)             /* fase 2: leer 16N B     */
        dentro += xs[i] * xs[i] + ys[i] * ys[i] <= 1.0;
    free(xs); free(ys);
    return dentro;
}

static long long contar_bloques(long long n, uint64_t semilla) {
    double xs[BLOQUE], ys[BLOQUE];                /* 16 KiB en la pila (L1) */
    xoshiro_t r; xo_semilla(&r, semilla);
    long long dentro = 0;
    for (long long base = 0; base < n; base += BLOQUE) {
        int m = (n - base < BLOQUE) ? (int)(n - base) : BLOQUE;
        for (int i = 0; i < m; i++) { xs[i] = xo_double(&r); ys[i] = xo_double(&r); }
        int local = 0;
        for (int i = 0; i < m; i++)               /* vectorizable */
            local += xs[i] * xs[i] + ys[i] * ys[i] <= 1.0;
        dentro += local;
    }
    return dentro;
}

int main(int argc, char **argv) {
    opciones_t op;
    if (!leer_opciones(argc, argv, &op, VARIANTES, "double")) return 1;

    long long (*f)(long long, uint64_t) = NULL;
    if      (!strcmp(op.variante, "rand"))    f = contar_rand;
    else if (!strcmp(op.variante, "double"))  f = contar_double;
    else if (!strcmp(op.variante, "entero"))  f = contar_entero;
    else if (!strcmp(op.variante, "arreglo")) f = contar_arreglo;
    else if (!strcmp(op.variante, "bloques")) f = contar_bloques;
    else { fprintf(stderr, "Variante desconocida: %s (%s)\n", op.variante, VARIANTES); return 1; }

    double t0 = reloj_segundos();
    long long dentro = f(op.n, op.semilla);
    double t = reloj_segundos() - t0;

    imprimir_resultado("dartboard_serial", &op, 1, 4.0 * (double)dentro / (double)op.n, t);
    return 0;
}
