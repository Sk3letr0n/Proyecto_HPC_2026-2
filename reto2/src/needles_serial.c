/* ============================================================================
 *  needles_serial.c - PI por Monte Carlo, metodo BUFFON'S NEEDLES, SERIAL
 *  Proyecto HPC 2026-2 - Reto 2
 *
 *  Agujas de longitud l = 1 sobre lineas separadas t = 2:
 *      P(cruce) = 2l / (t pi) = 1/pi    =>    pi ~= N / cruces
 *
 *  Variantes (-v):
 *
 *    rand     Linea base "de libro": libc rand() + sin(theta).
 *    sin      xoshiro256** + sin(theta) (nucleo de referencia de la version
 *             OpenMP). Usa pi para muestrear el angulo.
 *    rechazo  OPTIMIZACION DE CPU: angulo por muestreo por rechazo en el
 *             cuarto de circulo; prueba x^2 (u^2+v^2) <= v^2/4 sin sin(),
 *             sin sqrt() y SIN usar pi (ver nucleos.h).
 *    arreglo  ANTI-OPTIMIZACION DE MEMORIA: genera los N pares (x, theta) en
 *             dos arreglos (16 bytes * N) y luego cuenta los cruces.
 *    bloques  Genera B = 1024 pares en un buffer de 16 KiB (L1) y cuenta con
 *             un bucle separado que el compilador puede vectorizar (incluida
 *             la llamada a sin() con -Ofast/-ffast-math si hay libmvec).
 *
 *  Compilacion:
 *    gcc -O2 -Wall -Wextra -std=c11 -o bin/needles_serial src/needles_serial.c -lm
 * ========================================================================== */
#include "nucleos.h"

#define VARIANTES "rand | sin | rechazo | arreglo | bloques"
#define BLOQUE 1024

static long long contar_rand(long long n, uint64_t semilla) {
    srand((unsigned)semilla);
    long long cruces = 0;
    for (long long i = 0; i < n; i++) cruces += aguja_rand();
    return cruces;
}

static long long contar_sin(long long n, uint64_t semilla) {
    xoshiro_t r; xo_semilla(&r, semilla);
    long long cruces = 0;
    for (long long i = 0; i < n; i++) cruces += aguja_sin(&r);
    return cruces;
}

static long long contar_rechazo(long long n, uint64_t semilla) {
    xoshiro_t r; xo_semilla(&r, semilla);
    long long cruces = 0;
    for (long long i = 0; i < n; i++) cruces += aguja_rechazo(&r);
    return cruces;
}

static long long contar_arreglo(long long n, uint64_t semilla) {
    double *xs = malloc((size_t)n * sizeof *xs);
    double *th = malloc((size_t)n * sizeof *th);
    if (!xs || !th) {
        fprintf(stderr, "Error: sin memoria para %lld agujas (%.1f MiB)\n",
                n, 2.0 * n * sizeof(double) / (1024.0 * 1024.0));
        exit(1);
    }
    xoshiro_t r; xo_semilla(&r, semilla);
    for (long long i = 0; i < n; i++) {
        xs[i] = xo_double(&r);
        th[i] = xo_double(&r) * (M_PI / 2);
    }
    long long cruces = 0;
    for (long long i = 0; i < n; i++)
        cruces += xs[i] <= 0.5 * sin(th[i]);
    free(xs); free(th);
    return cruces;
}

static long long contar_bloques(long long n, uint64_t semilla) {
    double xs[BLOQUE], th[BLOQUE];
    xoshiro_t r; xo_semilla(&r, semilla);
    long long cruces = 0;
    for (long long base = 0; base < n; base += BLOQUE) {
        int m = (n - base < BLOQUE) ? (int)(n - base) : BLOQUE;
        for (int i = 0; i < m; i++) { xs[i] = xo_double(&r); th[i] = xo_double(&r) * (M_PI / 2); }
        int local = 0;
        for (int i = 0; i < m; i++)
            local += xs[i] <= 0.5 * sin(th[i]);
        cruces += local;
    }
    return cruces;
}

int main(int argc, char **argv) {
    opciones_t op;
    if (!leer_opciones(argc, argv, &op, VARIANTES, "sin")) return 1;

    long long (*f)(long long, uint64_t) = NULL;
    if      (!strcmp(op.variante, "rand"))    f = contar_rand;
    else if (!strcmp(op.variante, "sin"))     f = contar_sin;
    else if (!strcmp(op.variante, "rechazo")) f = contar_rechazo;
    else if (!strcmp(op.variante, "arreglo")) f = contar_arreglo;
    else if (!strcmp(op.variante, "bloques")) f = contar_bloques;
    else { fprintf(stderr, "Variante desconocida: %s (%s)\n", op.variante, VARIANTES); return 1; }

    double t0 = reloj_segundos();
    long long cruces = f(op.n, op.semilla);
    double t = reloj_segundos() - t0;

    double pi = cruces > 0 ? (double)op.n / (double)cruces : 0.0;
    imprimir_resultado("needles_serial", &op, 1, pi, t);
    return 0;
}
