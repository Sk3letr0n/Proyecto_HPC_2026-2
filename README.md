# Proyecto HPC 2026-2 — Multiplicación de Matrices

Implementación en C del algoritmo clásico de multiplicación de matrices cuadradas
(`C = A × B`), diseñada como **línea base secuencial** para después compararla
contra versiones paralelas (OpenMP, MPI, CUDA).

---

## 1. Características

| Requisito | Implementación |
|---|---|
| Matrices cuadradas | Orden `N × N`, mismo número de filas y columnas |
| Tipo de dato | `int` (entero simple con signo, 32 bits) — **no** `long`, **no** `double` |
| Valores | Enteros **positivos** aleatorios en el rango cerrado `[1, L]` |
| Control de desbordamiento | Se valida que `N · L² ≤ INT_MAX` antes de reservar memoria |
| Memoria | **Reserva dinámica** con `malloc` / `free` (ver sección 4) |
| Ejecución | **Paramétrica** por línea de comandos: el programa nunca pide datos por teclado |
| Medición | Cronómetro de reloj de pared (*wall clock*) sobre el núcleo de cálculo |

---

## 2. Estructura del repositorio

```
Proyecto_HPC_2026-2/
├── src/
│   └── matmul.c                    # Código fuente completo, comentado
├── docs/
│   └── conversacion-desarrollo.md  # Bitácora: cómo se construyó el proyecto
├── bin/                            # Ejecutable generado (ignorado por git)
├── Makefile                        # Compilación y targets de ejecución
├── .gitignore
└── README.md
```

La carpeta [`docs/`](docs/) contiene el transcript de la sesión de desarrollo:
cada decisión de diseño, los comandos ejecutados y sus resultados reales. Sirve
como bitácora del proyecto y como evidencia del proceso de construcción.

---

## 3. Compilación y ejecución

### 3.1 Con Makefile

En **Windows (MSYS2 / MinGW)** el binario de GNU Make se llama `mingw32-make`:

```bash
mingw32-make            # compila -> bin/matmul.exe
mingw32-make demo       # ejecución 5x5 imprimiendo las tres matrices
mingw32-make run N=1024 L=100 S=42
mingw32-make bench      # barrido de tamaños, salida CSV
mingw32-make debug      # compila con -O0 -g3 para depurar con gdb
mingw32-make asan       # compila con AddressSanitizer (requiere libasan)
mingw32-make clean
```

En **Linux / WSL / macOS** el mismo Makefile funciona con `make`.

### 3.2 Compilación manual

```bash
gcc -O2 -Wall -Wextra -Wpedantic -std=c11 -o bin/matmul src/matmul.c -lm
```

| Bandera | Para qué sirve |
|---|---|
| `-O2` | Optimización del compilador; imprescindible para medir rendimiento real |
| `-Wall -Wextra -Wpedantic` | Máximo nivel de advertencias (el código compila sin ninguna) |
| `-std=c11` | Estándar del lenguaje |
| `-lm` | Enlaza la librería matemática, requerida por `sqrt()` |

### 3.3 Parámetros de línea de comandos

```
Uso: matmul -n <orden> -l <limite> [-s <semilla>] [-p] [-c]
     matmul <orden> <limite> [semilla]
```

| Opción | Descripción |
|---|---|
| `-n <orden>` | Orden `N` de las matrices `N × N`. **Obligatorio** |
| `-l <limite>` | Valor máximo de celda; genera enteros en `[1, límite]`. **Obligatorio** |
| `-s <semilla>` | Semilla del generador aleatorio. Por defecto usa el reloj del sistema. Fijarla hace la ejecución **reproducible** |
| `-p` | Imprime las matrices A, B y C (solo para `N` pequeño) |
| `-c` | Salida en una sola línea CSV: `n,limite,semilla,segundos` |
| `-h` | Muestra la ayuda |

Se aceptan también los parámetros en forma posicional: `matmul 512 50 7`.

### 3.4 Ejemplos

```bash
./bin/matmul -n 5 -l 9 -s 42 -p     # demostración visible
./bin/matmul -n 1024 -l 100         # medición de rendimiento
./bin/matmul -n 2000 -l 50 -c       # una línea CSV, ideal para scripts
./bin/matmul 256 50 7               # forma posicional
```

Salida típica:

```
=============================================
  Multiplicacion de matrices   C = A x B
=============================================
  Orden de las matrices : 256 x 256
  Rango de las celdas   : [1, 50]
  Limite maximo seguro  : 2896
  Semilla aleatoria     : 7
  Memoria por matriz    : 0.25 MiB (x3 = 0.75 MiB)
  Operaciones enteras   : 3.355e+07
  Tiempo de calculo     : 0.006123 s
  Rendimiento           : 5.480 GOP/s
=============================================
```

