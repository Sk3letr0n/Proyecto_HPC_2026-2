/* ============================================================================
 *  mmOPENMP.c - Multiplicacion de matrices cuadradas de enteros positivos
 *               VERSION PARALELA CON OpenMP
 *  Proyecto HPC 2026-2
 *
 *  Es la contraparte paralela de matmul.c usando OpenMP. Comparte exactamente
 *  la misma estructura de datos, el mismo llenado aleatorio y el mismo nucleo
 *  de calculo (orden de bucles i-k-j), de modo que ambos programas son
 *  comparables 1:1 y el speedup medido es limpio.
 *
 *  IDEA DE LA PARALELIZACION (paralelismo de datos con OpenMP):
 *    - C = A x B se calcula fila por fila y cada fila de C es INDEPENDIENTE
 *      de las demas.
 *    - La sentinela  #pragma omp parallel for  reparte las filas de C entre
 *      los hilos del equipo de forma automatica.
 *    - A y B son de solo lectura  -> ningun hilo las modifica.
 *    - Cada hilo escribe filas DISJUNTAS de C -> no hay condiciones de
 *      carrera, no se necesitan mutex ni locks. OpenMP se encarga de crear,
 *      sincronizar y destruir los hilos.
 *    - schedule(static): las iteraciones se reparten en bloques contiguos
 *      de tamanio similar, igual que en mmHILOS.c, para maxima localidad.
 *
 *  DIRECTIVA OPENMP USADA (ganadora del analisis de los 6 ejemplos):
 *    #pragma omp parallel for schedule(runtime)
 *    Es la directiva compuesta que crea la region paralela y reparte el bucle
 *    en una sola linea. Las variables del bucle interno (k, j, a_ik, fila_b,
 *    fila_c) son automaticamente privadas por estar declaradas dentro del for.
 *
 *    schedule(runtime) permite controlar la politica de reparto con la variable
 *    de entorno OMP_SCHEDULE sin recompilar. Por defecto se fija a "static"
 *    desde el codigo con omp_set_schedule().
 *
 *  Variables de entorno utiles (se leen al arrancar, sin recompilar):
 *    OMP_NUM_THREADS=8          Numero de hilos (la opcion -t toma precedencia)
 *    OMP_SCHEDULE="static"      Politica de reparto: static, dynamic, guided, auto
 *    OMP_PROC_BIND=close        Fijar hilos a nucleos cercanos (mejor cache)
 *    OMP_PLACES=cores           Donde corren los hilos: threads, cores, sockets
 *    OMP_WAIT_POLICY=active     Hilos ociosos dan vueltas (mas rapido) o duermen
 *    OMP_DISPLAY_ENV=true       Muestra toda la configuracion OMP al arrancar
 *
 *  Compilacion:
 *      gcc -O2 -fopenmp -Wall -Wextra -std=c11 -o bin/mmOPENMP src/mmOPENMP.c -lm
 * ========================================================================== */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <stdint.h>
#include <errno.h>
#include <time.h>
#include <math.h>
#include <omp.h>    /* <-- cabecera de OpenMP */

/* ---------------------------------------------------------------------------
 *  Reloj de pared (wall clock) portable.
 *  En HPC lo que interesa es el tiempo REAL transcurrido, porque es el que se
 *  reduce al paralelizar.
 * ------------------------------------------------------------------------- */
#if defined(_WIN32)
  #include <windows.h>
  static double reloj_segundos(void) {
      LARGE_INTEGER frecuencia, marca;
      QueryPerformanceFrequency(&frecuencia);
      QueryPerformanceCounter(&marca);
      return (double)marca.QuadPart / (double)frecuencia.QuadPart;
  }
#else
  static double reloj_segundos(void) {
      struct timespec ts;
      clock_gettime(CLOCK_MONOTONIC, &ts);
      return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
  }
#endif

/* ===========================================================================
 *  TIPO MATRIZ  (identico a matmul.c)
 *
 *  La matriz vive en UN SOLO bloque contiguo (campo datos) y ademas en un
 *  vector de punteros (campo fila), donde fila[i] apunta al inicio de la
 *  fila i. Asi se conserva la sintaxis comoda m->fila[i][j] pero los datos
 *  quedan contiguos y aprovechan la linea de cache.
 * ========================================================================= */
