/* ============================================================================
 *  mmPROCESOS.c - Multiplicacion de matrices cuadradas de enteros positivos
 *                 VERSION PARALELA CON PROCESOS
 *  Proyecto HPC 2026-2
 *
 *  Es la contraparte con PROCESOS de mmHILOS.c. Comparte la misma estructura
 *  de datos, el mismo llenado aleatorio y EXACTAMENTE el mismo nucleo de
 *  calculo (orden de bucles i-k-j), de modo que los tres programas
 *  (matmul, mmHILOS, mmPROCESOS) son comparables 1:1.
 *
 *  ---------------------------------------------------------------------------
 *  PROCESOS FRENTE A HILOS: EL PROBLEMA DE LA MEMORIA
 *  ---------------------------------------------------------------------------
 *  Los hilos comparten el espacio de direcciones del proceso: basta pasarle a
 *  cada hilo un puntero a A, B y C y todos ven la misma memoria.
 *
 *  Los procesos NO. Cada proceso tiene su propio espacio de direcciones, asi
 *  que si el hijo escribiera en "su" C, el padre no veria nada: al terminar el
 *  hijo esa memoria se destruye. Por eso hay que pedirle explicitamente al
 *  sistema operativo un bloque de MEMORIA COMPARTIDA, visible por todos los
 *  procesos, y poner A, B y C dentro de ese bloque.
 *
 *  El bloque compartido se organiza asi (un solo bloque, tres zonas):
 *
 *      [ A: n*n enteros ][ B: n*n enteros ][ C: n*n enteros ]
 *        offset 0          offset n*n        offset 2*n*n
 *
 *  ---------------------------------------------------------------------------
 *  DOS IMPLEMENTACIONES SEGUN EL SISTEMA OPERATIVO
 *  ---------------------------------------------------------------------------
 *  (1) POSIX (Linux, WSL, macOS):  fork() + mmap(MAP_SHARED | MAP_ANONYMOUS)
 *
 *      fork() duplica el proceso actual. El hijo continua en la misma linea de
 *      codigo con un pid distinto (fork devuelve 0 en el hijo y el pid del
 *      hijo en el padre). Como el bloque se reservo con MAP_SHARED ANTES del
 *      fork, padre e hijos comparten esas paginas fisicas: lo que el hijo
 *      escribe en C, el padre lo ve. El padre espera con waitpid().
 *
 *  (2) Windows:  CreateProcess() + CreateFileMapping()
 *
 *      Windows NO tiene fork(); su API nativa solo sabe crear un proceso nuevo
 *      ejecutando un programa (el equivalente a fork + exec de una sola vez).
 *      Entonces el padre:
 *        a) crea un bloque compartido CON NOMBRE (CreateFileMapping sobre
 *           INVALID_HANDLE_VALUE = respaldado por memoria, no por un fichero);
 *        b) se relanza a si mismo P veces con CreateProcess, pasandole por
 *           linea de comandos el nombre del bloque y el rango de filas:
 *               mmPROCESOS --trabajador <nombre> <n> <inicio> <fin>
 *        c) cada hijo abre ese mismo bloque por su nombre, calcula sus filas
 *           y termina;
 *        d) el padre espera con WaitForSingleObject.
 *
 *      Es el mismo esquema logico que fork(), solo que el hijo arranca desde
 *      cero y recibe su trabajo por argumentos en vez de heredarlo.
 *
 *  ---------------------------------------------------------------------------
 *  REPARTO DEL TRABAJO (identico al de mmHILOS.c)
 *  ---------------------------------------------------------------------------
 *  Cada fila de C es independiente de las demas, asi que se reparten las n
 *  filas en bloques contiguos, uno por proceso. A y B son de solo lectura y
 *  cada proceso escribe filas DISJUNTAS de C: no hay condiciones de carrera y
 *  no se necesita ningun semaforo ni mutex. La unica sincronizacion es esperar
 *  a que todos los hijos terminen.
 *
 *  IMPORTANTE: el tiempo medido INCLUYE la creacion de los procesos. Es
 *  deliberado: ese coste es justamente la diferencia real frente a los hilos
 *  y es lo que se quiere observar en las tablas de resultados.
 *
 *  ---------------------------------------------------------------------------
 *  Compilacion:
 *      gcc -O2 -Wall -Wextra -std=c11 -o bin/mmPROCESOS src/mmPROCESOS.c -lm
 * ========================================================================== */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <stdint.h>
