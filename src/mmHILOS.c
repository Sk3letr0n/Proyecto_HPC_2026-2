/* ============================================================================
 *  mmHILOS.c - Multiplicacion de matrices cuadradas de enteros positivos
 *              VERSION PARALELA CON HILOS (POSIX threads / pthreads)
 *  Proyecto HPC 2026-2
 *
 *  Es la contraparte paralela de matmul.c. Comparte exactamente la misma
 *  estructura de datos, el mismo llenado aleatorio y el mismo nucleo de
 *  calculo (orden de bucles i-k-j), de modo que ambos programas son
 *  comparables 1:1 y el speedup medido es limpio.
 *
 *  IDEA DE LA PARALELIZACION (paralelismo de datos):
 *    - C = A x B se calcula fila por fila y cada fila de C es INDEPENDIENTE
 *      de las demas.
 *    - Se reparten las FILAS de C en bloques contiguos, un bloque por hilo.
 *    - A y B son de solo lectura  -> ningun hilo las modifica.
 *    - Cada hilo escribe filas DISJUNTAS de C -> no hay condiciones de
 *      carrera, no se necesitan mutex ni locks. La unica sincronizacion es
 *      esperar (join) a que todos terminen.
 *    - Los bloques de filas son grandes y contiguos en memoria, asi que
 *      tampoco hay "false sharing" apreciable entre hilos.
 *
 *  Compilacion:
 *      gcc -O2 -Wall -Wextra -std=c11 -pthread -o bin/mmHILOS src/mmHILOS.c -lm
 * ========================================================================== */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <stdint.h>
#include <errno.h>
#include <time.h>
#include <math.h>
#include <pthread.h>

/* ---------------------------------------------------------------------------
 *  Reloj de pared (wall clock) portable.
 *  En HPC lo que interesa es el tiempo REAL transcurrido, porque es el que se
 *  reduce al paralelizar. (El tiempo de CPU, en cambio, suele AUMENTAR con los
 *  hilos porque suma el trabajo de todos los nucleos.)
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
  #include <unistd.h>
  static double reloj_segundos(void) {
      struct timespec ts;
      clock_gettime(CLOCK_MONOTONIC, &ts);
      return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
  }
#endif

/* ---------------------------------------------------------------------------
 *  NUMERO DE NUCLEOS LOGICOS DISPONIBLES
 *
 *  Sirve para elegir un valor por defecto sensato de -t: por lo general no
 *  tiene sentido lanzar mas hilos de calculo que nucleos, porque solo anaden
 *  sobrecarga de planificacion sin ganar paralelismo real.
 * ------------------------------------------------------------------------- */
static int nucleos_disponibles(void)
{
#if defined(_WIN32)
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    return (int)info.dwNumberOfProcessors;
#else
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return (n > 0) ? (int)n : 1;
#endif
}

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
 *  Se exige  n * L * L <= INT_MAX  =>  L <= raiz(INT_MAX / n).
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
 *  Enteros POSITIVOS en [1, limite], recorriendo el bloque de forma lineal.
 * ------------------------------------------------------------------------- */
static void matriz_llenar_aleatoria(Matriz *m, int limite)
{
    size_t total = (size_t)m->n * (size_t)m->n;
    for (size_t i = 0; i < total; i++) {
        m->datos[i] = 1 + (rand() % limite);
    }
}

/* ===========================================================================
 *  NUCLEO PARALELO
 * ========================================================================= */

/* ---------------------------------------------------------------------------
 *  DATOS QUE RECIBE CADA HILO
 *
 *  Punteros a las tres matrices (compartidas; A y B solo se leen, C se escribe
 *  solo en las filas propias) y el rango de filas de C que le toca calcular:
 *  el intervalo semiabierto [fila_inicio, fila_fin).
 * ------------------------------------------------------------------------- */
typedef struct {
    const Matriz *A;
    const Matriz *B;
    Matriz       *C;
    int           fila_inicio;   /* primera fila (incluida)  */
    int           fila_fin;      /* fila limite (excluida)   */
} TareaHilo;

