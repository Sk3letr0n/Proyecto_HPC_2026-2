/* ============================================================================
 *  TEST 6 - #pragma omp task
 *
 *  Paralelismo basado en TAREAS: un hilo (single) genera N tareas (una por
 *  fila de C) y los demas hilos del equipo las ejecutan conforme se liberan.
 *
 *  Las tareas son mas flexibles que un parallel for (sirven para arboles,
 *  grafos, recursion), pero para bucles regulares como matmul tienen mas
 *  overhead: cada tarea se encola, se planifica y se asigna en tiempo de
 *  ejecucion, mientras que parallel for hace un reparto estatico sin colas.
 *
 *  Corresponde a: 6screen (tasking)
 * ========================================================================== */
#include "common.h"

#define NOMBRE_ENFOQUE "task"

static void multiplicar(const int *A, const int *B, int *C, int n)
{
    memset(C, 0, (size_t)n * n * sizeof(int));

    #pragma omp parallel
    {
        #pragma omp single
        {
            for (int i = 0; i < n; i++) {
                #pragma omp task firstprivate(i)
                {
                    for (int k = 0; k < n; k++) {
                        int a_ik = A[i*n + k];
                        for (int j = 0; j < n; j++) {
                            C[i*n + j] += a_ik * B[k*n + j];
                        }
                    }
                }
            }
        } /* barrera implicita: espera a que todas las tareas terminen */
    }
}

TEST_MAIN()
