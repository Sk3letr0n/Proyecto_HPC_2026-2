/* ============================================================================
 *  TEST 5 - #pragma omp for collapse(2)
 *
 *  Aplana los dos bucles externos en un solo espacio de iteraciones mas
 *  grande y lo reparte entre los hilos. Esto mejora el balance de carga
 *  cuando N es pequeno relativo al numero de hilos.
 *
 *  ATENCION: collapse(2) sobre i,k en orden i-k-j causa CONDICION DE CARRERA
 *  porque dos hilos con el mismo i pero distinto k escriben la misma fila de C.
 *
 *  Por eso este test usa el orden CLASICO i-j-k, donde collapse(2) sobre i,j
 *  es seguro: cada par (i,j) escribe en una celda UNICA de C.
 *  El precio: el bucle interno recorre B por columnas (peor cache).
 *
 *  Corresponde a: 5screen (collapse clause)
 * ========================================================================== */
#include "common.h"

#define NOMBRE_ENFOQUE "collapse"

static void multiplicar(const int *A, const int *B, int *C, int n)
{
    /* NOTA: aqui NO se usa memset porque cada C[i][j] se asigna directamente,
     * no se acumula con +=. El orden es i-j-k clasico (fila x columna). */

    #pragma omp parallel for collapse(2) schedule(static)
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            int suma = 0;
            for (int k = 0; k < n; k++) {
                suma += A[i*n + k] * B[k*n + j];
            }
            C[i*n + j] = suma;
        }
    }
}

TEST_MAIN()
