/* ============================================================================
 *  common.h - Infraestructura comun para los 6 tests de directivas OpenMP
 *  Proyecto HPC 2026-2
 *
 *  Cada test incluye este archivo y solo define su funcion multiplicar_XXX()
 *  y la constante NOMBRE_ENFOQUE. Asi la unica diferencia entre tests es el
 *  #pragma omp usado, y la comparacion de tiempos es limpia.
 * ========================================================================== */

#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <omp.h>

/* ---------------------------------------------------------------------------
 *  Reloj de pared portable
 * ------------------------------------------------------------------------- */
#if defined(_WIN32)
  #include <windows.h>
  static double reloj_segundos(void) {
      LARGE_INTEGER f, m;
      QueryPerformanceFrequency(&f);
      QueryPerformanceCounter(&m);
      return (double)m.QuadPart / (double)f.QuadPart;
  }
#else
  static double reloj_segundos(void) {
      struct timespec ts;
      clock_gettime(CLOCK_MONOTONIC, &ts);
      return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
  }
#endif

/* ---------------------------------------------------------------------------
 *  Matriz como arreglo plano (N*N enteros). Acceso: M[i*n + j]
 *  Se usa un arreglo plano para simplificar los tests; el rendimiento es
 *  identico al de la version con punteros de fila.
 * ------------------------------------------------------------------------- */
static int *matriz_crear(int n) {
    int *m = (int *)calloc((size_t)n * n, sizeof(int));
    if (!m) { fprintf(stderr, "Sin memoria para %dx%d\n", n, n); exit(1); }
    return m;
}

static void matriz_llenar(int *m, int n, int limite) {
    size_t total = (size_t)n * n;
    for (size_t i = 0; i < total; i++)
        m[i] = 1 + (rand() % limite);
}

/* ---------------------------------------------------------------------------
 *  Formato con coma decimal para Excel en espanol
 * ------------------------------------------------------------------------- */
static void fmt(char *buf, size_t sz, double v, int dec) {
    snprintf(buf, sz, "%.*f", dec, v);
    for (char *p = buf; *p; p++) if (*p == '.') *p = ',';
}

/* ---------------------------------------------------------------------------
 *  main() generico - cada test solo define multiplicar() y NOMBRE_ENFOQUE
 *
 *  Uso:  test_XXX  <N>  [hilos]
 *  CSV:  Enfoque;N;Hilos;Tiempo(s);Rendimiento(GOP/s)
 * ------------------------------------------------------------------------- */
#define TEST_MAIN()                                                          \
int main(int argc, char *argv[]) {                                           \
    int n = (argc > 1) ? atoi(argv[1]) : 1000;                              \
    int hilos = (argc > 2) ? atoi(argv[2]) : 0;                             \
                                                                             \
    if (n <= 0) { fprintf(stderr, "N debe ser >= 1\n"); return 1; }          \
    if (hilos > 0) omp_set_num_threads(hilos);                               \
    omp_set_dynamic(0);                                                      \
                                                                             \
    int hilos_usados = 1;                                                    \
    _Pragma("omp parallel")                                                  \
    { _Pragma("omp single") hilos_usados = omp_get_num_threads(); }          \
                                                                             \
    srand(42);                                                               \
    int *A = matriz_crear(n);                                                \
    int *B = matriz_crear(n);                                                \
    int *C = matriz_crear(n);                                                \
    matriz_llenar(A, n, 100);                                                \
    matriz_llenar(B, n, 100);                                                \
                                                                             \
    double t0 = reloj_segundos();                                            \
    multiplicar(A, B, C, n);                                                 \
    double t1 = reloj_segundos();                                            \
    double seg = t1 - t0;                                                    \
    double ops = 2.0 * (double)n * (double)n * (double)n;                    \
    double gops = ops / (seg > 0 ? seg : 1e-9) / 1e9;                       \
                                                                             \
    char ts[32], gs[32];                                                     \
    fmt(ts, sizeof ts, seg, 6);                                              \
    fmt(gs, sizeof gs, gops, 3);                                             \
    printf("%s;%d;%d;%s;%s\n", NOMBRE_ENFOQUE, n, hilos_usados, ts, gs);    \
                                                                             \
    free(A); free(B); free(C);                                               \
    return 0;                                                                \
}

#endif /* COMMON_H */
