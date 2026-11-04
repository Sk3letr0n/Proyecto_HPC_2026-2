/* ============================================================================
 *  dartboard_omp.c - PI por Monte Carlo, metodo DARTBOARD, VERSION OpenMP
 *  Proyecto HPC 2026-2 - Reto 2
 *
 *  El problema es "vergonzosamente paralelo": cada dardo es independiente.
 *  Lo unico que comparten los hilos es (a) el CONTADOR de aciertos y (b) el
 *  GENERADOR de numeros aleatorios. Cada variante resuelve esas dos cosas de
 *  una forma distinta, de la peor a la mejor:
 *
 *    critical  Contador compartido protegido con  #pragma omp critical.
 *              Cada acierto adquiere un candado global -> serializacion.
 *    atomic    Contador compartido con  #pragma omp atomic  (lock xadd).
 *              Sin candado, pero la linea de cache del contador "rebota"
 *              entre nucleos en cada acierto (contention + coherencia MESI).
 *    rand      reduction(+) correcta, pero usa libc rand(): estado oculto
 *              (en UCRT es por hilo y SIN sembrar en los hilos trabajadores:
 *              todos generan la MISMA secuencia) y 15 bits de resolucion.
 *    falso     Contador y generador privados de cada hilo, pero guardados en
 *              arreglos contiguos  contadores[p], estados[p]:  varios hilos
 *              escriben en la MISMA linea de cache de 64 B -> FALSE SHARING.
 *    padding   Igual que 'falso', pero cada hilo tiene su ranura alineada y
 *              rellena a 64 B (_Alignas(LINEA_CACHE)) -> sin false sharing.
 *    reduction Forma idiomatica: generador LOCAL (en registros), contador
 *              privado y  reduction(+:dentro)  al final. Una sola
 *              sincronizacion por hilo.
 *    opt       'reduction' + nucleo entero optimizado (1 aleatorio/dardo).
 *
 *  Generador: cada hilo usa xoshiro256** con la semilla base avanzada
 *  id * 2^128 posiciones (xo_salto): flujos independientes, sin solaparse y
 *  reproducibles para un numero de hilos y un reparto fijos.
 *
 *  Reparto: schedule(runtime). Por defecto static (todas las iteraciones
 *  cuestan lo mismo); se puede cambiar con OMP_SCHEDULE sin recompilar.
 *
 *  Compilacion:
 *    gcc -O2 -fopenmp -Wall -Wextra -std=c11 -o bin/dartboard_omp src/dartboard_omp.c -lm
 * ========================================================================== */
#include <omp.h>
#include "nucleos.h"

#define VARIANTES "critical | atomic | rand | falso | padding | reduction | opt"
#define MAX_HILOS 256

/* Ranura por hilo, alineada y rellena a una linea de cache completa.
 *
 * Por que 'volatile' en los contadores de 'falso' y 'padding':
 * con -O2 GCC hace "store motion": mantiene contadores[id] y estados[id] en
 * REGISTROS durante todo el bloque de iteraciones y solo los escribe al final
 * (comprobado en el ensamblador con gcc -S). Asi el false sharing desaparece
 * y las dos variantes miden lo mismo. 'volatile' obliga a una escritura en
 * memoria por muestra, que es lo que ocurre en codigo real cuando el
 * compilador no puede probar que no hay alias (p. ej. si hay una llamada a
 * una funcion opaca dentro del bucle, como sin() en needles_omp.c).
 * Ambas variantes llevan el mismo 'volatile': solo cambia la DISPOSICION en
 * memoria (8 contadores por linea de cache frente a 1).                     */
typedef struct {
    _Alignas(LINEA_CACHE) xoshiro_t r;   /* 32 B */
    volatile long long cont;             /*  8 B  (+24 B de relleno) */
} ranura_t;

static ranura_t  ranuras[MAX_HILOS];     /* padding: 1 linea por hilo       */
static xoshiro_t estados[MAX_HILOS];     /* falso: 2 estados por linea      */
static volatile long long contadores[MAX_HILOS]; /* falso: 8 contadores por linea */

static long long v_critical(long long n, uint64_t semilla) {
    long long dentro = 0;
    #pragma omp parallel
    {
        xoshiro_t r; xo_semilla_hilo(&r, semilla, omp_get_thread_num());
        #pragma omp for schedule(runtime)
        for (long long i = 0; i < n; i++)
            if (dardo_double(&r)) {
                #pragma omp critical
                dentro++;
            }
    }
    return dentro;
}

static long long v_atomic(long long n, uint64_t semilla) {
    long long dentro = 0;
    #pragma omp parallel
    {
        xoshiro_t r; xo_semilla_hilo(&r, semilla, omp_get_thread_num());
        #pragma omp for schedule(runtime)
        for (long long i = 0; i < n; i++)
            if (dardo_double(&r)) {
                #pragma omp atomic
                dentro++;
            }
    }
    return dentro;
}

static long long v_rand(long long n, uint64_t semilla) {
    long long dentro = 0;
    srand((unsigned)semilla);            /* solo siembra el hilo maestro */
    #pragma omp parallel for schedule(runtime) reduction(+:dentro)
    for (long long i = 0; i < n; i++)
        dentro += dardo_rand();
    return dentro;
}

static long long v_falso(long long n, uint64_t semilla) {
    #pragma omp parallel
    {
        int id = omp_get_thread_num();
        xo_semilla_hilo(&estados[id], semilla, id);
        contadores[id] = 0;
        #pragma omp for schedule(runtime)
        for (long long i = 0; i < n; i++)
            contadores[id] += dardo_double(&estados[id]);
    }
    long long dentro = 0;
    for (int i = 0; i < MAX_HILOS; i++) { dentro += contadores[i]; contadores[i] = 0; }
    return dentro;
}

static long long v_padding(long long n, uint64_t semilla) {
    #pragma omp parallel
    {
        int id = omp_get_thread_num();
        xo_semilla_hilo(&ranuras[id].r, semilla, id);
        ranuras[id].cont = 0;
        #pragma omp for schedule(runtime)
        for (long long i = 0; i < n; i++)
            ranuras[id].cont += dardo_double(&ranuras[id].r);
    }
    long long dentro = 0;
    for (int i = 0; i < MAX_HILOS; i++) { dentro += ranuras[i].cont; ranuras[i].cont = 0; }
    return dentro;
}

static long long v_reduction(long long n, uint64_t semilla) {
    long long dentro = 0;
    #pragma omp parallel reduction(+:dentro)
    {
        xoshiro_t r; xo_semilla_hilo(&r, semilla, omp_get_thread_num());
        #pragma omp for schedule(runtime)
        for (long long i = 0; i < n; i++)
            dentro += dardo_double(&r);
    }
    return dentro;
}

static long long v_opt(long long n, uint64_t semilla) {
    long long dentro = 0;
    #pragma omp parallel reduction(+:dentro)
    {
        xoshiro_t r; xo_semilla_hilo(&r, semilla, omp_get_thread_num());
        #pragma omp for schedule(runtime)
        for (long long i = 0; i < n; i++)
            dentro += dardo_entero(&r);
    }
    return dentro;
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
    long long dentro = f(op.n, op.semilla);
    double t = reloj_segundos() - t0;

    imprimir_resultado("dartboard_omp", &op, hilos, 4.0 * (double)dentro / (double)op.n, t);
    return 0;
}
