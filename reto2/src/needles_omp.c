/* ============================================================================
 *  needles_omp.c - PI por Monte Carlo, metodo BUFFON'S NEEDLES, VERSION OpenMP
 *  Proyecto HPC 2026-2 - Reto 2
 *
 *  Misma estrategia y mismas variantes que dartboard_omp.c (ver alli la
 *  explicacion detallada), aplicadas a las agujas de Buffon (l = 1, t = 2):
 *
 *    critical  contador compartido + #pragma omp critical      (serializa)
 *    atomic    contador compartido + #pragma omp atomic        (contention)
 *    rand      reduction(+) con libc rand() + sin()            (RNG inadecuado)
 *    falso     estado/contador por hilo en arreglos contiguos  (false sharing)
 *    padding   estado/contador por hilo alineado a 64 B        (sin false sharing)
 *    reduction generador local + reduction(+:cruces), nucleo sin()
 *    opt       'reduction' + nucleo por rechazo sin sin()/sqrt()/pi
 *
 *  Diferencia clave con Dartboard: aqui cada muestra cuesta MUCHO mas
 *  (sin() son decenas de ciclos) y casi no toca memoria, por lo que la
 *  relacion computo / sincronizacion es mayor: los efectos de contention y
 *  false sharing se diluyen, y el escalado es mas cercano al ideal.
 *
 *  Compilacion:
 *    gcc -O2 -fopenmp -Wall -Wextra -std=c11 -o bin/needles_omp src/needles_omp.c -lm
 * ========================================================================== */
#include <omp.h>
#include "nucleos.h"

#define VARIANTES "critical | atomic | rand | falso | padding | reduction | opt"
#define MAX_HILOS 256

/* Ranura por hilo alineada a 64 B. Contador volatile en falso/padding: ver dartboard_omp.c */
typedef struct {
    _Alignas(LINEA_CACHE) xoshiro_t r;
    volatile long long cont;             /*  8 B  (+24 B de relleno) */
} ranura_t;

static ranura_t  ranuras[MAX_HILOS];
static xoshiro_t estados[MAX_HILOS];
static volatile long long contadores[MAX_HILOS]; /* falso: 8 contadores por linea */

static long long v_critical(long long n, uint64_t semilla) {
    long long cruces = 0;
    #pragma omp parallel
    {
        xoshiro_t r; xo_semilla_hilo(&r, semilla, omp_get_thread_num());
        #pragma omp for schedule(runtime)
        for (long long i = 0; i < n; i++)
            if (aguja_sin(&r)) {
                #pragma omp critical
                cruces++;
            }
    }
    return cruces;
}

static long long v_atomic(long long n, uint64_t semilla) {
    long long cruces = 0;
    #pragma omp parallel
    {
        xoshiro_t r; xo_semilla_hilo(&r, semilla, omp_get_thread_num());
        #pragma omp for schedule(runtime)
        for (long long i = 0; i < n; i++)
            if (aguja_sin(&r)) {
                #pragma omp atomic
                cruces++;
            }
    }
    return cruces;
}

static long long v_rand(long long n, uint64_t semilla) {
    long long cruces = 0;
    srand((unsigned)semilla);
    #pragma omp parallel for schedule(runtime) reduction(+:cruces)
    for (long long i = 0; i < n; i++)
        cruces += aguja_rand();
    return cruces;
}

static long long v_falso(long long n, uint64_t semilla) {
    #pragma omp parallel
    {
        int id = omp_get_thread_num();
        xo_semilla_hilo(&estados[id], semilla, id);
        contadores[id] = 0;
        #pragma omp for schedule(runtime)
        for (long long i = 0; i < n; i++)
            contadores[id] += aguja_sin(&estados[id]);
    }
    long long cruces = 0;
    for (int i = 0; i < MAX_HILOS; i++) { cruces += contadores[i]; contadores[i] = 0; }
    return cruces;
}

static long long v_padding(long long n, uint64_t semilla) {
    #pragma omp parallel
    {
        int id = omp_get_thread_num();
        xo_semilla_hilo(&ranuras[id].r, semilla, id);
        ranuras[id].cont = 0;
        #pragma omp for schedule(runtime)
        for (long long i = 0; i < n; i++)
            ranuras[id].cont += aguja_sin(&ranuras[id].r);
    }
    long long cruces = 0;
    for (int i = 0; i < MAX_HILOS; i++) { cruces += ranuras[i].cont; ranuras[i].cont = 0; }
    return cruces;
}

static long long v_reduction(long long n, uint64_t semilla) {
    long long cruces = 0;
    #pragma omp parallel reduction(+:cruces)
    {
        xoshiro_t r; xo_semilla_hilo(&r, semilla, omp_get_thread_num());
        #pragma omp for schedule(runtime)
        for (long long i = 0; i < n; i++)
            cruces += aguja_sin(&r);
    }
    return cruces;
}

static long long v_opt(long long n, uint64_t semilla) {
    long long cruces = 0;
    #pragma omp parallel reduction(+:cruces)
    {
        xoshiro_t r; xo_semilla_hilo(&r, semilla, omp_get_thread_num());
        #pragma omp for schedule(runtime)
        for (long long i = 0; i < n; i++)
            cruces += aguja_rechazo(&r);
    }
    return cruces;
}

int main(int argc, char **argv) {
    opciones_t op;
    if (!leer_opciones(argc, argv, &op, VARIANTES, "reduction")) return 1;
    if (op.hilos > 0) omp_set_num_threads(op.hilos);
    if (!getenv("OMP_SCHEDULE")) omp_set_schedule(omp_sched_static, 0);

    int hilos = omp_get_max_threads();
    if (hilos > MAX_HILOS) { fprintf(stderr, "Maximo %d hilos\n", MAX_HILOS); return 1; }

    long long (*f)(long long, uint64_t) = NULL;
    if      (!strcmp(op.variante, "critical"))  f = v_critical;
    else if (!strcmp(op.variante, "atomic"))    f = v_atomic;
    else if (!strcmp(op.variante, "rand"))      f = v_rand;
    else if (!strcmp(op.variante, "falso"))     f = v_falso;
    else if (!strcmp(op.variante, "padding"))   f = v_padding;
    else if (!strcmp(op.variante, "reduction")) f = v_reduction;
    else if (!strcmp(op.variante, "opt"))       f = v_opt;
    else { fprintf(stderr, "Variante desconocida: %s (%s)\n", op.variante, VARIANTES); return 1; }

    double t0 = reloj_segundos();   /* QPC: omp_get_wtime() en MinGW tiene resolucion de ~1 ms */
    long long cruces = f(op.n, op.semilla);
    double t = reloj_segundos() - t0;

    double pi = cruces > 0 ? (double)op.n / (double)cruces : 0.0;
    imprimir_resultado("needles_omp", &op, hilos, pi, t);
    return 0;
}
