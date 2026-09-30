# Proyecto HPC 2026-2 — Multiplicación de Matrices

Implementación en C del algoritmo clásico de multiplicación de matrices cuadradas
(`C = A × B`), en tres versiones que comparten el mismo núcleo de cálculo y son
por tanto comparables 1:1:

| Versión | Fuente | Paralelismo |
|---|---|---|
| Secuencial (línea base) | [`src/matmul.c`](src/matmul.c) | — |
| Hilos | [`src/mmHILOS.c`](src/mmHILOS.c) | POSIX threads (`pthreads`) |
| Procesos | [`src/mmPROCESOS.c`](src/mmPROCESOS.c) | `fork()` / `CreateProcess` + memoria compartida |

Las secciones 4 a 7 describen la línea base secuencial; la **sección 8** cubre la
versión con procesos, incluidas las mediciones de *speedup* de las tres.

---

## 1. Características

| Requisito | Implementación |
|---|---|
| Matrices cuadradas | Orden `N × N`, mismo número de filas y columnas. El tamaño se indica como `5` o como `5x5` |
| Tipo de dato | `int` (entero simple con signo, 32 bits) — **no** `long`, **no** `double` |
| Valores | Enteros **positivos** aleatorios en el rango cerrado `[1, L]` |
| Control de desbordamiento | Se valida que `N · L² ≤ INT_MAX` antes de reservar memoria |
| Memoria | **Reserva dinámica** con `malloc` / `free` (ver sección 4) |
| Ejecución | **Paramétrica** por línea de comandos: el programa nunca pide datos por teclado |
| Medición | Cronómetro de reloj de pared (*wall clock*) sobre el núcleo de cálculo |
| Paralelismo | Por filas de `C`, que son independientes: sin secciones críticas ni mutex |

---

## 2. Estructura del repositorio

```
Proyecto_HPC_2026-2/
├── src/
│   ├── matmul.c                    # Versión SECUENCIAL (línea base)
│   ├── mmHILOS.c                   # Versión PARALELA con hilos (pthreads)
│   └── mmPROCESOS.c                # Versión PARALELA con procesos
├── docs/
│   └── conversacion-desarrollo.md  # Bitácora: cómo se construyó el proyecto
├── bin/                            # Ejecutables generados (ignorado por git)
├── commands.txt                    # Comandos de compilación y de los barridos
├── resultados_secuencial.csv       # Mediciones de matmul
├── resultados_hilos.csv            # Mediciones de mmHILOS
├── resultados_procesos.csv         # Mediciones de mmPROCESOS
├── Makefile                        # Compilación y targets de ejecución
├── .gitignore
└── README.md
```

Las tres versiones comparten la misma estructura de datos, el mismo llenado
aleatorio y **exactamente el mismo núcleo de cálculo** (bucles `i-k-j`), así que
con la misma semilla producen matrices idénticas y el *speedup* medido es
limpio. Los comandos exactos de compilación y de los barridos de medición están
en [`commands.txt`](commands.txt).

La carpeta [`docs/`](docs/) contiene el transcript de la sesión de desarrollo:
cada decisión de diseño, los comandos ejecutados y sus resultados reales. Sirve
como bitácora del proyecto y como evidencia del proceso de construcción.

---

## 3. Compilación y ejecución

### 3.1 Requisitos

