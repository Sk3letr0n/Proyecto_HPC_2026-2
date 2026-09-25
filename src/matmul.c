/* ============================================================================
 *  matmul.c - Multiplicacion de matrices cuadradas de enteros positivos
 *  Proyecto HPC 2026-2
 *
 *  Caracteristicas:
 *    - Matrices cuadradas N x N.
 *    - Celdas de tipo int (entero simple con signo, 32 bits) y valor positivo.
 *    - Llenado con valores pseudoaleatorios acotados por un limite L.
 *    - Reserva dinamica de memoria (bloque contiguo + vector de punteros).
 *    - Ejecucion parametrica: todo se pasa por linea de comandos, el programa
 *      nunca se queda esperando entrada del usuario.
 *    - El tamano admite las formas "5" y "5x5" (filas por columnas).
 *
 *  Compilacion:
 *      gcc -O2 -Wall -Wextra -std=c11 -o bin/matmul src/matmul.c
 * ========================================================================== */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <stdint.h>
#include <errno.h>
#include <time.h>
#include <math.h>

/* ---------------------------------------------------------------------------
 *  Reloj de pared (wall clock) portable.
 *  En HPC no interesa tanto el tiempo de CPU como el tiempo real transcurrido,
 *  porque es el que se reduce al paralelizar.
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
 *  TIPO MATRIZ
 *
 *  La matriz se guarda en UN SOLO bloque contiguo de memoria (campo datos)
 *  y ademas en un vector de punteros (campo fila), donde fila[i] apunta al
 *  inicio de la fila i dentro de ese bloque.
 *
 *  Ventaja: se conserva la sintaxis comoda  m->fila[i][j]  pero los datos
 *  quedan contiguos, lo que aprovecha la linea de cache del procesador.
 * ========================================================================= */
typedef struct {
    int   n;       /* orden de la matriz cuadrada (n x n) */
    int  *datos;   /* bloque contiguo de n*n enteros      */
    int **fila;    /* n punteros: fila[i] -> &datos[i*n]  */
} Matriz;

/* ---------------------------------------------------------------------------
 *  RESERVA DINAMICA DE MEMORIA
 *
 *  Se hacen exactamente TRES llamadas a malloc:
 *    0) el descriptor Matriz
 *    1) un bloque de n*n enteros  -> los datos reales de la matriz
 *    2) un bloque de n punteros   -> el indice de filas
 *
 *  Antes de reservar se verifica que n*n*sizeof(int) no desborde size_t,
 *  porque en ese caso malloc reservaria menos memoria de la pedida y se
 *  escribiria fuera del bloque (bug clasico de seguridad).
 * ------------------------------------------------------------------------- */
static Matriz *matriz_crear(int n)
{
    if (n <= 0) {
        fprintf(stderr, "Error: el orden de la matriz debe ser >= 1.\n");
        return NULL;
    }

    /* Proteccion contra desbordamiento en el calculo del tamano a reservar. */
    size_t orden      = (size_t)n;
    size_t max_celdas = SIZE_MAX / sizeof(int);
    if (orden > max_celdas / orden) {
        fprintf(stderr, "Error: n=%d es demasiado grande, n*n*sizeof(int) "
                        "desborda size_t.\n", n);
        return NULL;
    }

    /* (0) Descriptor de la matriz. */
    Matriz *m = (Matriz *)malloc(sizeof(Matriz));
    if (m == NULL) {
        fprintf(stderr, "Error: no se pudo reservar el descriptor de la matriz.\n");
        return NULL;
    }
    m->n     = n;
    m->datos = NULL;
    m->fila  = NULL;

    /* (1) Bloque contiguo con TODAS las celdas: n*n enteros. */
    m->datos = (int *)malloc(orden * orden * sizeof(int));
    if (m->datos == NULL) {
        fprintf(stderr, "Error: no hay memoria para %zu celdas (%.2f MiB).\n",
                orden * orden,
                (double)(orden * orden * sizeof(int)) / (1024.0 * 1024.0));
        free(m);
        return NULL;
    }

    /* (2) Vector de punteros a filas: n punteros. */
    m->fila = (int **)malloc(orden * sizeof(int *));
    if (m->fila == NULL) {
        fprintf(stderr, "Error: no hay memoria para el indice de %d filas.\n", n);
        free(m->datos);
        free(m);
        return NULL;
    }

    /* Se "cablea" cada puntero de fila a su posicion dentro del bloque. */
    for (int i = 0; i < n; i++) {
        m->fila[i] = m->datos + (size_t)i * orden;
    }

    return m;
}

