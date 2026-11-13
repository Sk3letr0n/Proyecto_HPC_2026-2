# Guión — Video Reto 2: Cálculo de π por Monte Carlo con OpenMP

**Duración total:** ~9 minutos  
**Participantes:** Estudiante A, Estudiante B, Estudiante C

---

## ESTUDIANTE A — Introducción y Análisis de los Algoritmos (~3 min)

### Intro (0:00 – 0:30)

> Hola, somos [nombres] y en este video vamos a presentar el Reto 2 del curso de High Performance Computing. El objetivo fue implementar dos métodos de Monte Carlo para calcular el número π: el método **Dartboard** y el método de las **agujas de Buffon**, primero en versiones seriales y luego en versiones paralelas con OpenMP, analizando las optimizaciones de CPU y memoria y las causas del desempeño que obtuvimos.
>
> Todas las mediciones las hicimos en un AMD Ryzen 5 5600G con 6 núcleos físicos y 12 hilos lógicos, 32 GB de RAM, compilando con GCC 13.1. Cada tiempo que reportamos es la mediana de 5 repeticiones.

### Algoritmo Dartboard (0:30 – 1:15)

> El primer método es **Dartboard** o tablero de dardos. La idea es muy sencilla: imaginen un cuadrado de lado 1 con un cuarto de círculo inscrito. Si lanzamos dardos al azar dentro del cuadrado, la fracción que cae dentro del cuarto de círculo se acerca a π/4, porque esa es la razón entre las áreas. Entonces π se estima como 4 por el número de dardos dentro dividido entre el total.
>
> ¿Qué tan costoso es esto? Cada dardo necesita solo 2 números aleatorios, 2 multiplicaciones, una suma y una comparación. Es un cálculo muy ligero, así que el **cuello de botella** resulta ser el generador de números aleatorios, no la operación matemática.
>
> El error baja como 1.64 dividido entre raíz de N. Eso significa que para ganar un dígito decimal más de precisión necesitamos 100 veces más muestras. Y en memoria, el algoritmo usa O(1): no necesitamos guardar los dardos.

### Algoritmo Buffon's Needles (1:15 – 2:00)

> El segundo método es el de las **agujas de Buffon**. Imaginemos un piso con líneas paralelas separadas una distancia de 2 unidades y tiramos agujas de longitud 1. La probabilidad de que una aguja cruce una línea resulta ser 1/π. Entonces estimamos π como N dividido entre el número de cruces.
>
> La diferencia importante con Dartboard es que cada aguja necesita calcular un **seno**, que en nuestra plataforma cuesta unos 50 nanosegundos — unas 30 veces más que un dardo. Además, tiene un problema conceptual interesante: para muestrear el ángulo en [0, π/2] necesitamos conocer π, que es justamente lo que queremos calcular. Después vamos a ver cómo resolvimos eso.
>
> El error es 2.37/√N, un poco mayor que Dartboard: necesita unas 2.1 veces más muestras para la misma precisión.

### Propiedades para el paralelismo (2:00 – 2:50)

> Lo que hace a estos dos algoritmos ideales para paralelismo es que son **vergonzosamente paralelos**: cada muestra es completamente independiente de las demás. El resultado es una suma — un conteo de aciertos — que se puede paralelizar con una **reducción**: cada hilo cuenta por su lado y al final se suman. Todas las iteraciones cuestan lo mismo, así que un reparto estático equilibra la carga. Y usan O(1) de memoria.
>
> Lo único que hay que manejar con cuidado es el **generador aleatorio**: es el único estado compartido oculto. Cada hilo necesita su propio flujo de números aleatorios, independiente y que no se solape con el de los demás.

---

## ESTUDIANTE B — Versiones Seriales y Optimización (~3 min)

### Versiones seriales (2:50 – 4:00)

> Gracias [Estudiante A]. Ahora veamos las implementaciones. Para cada algoritmo hicimos 5 variantes seriales. La línea base es usar `rand()` de la biblioteca estándar de C. En Dartboard tarda 27.3 nanosegundos por dardo.
>
> Pero `rand()` tiene tres problemas graves: es una llamada a una DLL que no se puede optimizar, tiene solo 15 bits de resolución — o sea que el tablero queda en una rejilla muy gruesa — y su estado está oculto, lo cual lo hace inútil para paralelismo.
>
> La primera mejora fue cambiar el generador a **xoshiro256 doble estrella**, que son unas 10 operaciones enteras, cabe en registros y tiene una función de salto que da flujos independientes por hilo. Solo con ese cambio, Dartboard pasó de 27.3 a **1.73 nanosegundos**: casi **16 veces más rápido**.
>
> La variante **entero** va un paso más allá: un solo número de 64 bits da las dos coordenadas de 31 bits cada una y la comparación se hace en aritmética entera, sin punto flotante. Resultado: 1.50 ns, **18 veces más rápido** que rand.
>
> En Needles, la optimización clave fue la variante de **rechazo**: en vez de calcular seno, tomamos un vector aleatorio dentro del cuarto de círculo por rechazo y elevamos al cuadrado la condición. Así eliminamos sin(), sqrt() y π. Pasó de 49.9 a **7.1 nanosegundos**: **7 veces más rápido**.

### Optimización de memoria (4:00 – 4:40)