#include <errno.h>
#include <time.h>
#include <math.h>

#if defined(_WIN32)
  #include <windows.h>
#else
  #include <unistd.h>
  #include <sys/mman.h>
  #include <sys/wait.h>
#endif

/* ---------------------------------------------------------------------------
 *  Reloj de pared (wall clock) portable.
 *  En HPC lo que interesa es el tiempo REAL transcurrido, porque es el que se
 *  reduce al paralelizar. (El tiempo de CPU, en cambio, suele AUMENTAR al
 *  paralelizar, porque suma el trabajo de todos los nucleos.)
 * ------------------------------------------------------------------------- */
#if defined(_WIN32)
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

/* ---------------------------------------------------------------------------
 *  NUMERO DE NUCLEOS LOGICOS DISPONIBLES
 *  Sirve para elegir un valor por defecto sensato de -np.
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
 *  REGION DE MEMORIA COMPARTIDA
 *
 *  Un unico bloque con las tres matrices seguidas. Se guarda solo el puntero
 *  base y el tamano; los punteros a A, B y C se derivan con aritmetica.
 *
 *  OJO CON UN DETALLE FINO: no se puede guardar dentro del bloque compartido
 *  un vector de punteros a filas (el campo 'fila' de matmul.c y mmHILOS.c),
 *  porque cada proceso puede mapear el bloque en una DIRECCION VIRTUAL
 *  DISTINTA. Un puntero valido en el padre seria basura en el hijo. Por eso
 *  aqui el nucleo de calculo indexa siempre de forma plana (base + i*n) y
 *  calcula los punteros de fila en local, dentro de cada proceso.
 * ========================================================================= */
typedef struct {
    int    *base;        /* inicio del bloque: [A][B][C]          */
    size_t  celdas;      /* 3 * n * n                             */
    size_t  bytes;       /* celdas * sizeof(int)                  */
#if defined(_WIN32)
    HANDLE  mapeo;       /* handle del objeto de mapeo            */
    char    nombre[64];  /* nombre con el que lo abren los hijos  */
#endif
} Region;

/* Punteros a cada matriz dentro del bloque compartido. */
#define MAT_A(r, n) ((r)->base)
#define MAT_B(r, n) ((r)->base + (size_t)(n) * (size_t)(n))
#define MAT_C(r, n) ((r)->base + 2u * (size_t)(n) * (size_t)(n))

/* ---------------------------------------------------------------------------
 *  Calculo protegido del tamano a reservar.
 *  Se comprueba que 3*n*n*sizeof(int) no desborde size_t: si desbordara se
 *  reservaria menos memoria de la pedida y se escribiria fuera del bloque
 *  (bug clasico de seguridad).
 * ------------------------------------------------------------------------- */
static int calcular_bytes(int n, size_t *celdas, size_t *bytes)
{
    size_t orden      = (size_t)n;
    size_t max_celdas = SIZE_MAX / sizeof(int);

    if (orden > max_celdas / orden) {
        fprintf(stderr, "Error: n=%d es demasiado grande, n*n*sizeof(int) "
                        "desborda size_t.\n", n);
        return 0;
    }
    size_t una = orden * orden;
    if (una > max_celdas / 3u) {
        fprintf(stderr, "Error: n=%d es demasiado grande para las tres "
                        "matrices juntas.\n", n);
        return 0;
    }
    *celdas = 3u * una;
    *bytes  = *celdas * sizeof(int);
    return 1;
}

/* ---------------------------------------------------------------------------
 *  RESERVA DE LA REGION COMPARTIDA (la hace el proceso PADRE)
 * ------------------------------------------------------------------------- */