/* ---------------------------------------------------------------------------
 *  LIBERACION DE MEMORIA
 *  Se libera en orden inverso al de reserva. Cada malloc tiene su free.
 * ------------------------------------------------------------------------- */
static void matriz_liberar(Matriz *m)
{
    if (m == NULL) return;
    free(m->fila);    /* (2) indice de filas */
    free(m->datos);   /* (1) bloque de datos */
    free(m);          /* (0) descriptor      */
}

/* ---------------------------------------------------------------------------
 *  LIMITE MAXIMO SEGURO PARA EL VALOR DE CADA CELDA
 *
 *  Cada celda del resultado es:  C[i][j] = suma de n productos A[i][k]*B[k][j]
 *  En el peor caso todas las celdas valen L, por lo tanto:
 *
 *        valor maximo posible = n * L * L
 *
 *  Para que ese valor quepa en un int (INT_MAX = 2147483647) se exige:
 *
 *        n * L * L <= INT_MAX   =>   L <= raiz(INT_MAX / n)
 *
 *  Esta funcion devuelve ese L maximo, calculado con aritmetica de 64 bits
 *  para que la propia verificacion no desborde.
 * ------------------------------------------------------------------------- */
static int limite_seguro(int n)
{
    long long objetivo = (long long)INT_MAX;
    long long L = (long long)sqrt((double)objetivo / (double)n);

    if (L < 1) L = 1;
    /* Ajuste fino por errores de redondeo del sqrt en punto flotante. */
    while ((L + 1) * (L + 1) * (long long)n <= objetivo) L++;
    while (L > 1 && L * L * (long long)n > objetivo)     L--;

    return (int)L;
}

/* ---------------------------------------------------------------------------
 *  LLENADO ALEATORIO
 *  Genera enteros POSITIVOS en el rango cerrado [1, limite].
 *  Se recorre el bloque contiguo de forma lineal (maxima localidad).
 * ------------------------------------------------------------------------- */
static void matriz_llenar_aleatoria(Matriz *m, int limite)
{
    size_t total = (size_t)m->n * (size_t)m->n;
    for (size_t i = 0; i < total; i++) {
        m->datos[i] = 1 + (rand() % limite);
    }
}

/* ---------------------------------------------------------------------------
 *  MULTIPLICACION DE MATRICES:  C = A x B
 *
 *  Orden de bucles i-k-j (en lugar del clasico i-j-k):
 *  con este orden el bucle interno recorre B y C por filas, es decir de
 *  forma contigua en memoria, lo que reduce mucho los fallos de cache.
 *  Es la version secuencial que sirve de linea base para comparar contra
 *  las versiones paralelas del proyecto.
 * ------------------------------------------------------------------------- */