Hacen falta **gcc** y **GNU Make**. En Windows se obtienen instalando
[MSYS2](https://www.msys2.org/); el compilador queda en `C:\msys64\ucrt64\bin`,
ruta que debe estar en el `PATH` del sistema.

> ⚠️ En Windows el ejecutable de GNU Make se llama **`mingw32-make`**, no `make`.

Comprobación previa (sirve en cualquier consola):

```
gcc --version
mingw32-make --version
```

### 3.2 Compilar y ejecutar con el Makefile

Abre la consola, sitúate en la raíz del proyecto y lanza el target que quieras:

```powershell
cd C:\Repositorios\Proyecto_HPC_2026-2

mingw32-make                        # compila -> bin\matmul.exe
mingw32-make demo                   # ejecución 5x5 imprimiendo las tres matrices
mingw32-make run N=1024 L=100 S=42  # ejecución parametrizada
mingw32-make bench                  # barrido de tamaños, salida CSV
mingw32-make info                   # diagnóstico: qué consola detectó
mingw32-make help                   # lista de targets
mingw32-make clean                  # borra bin/
```

Los parámetros de `run` se sobrescriben desde la propia línea de comandos: `N` es
el orden de la matriz, `L` el límite de valor de celda y `S` la semilla.

En **Linux, WSL o macOS** son los mismos targets, con `make` en vez de
`mingw32-make`.

#### Por qué el Makefile detecta la consola

GNU Make en Windows busca `sh.exe` en el `PATH`. Desde **Git Bash** lo encuentra
y ejecuta las recetas con un shell Unix; desde **PowerShell o CMD** no lo
encuentra y las ejecuta con **`cmd.exe`**, donde `mkdir -p` o `rm -rf` no
existen. Por eso el Makefile comprueba si `SHELL` trae una ruta completa y elige
los comandos apropiados en cada caso. Con `mingw32-make info` se ve qué detectó:

```
Consola detectada : cmd.exe          <- PowerShell / CMD
Consola detectada : .../sh.exe       <- Git Bash / MSYS2
```

### 3.3 Ejecutar el binario directamente

Una vez compilado, el ejecutable queda en `bin\matmul.exe` y se invoca de forma
distinta según la consola:

| Consola | Sintaxis |
|---|---|
| PowerShell | `.\bin\matmul.exe -n 512 -l 100` |
| CMD | `bin\matmul.exe -n 512 -l 100` |
| Git Bash / MSYS2 | `./bin/matmul -n 512 -l 100` |
| Linux / WSL / macOS | `./bin/matmul -n 512 -l 100` |

En PowerShell el prefijo `.\` es **obligatorio**: por seguridad no ejecuta
binarios del directorio actual sin él.

### 3.4 Compilación manual, sin Makefile

```
gcc -O2 -Wall -Wextra -Wpedantic -std=c11 -o bin/matmul src/matmul.c -lm
```

| Bandera | Para qué sirve |
|---|---|
| `-O2` | Optimización del compilador; imprescindible para medir rendimiento real |
| `-Wall -Wextra -Wpedantic` | Máximo nivel de advertencias (el código compila sin ninguna) |
| `-std=c11` | Estándar del lenguaje |
| `-lm` | Enlaza la librería matemática, requerida por `sqrt()` |

Si la carpeta `bin` no existe hay que crearla antes: `mkdir bin` en PowerShell,
`mkdir -p bin` en Git Bash.

### 3.5 Parámetros de línea de comandos

```
Uso: matmul -n <tamaño> -l <limite> [-s <semilla>] [-p] [-c]
     matmul <tamaño> <limite> [semilla] [-p] [-c]
```

| Opción | Descripción |
|---|---|
| `-n <tamaño>` | Tamaño de las matrices. Admite **dos formas equivalentes**: `5` (un solo número) o `5x5` (filas por columnas). **Obligatorio** |
| `-l <limite>` | Valor máximo de celda; genera enteros en `[1, límite]`. **Obligatorio** |
| `-s <semilla>` | Semilla del generador aleatorio. Por defecto usa el reloj del sistema. Fijarla hace la ejecución **reproducible** |
| `-p` | Imprime las matrices A, B y C (solo para `N` pequeño) |
| `-c` | Salida en una sola línea CSV: `n,limite,semilla,segundos` |
| `-h` | Muestra la ayuda |

Se aceptan también los parámetros en forma posicional: `matmul 512x512 50 7`.

#### Indicar el tamaño como «filas por columnas»

El tamaño se puede escribir de las dos maneras, según lo que resulte más
natural:

```powershell
.\bin\matmul.exe -n 5     -l 9    # un solo número
.\bin\matmul.exe -n 5x5   -l 9    # filas por columnas, equivalente al anterior
.\bin\matmul.exe -n 5X5   -l 9    # la X mayúscula también vale
```

Reglas de esta notación:

- **Se escribe sin espacios**: `5x5`, no `5 x 5`. Con espacios la consola lo
  parte en tres argumentos distintos y el programa no puede reconocerlo.
- **Ambos números deben coincidir**, porque las matrices del proyecto son
  cuadradas. Si no coinciden, el programa se detiene y lo explica en vez de
  quedarse callado con uno de los dos valores:

```
$ .\bin\matmul.exe -n 3x5 -l 9
Error: las matrices deben ser cuadradas, pero se pidio 3x5.
       El numero de filas y el de columnas deben coincidir,
       por ejemplo 3x3.
```

### 3.6 Ejemplos

En PowerShell:

```powershell
.\bin\matmul.exe -n 5x5 -l 9 -s 42 -p   # demostración visible
.\bin\matmul.exe -n 1024 -l 100         # medición de rendimiento
.\bin\matmul.exe -n 2000 -l 50 -c       # una línea CSV, ideal para scripts
.\bin\matmul.exe 256x256 50 7           # forma posicional
.\bin\matmul.exe 5x5 9 42 -p            # posicional con banderas al final
.\bin\matmul.exe -h                     # ayuda
```

En Git Bash, Linux o WSL es lo mismo con `./bin/matmul`.

Para guardar un barrido de tiempos en un archivo:

```powershell
mingw32-make bench | Out-File -Encoding utf8 resultados.csv   # PowerShell
mingw32-make bench > resultados.csv                           # Git Bash / Linux
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

## 8. Versión paralela con procesos (`mmPROCESOS.c`)

[`src/mmPROCESOS.c`](src/mmPROCESOS.c) es la contraparte con **procesos** de la
versión con hilos. Comparte con [`matmul.c`](src/matmul.c) y
[`mmHILOS.c`](src/mmHILOS.c) la misma estructura de datos, el mismo llenado
aleatorio y **exactamente el mismo núcleo de cálculo** (bucles `i-k-j`), así que
con la misma semilla las tres producen matrices idénticas y la comparación es
1:1.

### 8.1 El problema de fondo: los procesos no comparten memoria

Esta es la única diferencia conceptual real frente a los hilos, y es la que
obliga a reescribir la mecánica del programa:

| | Hilos | Procesos |
|---|---|---|
| Espacio de direcciones | **Compartido** | **Uno por proceso** |
| Pasar `A`, `B`, `C` | Basta un puntero | Hay que pedir memoria compartida al S.O. |
| Riesgo de carrera | Sí, si se escriben las mismas celdas | Igual, dentro de la zona compartida |
| Coste de crear uno | Bajo (~microsegundos) | Alto (milisegundos) |

Si un proceso hijo escribiera en «su» `C`, el padre no vería nada: al terminar
el hijo esa memoria se destruye. Por eso las tres matrices viven en un **único
bloque de memoria compartida** pedido explícitamente al sistema operativo:

```
[ A: n*n enteros ][ B: n*n enteros ][ C: n*n enteros ]
  offset 0          offset n*n        offset 2*n*n
```

**Un detalle fino que hay que respetar:** dentro del bloque compartido *no* se
puede guardar el vector de punteros a filas (el campo `fila` que sí usan
`matmul.c` y `mmHILOS.c`), porque cada proceso puede mapear el bloque en una
**dirección virtual distinta**; un puntero válido en el padre sería basura en el
hijo. Por eso el núcleo de cálculo de esta versión indexa siempre de forma plana
(`base + i*n`) y cada proceso deriva sus punteros de fila en local.

### 8.2 Dos implementaciones, según el sistema operativo

**POSIX (Linux, WSL, macOS): `fork()` + `mmap(MAP_SHARED)`**

`fork()` duplica el proceso actual; el hijo continúa en la misma línea de código
(devuelve `0` en el hijo y el PID del hijo en el padre). Como el bloque se
reserva con `MAP_SHARED | MAP_ANONYMOUS` **antes** del `fork`, padre e hijos
comparten esas páginas físicas y el padre espera con `waitpid()`.

```c
pid_t pid = fork();
if (pid == 0) {                      /* HIJO  */
    calcular_bloque(A, B, C, n, inicio, fin);
    _exit(EXIT_SUCCESS);             /* _exit: no vacía los buffers de stdio */
}
hijos[creados++] = pid;              /* PADRE */
```

**Windows: `CreateProcess()` + `CreateFileMapping()`**

Windows **no tiene `fork()`**: su API nativa solo sabe crear un proceso nuevo
ejecutando un programa (el equivalente a `fork` + `exec` de una sola vez). El
esquema lógico es el mismo, pero el hijo arranca desde cero y recibe su trabajo
por argumentos en vez de heredarlo:

1. El padre crea un bloque compartido **con nombre** (`CreateFileMapping` sobre
   `INVALID_HANDLE_VALUE`, es decir respaldado por memoria y no por un fichero).
   El nombre incluye su PID, así que es único: `Local\mmPROCESOS_<pid>`.
2. El padre se relanza a sí mismo `P` veces, pasando el nombre del bloque y el
   rango de filas:
   `mmPROCESOS --trabajador <nombre> <n> <inicio> <fin>`
3. Cada hijo abre ese bloque por su nombre (`OpenFileMapping` +
   `MapViewOfFile`), calcula sus filas y termina.
4. El padre espera con `WaitForSingleObject` y comprueba el código de salida de
   cada hijo.

La opción `--trabajador` es interna: se detecta al principio de `main`, antes de
cualquier otro parseo, y esa rama no imprime nada ni reserva matrices.

> El compilador usado en este proyecto es el de MSYS2/UCRT64, que **no ofrece
> `fork()`**. Por eso en Windows la ruta real es la de `CreateProcess`, y es la
> única que está **verificada por ejecución**. La rama con `fork()` está escrita
> para compilar en Linux/WSL sin cambiar nada más, pero **no se ha podido probar
> en esta máquina** (no hay ningún toolchain Linux disponible aquí); conviene
> compilarla y ejecutarla antes de darla por buena.

### 8.3 Reparto del trabajo

Idéntico al de la versión con hilos, porque cada fila de `C` es independiente de
las demás: se reparten las `n` filas en bloques contiguos, `n / P` a cada
proceso y **una fila extra** a los primeros `n % P`, de modo que la diferencia
de carga entre procesos sea de una sola fila como máximo.

`A` y `B` son de solo lectura y cada proceso escribe filas **disjuntas** de `C`:
no hay condiciones de carrera, y por tanto **no se necesita ningún semáforo ni
mutex**. La única sincronización es esperar a que todos los hijos terminen.

Si la creación de un proceso falla a mitad de camino, el padre calcula él mismo
las filas que quedaron sin asignar, para no devolver un resultado parcial.

### 8.4 Compilar y ejecutar

No necesita `-pthread` ni ninguna librería extra:

```powershell
gcc -O2 -Wall -Wextra -std=c11 -o bin/mmPROCESOS src/mmPROCESOS.c -lm
```

```powershell
.\bin\mmPROCESOS.exe -n 1500 -l 100 -np 8          # 8 procesos
.\bin\mmPROCESOS.exe -n 5x5 -l 9 -s 42 -p          # imprime A, B y C
.\bin\mmPROCESOS.exe 512x512 50 7 -np 4            # forma posicional
.\bin\mmPROCESOS.exe -n 1000 -l 100 -np 8 -c       # una línea para Excel
```

Acepta los mismos parámetros que las otras dos versiones (incluida la notación
`5x5`), más uno propio:

| Opción | Significado |
|---|---|
| `-np <procesos>` | Número de procesos de cálculo. Por defecto, los núcleos lógicos del sistema; se acota a `[1, N]`. Se acepta `-t` como sinónimo, por simetría con `mmHILOS`. |

Con `-c` la línea sale como `Orden;NumProcesos;Tiempo(s);Rendimiento(GOP/s)`,
con coma decimal, lista para pegar en Excel. Los comandos de los barridos
completos están en [`commands.txt`](commands.txt).

### 8.5 Resultados medidos

Medido en una máquina de **12 núcleos lógicos**, con `-l 100`. El tiempo medido
**incluye deliberadamente la creación de los procesos**: ese coste es justamente
la diferencia real frente a los hilos.

Medianas de [`resultados_procesos.csv`](resultados_procesos.csv) (10 turnos por
combinación, 120 mediciones):

| Procesos | n = 500 | n = 1000 | n = 1500 |
|---:|---:|---:|---:|
| 2 | 0,0309 s | 0,1653 s | 0,5221 s |
| 4 | 0,0305 s | 0,1071 s | 0,3047 s |
| 8 | 0,0387 s | 0,0957 s | 0,2465 s |
| 16 | 0,0690 s | 0,1052 s | 0,2550 s |

Y la comparación directa contra las otras dos versiones, midiendo las tres
**intercaladas en una sola sesión** (mediana de 5 repeticiones), que es la forma
en que el *speedup* resulta creíble:

| n | Secuencial | P | Hilos | *Speedup* | Procesos | *Speedup* | Diferencia |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 500 | 0,0321 s | 2 | 0,0171 s | 1,88× | 0,0280 s | 1,15× | +10,9 ms |
| | | 4 | 0,0147 s | 2,18× | 0,0279 s | 1,15× | +13,2 ms |
| | | 8 | 0,0094 s | 3,42× | 0,0387 s | **0,83×** | +29,3 ms |
| | | 16 | 0,0099 s | 3,26× | 0,0640 s | **0,50×** | +54,2 ms |
| 1000 | 0,2541 s | 2 | 0,1399 s | 1,82× | 0,1500 s | 1,69× | +10,2 ms |
| | | 4 | 0,0922 s | 2,76× | 0,1010 s | 2,52× | +8,8 ms |
| | | 8 | 0,0659 s | 3,86× | 0,0911 s | 2,79× | +25,2 ms |
| | | 16 | 0,0691 s | 3,68× | 0,1018 s | 2,50× | +32,8 ms |
| 1500 | 0,9404 s | 2 | 0,4871 s | 1,93× | 0,4883 s | 1,93× | +1,3 ms |
| | | 4 | 0,2826 s | 3,33× | 0,2957 s | 3,18× | +13,1 ms |
| | | 8 | 0,2058 s | 4,57× | 0,2361 s | 3,98× | +30,4 ms |
| | | 16 | 0,2150 s | 4,37× | 0,2337 s | 4,02× | +18,7 ms |

### 8.6 Lectura de los resultados

**1. Los hilos siempre ganan, y la diferencia es un coste casi fijo.** La
columna «Diferencia» no crece con `n`, solo con el número de procesos: son del
orden de **1 a 4 ms por proceso creado**. Es el precio de `CreateProcess`, que
tiene que construir un espacio de direcciones nuevo, cargar el ejecutable y sus
DLL, y arrancar un `main` desde cero. Crear un hilo, en cambio, solo cuesta una
pila nueva.

**2. Con problemas pequeños, los procesos son contraproducentes.** En `n = 500`
con 8 y 16 procesos el *speedup* cae **por debajo de 1**: el programa paralelo
es más lento que el secuencial. Los 54 ms que cuesta crear 16 procesos son más
que los 32 ms que tarda en multiplicar la matriz entera un solo núcleo. Es el
caso de libro en que la sobrecarga de paralelizar supera el trabajo útil.

**3. Con problemas grandes, la diferencia se amortiza.** En `n = 1500` el
cálculo dura casi un segundo en secuencial, así que unas decenas de milisegundos
de arranque son ruido: los procesos alcanzan 4,02× frente a 4,37× de los hilos,
un 8 % de diferencia. La regla práctica es que **los procesos solo compiten
cuando el trabajo por proceso es mucho mayor que el coste de crearlo**.

**4. El *speedup* satura en torno a 4–4,5× con 12 núcleos, en las dos
versiones.** El techo no lo pone el modelo de paralelismo sino el **ancho de
banda de memoria**: el núcleo `i-k-j` está vectorizado y hace muy pocas
operaciones por byte leído, así que a partir de 8 hilos/procesos los núcleos
esperan a la RAM. Pasar de 8 a 16 no mejora nada (con 12 núcleos físicos, 16 ya
es sobresuscripción) e incluso empeora un poco.

**5. Nota sobre las tablas.** Los ficheros
[`resultados_secuencial.csv`](resultados_secuencial.csv) y
[`resultados_hilos.csv`](resultados_hilos.csv) se midieron en sesiones
anteriores, con otra carga de máquina; por eso la comparación de la segunda
tabla se rehízo midiendo las tres versiones seguidas. Para un informe formal
conviene regenerar los tres barridos del tirón, con los comandos de
[`commands.txt`](commands.txt).

---
## 9. Próximos pasos del proyecto

- [x] Versión paralela con hilos POSIX (`src/mmHILOS.c`)
- [x] Versión paralela con procesos (`src/mmPROCESOS.c`)
- [x] Medición de *speedup* y eficiencia frente a esta línea base
- [ ] Versión paralela con OpenMP (`#pragma omp parallel for` sobre el bucle `i`)
- [ ] Versión por bloques (*tiling*) para mejorar el uso de la caché L2/L3
- [ ] Versión distribuida con MPI