typedef struct {
    int   n;       /* orden de la matriz cuadrada (n x n) */
    int  *datos;   /* bloque contiguo de n*n enteros      */
    int **fila;    /* n punteros: fila[i] -> &datos[i*n]  */
} Matriz;

/* ---------------------------------------------------------------------------
 *  RESERVA DINAMICA DE MEMORIA (identica a matmul.c)
 *  Tres malloc: descriptor, bloque de n*n enteros e indice de n filas.
 *  Se protege el calculo del tamano contra desbordamiento de size_t.
 * ------------------------------------------------------------------------- */
static Matriz *matriz_crear(int n)
{
    if (n <= 0) {
        fprintf(stderr, "Error: el orden de la matriz debe ser >= 1.\n");
        return NULL;
    }

    size_t orden      = (size_t)n;
    size_t max_celdas = SIZE_MAX / sizeof(int);
    if (orden > max_celdas / orden) {
        fprintf(stderr, "Error: n=%d es demasiado grande, n*n*sizeof(int) "
                        "desborda size_t.\n", n);
        return NULL;
    }

    Matriz *m = (Matriz *)malloc(sizeof(Matriz));
    if (m == NULL) {
        fprintf(stderr, "Error: no se pudo reservar el descriptor de la matriz.\n");
        return NULL;
    }
    m->n     = n;
    m->datos = NULL;
    m->fila  = NULL;

    m->datos = (int *)malloc(orden * orden * sizeof(int));
    if (m->datos == NULL) {
        fprintf(stderr, "Error: no hay memoria para %zu celdas (%.2f MiB).\n",
                orden * orden,
                (double)(orden * orden * sizeof(int)) / (1024.0 * 1024.0));
        free(m);
        return NULL;
    }

    m->fila = (int **)malloc(orden * sizeof(int *));
    if (m->fila == NULL) {
        fprintf(stderr, "Error: no hay memoria para el indice de %d filas.\n", n);
        free(m->datos);
        free(m);
        return NULL;
    }

    for (int i = 0; i < n; i++) {
        m->fila[i] = m->datos + (size_t)i * orden;
    }

    return m;
}

/* ---------------------------------------------------------------------------
 *  LIBERACION DE MEMORIA (identica a matmul.c). Orden inverso al de reserva.
 * ------------------------------------------------------------------------- */
static void matriz_liberar(Matriz *m)
{
    if (m == NULL) return;
    free(m->fila);
    free(m->datos);
    free(m);
}

/* ---------------------------------------------------------------------------
 *  LIMITE MAXIMO SEGURO PARA EL VALOR DE CADA CELDA (identico a matmul.c)
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
 *  LLENADO ALEATORIO (identico a matmul.c)
 * ------------------------------------------------------------------------- */
static void matriz_llenar_aleatoria(Matriz *m, int limite)
{
    size_t total = (size_t)m->n * (size_t)m->n;
    for (size_t i = 0; i < total; i++) {
        m->datos[i] = 1 + (rand() % limite);
    }
}

/* ---------------------------------------------------------------------------
 *  MULTIPLICACION DE MATRICES CON OpenMP:  C = A x B
 *
 *  Orden de bucles i-k-j (identico al nucleo de matmul.c).
 *
 *  DIRECTIVA OPENMP:
 *    #pragma omp parallel for schedule(static)
 *
 *  - "parallel"  : crea un equipo de hilos (region paralela).
 *  - "for"       : reparte las iteraciones del bucle i entre los hilos.
 *  - "schedule(runtime)": la politica de reparto se decide en tiempo de
 *    ejecucion. Por defecto fijamos "static" con omp_set_schedule(), que
 *    da a cada hilo un bloque contiguo de filas (maxima localidad de cache,
 *    mismo esquema que mmHILOS.c). Pero si el usuario fija la variable
 *    OMP_SCHEDULE (ej: OMP_SCHEDULE="dynamic,64") se usa esa otra politica
 *    SIN necesidad de recompilar.
 *
 *  No hace falta declarar private(k, j, a_ik, fila_b, fila_c) porque esas
 *  variables se declaran DENTRO del cuerpo del for, asi que OpenMP las trata
 *  como privadas automaticamente (regla del estandar: las variables con
 *  almacenamiento automatico declaradas dentro de la region son privadas).
 *
 *  NUMERO DE HILOS: se controla con omp_set_num_threads() antes de la region,
 *  o con la variable de entorno OMP_NUM_THREADS. Si no se fija, OpenMP usa
 *  tantos hilos como nucleos logicos tenga el sistema.
 * ------------------------------------------------------------------------- */
