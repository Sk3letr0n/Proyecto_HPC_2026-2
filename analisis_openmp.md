# Análisis de directivas OpenMP aplicadas a multiplicación de matrices

Proyecto HPC 2026-2 · Caso de estudio: `src/matmul.c` (bucle triple anidado i-k-j)

---

## ¿Qué es OpenMP en una frase?

OpenMP es una forma de decirle al compilador **"este pedazo de código se puede repartir
entre varios núcleos"** usando comentarios especiales llamados **pragmas** (empiezan con
`#pragma omp`). Si compilas SIN la bandera `-fopenmp`, esos comentarios se ignoran y el
programa corre normal, secuencial.

---

## Los 6 ejemplos del libro, explicados de forma sencilla

| # | Ejemplo | Directiva clave | Versión OpenMP |
|---|---------|-----------------|----------------|
| 1 | compound_names.1.c | `parallel for`, `parallel for simd`, `parallel masked taskloop`, `target teams distribute parallel for` | 6.0 |
| 2 | directive_name_modifier.1.c | `if(directiva: condicion)` | 6.0 |
| 3 | ploop.1.c | `parallel for` | pre-3.0 |
| 4 | nthrs_dynamic.1.c | `parallel num_threads(N)` | pre-3.0 |
| 5 | collapse.1.c | `for collapse(2)` | 3.0 |
| 6 | tasking.5.c | `parallel` + `single` + `task` | 3.0 |

---

### 1. `parallel for` (screenshots 1 y 3)

**Analogía:** Imagina que tienes que pintar 1000 filas de una pared. En vez de pintarlas
tú solo, llamas a 7 amigos y cada uno pinta ~125 filas. Nadie se estorba porque cada
quien trabaja en filas diferentes.

**Qué hace:** Crea un grupo de hilos (trabajadores) y le da a cada uno un pedazo del
bucle `for`. En matmul, cada hilo calcula filas completas de la matriz C.

```c
#pragma omp parallel for
for (int i = 0; i < n; i++) {  // cada hilo toma un grupo de filas
    // ... calcula toda la fila i de C ...
}
```

| ✅ Ventajas | ❌ Desventajas |
|-------------|----------------|
| Solo 1 línea de código extra | Para matrices muy chicas (<100) es más lento que hacerlo solo, porque "llamar a los amigos" toma tiempo |
| No hay peleas: cada hilo trabaja en filas diferentes | No le dice al procesador que vectorice (pero con `-O2` lo hace solo) |
| El más rápido para nuestro caso | — |

**Veredicto: ⭐ GANADOR.** Simple, correcto y rápido.

---

### 2. `if(condicion)` — paralelización condicional (screenshot 2)

**Analogía:** Si solo tienes que pintar 5 filas, no vale la pena llamar a 7 amigos (el
tiempo de llamarlos es mayor que lo que tardarías solo). El `if` dice: "solo llama a
tus amigos si hay suficiente trabajo".

**Qué hace:** Le pone una condición al `parallel for`. Si la condición es falsa, corre
secuencial (sin hilos). Si es verdadera, paraleliza normalmente.

```c
#pragma omp parallel for if(n > 64)   // solo paraleliza si la matriz es > 64
for (int i = 0; i < n; i++) { ... }
```

| ✅ Ventajas | ❌ Desventajas |
|-------------|----------------|
| Evita perder tiempo en problemas pequeños | No hace que el programa sea más rápido, solo evita que sea más lento |
| Se agrega con 10 caracteres extra | Hay que saber cuál es el tamaño mínimo que conviene (depende de la máquina) |

**Rol:** Es un "complemento" del ganador. No es una alternativa, se le agrega encima.

---

### 3. `parallel for simd` (screenshot 1, segundo bloque)

**Analogía:** Tus amigos ya se repartieron las filas. Ahora además, cada uno usa una
brocha extra ancha que pinta 4 celdas a la vez en lugar de 1. Eso es SIMD: una sola
instrucción del procesador opera sobre varios números al mismo tiempo (SSE = 4 enteros,
AVX = 8 enteros).

**Qué hace:** Combina dos cosas: repartir filas entre hilos (`parallel for`) y además
forzar que el bucle más interno use instrucciones vectoriales (`simd`).