/* ---------------------------------------------------------------------------
 *  FUNCION QUE EJECUTA CADA HILO
 *
 *  Calcula el bloque de filas [fila_inicio, fila_fin) de C con el mismo orden
 *  de bucles i-k-j que la version secuencial: el bucle interno recorre B y C
 *  por filas (contiguo en memoria), minimizando los fallos de cache.
 *
 *  Como cada hilo pone a cero e inicializa UNICAMENTE sus propias filas de C,
 *  no hace falta un memset global previo ni ninguna sincronizacion entre hilos.
 * ------------------------------------------------------------------------- */
static void *hilo_trabajador(void *arg)
{
    TareaHilo *t = (TareaHilo *)arg;
    const Matriz *A = t->A;
    const Matriz *B = t->B;
    Matriz       *C = t->C;
    int n = A->n;

    for (int i = t->fila_inicio; i < t->fila_fin; i++) {
        int *fila_c = C->fila[i];

        /* La fila i de C se pone a cero porque se acumula sobre ella. */
        memset(fila_c, 0, (size_t)n * sizeof(int));

        for (int k = 0; k < n; k++) {
            int a_ik = A->fila[i][k];          /* invariante del bucle interno */
            const int *fila_b = B->fila[k];
            for (int j = 0; j < n; j++) {
                fila_c[j] += a_ik * fila_b[j];
            }
        }
    }
    return NULL;
}

/* ---------------------------------------------------------------------------
 *  MULTIPLICACION PARALELA:  C = A x B  con num_hilos hilos
 *
 *  Reparte las n filas de C entre los hilos en bloques contiguos y lo mas
 *  equilibrados posible: si n no es multiplo de num_hilos, el residuo r = n%T
 *  se reparte dando UNA fila extra a los primeros r hilos. Asi la diferencia
 *  de carga entre hilos es de una sola fila como maximo.
 *
 *  Devuelve 0 si todo fue bien, distinto de 0 si fallo la creacion de hilos.
 * ------------------------------------------------------------------------- */
static int matriz_multiplicar_hilos(const Matriz *A, const Matriz *B,
                                    Matriz *C, int num_hilos)
{
    int n = A->n;

    /* Nunca tiene sentido usar mas hilos que filas hay para repartir. */
    if (num_hilos > n) num_hilos = n;
    if (num_hilos < 1) num_hilos = 1;

    /* Caso trivial: un solo hilo -> se ejecuta el nucleo directamente, sin
     * pagar el coste de crear y unir hilos. */
    if (num_hilos == 1) {
        TareaHilo t = { A, B, C, 0, n };
        hilo_trabajador(&t);
        return 0;
    }

    pthread_t *hilos  = (pthread_t *)malloc((size_t)num_hilos * sizeof(pthread_t));
    TareaHilo *tareas = (TareaHilo *)malloc((size_t)num_hilos * sizeof(TareaHilo));
    if (hilos == NULL || tareas == NULL) {
        fprintf(stderr, "Error: no hay memoria para %d hilos.\n", num_hilos);
        free(hilos);
        free(tareas);
        return 1;
    }

    /* Reparto equilibrado de filas: base filas a cada hilo y una extra a los
     * primeros (n % num_hilos). */
    int base    = n / num_hilos;
    int residuo = n % num_hilos;

    int creados = 0;   /* cuantos hilos se lograron crear realmente */
    int inicio  = 0;
    int fallo   = 0;

    for (int h = 0; h < num_hilos; h++) {
        int filas_h = base + (h < residuo ? 1 : 0);
        tareas[h].A = A;
        tareas[h].B = B;
        tareas[h].C = C;
        tareas[h].fila_inicio = inicio;
        tareas[h].fila_fin    = inicio + filas_h;
        inicio += filas_h;

        if (pthread_create(&hilos[h], NULL, hilo_trabajador, &tareas[h]) != 0) {
            fprintf(stderr, "Error: no se pudo crear el hilo %d de %d.\n",
                    h, num_hilos);
            fallo = 1;
            break;           /* dejamos de crear; unimos los ya creados abajo */
        }
        creados++;
    }

    /* Se espera (join) a todos los hilos que si se crearon. */
    for (int h = 0; h < creados; h++) {
        pthread_join(hilos[h], NULL);
    }

    /* Si hubo fallo a mitad de camino, completamos en el hilo principal las
     * filas que quedaron sin asignar, para no devolver un resultado parcial. */
    if (fallo && inicio < n) {
        TareaHilo t = { A, B, C, inicio, n };
        hilo_trabajador(&t);
    }

    free(hilos);
    free(tareas);
    return fallo;
}