static void matriz_multiplicar_omp(const Matriz *A, const Matriz *B, Matriz *C)
{
    int n = A->n;

    /* C empieza en ceros porque se acumula sobre el. */
    memset(C->datos, 0, (size_t)n * (size_t)n * sizeof(int));

    /* --- SENTINELA OpenMP: directiva que indica la region paralela ---
     *  schedule(runtime) toma la politica de OMP_SCHEDULE o de
     *  omp_set_schedule(). Por defecto se fija a static en main(). */
    #pragma omp parallel for schedule(runtime)
    for (int i = 0; i < n; i++) {
        for (int k = 0; k < n; k++) {
            int a_ik = A->fila[i][k];          /* invariante del bucle interno */
            const int *fila_b = B->fila[k];
            int       *fila_c = C->fila[i];
            for (int j = 0; j < n; j++) {
                fila_c[j] += a_ik * fila_b[j];
            }
        }
    }
}

/* ---------------------------------------------------------------------------
 *  Formatea un double con coma decimal (identico a matmul.c)
 * ------------------------------------------------------------------------- */
static void formato_coma(char *destino, size_t tam, double valor, int decimales)
{
    snprintf(destino, tam, "%.*f", decimales, valor);
    for (char *p = destino; *p != '\0'; p++) {
        if (*p == '.') *p = ',';
    }
}

/* ---------------------------------------------------------------------------
 *  IMPRESION (identica a matmul.c, solo util con -p)
 * ------------------------------------------------------------------------- */
static void matriz_imprimir(const Matriz *m, const char *nombre)
{
    printf("\nMatriz %s (%dx%d):\n", nombre, m->n, m->n);
    for (int i = 0; i < m->n; i++) {
        for (int j = 0; j < m->n; j++) {
            printf("%10d", m->fila[i][j]);
        }
        putchar('\n');
    }
}

/* ---------------------------------------------------------------------------
 *  Extrae el nombre del ejecutable de la ruta completa (identico a matmul.c)
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
    "Uso: %s -n <tamano> -l <limite> [-t <hilos>] [-s <semilla>] [-p] [-c]\n"
    "\n"
    "Opciones:\n"
    "  -n <tamano>  Tamano de las matrices (obligatorio). Formas: 5 o 5x5.\n"
    "  -l <limite>  Valor maximo de cada celda; genera enteros en [1, limite].\n"
    "               Debe cumplir  N * limite^2 <= %d.\n"
    "  -t <hilos>   Numero de hilos OpenMP (por defecto: todos los nucleos).\n"
    "               Equivale a OMP_NUM_THREADS pero desde la linea de comandos.\n"
    "  -s <semilla> Semilla del generador aleatorio. Por defecto usa el reloj.\n"
    "  -p           Imprime las matrices (usar solo con N pequeno).\n"
    "  -c           Salida CSV:  Orden;NumHilos;Tiempo(s);Rendimiento(GOP/s)\n"
    "  -h           Muestra esta ayuda.\n"
    "\n"
    "Variables de entorno:\n"
    "  OMP_NUM_THREADS=<N>   Fija el numero de hilos (la opcion -t toma\n"
    "                        precedencia si ambas se usan).\n"
    "\n"
    "Ejemplos:\n"
    "  %s -n 1000 -l 100 -t 4\n"
    "  $env:OMP_NUM_THREADS = 8; .\\bin\\mmOPENMP.exe -n 1000 -l 100\n",
    prog, INT_MAX, prog);
}

/* ---------------------------------------------------------------------------
 *  Conversion segura de cadena a entero (identica a matmul.c)
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
 *  Lectura del tamano de la matriz (identica a matmul.c)
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
        fprintf(stderr, "Error: '%s' no es un tamano valido. "
                        "Use por ejemplo 5 o 5x5.\n", texto);
        return 0;
    }
    memcpy(filas_txt, texto, largo);
    filas_txt[largo] = '\0';

    long filas = 0, columnas = 0;
    if (!leer_entero(filas_txt, &filas) ||
        !leer_entero(separador + 1, &columnas)) {
        fprintf(stderr, "Error: '%s' no es un tamano valido. "
                        "Use por ejemplo 5 o 5x5.\n", texto);
        return 0;
    }

    if (filas != columnas) {
        fprintf(stderr,
            "Error: las matrices deben ser cuadradas, pero se pidio %ldx%ld.\n"
            "       El numero de filas y el de columnas deben coincidir,\n"
            "       por ejemplo %ldx%ld.\n",
            filas, columnas, filas, filas);
        return 0;
    }

    *destino = filas;
    return 1;
}

/* ===========================================================================
 *  PROGRAMA PRINCIPAL
 * ========================================================================= */