```c
#pragma omp parallel for
for (int i = 0; i < n; i++) {
    for (int k = 0; k < n; k++) {
        #pragma omp simd          // "usa la brocha ancha en este bucle"
        for (int j = 0; j < n; j++) {
            C[i][j] += A[i][k] * B[k][j];
        }
    }
}
```

| ✅ Ventajas | ❌ Desventajas |
|-------------|----------------|
| En nuestras pruebas fue el más rápido en tiempo absoluto (~1.7× vs. parallel for solo) | Con enteros de 32 bits y `-O2`, el compilador muchas veces ya vectoriza solo sin pedírselo |
| Fuerza al compilador a vectorizar aunque no quiera | Para que funcione al 100%, la memoria debe estar alineada (detalle técnico avanzado) |

**Rol:** Mejora real si el compilador no vectorizó solo. Es "bonus", no indispensable.

---

### 4. `num_threads(N)` y `omp_set_dynamic` (screenshot 4)

**Analogía:** En vez de dejar que vengan los amigos que quieran, tú decides exactamente
cuántos llamar: "quiero EXACTAMENTE 4 amigos, ni más ni menos". Y además le dices al
coordinador que no cambie esa cantidad por su cuenta.

**Qué hace:** Fija el número exacto de hilos y desactiva el ajuste automático del
runtime de OpenMP.

```c
omp_set_dynamic(0);                               // "no cambies lo que te pido"
#pragma omp parallel for num_threads(4)            // "usa exactamente 4 hilos"
for (int i = 0; i < n; i++) { ... }
```

| ✅ Ventajas | ❌ Desventajas |
|-------------|----------------|
| Resultados reproducibles para comparar 2, 4, 8, 16 hilos | No hace el programa más rápido en sí |
| Esencial para benchmarks serios | Si pides demasiados hilos para poco trabajo, es contraproducente |

**Rol:** Herramienta de control para mediciones. Se integra en el ganador con `-t`
o con la variable `OMP_NUM_THREADS`.

---

### 5. `collapse(2)` — aplanar bucles anidados (screenshot 5)

**Analogía:** En vez de repartir 1000 filas entre 8 amigos, juntas los dos primeros
bucles para tener 1000×1000 = 1 millón de "mini-trabajos" y los repartes. Así hay más
trabajo para dar y nadie se queda sin hacer nada.

**El problema con nuestra matmul:** En el orden i-k-j, si dos hilos tienen el mismo `i`
pero distinto `k`, los dos escriben en la misma fila de C al mismo tiempo → **se pisan
los datos** (condición de carrera).

Para que `collapse(2)` funcione hay que usar el orden clásico i-j-k, pero ese orden es
mucho más lento porque recorre la matriz B "saltando" en memoria (por columnas).

```c
/* ⚠ Esto FALLA con i-k-j (dos hilos escriben la misma celda) */
#pragma omp parallel for collapse(2)
for (int i = 0; i < n; i++) {
    for (int k = 0; k < n; k++) {  // hilos con mismo i pelean por C[i]
        ...
    }
}

/* ✅ Esto FUNCIONA con i-j-k, pero i-j-k es más lento */
#pragma omp parallel for collapse(2)
for (int i = 0; i < n; i++) {
    for (int j = 0; j < n; j++) {  // cada par (i,j) es único → sin peleas
        ...
    }
}
```

| ✅ Ventajas | ❌ Desventajas |
|-------------|----------------|
| Más trabajo para repartir → útil con pocas filas y muchos hilos | **Con i-k-j (nuestro caso) causa errores** |
| Una sola línea de código | Obliga a usar i-j-k que es ~2× más lento por fallos de caché |

**Veredicto: ❌ Descartado.** O da resultados incorrectos (i-k-j), o es lento (i-j-k).

---

### 6. `task` — paralelismo por tareas (screenshot 6)

**Analogía:** En vez de repartir las filas de antemano, un amigo (el "jefe") va poniendo
tareas en una pizarra: "pintar fila 1", "pintar fila 2"... Los demás las van tomando
conforme terminan la anterior. Es más flexible pero el jefe pierde tiempo escribiendo.