---

## 4. Reserva dinámica de memoria (explicación detallada)

Este es el punto central del proyecto. El código **no** usa arreglos estáticos
(`int m[100][100]`) porque el tamaño se decide en tiempo de ejecución, a partir
de un parámetro de la línea de comandos.

### 4.1 El problema de las dos estrategias clásicas

**Estrategia A — arreglo de punteros (la más enseñada):**

```c
int **m = malloc(n * sizeof(int *));
for (int i = 0; i < n; i++)
    m[i] = malloc(n * sizeof(int));   // n mallocs independientes
```

Funciona, pero tiene un problema serio para HPC: cada fila queda en un lugar
**distinto y disperso** del heap. Al recorrer la matriz el procesador salta por
toda la memoria, se producen fallos de caché constantes y el rendimiento cae.
Además implica `n + 1` llamadas a `malloc` y otras tantas a `free`.

**Estrategia B — un solo bloque plano:**

```c
int *m = malloc(n * n * sizeof(int));
// acceso:  m[i * n + j]
```

Los datos quedan contiguos (perfecto para la caché), pero se pierde la sintaxis
natural `m[i][j]` y hay que escribir la aritmética de índices a mano en todas
partes.

### 4.2 La estrategia usada aquí: híbrida

Se combinan las dos ventajas. La estructura es:

```c
typedef struct {
    int   n;       // orden de la matriz
    int  *datos;   // UN bloque contiguo de n*n enteros
    int **fila;    // n punteros: fila[i] -> &datos[i*n]
} Matriz;
```

El proceso de reserva, paso a paso, en `matriz_crear()`:

**Paso 1 — Verificar que el tamaño no desborde `size_t`.**

```c
size_t orden      = (size_t)n;
size_t max_celdas = SIZE_MAX / sizeof(int);
if (orden > max_celdas / orden) { /* error */ }
```

Si se escribiera directamente `malloc(n * n * sizeof(int))` con un `n` enorme,
la multiplicación podría *dar la vuelta* al contador y pedir, por ejemplo, 100
bytes en lugar de 16 GB. `malloc` tendría éxito, el programa escribiría fuera
del bloque y se corrompería la memoria. Es un fallo de seguridad clásico, por
eso la división se hace **antes** de multiplicar.

**Paso 2 — Reservar el descriptor.**

```c
Matriz *m = malloc(sizeof(Matriz));
```

**Paso 3 — Reservar el bloque de datos: una sola llamada para toda la matriz.**

```c
m->datos = malloc(orden * orden * sizeof(int));
```

Para `n = 1000` esto es un único bloque de 4 MB perfectamente contiguo.

**Paso 4 — Reservar el índice de filas.**

```c
m->fila = malloc(orden * sizeof(int *));
```

**Paso 5 — "Cablear" cada puntero de fila dentro del bloque.**

```c
for (int i = 0; i < n; i++)
    m->fila[i] = m->datos + (size_t)i * orden;
```

Aquí no se reserva nada nuevo: solo se calculan direcciones **dentro** del
bloque ya reservado.

```
datos:  [ fila 0 ][ fila 1 ][ fila 2 ] ... [ fila n-1 ]   <- un solo malloc
           ^         ^         ^              ^
fila:   [  ·    ,    ·    ,    ·    , ... ,   ·  ]        <- otro malloc
```

Resultado: se escribe `m->fila[i][j]` con toda naturalidad, pero los datos están
contiguos en memoria.

### 4.3 Manejo de errores y liberación

Cada `malloc` se comprueba contra `NULL`. Si uno falla a mitad del proceso, se
liberan los anteriores antes de retornar, para no dejar fugas de memoria:

```c
m->fila = malloc(orden * sizeof(int *));
if (m->fila == NULL) {
    free(m->datos);   // se deshace lo ya reservado
    free(m);
    return NULL;
}
```

La liberación se hace en **orden inverso** al de reserva:

```c
free(m->fila);    // (2) índice de filas
free(m->datos);   // (1) bloque de datos
free(m);          // (0) descriptor
```

Cada `malloc` tiene exactamente un `free`. Con tres matrices (A, B y C) son
9 reservas y 9 liberaciones.

Para comprobar que no hay fugas ni accesos fuera de rango se usa el target
`asan`, que compila con AddressSanitizer y UndefinedBehaviorSanitizer:

```bash
mingw32-make asan && ./bin/matmul_asan -n 200 -l 50 -s 1
```