> También exploramos qué pasa con la memoria. Hicimos una variante `arreglo` que guarda todos los N puntos en memoria antes de contarlos. Con 100 millones de dardos eso consume **1.5 GB** de RAM y genera casi 400 mil fallos de página. El 46% del tiempo se va en el kernel porque Windows tiene que poner a cero cada página nueva. Se volvió casi 3 veces más lenta.
>
> La variante `bloques` usa un buffer de 16 KB que cabe en la caché L1, pero tampoco supera al streaming simple porque el cuello de botella es el generador, no el acceso a datos.
>
> La conclusión de memoria es clara: para Monte Carlo, la mejor optimización de memoria es **no usar memoria**. Generar y consumir cada muestra directamente en registros.

### Optimización por compilador (4:40 – 5:10)

> En cuanto a banderas del compilador: pasar de `-O0` a `-O2` da una mejora de **13 a 14 veces** en Dartboard, porque con `-O2` el generador se inlinea completamente y el estado vive en registros en vez de ir a la pila en cada iteración. Pero `-O3` y `-Ofast -march=native` prácticamente no aportan nada extra consistente, porque no hay bucles vectorizables significativos — el generador es secuencial por naturaleza.

---

## ESTUDIANTE C — Paralelización OpenMP y Causas del Desempeño (~3 min)

### Estrategias OpenMP (5:10 – 6:30)

> Gracias [Estudiante B]. Ahora viene la parte de OpenMP. Implementamos **7 estrategias** diferentes para ver qué pasa cuando manejas mal — o bien — la sincronización.
>
> Las peores son **critical** y **atomic**. Con critical, cada acierto toma un candado global — un mutex del sistema operativo. Con 12 hilos, Dartboard se vuelve **más lento** que con 1 hilo: 1.7 segundos contra 0.7. El perfilado muestra 4 segundos de CPU en kernel solo por los cambios de contexto. Con atomic es similar: no usa candado pero la línea de caché del contador rebota entre los 12 núcleos millones de veces. 11 núcleos al 100% hacen el trabajo que 1 hace en 0.19 segundos.
>
> Luego probamos **false sharing**: ponemos los contadores de cada hilo en posiciones contiguas de un arreglo. Aunque cada hilo escribe en su propia posición, como 8 contadores caben en una sola línea de caché de 64 bytes, cada escritura invalida la copia de la línea en los demás núcleos. Resultado: **9.4 veces más lento** que con el relleno correcto. Solo con alinear cada contador a 64 bytes, el problema desaparece.
>
> La forma correcta es **reduction**: cada hilo tiene su contador privado y su generador local en registros, y OpenMP suma todo al final con una sola sincronización por hilo. Combinada con el núcleo optimizado (entero o rechazo), es la variante **opt**, la más rápida.
>
> Un dato interesante: en Needles todos estos antipatrones hacen **menos daño** porque la tasa de escritura al contador es mucho menor — un acierto cada 150 nanosegundos contra cada 2 nanosegundos en Dartboard. **La contención depende de la frecuencia de acceso, no solo de que exista.**

### Causas del desempeño (6:30 – 7:50)

> ¿Por qué Dartboard escala peor que Needles? Con 12 hilos, Dartboard logra 5.7× y Needles 10.2×. Identificamos tres causas:
>
> **Primera**, la planificación de Windows. Sin control de afinidad, a veces Windows coloca dos hilos en el mismo núcleo físico — en los dos hilos SMT hermanos. Con reparto estático todos esperan al hilo más lento. Cuando fijamos un hilo por núcleo físico, Dartboard mejoró de 4.2× a 5.3×.
>
> **Segunda**, el Simultaneous Multithreading funciona diferente para cada algoritmo. Dartboard con el núcleo entero tiene mucho paralelismo a nivel de instrucción — un hilo ya satura las ALU y el hermano SMT casi no encuentra qué hacer. En cambio, la función seno en Needles tiene cadenas largas de dependencia que dejan unidades de ejecución ociosas, y el hermano SMT las aprovecha. Por eso Needles con 12 hilos logra un speedup casi lineal.
>
> **Tercera**, la frecuencia del procesador: con todos los núcleos activos, el Ryzen baja su frecuencia por límites de potencia.
>
> Importante: **Amdahl no es el límite aquí**. La fracción serial es menor al 0.01%. Los límites reales son de hardware y del sistema operativo.

### Resultado final y cierre (7:50 – 9:00)

> Otro hallazgo importante fue que `rand()` en paralelo da resultados **incorrectos** de forma silenciosa. En Windows UCRT, cada hilo tiene su propio estado de rand(), pero todos empiezan con semilla 1 — solo el hilo maestro recibe el srand(). Así que 11 de 12 hilos generan la misma secuencia, produciendo un π con sesgo sistemático. El programa parece funcionar bien, pero el resultado está mal.
>
> Ahora, el resultado final: combinando el núcleo optimizado, las mejores banderas de compilación y 12 hilos, logramos una mejora total de **109 veces** en Dartboard y **91 veces** en Needles respecto a la versión serial con rand().
>
> Pero aquí está la conclusión más importante: de ese ×109, **×18 vino de la optimización serial** — el generador y el núcleo — y solo **×6 del paralelismo**. La optimización del algoritmo pesó tres veces más que los 12 hilos del procesador.
>
> Para cerrar: en Monte Carlo, la paralelización correcta es simple — una reducción con generador local por hilo. Pero antes de paralelizar, hay que asegurarse de que el código serial ya sea eficiente. Y hay que evitar los antipatrones clásicos: secciones críticas innecesarias, operaciones atómicas en datos calientes y false sharing. Cuando se hacen las cosas bien, ambos algoritmos escalan sin sincronización y sin datos compartidos. Gracias por su atención.