**Qué hace:** Un hilo genera tareas y los demás las ejecutan. Es ideal para trabajos
irregulares (como recorrer un árbol), pero para un bucle regular como matmul es
excesivo.

```c
#pragma omp parallel
{
    #pragma omp single              // solo 1 hilo genera las tareas
    {
        for (int i = 0; i < n; i++) {
            #pragma omp task         // cada fila es una "tarea"
            { /* calcular fila i de C */ }
        }
    }
}
```

| ✅ Ventajas | ❌ Desventajas |
|-------------|----------------|
| Muy flexible para problemas irregulares (árboles, grafos) | Matmul es regular → no necesita esa flexibilidad |
| — | Más lento que `parallel for` (overhead de crear/encolar tareas) |
| — | Mucho más código (10+ líneas vs. 1 línea) |

**Veredicto: ❌ Descartado.** Es como usar un dron para entregar una carta al vecino:
funciona, pero sobra.

---

## Tabla comparativa final

| Directiva | ¿Funciona con i-k-j? | Rendimiento | Complejidad | Recomendación |
|-----------|----------------------|-------------|-------------|---------------|
| `parallel for` | ✅ sí | ⭐⭐⭐⭐⭐ | 1 línea | **GANADOR** |
| `if(n > umbral)` | ✅ complemento | — (evita degradación) | +10 caracteres | Agregar al ganador |
| `parallel for` + `simd` | ✅ sí | ⭐⭐⭐⭐ (marginal con int) | 1 línea extra | Opcional/bonus |
| `num_threads(N)` | ✅ complemento | — (control) | 1 línea | Agregar al ganador |
| `collapse(2)` | ❌ CAUSA ERRORES | ⭐⭐⭐ (solo con i-j-k lento) | 1 línea | Descartado |
| `task` | ✅ sí | ⭐⭐ (overhead) | 10+ líneas | Descartado |

---

## Resultados experimentales (matmul NxN, 8 hilos, -O2)

Archivos de prueba: `tests_openmp/test1..6_*.c` (uno por directiva).
Todos usan semilla fija (42), misma máquina, mismo compilador.

### N = 1000 (8 hilos)

| # | Enfoque | Tiempo (s) | GOP/s | vs. parallel_for |
|---|---------|-----------|-------|------------------|
| 1 | `parallel for` | 0.055 | 36.1 | 1.00× (base) |
| 2 | `parallel for if` | 0.058 | 34.2 | 0.95× |
| 3 | `parallel for` + `simd` | **0.032** | **62.5** | **1.73×** |
| 4 | `num_threads` | 0.058 | 34.8 | 0.96× |
| 5 | `collapse(2)` (i-j-k) | 0.115 | 17.4 | 0.48× |
| 6 | `task` | 0.055 | 36.3 | 1.01× |

### N = 1500 (8 hilos)

| # | Enfoque | Tiempo (s) | GOP/s | vs. parallel_for |
|---|---------|-----------|-------|------------------|
| 1 | `parallel for` | 0.192 | 35.2 | 1.00× |
| 2 | `parallel for if` | 0.184 | 36.6 | 1.04× |
| 3 | `parallel for` + `simd` | **0.100** | **67.2** | **1.91×** |
| 4 | `num_threads` | 0.189 | 35.7 | 1.01× |
| 5 | `collapse(2)` (i-j-k) | 0.349 | 19.3 | 0.55× |
| 6 | `task` | 0.176 | 38.3 | 1.09× |

### N = 50 (8 hilos — matriz pequeña)

| # | Enfoque | Tiempo (s) | GOP/s | Observación |
|---|---------|-----------|-------|-------------|
| 1 | `parallel for` | 0.000050 | 5.0 | Overhead de hilos visible |
| 2 | `parallel for if` | 0.000059 | 4.3 | Corre secuencial (n ≤ 64) |
| 3 | `parallel for` + `simd` | 0.000048 | 5.2 | Mejor absoluto |
| 4 | `num_threads` | 0.000684 | 0.4 | **Peor**: overhead de forzar 8 hilos |
| 5 | `collapse(2)` | 0.000056 | 4.5 | Aceptable (poco trabajo) |
| 6 | `task` | 0.000101 | 2.5 | Overhead de encolar 50 tareas |