> **Nota:** este target necesita `libasan` / `libubsan`. Están disponibles en
> Linux y WSL; en MSYS2 puede ser necesario instalarlas con
> `pacman -S mingw-w64-ucrt-x86_64-gcc-libs`. Si no están presentes, el enlazado
> falla con `cannot find -lasan`; en ese caso se puede usar `mingw32-make debug`,
> que compila con `-O0 -g3` para depurar con `gdb` y funciona siempre.

---

## 5. Control de desbordamiento

Cada celda del resultado es la suma de `N` productos:

```
C[i][j] = A[i][0]·B[0][j] + A[i][1]·B[1][j] + ... + A[i][N-1]·B[N-1][j]
```

En el **peor caso** todas las celdas de A y B valen `L`, de modo que:

```
valor máximo de C[i][j] = N · L²
```

Como las celdas son `int` (32 bits con signo), el valor debe caber en
`INT_MAX = 2 147 483 647`. La condición es:

```
N · L² ≤ INT_MAX        =>        L ≤ √(INT_MAX / N)
```

La función `limite_seguro(n)` calcula ese `L` máximo usando aritmética de
64 bits (`long long`), para que la propia verificación no desborde. Si el
usuario pide un límite mayor, el programa **se detiene con un mensaje claro** en
lugar de producir resultados silenciosamente incorrectos (en C el desbordamiento
de enteros con signo es *comportamiento indefinido*, no simplemente un número
negativo):

```
$ ./bin/matmul -n 1000 -l 50000
Error: el limite 50000 provoca desbordamiento de int.
       Peor caso por celda: n * limite^2 = 2500000000000 > INT_MAX (2147483647).
       Para n=1000 el limite maximo seguro es 1465.
```

Límites máximos seguros para algunos tamaños:

| N | L máximo seguro |
|---|---|
| 100 | 4 634 |
| 256 | 2 896 |
| 512 | 2 047 |
| 1 000 | 1 465 |
| 2 000 | 1 036 |
| 4 096 | 724 |

En la práctica conviene usar límites bastante menores (por ejemplo `L = 100`),
porque el peor caso teórico es muy improbable con valores aleatorios y así queda
un margen amplio.

---

## 6. Decisiones de rendimiento

**Orden de bucles `i-k-j` en lugar del clásico `i-j-k`.** Con el orden
tradicional, el bucle interno recorre la matriz `B` **por columnas**, saltando
`N × 4` bytes en cada iteración, lo que desperdicia cada línea de caché que se
carga. Con el orden `i-k-j`:

```c
for (i...)
  for (k...) {
    int a_ik = A->fila[i][k];      // constante en el bucle interno
    for (j...)
      fila_c[j] += a_ik * fila_b[j];   // B y C recorridas por filas
  }
```

tanto `B` como `C` se recorren linealmente, aprovechando la contigüidad que
garantiza la reserva de memoria descrita en la sección 4. Además, `a_ik` sale
del bucle interno y el compilador puede vectorizar la operación con
instrucciones SIMD.

**Complejidad:** `O(N³)` — son `2N³` operaciones enteras (`N³` multiplicaciones
y `N³` sumas). Medición real de este código (`mingw32-make bench`, gcc 13.1
con `-O2`):

| N | Tiempo (s) | Factor |
|---|---|---|
| 64 | 0.000085 | — |
| 128 | 0.000886 | ×10.4 |
| 256 | 0.006270 | ×7.1 |
| 512 | 0.046012 | ×7.3 |
| 1024 | 0.425144 | ×9.2 |

El factor cercano a ×8 al duplicar `N` confirma empíricamente el comportamiento
cúbico (`2³ = 8`).

---

## 7. Verificación de correctitud

Con `N` pequeño se puede comprobar el resultado a mano:

```bash
./bin/matmul -n 5 -l 9 -s 42 -p
```

Por ejemplo, con la primera fila de A = `[5 5 5 6 1]` y la primera columna de
B = `[3 6 8 2 1]`:

```
C[0][0] = 5·3 + 5·6 + 5·8 + 6·2 + 1·1 = 15 + 30 + 40 + 12 + 1 = 98  ✓
```

La opción `-s` fija la semilla, de modo que la misma ejecución siempre produce
las mismas matrices y el resultado es reproducible.

---

## 8. Próximos pasos del proyecto

- [ ] Versión paralela con OpenMP (`#pragma omp parallel for` sobre el bucle `i`)
- [ ] Medición de *speedup* y eficiencia frente a esta línea base
- [ ] Versión por bloques (*tiling*) para mejorar el uso de la caché L2/L3
- [ ] Versión distribuida con MPI
