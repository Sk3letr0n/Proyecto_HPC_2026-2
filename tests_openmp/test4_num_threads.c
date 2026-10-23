/* ============================================================================
 *  TEST 4 - #pragma omp parallel num_threads(N) + omp_set_dynamic(0)
 *
 *  Fija el numero exacto de hilos desde la directiva y desactiva el ajuste
 *  dinamico del runtime. Util para benchmarks reproducibles.
 *
 *  En este test el numero de hilos se fija con num_threads() en el pragma
 *  en vez de usar omp_set_num_threads() antes. El efecto es el mismo,
 *  pero demuestra la clausula del 4screen.
 *
 *  Corresponde a: 4screen (nthrs_dynamic)
 * ========================================================================== */
#include "common.h"

#define NOMBRE_ENFOQUE "num_threads"

/* Se usa una variable global para que el pragma pueda leerla.
 * El valor real se fija en main() antes de llamar a multiplicar(). */
static int g_hilos = 4;

static void multiplicar(const int *A, const int *B, int *C, int n)
{
    memset(C, 0, (size_t)n * n * sizeof(int));

    omp_set_dynamic(0);
    #pragma omp parallel for schedule(static) num_threads(g_hilos)
    for (int i = 0; i < n; i++) {
        for (int k = 0; k < n; k++) {
            int a_ik = A[i*n + k];
            for (int j = 0; j < n; j++) {
                C[i*n + j] += a_ik * B[k*n + j];
            }
        }
    }
}

/* Override del main generico para usar g_hilos */
int main(int argc, char *argv[])
{
    int n = (argc > 1) ? atoi(argv[1]) : 1000;
    int hilos = (argc > 2) ? atoi(argv[2]) : 4;

    if (n <= 0) { fprintf(stderr, "N debe ser >= 1\n"); return 1; }
    g_hilos = (hilos > 0) ? hilos : 4;

    int hilos_usados = g_hilos;

    srand(42);
    int *A = matriz_crear(n);
    int *B = matriz_crear(n);
    int *C = matriz_crear(n);
    matriz_llenar(A, n, 100);
    matriz_llenar(B, n, 100);

    double t0 = reloj_segundos();
    multiplicar(A, B, C, n);
    double t1 = reloj_segundos();
    double seg = t1 - t0;
    double ops = 2.0 * (double)n * (double)n * (double)n;
    double gops = ops / (seg > 0 ? seg : 1e-9) / 1e9;

    char ts[32], gs[32];
    fmt(ts, sizeof ts, seg, 6);
    fmt(gs, sizeof gs, gops, 3);
    printf("%s;%d;%d;%s;%s\n", NOMBRE_ENFOQUE, n, hilos_usados, ts, gs);

    free(A); free(B); free(C);
    return 0;
}