### Conclusiones de los datos

1. **`parallel for` + `simd`** da el mejor rendimiento absoluto (~1.7–1.9×), porque
   fuerza vectorización del bucle interno j.
2. **`parallel for`** sin `simd` es la opción más sencilla y consistente.
3. **`collapse(2)`** es el peor (~0.5×) porque obliga a usar i-j-k (peor caché).
4. **`task`** rinde similar pero con más código.
5. **`num_threads`** e **`if`** no afectan el rendimiento, son de control.

**Ganador para mmOPENMP.c: `parallel for schedule(runtime)`** — rendimiento + simplicidad +
flexibilidad (la política de reparto se controla con `OMP_SCHEDULE` sin recompilar).

---

## Variables de entorno útiles (de la OpenMP 6.0 Reference Card)

Fuente: `OpenMPRefCard-6-r7-2026-web.pdf`, sección "Environment Variables" (pág. 18).

De las ~25 variables de entorno que lista la Reference Card, estas son las que sirven
para nuestro caso de multiplicación de matrices. Las demás son para GPUs (`OMP_TARGET_OFFLOAD`),
herramientas de depuración (`OMP_TOOL`), o características que no usamos.

### Variables que usamos en mmOPENMP.c

| Variable | Valores | Para qué sirve | Analogía |
|----------|---------|-----------------|----------|
| `OMP_NUM_THREADS` | un número: `8` | Cuántos hilos (amigos) usar | "Quiero 8 personas pintando" |
| `OMP_SCHEDULE` | `static`, `dynamic`, `guided`, `auto` + tamaño de bloque | Cómo se reparten las filas entre los hilos | "¿Cada quien agarra 125 filas de golpe, o van tomando de 10 en 10?" |
| `OMP_PROC_BIND` | `close`, `spread`, `true`, `false` | Si los hilos se quedan pegados a un núcleo fijo | "Cada amigo se queda en su zona de la pared sin moverse" |
| `OMP_PLACES` | `threads`, `cores`, `sockets` | Dónde pueden correr los hilos (hilos lógicos, núcleos físicos, o sockets) | "¿Pueden trabajar en cualquier cuarto, o solo en el suyo?" |
| `OMP_DYNAMIC` | `true` o `false` | Si OpenMP puede reducir hilos por su cuenta | "¿El coordinador puede mandar amigos a casa si cree que sobran?" |
| `OMP_WAIT_POLICY` | `active` o `passive` | Qué hacen los hilos cuando no tienen trabajo | `active` = dan vueltas esperando (más rápido); `passive` = se duermen (ahorra energía) |
| `OMP_DISPLAY_ENV` | `true`, `false` o `verbose` | Mostrar al inicio TODOS los valores de OMP_* activos | Para verificar que las variables se aplicaron bien |
| `OMP_STACKSIZE` | `64K`, `1M`, `8M`... | Cuánta memoria de pila tiene cada hilo | Normalmente no hay que tocarla, pero si el programa truena con "stack overflow" hay que subirla |

### Explicación detallada de las 3 más importantes (aparte de NUM_THREADS)

#### `OMP_SCHEDULE` — cómo se reparten las iteraciones

Imagina que hay 1000 filas y 4 hilos:

| Valor | Qué hace | Ejemplo con 1000 filas y 4 hilos |
|-------|----------|----------------------------------|
| `static` | Cada hilo toma un bloque fijo del mismo tamaño | Hilo 0: filas 0-249, Hilo 1: filas 250-499, etc. |
| `static,50` | Bloques fijos de 50 filas, se van alternando | Hilo 0: 0-49, Hilo 1: 50-99, Hilo 0: 200-249... |
| `dynamic` | Los hilos van "agarrando" una fila a la vez conforme terminan | El que acaba primero toma la siguiente disponible |
| `dynamic,50` | Van agarrando de 50 en 50 | Menos overhead que de 1 en 1 |
| `guided` | Empiezan con bloques grandes y van achicando | Primero bloques de 250, luego 125, luego 62... |

**Para matmul:** `static` es el mejor porque todas las filas tardan lo mismo (trabajo
regular). `dynamic` y `guided` sirven cuando unas iteraciones tardan más que otras.