static int region_crear(Region *r, int n)
{
    if (n <= 0) {
        fprintf(stderr, "Error: el orden de la matriz debe ser >= 1.\n");
        return 0;
    }
    if (!calcular_bytes(n, &r->celdas, &r->bytes)) return 0;

#if defined(_WIN32)
    /* Nombre unico para esta ejecucion: el pid basta, porque no puede haber
     * dos procesos vivos con el mismo pid. El prefijo Local\ restringe el
     * nombre a la sesion del usuario. */
    snprintf(r->nombre, sizeof r->nombre, "Local\\mmPROCESOS_%lu",
             (unsigned long)GetCurrentProcessId());

    /* CreateFileMapping con INVALID_HANDLE_VALUE = bloque respaldado por el
     * fichero de paginacion, es decir memoria compartida pura. El tamano se
     * pasa partido en dos mitades de 32 bits. */
    DWORD alto = (DWORD)(((unsigned long long)r->bytes >> 32) & 0xFFFFFFFFu);
    DWORD bajo = (DWORD)((unsigned long long)r->bytes & 0xFFFFFFFFu);

    r->mapeo = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
                                  alto, bajo, r->nombre);
    if (r->mapeo == NULL) {
        fprintf(stderr, "Error: no se pudo crear la memoria compartida de "
                        "%.2f MiB (codigo %lu).\n",
                (double)r->bytes / (1024.0 * 1024.0),
                (unsigned long)GetLastError());
        return 0;
    }

    r->base = (int *)MapViewOfFile(r->mapeo, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    if (r->base == NULL) {
        fprintf(stderr, "Error: no se pudo mapear la memoria compartida "
                        "(codigo %lu).\n", (unsigned long)GetLastError());
        CloseHandle(r->mapeo);
        r->mapeo = NULL;
        return 0;
    }
#else
    /* MAP_SHARED    -> las escrituras son visibles por los procesos que
     *                  hereden el mapeo (los hijos de fork()).
     * MAP_ANONYMOUS -> sin fichero de respaldo, memoria pura. */
    void *p = mmap(NULL, r->bytes, PROT_READ | PROT_WRITE,
                   MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) {
        fprintf(stderr, "Error: no se pudo reservar memoria compartida de "
                        "%.2f MiB.\n", (double)r->bytes / (1024.0 * 1024.0));
        return 0;
    }
    r->base = (int *)p;
#endif
    return 1;
}

#if defined(_WIN32)
/* ---------------------------------------------------------------------------
 *  APERTURA DE LA REGION YA EXISTENTE (la hace cada proceso HIJO en Windows)
 *  El hijo no crea nada: abre por nombre el bloque que creo el padre.
 * ------------------------------------------------------------------------- */
static int region_abrir(Region *r, const char *nombre, int n)
{
    if (!calcular_bytes(n, &r->celdas, &r->bytes)) return 0;

    snprintf(r->nombre, sizeof r->nombre, "%s", nombre);

    r->mapeo = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, nombre);
    if (r->mapeo == NULL) {
        fprintf(stderr, "Error (trabajador): no se pudo abrir la memoria "
                        "compartida %s (codigo %lu).\n",
                nombre, (unsigned long)GetLastError());
        return 0;
    }
    r->base = (int *)MapViewOfFile(r->mapeo, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    if (r->base == NULL) {
        fprintf(stderr, "Error (trabajador): no se pudo mapear la memoria "
                        "compartida (codigo %lu).\n",
                (unsigned long)GetLastError());
        CloseHandle(r->mapeo);
        r->mapeo = NULL;
        return 0;
    }
    return 1;
}
#endif

/* ---------------------------------------------------------------------------
 *  LIBERACION DE LA REGION. Orden inverso al de reserva.
 * ------------------------------------------------------------------------- */
