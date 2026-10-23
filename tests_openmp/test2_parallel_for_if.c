/* ============================================================================
 *  TEST 2 - #pragma omp parallel for if(condicion)
 *
 *  Igual que el test 1, pero con clausula if: solo paraleliza si N > 64.
 *  Con matrices pequenas evita el overhead de crear hilos.
 *  Con matrices grandes se comporta identico al test 1.
 *
 *  Corresponde a: 2screen (directive_name_modifier)
 * ========================================================================== */
#include "common.h"

#define NOMBRE_ENFOQUE "parallel_for_if"

static void multiplicar(const int *A, const int *B, int *C, int n)
{
    memset(C, 0, (size_t)n * n * sizeof(int));

    #pragma omp parallel for schedule(static) if(n > 64)
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
