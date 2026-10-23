/* ============================================================================
 *  TEST 3 - #pragma omp parallel for simd
 *
 *  Combina paralelismo entre hilos con vectorizacion SIMD explicita.
 *  El "simd" le dice al compilador que vectorice el bucle mas interno
 *  usando instrucciones SSE/AVX, ademas del reparto entre hilos.
 *
 *  Nota: con enteros de 32 bits y -O2, el compilador ya suele vectorizar
 *  automaticamente. La ganancia sobre el test 1 deberia ser marginal.
 *
 *  Corresponde a: 1screen (compound_names, segundo bloque)
 * ========================================================================== */
#include "common.h"

#define NOMBRE_ENFOQUE "parallel_for_simd"

static void multiplicar(const int *A, const int *B, int *C, int n)
{
    memset(C, 0, (size_t)n * n * sizeof(int));

    #pragma omp parallel for schedule(static)
    for (int i = 0; i < n; i++) {
        for (int k = 0; k < n; k++) {
            int a_ik = A[i*n + k];
            #pragma omp simd
            for (int j = 0; j < n; j++) {
                C[i*n + j] += a_ik * B[k*n + j];
            }
        }
    }
}

TEST_MAIN()