int main(int argc, char *argv[])
{
    long n = -1;            /* orden de la matriz     */
    long limite = -1;       /* valor maximo de celda  */
    long semilla = -1;      /* semilla del generador  */
    long num_hilos = -1;    /* hilos OpenMP (-t)      */
    int  imprimir = 0;      /* bandera -p             */
    int  modo_csv = 0;      /* bandera -c             */
    long valor = 0;

    const char *prog = nombre_programa(argv[0]);

    /* ----- 1. Lectura de parametros de la linea de comandos ----- */
    if (argc == 1) {
        mostrar_uso(prog);
        return EXIT_FAILURE;
    }

    for (int i = 1; i < argc; i++) {
        const char *op = argv[i];

        if (strcmp(op, "-h") == 0 || strcmp(op, "--help") == 0) {
            mostrar_uso(prog);
            return EXIT_SUCCESS;
        }
        if (strcmp(op, "-p") == 0) { imprimir = 1; continue; }
        if (strcmp(op, "-c") == 0) { modo_csv = 1; continue; }

        if (strcmp(op, "-n") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: la opcion -n requiere un valor.\n");
                return EXIT_FAILURE;
            }
            if (!leer_dimension(argv[i + 1], &n)) return EXIT_FAILURE;
            i++;
            continue;
        }

        if (strcmp(op, "-t") == 0 || strcmp(op, "-l") == 0 ||
            strcmp(op, "-s") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: la opcion %s requiere un valor.\n", op);
                return EXIT_FAILURE;
            }
            if (!leer_entero(argv[i + 1], &valor)) {
                fprintf(stderr, "Error: el valor de %s ('%s') no es un entero "
                                "valido.\n", op, argv[i + 1]);
                return EXIT_FAILURE;
            }
            if      (op[1] == 't') num_hilos = valor;
            else if (op[1] == 'l') limite    = valor;
            else                   semilla   = valor;
            i++;
            continue;
        }

        fprintf(stderr, "Error: opcion desconocida '%s'.\n\n", op);
        mostrar_uso(prog);
        return EXIT_FAILURE;
    }

    /* ----- 2. Validacion de los parametros ----- */
    if (n <= 0) {
        fprintf(stderr, "Error: debe indicar el orden de la matriz (-n) y ser >= 1.\n");
        return EXIT_FAILURE;
    }
    if (n > INT_MAX) {
        fprintf(stderr, "Error: el orden n=%ld excede el rango de int.\n", n);
        return EXIT_FAILURE;
    }
    if (limite <= 0) {
        fprintf(stderr, "Error: debe indicar el limite de celda (-l) y ser >= 1.\n");
        return EXIT_FAILURE;
    }

    int lmax = limite_seguro((int)n);
    if (limite > lmax) {
        fprintf(stderr,
            "Error: el limite %ld provoca desbordamiento de int.\n"
            "       Peor caso por celda: n * limite^2 = %lld > INT_MAX (%d).\n"
            "       Para n=%ld el limite maximo seguro es %d.\n",
            limite, (long long)n * limite * limite, INT_MAX, n, lmax);
        return EXIT_FAILURE;
    }

    /* ----- 3. Configuracion de OpenMP ----- */
    if (num_hilos > 0) {
        omp_set_num_threads((int)num_hilos);
    }
    /* Desactivar ajuste dinamico: queremos exactamente los hilos pedidos. */
    omp_set_dynamic(0);

    /* Politica de reparto por defecto: static (bloque contiguo por hilo).
     * Se fija siempre con omp_set_schedule, SALVO que el usuario haya
     * definido la variable de entorno OMP_SCHEDULE. Si OMP_SCHEDULE esta
     * fijada, respetamos lo que pidio el usuario. */
    if (getenv("OMP_SCHEDULE") == NULL) {
        omp_set_schedule(omp_sched_static, 0);   /* 0 = bloque por defecto */
    }

    /* Obtener el numero real de hilos que usara la region paralela.
     * Se abre una region paralela minima para leerlo, porque fuera de una
     * region omp_get_num_threads() siempre devuelve 1. */
    int hilos_usados = 1;
    #pragma omp parallel
    {
        #pragma omp single
        hilos_usados = omp_get_num_threads();
    }

    /* Semilla */
    if (semilla < 0) semilla = (long)time(NULL);
    srand((unsigned int)semilla);

    /* ----- 4. Reserva dinamica de las tres matrices ----- */
    Matriz *A = matriz_crear((int)n);
    Matriz *B = matriz_crear((int)n);
    Matriz *C = matriz_crear((int)n);

    if (A == NULL || B == NULL || C == NULL) {
        matriz_liberar(A);
        matriz_liberar(B);
        matriz_liberar(C);
        return EXIT_FAILURE;
    }

    /* ----- 5. Llenado aleatorio de A y B ----- */
    matriz_llenar_aleatoria(A, (int)limite);
    matriz_llenar_aleatoria(B, (int)limite);

    /* ----- 6. Multiplicacion cronometrada con OpenMP ----- */
    double t_inicio = reloj_segundos();
    matriz_multiplicar_omp(A, B, C);
    double t_fin = reloj_segundos();
    double segundos = t_fin - t_inicio;

    /* ----- 7. Reporte ----- */
    double operaciones = 2.0 * (double)n * (double)n * (double)n;
    double gops = operaciones / (segundos > 0.0 ? segundos : 1e-9) / 1e9;

    /* Leer la politica de schedule que se uso realmente */
    omp_sched_t sched_usado;
    int chunk_usado;
    omp_get_schedule(&sched_usado, &chunk_usado);
    const char *nombre_sched = "desconocido";
    switch ((int)sched_usado & 0xFF) {     /* mascara para ignorar monotonic bit */
        case 1: nombre_sched = "static";  break;
        case 2: nombre_sched = "dynamic"; break;
        case 3: nombre_sched = "guided";  break;
        case 4: nombre_sched = "auto";    break;
    }

    if (modo_csv) {
        char t_txt[32], g_txt[32];
        formato_coma(t_txt, sizeof t_txt, segundos, 6);
        formato_coma(g_txt, sizeof g_txt, gops, 3);
        printf("%ld;%d;%s;%s\n", n, hilos_usados, t_txt, g_txt);
    } else {
        double mib = (double)((size_t)n * (size_t)n * sizeof(int))
                     / (1024.0 * 1024.0);

        printf("=============================================\n");
        printf("  Multiplicacion de matrices   C = A x B\n");
        printf("  VERSION OpenMP (parallel for)\n");
        printf("=============================================\n");
        printf("  Orden de las matrices : %ld x %ld\n", n, n);
        printf("  Rango de las celdas   : [1, %ld]\n", limite);
        printf("  Limite maximo seguro  : %d\n", lmax);
        printf("  Semilla aleatoria     : %ld\n", semilla);
        printf("  Hilos OpenMP          : %d\n", hilos_usados);
        printf("  Schedule              : %s", nombre_sched);
        if (chunk_usado > 0) printf(" (chunk=%d)", chunk_usado);
        printf("\n");
        printf("  Memoria por matriz    : %.2f MiB (x3 = %.2f MiB)\n", mib, mib * 3);
        printf("  Operaciones enteras   : %.3e\n", operaciones);
        printf("  Tiempo de calculo     : %.6f s\n", segundos);
        printf("  Rendimiento           : %.3f GOP/s\n", gops);
        printf("=============================================\n");

        if (imprimir) {
            matriz_imprimir(A, "A");
            matriz_imprimir(B, "B");
            matriz_imprimir(C, "C = A x B");
        }
    }

    /* ----- 8. Liberacion de toda la memoria reservada ----- */
    matriz_liberar(A);
    matriz_liberar(B);
    matriz_liberar(C);

    return EXIT_SUCCESS;
}
