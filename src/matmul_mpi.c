/* ============================================================================
 *  matmul_mpi.c - Multiplicacion de matrices con MPI (memoria distribuida)
 *  Proyecto HPC 2026-2
 *
 *  Estrategia de distribucion (operaciones colectivas, NO punto a punto):
 *    1. Proceso raiz (rank 0) genera las matrices A y B.
 *    2. MPI_Bcast   envia la matriz B completa a todos los procesos.
 *    3. MPI_Scatter  reparte las filas de A equitativamente.
 *    4. Cada proceso calcula su porcion local de C = A_local x B.
 *    5. MPI_Gather   recolecta las porciones de C en el proceso raiz.
 *
 *  Si N no es divisible por el numero de procesos, se rellenan filas de A
 *  con ceros (padding) para que Scatter reparta bloques iguales.
 *
 *  Compilacion:
 *    Linux:   mpicc -O2 -Wall -Wextra -std=c11 -o bin/matmul_mpi src/matmul_mpi.c -lm
 *    Windows (MS-MPI):
 *      gcc -O2 -Wall -Wextra -std=c11 -I"%MSMPI_INC%" ^
 *          -o bin\matmul_mpi.exe src\matmul_mpi.c -L"%MSMPI_LIB64%" -lmsmpi -lm
 *
 *  Ejecucion:
 *    Linux:   mpirun -np 4 ./bin/matmul_mpi -n 1000 -l 100
 *    Windows: mpiexec -n 4 bin\matmul_mpi.exe -n 1000 -l 100
 * ========================================================================== */

#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <stdint.h>
#include <errno.h>
#include <time.h>
#include <math.h>

/* ---------------------------------------------------------------------------
 *  Limite maximo seguro para el valor de cada celda.
 *  n * L * L <= INT_MAX  =>  L <= sqrt(INT_MAX / n)
 * ------------------------------------------------------------------------- */
static int limite_seguro(int n)
{
    long long objetivo = (long long)INT_MAX;
    long long L = (long long)sqrt((double)objetivo / (double)n);
    if (L < 1) L = 1;
    while ((L + 1) * (L + 1) * (long long)n <= objetivo) L++;
    while (L > 1 && L * L * (long long)n > objetivo)     L--;
    return (int)L;
}

/* ---------------------------------------------------------------------------
 *  Formatea un double con coma decimal (para Excel con locale en espanol).
 * ------------------------------------------------------------------------- */
static void formato_coma(char *destino, size_t tam, double valor, int decimales)
{
    snprintf(destino, tam, "%.*f", decimales, valor);
    for (char *p = destino; *p != '\0'; p++) {
        if (*p == '.') *p = ',';
    }
}

/* ---------------------------------------------------------------------------
 *  Extrae el nombre del ejecutable de argv[0].
 * ------------------------------------------------------------------------- */
static const char *nombre_programa(const char *ruta)
{
    const char *base = ruta;
    for (const char *p = ruta; *p != '\0'; p++) {
        if (*p == '/' || *p == '\\') base = p + 1;
    }
    return base;
}

/* ---------------------------------------------------------------------------
 *  AYUDA / USO
 * ------------------------------------------------------------------------- */
static void mostrar_uso(const char *prog)
{
    printf(
    "Uso: mpirun -np <P> %s -n <tamano> -l <limite> [-s <semilla>] [-p] [-c]\n"
    "     mpirun -np <P> %s <tamano> <limite> [semilla] [-p] [-c]\n"
    "\n"
    "Opciones:\n"
    "  -n <tamano>    Tamano de las matrices cuadradas (obligatorio).\n"
    "                 Formas validas: 5  o  5x5  (deben coincidir).\n"
    "  -l <limite>    Valor maximo de cada celda; genera enteros en [1, limite]\n"
    "                 (obligatorio). Debe cumplir  N * limite^2 <= %d.\n"
    "  -s <semilla>   Semilla del generador aleatorio (por defecto: reloj).\n"
    "  -p             Imprime las matrices A, B y C (usar solo con N pequeno).\n"
    "  -c             Salida CSV:  Orden;Procesos;Tiempo(s);GOP/s\n"
    "  -h             Muestra esta ayuda.\n"
    "\n"
    "Ejemplos:\n"
    "  mpirun -np 4 %s -n 1000 -l 100\n"
    "  mpiexec -n 2 %s -n 5x5 -l 9 -s 42 -p\n",
    prog, prog, INT_MAX, prog, prog);
}