#### `OMP_PROC_BIND` — fijar hilos a núcleos

| Valor | Qué hace | Cuándo conviene |
|-------|----------|-----------------|
| `close` | Pone los hilos en núcleos vecinos (0,1,2,3...) | Cuando los hilos comparten datos (buena caché L2/L3) |
| `spread` | Los esparce lo más posible (0,4,8,12...) | Cuando cada hilo necesita su propio ancho de banda de memoria |
| `false` | El sistema operativo decide libremente | Cuando no importa o no sabes |

**Para matmul:** `close` suele ser mejor porque los hilos leen de las mismas matrices
A y B, y si están en núcleos vecinos comparten caché.

#### `OMP_WAIT_POLICY` — qué hacen los hilos cuando no tienen trabajo

| Valor | Qué hace | Cuándo conviene |
|-------|----------|-----------------|
| `active` | El hilo "da vueltas" preguntando "¿ya hay trabajo?" (busy-wait) | Benchmarks: la reacción es instantánea pero gasta CPU |
| `passive` | El hilo se duerme y el SO lo despierta cuando hay trabajo | Producción: ahorra energía, pero tarda un poco en despertar |

**Para matmul benchmarks:** `active` es mejor para medir el tiempo más bajo posible.

---

## Cómo usar las variables de entorno (PowerShell)

```powershell
# Ejemplo: 8 hilos, reparto dinámico, hilos pegados a núcleos cercanos,
# espera activa, y mostrar la configuración al arrancar

$env:OMP_NUM_THREADS = 8
$env:OMP_SCHEDULE = "static"
$env:OMP_PROC_BIND = "close"
$env:OMP_PLACES = "cores"
$env:OMP_WAIT_POLICY = "active"
$env:OMP_DISPLAY_ENV = "true"

.\bin\mmOPENMP.exe -n 1000 -l 100
```

No hace falta recompilar. Las variables se leen cuando el programa arranca.

La opción `-t` del ejecutable tiene precedencia sobre `OMP_NUM_THREADS`.

---

### Compilación y ejecución

```powershell
# Compilar mmOPENMP (version final con parallel for)
gcc -O2 -fopenmp -Wall -Wextra -std=c11 -o bin/mmOPENMP src/mmOPENMP.c -lm

# Compilar los 6 tests de comparacion
foreach ($t in 'test1_parallel_for','test2_parallel_for_if','test3_parallel_for_simd','test4_num_threads','test5_collapse','test6_task') { gcc -O2 -fopenmp -Wall -Wextra -std=c11 -I tests_openmp -o "bin/$t" "tests_openmp/$t.c" -lm }

# Ejecutar comparacion rapida (N=1000, 8 hilos)
foreach ($t in @('test1_parallel_for','test2_parallel_for_if','test3_parallel_for_simd','test4_num_threads','test5_collapse','test6_task')) { & ".\bin\$t.exe" 1000 8 }

# Ejecutar mmOPENMP con 4 hilos (opción -t)
.\bin\mmOPENMP.exe -n 1000 -l 100 -t 4

# Ejecutar con variables de entorno
$env:OMP_NUM_THREADS = 8; .\bin\mmOPENMP.exe -n 1000 -l 100

# Probar distintas politicas de reparto SIN recompilar
$env:OMP_SCHEDULE = "dynamic,64"; .\bin\mmOPENMP.exe -n 1000 -l 100

# Ver TODA la configuracion OpenMP al arrancar
$env:OMP_DISPLAY_ENV = "true"; .\bin\mmOPENMP.exe -n 1000 -l 100

# Barrido completo: 10 turnos x 8 tamaños x 4 cantidades de hilos
"Orden;NumHilos;Tiempo(s);Rendimiento(GOP/s)" | Out-File resultados_openmp.csv -Encoding utf8
1..10 | ForEach-Object {
  foreach ($n in 250,500,750,1000,1250,1500,1750,2000) {
    foreach ($t in 2,4,8,16) {
      .\bin\mmOPENMP.exe -n $n -l 100 -t $t -c |
        Out-File resultados_openmp.csv -Encoding utf8 -Append
    }
  }
}
```