/* ---------------------------------------------------------------------------
 *  IMPRESION (identica a matmul.c; solo util con -p y N pequeno)
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
 *  Nombre corto del ejecutable a partir de argv[0] (identico a matmul.c).
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
    "     %s <tamano> <limite> [semilla] [-t <hilos>] [-p] [-c]\n"
    "\n"
    "Version PARALELA con hilos (pthreads) de la multiplicacion de matrices.\n"
    "\n"
    "Opciones:\n"
    "  -n <tamano>    Tamano de las matrices (obligatorio). Dos formas validas:\n"
    "                   5     un solo numero\n"
    "                   5x5   filas por columnas (deben coincidir, son cuadradas)\n"
    "  -l <limite>    Valor maximo de cada celda; enteros en [1, limite]\n"
    "                 (obligatorio). Debe cumplir  N * limite^2 <= %d.\n"
    "  -t <hilos>     Numero de hilos de calculo. Por defecto = nucleos logicos\n"
    "                 del sistema (aqui: %d). Se acota a [1, N].\n"
    "  -s <semilla>   Semilla del generador aleatorio. Por defecto usa el reloj.\n"
    "  -p             Imprime las matrices A, B y C (usar solo con N pequeno).\n"
    "  -c             Salida en una sola linea separada por ';' (para Excel):\n"
    "                   Orden;NumHilos;Tiempo(s);Rendimiento(GOP/s)\n"
    "  -h             Muestra esta ayuda.\n"
    "\n"
    "Ejemplos:\n"
    "  %s -n 1000 -l 100 -t 8\n"
    "  %s -n 5x5 -l 9 -s 42 -p\n"
    "  %s 512x512 50 7 -t 4\n",
    prog, prog, INT_MAX, nucleos_disponibles(), prog, prog, prog);
}

/* ---------------------------------------------------------------------------
 *  Conversion segura de cadena a entero (identica a matmul.c).
 *  Devuelve 1 si la conversion fue valida, 0 si no.
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
 *  LECTURA DEL TAMANO (identica a matmul.c). Admite "5" y "5x5" cuadradas.
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
    long n       = -1;      /* orden de la matriz    */
    long limite  = -1;      /* valor maximo de celda */
    long semilla = -1;      /* semilla del generador */
    long hilos   = -1;      /* numero de hilos (-t)  */
    int  imprimir = 0;      /* bandera -p            */
    int  modo_csv = 0;      /* bandera -c            */
    long valor = 0;

    const char *prog = nombre_programa(argv[0]);

    /* ----- 1. Lectura de parametros de la linea de comandos ----- */
    if (argc == 1) {
        mostrar_uso(prog);
        return EXIT_FAILURE;
    }

    if (argv[1][0] != '-') {
        /* Forma posicional: mmHILOS <tamano> <limite> [semilla] [-t H] [-p] [-c] */
        if (argc < 3) {
            fprintf(stderr, "Error: faltan parametros.\n\n");
            mostrar_uso(prog);
            return EXIT_FAILURE;
        }
        if (!leer_dimension(argv[1], &n)) {
            return EXIT_FAILURE;
        }
        if (!leer_entero(argv[2], &limite)) {
            fprintf(stderr, "Error: el limite ('%s') debe ser un numero "
                            "entero.\n", argv[2]);
            return EXIT_FAILURE;
        }
        /* El resto: semilla, -t <hilos>, y/o banderas -p y -c, en cualquier orden. */
        for (int i = 3; i < argc; i++) {
            if (strcmp(argv[i], "-p") == 0) { imprimir = 1; continue; }
            if (strcmp(argv[i], "-c") == 0) { modo_csv = 1; continue; }
            if (strcmp(argv[i], "-t") == 0) {
                if (i + 1 >= argc || !leer_entero(argv[i + 1], &hilos)) {
                    fprintf(stderr, "Error: la opcion -t requiere un numero de "
                                    "hilos valido.\n");
                    return EXIT_FAILURE;
                }
                i++;
                continue;
            }
            if (semilla < 0 && leer_entero(argv[i], &semilla)) continue;
            fprintf(stderr, "Error: parametro '%s' no reconocido.\n\n", argv[i]);
            mostrar_uso(prog);
            return EXIT_FAILURE;
        }
    } else {
        /* Forma con banderas: mmHILOS -n N -l L [-t H] [-s S] [-p] [-c] */
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
                if (!leer_dimension(argv[i + 1], &n)) {
                    return EXIT_FAILURE;
                }
                i++;
                continue;
            }

            if (strcmp(op, "-l") == 0 || strcmp(op, "-s") == 0 ||
                strcmp(op, "-t") == 0) {
                if (i + 1 >= argc) {
                    fprintf(stderr, "Error: la opcion %s requiere un valor.\n", op);
                    return EXIT_FAILURE;
                }
                if (!leer_entero(argv[i + 1], &valor)) {
                    fprintf(stderr, "Error: el valor de %s ('%s') no es un entero "
                                    "valido.\n", op, argv[i + 1]);
                    return EXIT_FAILURE;
                }
                if      (op[1] == 'l') limite  = valor;
                else if (op[1] == 's') semilla = valor;
                else                   hilos   = valor;   /* -t */
                i++;
                continue;
            }

            fprintf(stderr, "Error: opcion desconocida '%s'.\n\n", op);
            mostrar_uso(prog);
            return EXIT_FAILURE;
        }
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

    /* Numero de hilos: por defecto = nucleos logicos; se acota a [1, n]. */
    if (hilos <= 0) hilos = nucleos_disponibles();
    if (hilos < 1)  hilos = 1;
    if (hilos > n)  hilos = n;

    /* Semilla: si no se indico, se toma del reloj del sistema. */
    if (semilla < 0) semilla = (long)time(NULL);
    srand((unsigned int)semilla);

    /* ----- 3. Reserva dinamica de las tres matrices ----- */
    Matriz *A = matriz_crear((int)n);
    Matriz *B = matriz_crear((int)n);
    Matriz *C = matriz_crear((int)n);

    if (A == NULL || B == NULL || C == NULL) {
        matriz_liberar(A);
        matriz_liberar(B);
        matriz_liberar(C);
        return EXIT_FAILURE;
    }

    /* ----- 4. Llenado aleatorio de A y B ----- */
    matriz_llenar_aleatoria(A, (int)limite);
    matriz_llenar_aleatoria(B, (int)limite);

    /* ----- 5. Multiplicacion PARALELA cronometrada ----- */
    double t_inicio = reloj_segundos();
    int rc = matriz_multiplicar_hilos(A, B, C, (int)hilos);
    double t_fin = reloj_segundos();
    double segundos = t_fin - t_inicio;

    if (rc != 0) {
        fprintf(stderr, "Error: la multiplicacion paralela fallo.\n");
        matriz_liberar(A);
        matriz_liberar(B);
        matriz_liberar(C);
        return EXIT_FAILURE;
    }

    /* ----- 6. Reporte ----- */
    double operaciones = 2.0 * (double)n * (double)n * (double)n;
    double gops = operaciones / (segundos > 0.0 ? segundos : 1e-9) / 1e9;

    if (modo_csv) {
        /* Linea unica separada por punto y coma, lista para pegar en Excel:
         *   Orden ; NumHilos ; Tiempo(s) ; Rendimiento(GOP/s)              */
        printf("%ldx%ld;%ld;%.6f;%.3f\n", n, n, hilos, segundos, gops);
    } else {
        double mib = (double)((size_t)n * (size_t)n * sizeof(int))
                     / (1024.0 * 1024.0);

        printf("=============================================\n");
        printf("  Multiplicacion de matrices (HILOS)  C = A x B\n");
        printf("=============================================\n");
        printf("  Orden de las matrices : %ld x %ld\n", n, n);
        printf("  Rango de las celdas   : [1, %ld]\n", limite);
        printf("  Limite maximo seguro  : %d\n", lmax);
        printf("  Semilla aleatoria     : %ld\n", semilla);
        printf("  Numero de hilos       : %ld  (nucleos: %d)\n",
               hilos, nucleos_disponibles());
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

    /* ----- 7. Liberacion de toda la memoria reservada ----- */
    matriz_liberar(A);
    matriz_liberar(B);
    matriz_liberar(C);

    return EXIT_SUCCESS;
}