/* ---------------------------------------------------------------------------
 *  Conversion segura de cadena a entero.
 * ------------------------------------------------------------------------- */
static int leer_entero(const char *texto, long *destino)
{
    char *sobrante = NULL;
    errno = 0;
    long valor = strtol(texto, &sobrante, 10);
    if (errno != 0 || sobrante == texto || *sobrante != '\0') return 0;
    *destino = valor;
    return 1;
}

/* ---------------------------------------------------------------------------
 *  Lectura del tamano de la matriz (admite "5" y "5x5").
 * ------------------------------------------------------------------------- */
static int leer_dimension(const char *texto, long *destino)
{
    const char *separador = strpbrk(texto, "xX");

    if (separador == NULL) {
        if (!leer_entero(texto, destino)) {
            fprintf(stderr, "Error: '%s' no es un tamano valido. "
                            "Use por ejemplo 5 o 5x5.\n", texto);
            return 0;
        }
        return 1;
    }

    char filas_txt[32];
    size_t largo = (size_t)(separador - texto);
    if (largo == 0 || largo >= sizeof(filas_txt)) {
        fprintf(stderr, "Error: '%s' no es un tamano valido.\n", texto);
        return 0;
    }
    memcpy(filas_txt, texto, largo);
    filas_txt[largo] = '\0';

    long filas = 0, columnas = 0;
    if (!leer_entero(filas_txt, &filas) ||
        !leer_entero(separador + 1, &columnas)) {
        fprintf(stderr, "Error: '%s' no es un tamano valido.\n", texto);
        return 0;
    }

    if (filas != columnas) {
        fprintf(stderr,
            "Error: las matrices deben ser cuadradas, pero se pidio %ldx%ld.\n",
            filas, columnas);
        return 0;
    }

    *destino = filas;
    return 1;
}

/* ---------------------------------------------------------------------------
 *  Imprime una matriz almacenada en arreglo contiguo (solo para N pequeno).
 * ------------------------------------------------------------------------- */
static void imprimir_matriz(const int *datos, int n, const char *nombre)
{
    printf("\nMatriz %s (%dx%d):\n", nombre, n, n);
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            printf("%10d", datos[i * n + j]);
        }
        putchar('\n');
    }
}

/* ===========================================================================
 *  PROGRAMA PRINCIPAL
 * ========================================================================= */
