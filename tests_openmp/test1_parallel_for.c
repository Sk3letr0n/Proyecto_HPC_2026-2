/* ============================================================================
 *  TEST 1 - #pragma omp parallel for
 *
 *  La directiva compuesta mas simple: crea una region paralela y reparte
 *  las iteraciones del bucle externo (i) entre los hilos.
 *  Cada hilo calcula filas completas de C -> sin condiciones de carrera.
 *
 *  Corresponde a: 1screen (compound_names) y 3screen (ploop)
 * ========================================================================== */
#include "common.h"

#define NOMBRE_ENFOQUE "parallel_for"

static void multiplicar(const int *A, const int *B, int *C, int n)
{
    memset(C, 0, (size_t)n * n * sizeof(int));

    #pragma omp parallel for schedule(static)
    for (int i = 0; i < n; i++) {
        for (int k = 0; k < n; k++) {
            int a_ik = A[i*n + k];
            for (int j = 0; j < n; j++) {
                C[i*n + j] += a_ik * B[k*n + j];
            }
        }
    }
}

TEST_MAIN()