static void matriz_multiplicar(const Matriz *A, const Matriz *B, Matriz *C)
{
    int n = A->n;

    /* C empieza en ceros porque se acumula sobre el. */
    memset(C->datos, 0, (size_t)n * (size_t)n * sizeof(int));

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
 *  Formatea un double con coma decimal (ej. 0,004 en vez de 0.004), que es lo
 *  que espera Excel con configuracion regional en espanol. Se reemplaza el
 *  punto a mano para no depender del locale del sistema.
 * ------------------------------------------------------------------------- */
static void formato_coma(char *destino, size_t tam, double valor, int decimales)
{
    snprintf(destino, tam, "%.*f", decimales, valor);
    for (char *p = destino; *p != '\0'; p++) {
        if (*p == '.') *p = ',';
    }
}

/* ---------------------------------------------------------------------------
 *  IMPRESION (solo util para matrices pequenas, con la opcion -p)
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
 *  Extrae el nombre del ejecutable de la ruta completa que llega en argv[0].
 *  PowerShell y CMD entregan la ruta absoluta; sin esto la ayuda mostraria
 *  algo como "C:\Repositorios\...\bin\matmul.exe" en cada linea.
 *  Se aceptan los dos separadores porque en Windows conviven ambos.
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
    "Uso: %s -n <tamano> -l <limite> [-s <semilla>] [-p] [-c]\n"
    "     %s <tamano> <limite> [semilla] [-p] [-c]\n"
    "\n"
    "Opciones:\n"
    "  -n <tamano>    Tamano de las matrices (obligatorio). Dos formas validas:\n"
    "                   5     un solo numero\n"
    "                   5x5   filas por columnas\n"
    "                 Las matrices son cuadradas: en la forma FxC ambos numeros\n"
    "                 deben coincidir. Escribalo sin espacios.\n"
    "  -l <limite>    Valor maximo de cada celda; genera enteros en [1, limite]\n"
    "                 (obligatorio). Debe cumplir  N * limite^2 <= %d.\n"
    "  -s <semilla>   Semilla del generador aleatorio. Por defecto usa el reloj.\n"
    "                 Fijarla permite reproducir exactamente la misma ejecucion.\n"
    "  -p             Imprime las matrices A, B y C (usar solo con N pequeno).\n"
    "  -c             Salida en una sola linea separada por ';' (para Excel):\n"
    "                   Orden;Tiempo(s);Rendimiento(GOP/s)\n"
    "  -h             Muestra esta ayuda.\n"
    "\n"
    "Ejemplos:\n"
    "  %s -n 5x5 -l 9 -s 42 -p\n"
    "  %s -n 1000 -l 100\n"
    "  %s 512x512 50 7\n",
    prog, prog, INT_MAX, prog, prog, prog);
}

/* ---------------------------------------------------------------------------
 *  Conversion segura de cadena a entero (sustituye a atoi, que no detecta
 *  errores). Devuelve 1 si la conversion fue valida, 0 si no.
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
 *  LECTURA DEL TAMANO DE LA MATRIZ
 *
 *  Admite dos formas equivalentes:
 *      "5"     -> un solo numero
 *      "5x5"   -> filas por columnas (tambien vale "5X5")
 *
 *  Como el proyecto trabaja con matrices CUADRADAS, en la forma "FxC" se
 *  exige que ambos valores coincidan; si no, se rechaza con un mensaje que
 *  explica el motivo en lugar de tomar uno de los dos en silencio.
 *
 *  Devuelve 1 si el tamano es valido, 0 si no.
 * ------------------------------------------------------------------------- */
static int leer_dimension(const char *texto, long *destino)
{
    const char *separador = strpbrk(texto, "xX");

    /* Forma simple: un unico numero. */
    if (separador == NULL) {
        if (!leer_entero(texto, destino)) {
            fprintf(stderr, "Error: '%s' no es un tamano valido. "
                            "Use por ejemplo 5 o 5x5.\n", texto);
            return 0;
        }
        return 1;
    }

    /* Forma FxC: se copia la parte de las filas para poder convertirla. */
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
    long n = -1;            /* orden de la matriz    */
    long limite = -1;       /* valor maximo de celda */
    long semilla = -1;      /* semilla del generador */
    int  imprimir = 0;      /* bandera -p            */
    int  modo_csv = 0;      /* bandera -c            */
    long valor = 0;

    /* Nombre corto del ejecutable, para los mensajes de ayuda. */
    const char *prog = nombre_programa(argv[0]);

    /* ----- 1. Lectura de parametros de la linea de comandos ----- */
    if (argc == 1) {
        mostrar_uso(prog);
        return EXIT_FAILURE;
    }

    if (argv[1][0] != '-') {
        /* Forma posicional:  matmul <tamano> <limite> [semilla] [-p] [-c] */
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
        /* El resto son la semilla y/o las banderas -p y -c, en cualquier orden. */
        for (int i = 3; i < argc; i++) {
            if (strcmp(argv[i], "-p") == 0) { imprimir = 1; continue; }
            if (strcmp(argv[i], "-c") == 0) { modo_csv = 1; continue; }
            if (semilla < 0 && leer_entero(argv[i], &semilla)) continue;
            fprintf(stderr, "Error: parametro '%s' no reconocido.\n\n", argv[i]);
            mostrar_uso(prog);
            return EXIT_FAILURE;
        }
    } else {
        /* Forma con banderas:  matmul -n N -l L [-s S] [-p] [-c] */
        for (int i = 1; i < argc; i++) {
            const char *op = argv[i];

            if (strcmp(op, "-h") == 0 || strcmp(op, "--help") == 0) {
                mostrar_uso(prog);
                return EXIT_SUCCESS;
            }
            if (strcmp(op, "-p") == 0) { imprimir = 1; continue; }
            if (strcmp(op, "-c") == 0) { modo_csv = 1; continue; }

            /* -n acepta tanto "5" como "5x5", por eso se trata aparte. */
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

            if (strcmp(op, "-l") == 0 || strcmp(op, "-s") == 0) {
                if (i + 1 >= argc) {
                    fprintf(stderr, "Error: la opcion %s requiere un valor.\n", op);
                    return EXIT_FAILURE;
                }
                if (!leer_entero(argv[i + 1], &valor)) {
                    fprintf(stderr, "Error: el valor de %s ('%s') no es un entero "
                                    "valido.\n", op, argv[i + 1]);
                    return EXIT_FAILURE;
                }
                if (op[1] == 'l') limite  = valor;
                else              semilla = valor;
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

    /* Verificacion anti-desbordamiento: n * limite^2 debe caber en un int. */
    int lmax = limite_seguro((int)n);
    if (limite > lmax) {
        fprintf(stderr,
            "Error: el limite %ld provoca desbordamiento de int.\n"
            "       Peor caso por celda: n * limite^2 = %lld > INT_MAX (%d).\n"
            "       Para n=%ld el limite maximo seguro es %d.\n",
            limite, (long long)n * limite * limite, INT_MAX, n, lmax);
        return EXIT_FAILURE;
    }

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

    /* ----- 5. Multiplicacion cronometrada ----- */
    double t_inicio = reloj_segundos();
    matriz_multiplicar(A, B, C);
    double t_fin = reloj_segundos();
    double segundos = t_fin - t_inicio;

    /* ----- 6. Reporte ----- */
    double operaciones = 2.0 * (double)n * (double)n * (double)n;
    double gops = operaciones / (segundos > 0.0 ? segundos : 1e-9) / 1e9;

    if (modo_csv) {
        /* Linea unica separada por punto y coma, lista para pegar en Excel:
         *   Orden ; Tiempo(s) ; Rendimiento(GOP/s)
         * El orden va como un solo numero y los decimales con coma.          */
        char t_txt[32], g_txt[32];
        formato_coma(t_txt, sizeof t_txt, segundos, 6);
        formato_coma(g_txt, sizeof g_txt, gops, 3);
        printf("%ld;%s;%s\n", n, t_txt, g_txt);
    } else {
        double mib = (double)((size_t)n * (size_t)n * sizeof(int))
                     / (1024.0 * 1024.0);

        printf("=============================================\n");
        printf("  Multiplicacion de matrices   C = A x B\n");
        printf("=============================================\n");
        printf("  Orden de las matrices : %ld x %ld\n", n, n);
        printf("  Rango de las celdas   : [1, %ld]\n", limite);
        printf("  Limite maximo seguro  : %d\n", lmax);
        printf("  Semilla aleatoria     : %ld\n", semilla);
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