int main(int argc, char *argv[])
{
    MPI_Init(&argc, &argv);

    int rank, nprocs;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &nprocs);

    /* --- Parametros (solo el raiz los parsea) --- */
    int    N        = 0;
    long   n_param  = -1, limite = -1, semilla = -1;
    int    imprimir = 0, modo_csv = 0;
    int    parse_ok = 1;

    if (rank == 0) {
        const char *prog = nombre_programa(argv[0]);
        long valor = 0;

        if (argc == 1) {
            mostrar_uso(prog);
            parse_ok = 0;
        }
        else if (argv[1][0] != '-') {
            /* Forma posicional: matmul <tamano> <limite> [semilla] [-p] [-c] */
            if (argc < 3) {
                fprintf(stderr, "Error: faltan parametros.\n\n");
                mostrar_uso(prog);
                parse_ok = 0;
            } else {
                if (!leer_dimension(argv[1], &n_param))
                    parse_ok = 0;
                else if (!leer_entero(argv[2], &limite)) {
                    fprintf(stderr, "Error: el limite ('%s') debe ser un "
                                    "numero entero.\n", argv[2]);
                    parse_ok = 0;
                } else {
                    for (int i = 3; i < argc && parse_ok; i++) {
                        if (strcmp(argv[i], "-p") == 0) { imprimir = 1; continue; }
                        if (strcmp(argv[i], "-c") == 0) { modo_csv = 1; continue; }
                        if (semilla < 0 && leer_entero(argv[i], &semilla)) continue;
                        fprintf(stderr, "Error: parametro '%s' no reconocido.\n\n",
                                argv[i]);
                        mostrar_uso(prog);
                        parse_ok = 0;
                    }
                }
            }
        } else {
            /* Forma con banderas: matmul -n N -l L [-s S] [-p] [-c] */
            for (int i = 1; i < argc && parse_ok; i++) {
                const char *op = argv[i];

                if (strcmp(op, "-h") == 0 || strcmp(op, "--help") == 0) {
                    mostrar_uso(prog);
                    parse_ok = 0;
                    break;
                }
                if (strcmp(op, "-p") == 0) { imprimir = 1; continue; }
                if (strcmp(op, "-c") == 0) { modo_csv = 1; continue; }

                if (strcmp(op, "-n") == 0) {
                    if (i + 1 >= argc) {
                        fprintf(stderr, "Error: -n requiere un valor.\n");
                        parse_ok = 0; break;
                    }
                    if (!leer_dimension(argv[++i], &n_param)) {
                        parse_ok = 0; break;
                    }
                    continue;
                }

                if (strcmp(op, "-l") == 0 || strcmp(op, "-s") == 0) {
                    if (i + 1 >= argc) {
                        fprintf(stderr, "Error: %s requiere un valor.\n", op);
                        parse_ok = 0; break;
                    }
                    if (!leer_entero(argv[++i], &valor)) {
                        fprintf(stderr, "Error: valor de %s ('%s') invalido.\n",
                                op, argv[i]);
                        parse_ok = 0; break;
                    }
                    if (op[1] == 'l') limite  = valor;
                    else              semilla = valor;
                    continue;
                }

                fprintf(stderr, "Error: opcion desconocida '%s'.\n\n", op);
                mostrar_uso(prog);
                parse_ok = 0;
            }
        }

        /* Validacion de parametros */
        if (parse_ok) {
            if (n_param <= 0) {
                fprintf(stderr, "Error: debe indicar -n >= 1.\n");
                parse_ok = 0;
            } else if (n_param > INT_MAX) {
                fprintf(stderr, "Error: n=%ld excede INT_MAX.\n", n_param);
                parse_ok = 0;
            } else if ((long long)n_param * n_param > INT_MAX) {
                fprintf(stderr, "Error: n*n desborda int (limite para MPI).\n");
                parse_ok = 0;
            } else if (limite <= 0) {
                fprintf(stderr, "Error: debe indicar -l >= 1.\n");
                parse_ok = 0;
            } else {
                int lmax = limite_seguro((int)n_param);
                if (limite > lmax) {
                    fprintf(stderr,
                        "Error: limite %ld provoca desbordamiento.\n"
                        "       Para n=%ld el maximo seguro es %d.\n",
                        limite, n_param, lmax);
                    parse_ok = 0;
                }
            }
        }

        if (parse_ok) {
            N = (int)n_param;
            if (semilla < 0) semilla = (long)time(NULL);
        }
    }

    /* ---------------------------------------------------------------
     *  Broadcast del estado del parseo y del orden de la matriz.
     *  Si hubo error, todos los procesos salen limpiamente.
     * --------------------------------------------------------------- */
    MPI_Bcast(&parse_ok, 1, MPI_INT, 0, MPI_COMM_WORLD);
    if (!parse_ok) {
        MPI_Finalize();
        return EXIT_FAILURE;
    }
    MPI_Bcast(&N, 1, MPI_INT, 0, MPI_COMM_WORLD);

    /* ---------------------------------------------------------------
     *  Calculo de la distribucion de filas.
     *  rows_per_proc filas por proceso; si N no es divisible se
     *  rellena con filas de ceros (padding) hasta padded_N.
     * --------------------------------------------------------------- */
    int rows_per_proc = (N + nprocs - 1) / nprocs;
    int padded_N      = rows_per_proc * nprocs;
    int chunk         = rows_per_proc * N;

    /* ---------------------------------------------------------------
     *  Asignacion de memoria.
     *  - B se necesita completa en TODOS los procesos.
     *  - local_A y local_C: porcion local de cada proceso.
     *  - A_padded y C_padded: solo en el raiz (tamano padded_N x N).
     * --------------------------------------------------------------- */
    int *B       = (int *)malloc((size_t)N * N * sizeof(int));
    int *local_A = (int *)malloc((size_t)chunk * sizeof(int));
    int *local_C = (int *)calloc((size_t)chunk, sizeof(int));

    if (B == NULL || local_A == NULL || local_C == NULL) {
        fprintf(stderr, "[rank %d] Error: malloc fallo.\n", rank);
        MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
    }

    int *A_padded = NULL;
    int *C_padded = NULL;

    if (rank == 0) {
        A_padded = (int *)calloc((size_t)padded_N * N, sizeof(int));
        C_padded = (int *)malloc((size_t)padded_N * N * sizeof(int));

        if (A_padded == NULL || C_padded == NULL) {
            fprintf(stderr, "Error: no hay memoria para las matrices completas.\n");
            MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
        }

        /* Llenado aleatorio (misma semilla = mismo resultado que matmul.c) */
        srand((unsigned int)semilla);
        for (size_t i = 0; i < (size_t)N * N; i++)
            A_padded[i] = 1 + (rand() % (int)limite);

        for (size_t i = 0; i < (size_t)N * N; i++)
            B[i] = 1 + (rand() % (int)limite);
    }

    /* ===============================================================
     *  SECCION PARALELA  (Bcast + Scatter + Multiplicacion + Gather)
     * =============================================================== */
    MPI_Barrier(MPI_COMM_WORLD);
    double t_inicio = MPI_Wtime();

    /* 1. Enviar B completa a todos los procesos (Bcast) */
    MPI_Bcast(B, N * N, MPI_INT, 0, MPI_COMM_WORLD);

    /* 2. Repartir filas de A entre los procesos (Scatter) */
    MPI_Scatter(A_padded, chunk, MPI_INT,
                local_A,  chunk, MPI_INT,
                0, MPI_COMM_WORLD);

    /* 3. Multiplicacion local: local_C = local_A x B  (orden i-k-j) */
    for (int i = 0; i < rows_per_proc; i++) {
        for (int k = 0; k < N; k++) {
            int        a_ik   = local_A[i * N + k];
            const int *fila_b = &B[k * N];
            int       *fila_c = &local_C[i * N];
            for (int j = 0; j < N; j++) {
                fila_c[j] += a_ik * fila_b[j];
            }
        }
    }

    /* 4. Recolectar bloques de C en el proceso raiz (Gather) */
    MPI_Gather(local_C,  chunk, MPI_INT,
               C_padded, chunk, MPI_INT,
               0, MPI_COMM_WORLD);

    double t_fin    = MPI_Wtime();
    double segundos = t_fin - t_inicio;

    /* ===============================================================
     *  REPORTE (solo el proceso raiz)
     * =============================================================== */
    if (rank == 0) {
        double operaciones = 2.0 * (double)N * (double)N * (double)N;
        double gops = operaciones / (segundos > 0.0 ? segundos : 1e-9) / 1e9;

        if (modo_csv) {
            char t_txt[32], g_txt[32];
            formato_coma(t_txt, sizeof t_txt, segundos, 6);
            formato_coma(g_txt, sizeof g_txt, gops, 3);
            printf("%d;%d;%s;%s\n", N, nprocs, t_txt, g_txt);
        } else {
            int lmax = limite_seguro(N);
            double mib = (double)((size_t)N * N * sizeof(int))
                         / (1024.0 * 1024.0);

            printf("=============================================\n");
            printf("  Multiplicacion de matrices  C = A x B\n");
            printf("  (MPI - memoria distribuida)\n");
            printf("=============================================\n");
            printf("  Orden de las matrices : %d x %d\n", N, N);
            printf("  Rango de las celdas   : [1, %ld]\n", limite);
            printf("  Limite maximo seguro  : %d\n", lmax);
            printf("  Semilla aleatoria     : %ld\n", semilla);
            printf("  Memoria por matriz    : %.2f MiB (x3 = %.2f MiB)\n",
                   mib, mib * 3);
            printf("  Procesos MPI          : %d\n", nprocs);
            printf("  Filas por proceso     : %d", rows_per_proc);
            if (padded_N != N)
                printf("  (padding: %d filas extra)", padded_N - N);
            putchar('\n');
            printf("  Operaciones enteras   : %.3e\n", operaciones);
            printf("  Tiempo total (distrib): %.6f s\n", segundos);
            printf("  Rendimiento           : %.3f GOP/s\n", gops);
            printf("=============================================\n");

            if (imprimir) {
                imprimir_matriz(A_padded, N, "A");
                imprimir_matriz(B,        N, "B");
                imprimir_matriz(C_padded, N, "C = A x B");
            }
        }
    }

    /* --- Liberacion de memoria --- */
    free(local_A);
    free(local_C);
    free(B);
    if (rank == 0) {
        free(A_padded);
        free(C_padded);
    }

    MPI_Finalize();
    return EXIT_SUCCESS;
}