static void region_liberar(Region *r)
{
    if (r == NULL || r->base == NULL) return;
#if defined(_WIN32)
    UnmapViewOfFile(r->base);
    if (r->mapeo != NULL) CloseHandle(r->mapeo);
    r->mapeo = NULL;
#else
    munmap(r->base, r->bytes);
#endif
    r->base = NULL;
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
 *  LLENADO ALEATORIO (identico a matmul.c y mmHILOS.c)
 *  Enteros POSITIVOS en [1, limite], recorriendo el bloque de forma lineal.
 *  Lo hace SIEMPRE el padre y ANTES de crear los hijos, para que todos vean
 *  los mismos datos de entrada.
 * ------------------------------------------------------------------------- */
static void llenar_aleatoria(int *datos, int n, int limite)
{
    size_t total = (size_t)n * (size_t)n;
    for (size_t i = 0; i < total; i++) {
        datos[i] = 1 + (rand() % limite);
    }
}

/* ===========================================================================
 *  NUCLEO DE CALCULO
 *
 *  Calcula el bloque de filas [inicio, fin) de C. Es el mismo bucle i-k-j de
 *  las otras dos versiones: el bucle interno recorre B y C por filas, o sea de
 *  forma contigua en memoria, lo que minimiza los fallos de cache y permite
 *  que el compilador vectorice.
 *
 *  Cada proceso pone a cero UNICAMENTE sus propias filas de C, asi que no hace
 *  falta un memset global previo ni ninguna sincronizacion.
 * ========================================================================= */
static void calcular_bloque(const int *A, const int *B, int *C, int n,
                            int inicio, int fin)
{
    for (int i = inicio; i < fin; i++) {
        int *fila_c = C + (size_t)i * (size_t)n;

        /* La fila i de C se pone a cero porque se acumula sobre ella. */
        memset(fila_c, 0, (size_t)n * sizeof(int));

        const int *fila_a = A + (size_t)i * (size_t)n;
        for (int k = 0; k < n; k++) {
            int a_ik = fila_a[k];                    /* invariante del bucle */
            const int *fila_b = B + (size_t)k * (size_t)n;
            for (int j = 0; j < n; j++) {
                fila_c[j] += a_ik * fila_b[j];
            }
        }
    }
}

/* ---------------------------------------------------------------------------
 *  REPARTO DE FILAS ENTRE PROCESOS (identico al de mmHILOS.c)
 *
 *  base = n / P filas a cada proceso y UNA fila extra a los primeros n % P,
 *  de modo que la diferencia de carga entre procesos sea de una sola fila
 *  como maximo. Devuelve el rango [inicio, fin) que le toca al proceso p.
 * ------------------------------------------------------------------------- */
static void rango_de(int n, int P, int p, int *inicio, int *fin)
{
    int cuantas = n / P;
    int residuo = n % P;

    /* Los primeros 'residuo' procesos llevan una fila extra. */
    if (p < residuo) {
        *inicio = p * (cuantas + 1);
        *fin    = *inicio + cuantas + 1;
    } else {
        *inicio = residuo * (cuantas + 1) + (p - residuo) * cuantas;
        *fin    = *inicio + cuantas;
    }
}

/* ===========================================================================
 *  MULTIPLICACION PARALELA CON PROCESOS
 * ========================================================================= */

#if defined(_WIN32)
/* ---------------------------------------------------------------------------
 *  VERSION WINDOWS: CreateProcess
 *
 *  El padre se relanza a si mismo P veces en modo trabajador. Cada hijo recibe
 *  por linea de comandos el nombre del bloque compartido, el orden n y su
 *  rango de filas.
 * ------------------------------------------------------------------------- */
static int multiplicar_procesos(Region *r, int n, int P)
{
    /* Ruta completa del propio ejecutable, para poder relanzarlo. */
    char exe[MAX_PATH];
    if (GetModuleFileNameA(NULL, exe, (DWORD)sizeof exe) == 0) {
        fprintf(stderr, "Error: no se pudo obtener la ruta del ejecutable "
                        "(codigo %lu).\n", (unsigned long)GetLastError());
        return 1;
    }

    HANDLE *hijos = (HANDLE *)malloc((size_t)P * sizeof(HANDLE));
    if (hijos == NULL) {
        fprintf(stderr, "Error: no hay memoria para %d procesos.\n", P);
        return 1;
    }

    int creados = 0;
    int fallo   = 0;

    for (int p = 0; p < P; p++) {
        int inicio, fin;
        rango_de(n, P, p, &inicio, &fin);

        /* La linea de comandos del hijo. El ejecutable va entre comillas
         * porque la ruta puede contener espacios. */
        char linea[MAX_PATH + 128];
        snprintf(linea, sizeof linea, "\"%s\" --trabajador %s %d %d %d",
                 exe, r->nombre, n, inicio, fin);

        STARTUPINFOA si;
        PROCESS_INFORMATION pi;
        ZeroMemory(&si, sizeof si);
        si.cb = sizeof si;
        ZeroMemory(&pi, sizeof pi);

        if (!CreateProcessA(NULL, linea, NULL, NULL, FALSE, 0,
                            NULL, NULL, &si, &pi)) {
            fprintf(stderr, "Error: no se pudo crear el proceso %d de %d "
                            "(codigo %lu).\n",
                    p, P, (unsigned long)GetLastError());
            fallo = 1;
            break;      /* se espera a los ya creados y se completa abajo */
        }

        /* El handle del hilo principal del hijo no se usa: se cierra ya. */
        CloseHandle(pi.hThread);
        hijos[creados++] = pi.hProcess;
    }

    /* Se espera a todos los hijos que si se crearon y se revisa su salida. */
    for (int h = 0; h < creados; h++) {
        WaitForSingleObject(hijos[h], INFINITE);

        DWORD codigo = 0;
        if (GetExitCodeProcess(hijos[h], &codigo) && codigo != 0) {
            fprintf(stderr, "Error: el proceso hijo %d termino con codigo "
                            "%lu.\n", h, (unsigned long)codigo);
            fallo = 1;
        }
        CloseHandle(hijos[h]);
    }

    /* Si fallo la creacion a mitad de camino, el padre completa las filas que
     * quedaron sin asignar, para no devolver un resultado parcial. */
    if (fallo && creados < P) {
        int inicio, fin;
        rango_de(n, P, creados, &inicio, &fin);
        /* Desde el primer bloque no asignado hasta el final de la matriz. */
        calcular_bloque(MAT_A(r, n), MAT_B(r, n), MAT_C(r, n), n, inicio, n);
    }

    free(hijos);
    return fallo;
}

#else
/* ---------------------------------------------------------------------------
 *  VERSION POSIX: fork
 *
 *  fork() duplica el proceso. En el hijo devuelve 0, en el padre devuelve el
 *  pid del hijo. El hijo calcula su bloque de filas sobre la memoria
 *  compartida (heredada del padre) y termina con _exit(), que NO vacia los
 *  buffers de stdio: asi no se duplica la salida que el padre tenga pendiente.
 * ------------------------------------------------------------------------- */
static int multiplicar_procesos(Region *r, int n, int P)
{
    pid_t *hijos = (pid_t *)malloc((size_t)P * sizeof(pid_t));
    if (hijos == NULL) {
        fprintf(stderr, "Error: no hay memoria para %d procesos.\n", P);
        return 1;
    }

    int creados = 0;
    int fallo   = 0;

    for (int p = 0; p < P; p++) {
        int inicio, fin;
        rango_de(n, P, p, &inicio, &fin);

        fflush(NULL);           /* nada pendiente antes de duplicar */
        pid_t pid = fork();

        if (pid < 0) {
            fprintf(stderr, "Error: fork fallo en el proceso %d de %d: %s\n",
                    p, P, strerror(errno));
            fallo = 1;
            break;
        }
        if (pid == 0) {
            /* ---- Codigo del HIJO ---- */
            calcular_bloque(MAT_A(r, n), MAT_B(r, n), MAT_C(r, n),
                            n, inicio, fin);
            _exit(EXIT_SUCCESS);
        }
        /* ---- Codigo del PADRE ---- */
        hijos[creados++] = pid;
    }

    /* El padre espera a cada hijo y revisa como termino. */
    for (int h = 0; h < creados; h++) {
        int estado = 0;
        if (waitpid(hijos[h], &estado, 0) < 0) {
            fprintf(stderr, "Error: waitpid fallo para el hijo %d.\n", h);
            fallo = 1;
            continue;
        }
        if (!WIFEXITED(estado) || WEXITSTATUS(estado) != 0) {
            fprintf(stderr, "Error: el proceso hijo %d no termino "
                            "correctamente.\n", h);
            fallo = 1;
        }
    }

    /* Si fallo algun fork, el padre completa las filas sin asignar. */
    if (fallo && creados < P) {
        int inicio, fin;
        rango_de(n, P, creados, &inicio, &fin);
        /* Desde el primer bloque no asignado hasta el final de la matriz. */
        calcular_bloque(MAT_A(r, n), MAT_B(r, n), MAT_C(r, n), n, inicio, n);
    }

    free(hijos);
    return fallo;
}
#endif

/* ---------------------------------------------------------------------------
 *  Formatea un double con coma decimal (identico a matmul.c y mmHILOS.c).
 *  Se reemplaza el punto a mano para no depender del locale del sistema.
 * ------------------------------------------------------------------------- */
static void formato_coma(char *destino, size_t tam, double valor, int decimales)
{
    snprintf(destino, tam, "%.*f", decimales, valor);
    for (char *p = destino; *p != '\0'; p++) {
        if (*p == '.') *p = ',';
    }
}

/* ---------------------------------------------------------------------------
 *  IMPRESION (solo util con -p y n pequeno)
 * ------------------------------------------------------------------------- */
static void imprimir_matriz(const int *datos, int n, const char *nombre)
{
    printf("\nMatriz %s (%dx%d):\n", nombre, n, n);
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            printf("%10d", datos[(size_t)i * (size_t)n + j]);
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
    "Uso: %s -n <tamano> -l <limite> [-np <procesos>] [-s <semilla>] [-p] [-c]\n"
    "     %s <tamano> <limite> [semilla] [-np <procesos>] [-p] [-c]\n"
    "\n"
    "Version PARALELA con PROCESOS de la multiplicacion de matrices.\n"
    "Usa memoria compartida para que los hijos escriban en la misma C.\n"
    "\n"
    "Opciones:\n"
    "  -n <tamano>    Tamano de las matrices (obligatorio). Dos formas validas:\n"
    "                   5     un solo numero\n"
    "                   5x5   filas por columnas (deben coincidir, son cuadradas)\n"
    "  -l <limite>    Valor maximo de cada celda; enteros en [1, limite]\n"
    "                 (obligatorio). Debe cumplir  N * limite^2 <= %d.\n"
    "  -np <procesos> Numero de procesos de calculo. Por defecto = nucleos\n"
    "                 logicos del sistema (aqui: %d). Se acota a [1, N].\n"
    "                 Se acepta -t como sinonimo, por simetria con mmHILOS.\n"
    "  -s <semilla>   Semilla del generador aleatorio. Por defecto usa el reloj.\n"
    "  -p             Imprime las matrices A, B y C (usar solo con N pequeno).\n"
    "  -c             Salida en una sola linea separada por ';' (para Excel):\n"
    "                   Orden;NumProcesos;Tiempo(s);Rendimiento(GOP/s)\n"
    "  -h             Muestra esta ayuda.\n"
    "\n"
    "Ejemplos:\n"
    "  %s -n 1000 -l 100 -np 8\n"
    "  %s -n 5x5 -l 9 -s 42 -p\n"
    "  %s 512x512 50 7 -np 4\n",
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

#if defined(_WIN32)
/* ===========================================================================
 *  MODO TRABAJADOR (solo Windows)
 *
 *  Se activa cuando el programa se invoca como:
 *      mmPROCESOS --trabajador <nombre_memoria> <n> <inicio> <fin>
 *
 *  No imprime nada, no reserva matrices y no vuelve al flujo normal: abre la
 *  memoria compartida que creo el padre, calcula su bloque de filas y sale.
 *  Es el equivalente al "codigo del hijo" que en POSIX va justo despues del
 *  fork().
 * ========================================================================= */
static int modo_trabajador(int argc, char *argv[])
{
    if (argc < 6) {
        fprintf(stderr, "Error (trabajador): faltan argumentos internos.\n");
        return EXIT_FAILURE;
    }

    const char *nombre = argv[2];
    long n = 0, inicio = 0, fin = 0;

    if (!leer_entero(argv[3], &n) ||
        !leer_entero(argv[4], &inicio) ||
        !leer_entero(argv[5], &fin)) {
        fprintf(stderr, "Error (trabajador): argumentos internos invalidos.\n");
        return EXIT_FAILURE;
    }
    if (n <= 0 || inicio < 0 || fin > n || inicio > fin) {
        fprintf(stderr, "Error (trabajador): rango de filas invalido "
                        "(%ld, %ld] con n=%ld.\n", inicio, fin, n);
        return EXIT_FAILURE;
    }

    Region r;
    memset(&r, 0, sizeof r);
    if (!region_abrir(&r, nombre, (int)n)) return EXIT_FAILURE;

    calcular_bloque(MAT_A(&r, n), MAT_B(&r, n), MAT_C(&r, n),
                    (int)n, (int)inicio, (int)fin);

    region_liberar(&r);
    return EXIT_SUCCESS;
}
#endif

/* ===========================================================================
 *  PROGRAMA PRINCIPAL
 * ========================================================================= */
int main(int argc, char *argv[])
{
    long n        = -1;     /* orden de la matriz     */
    long limite   = -1;     /* valor maximo de celda  */
    long semilla  = -1;     /* semilla del generador  */
    long procesos = -1;     /* numero de procesos     */
    int  imprimir = 0;      /* bandera -p             */
    int  modo_csv = 0;      /* bandera -c             */
    long valor = 0;

    const char *prog = nombre_programa(argv[0]);

#if defined(_WIN32)
    /* Antes que nada: si es un hijo relanzado, hace su trabajo y sale. */
    if (argc >= 2 && strcmp(argv[1], "--trabajador") == 0) {
        return modo_trabajador(argc, argv);
    }
#endif

    /* ----- 1. Lectura de parametros de la linea de comandos ----- */
    if (argc == 1) {
        mostrar_uso(prog);
        return EXIT_FAILURE;
    }

    if (argv[1][0] != '-') {
        /* Forma posicional: mmPROCESOS <tamano> <limite> [semilla] [-np P] [-p] [-c] */
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
        /* El resto: semilla, -np <P> y/o las banderas -p y -c, en cualquier orden. */
        for (int i = 3; i < argc; i++) {
            if (strcmp(argv[i], "-p") == 0) { imprimir = 1; continue; }
            if (strcmp(argv[i], "-c") == 0) { modo_csv = 1; continue; }
            if (strcmp(argv[i], "-np") == 0 || strcmp(argv[i], "-t") == 0) {
                if (i + 1 >= argc || !leer_entero(argv[i + 1], &procesos)) {
                    fprintf(stderr, "Error: la opcion %s requiere un numero de "
                                    "procesos valido.\n", argv[i]);
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
        /* Forma con banderas: mmPROCESOS -n N -l L [-np P] [-s S] [-p] [-c] */
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

            if (strcmp(op, "-l")  == 0 || strcmp(op, "-s") == 0 ||
                strcmp(op, "-np") == 0 || strcmp(op, "-t") == 0) {
                if (i + 1 >= argc) {
                    fprintf(stderr, "Error: la opcion %s requiere un valor.\n", op);
                    return EXIT_FAILURE;
                }
                if (!leer_entero(argv[i + 1], &valor)) {
                    fprintf(stderr, "Error: el valor de %s ('%s') no es un entero "
                                    "valido.\n", op, argv[i + 1]);
                    return EXIT_FAILURE;
                }
                if      (strcmp(op, "-l") == 0) limite   = valor;
                else if (strcmp(op, "-s") == 0) semilla  = valor;
                else                            procesos = valor;
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

    /* Numero de procesos: por defecto = nucleos logicos; se acota a [1, n],
     * porque no tiene sentido crear mas procesos que filas para repartir. */
    if (procesos <= 0) procesos = nucleos_disponibles();
    if (procesos < 1)  procesos = 1;
    if (procesos > n)  procesos = n;

    /* Semilla: si no se indico, se toma del reloj del sistema. */
    if (semilla < 0) semilla = (long)time(NULL);
    srand((unsigned int)semilla);

    /* ----- 3. Reserva de la memoria COMPARTIDA con las tres matrices ----- */
    Region r;
    memset(&r, 0, sizeof r);
    if (!region_crear(&r, (int)n)) {
        return EXIT_FAILURE;
    }

    int *A = MAT_A(&r, n);
    int *B = MAT_B(&r, n);
    int *C = MAT_C(&r, n);

    /* ----- 4. Llenado aleatorio de A y B (lo hace el padre, antes de crear
     *          los hijos, para que todos vean los mismos datos) ----- */
    llenar_aleatoria(A, (int)n, (int)limite);
    llenar_aleatoria(B, (int)n, (int)limite);

    /* ----- 5. Multiplicacion con PROCESOS, cronometrada ----- */
    double t_inicio = reloj_segundos();
    int rc = multiplicar_procesos(&r, (int)n, (int)procesos);
    double t_fin = reloj_segundos();
    double segundos = t_fin - t_inicio;

    if (rc != 0) {
        fprintf(stderr, "Error: la multiplicacion con procesos fallo.\n");
        region_liberar(&r);
        return EXIT_FAILURE;
    }

    /* ----- 6. Reporte ----- */
    double operaciones = 2.0 * (double)n * (double)n * (double)n;
    double gops = operaciones / (segundos > 0.0 ? segundos : 1e-9) / 1e9;

    if (modo_csv) {
        /* Linea unica separada por punto y coma, lista para pegar en Excel:
         *   Orden ; NumProcesos ; Tiempo(s) ; Rendimiento(GOP/s)           */
        char t_txt[32], g_txt[32];
        formato_coma(t_txt, sizeof t_txt, segundos, 6);
        formato_coma(g_txt, sizeof g_txt, gops, 3);
        printf("%ld;%ld;%s;%s\n", n, procesos, t_txt, g_txt);
    } else {
        double mib = (double)((size_t)n * (size_t)n * sizeof(int))
                     / (1024.0 * 1024.0);

        printf("=================================================\n");
        printf("  Multiplicacion de matrices (PROCESOS)  C = A x B\n");
        printf("=================================================\n");
        printf("  Orden de las matrices : %ld x %ld\n", n, n);
        printf("  Rango de las celdas   : [1, %ld]\n", limite);
        printf("  Limite maximo seguro  : %d\n", lmax);
        printf("  Semilla aleatoria     : %ld\n", semilla);
        printf("  Numero de procesos    : %ld  (nucleos: %d)\n",
               procesos, nucleos_disponibles());
        printf("  Memoria compartida    : %.2f MiB (3 matrices de %.2f MiB)\n",
               (double)r.bytes / (1024.0 * 1024.0), mib);
        printf("  Operaciones enteras   : %.3e\n", operaciones);
        printf("  Tiempo de calculo     : %.6f s\n", segundos);
        printf("  Rendimiento           : %.3f GOP/s\n", gops);
        printf("=================================================\n");

        if (imprimir) {
            imprimir_matriz(A, (int)n, "A");
            imprimir_matriz(B, (int)n, "B");
            imprimir_matriz(C, (int)n, "C = A x B");
        }
    }

    /* ----- 7. Liberacion de la memoria compartida ----- */
    region_liberar(&r);

    return EXIT_SUCCESS;
}
