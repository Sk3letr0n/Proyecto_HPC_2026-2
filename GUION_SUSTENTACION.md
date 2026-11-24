# Guion de Sustentacion - Multiplicacion de Matrices con MPI
## Duracion total: ~6 minutos (2 minutos por persona)

---

## PERSONA 1: Introduccion, Infraestructura y Configuracion (~2 min)

**[0:00 - 0:30] Apertura y contexto**

"Buenas tardes. Nuestro caso de estudio consiste en la multiplicacion de matrices distribuida usando MPI, ejecutada en un cluster computacional en AWS. La multiplicacion de matrices tiene complejidad O(n^3), lo que la convierte en un candidato ideal para paralelismo. Probamos con matrices de 1000x1000, 2000x2000 y 3000x3000."

**[0:30 - 1:15] Cluster en AWS**

"Para el cluster utilizamos 4 instancias EC2 de tipo t3.micro en AWS, cada una con 2 vCPUs y 1 GB de RAM, todas en la misma subred para comunicacion directa por IP privada. Tenemos un nodo head que actua como coordinador y 3 worker nodes: wn1, wn2 y wn3.

La configuracion incluyo:
- Crear un usuario 'cluster' en todos los nodos.
- Configurar SSH sin contrasena con ssh-keygen y authorized_keys para que MPI pueda lanzar procesos remotos.
- Configurar /etc/hosts para resolver los nombres por IP privada.
- Instalar OpenMPI y GCC en todos los nodos.
- Crear un archivo hosts que le indica a mpirun cuantos procesos ejecutar en cada nodo."

**[1:15 - 2:00] Implementacion MPI**

"Nuestra implementacion usa exclusivamente operaciones colectivas de MPI. El proceso raiz genera las matrices A y B. Luego, MPI_Bcast envia B completa a todos los procesos, MPI_Scatter reparte las filas de A equitativamente, cada proceso calcula su porcion local de C usando el orden de bucles i-k-j que es mas eficiente en cache, y finalmente MPI_Gather recolecta todos los bloques en el raiz. Si N no es divisible por el numero de procesos, se usa padding con ceros."

---

## PERSONA 2: Resultados MPI y Analisis (~2 min)

**[2:00 - 2:40] Resultados en maquina local**

"Primero ejecutamos las pruebas en la maquina local con 4 procesos MPI. Con matrices de 1000x1000, el tiempo fue de solo 0.056 segundos con un rendimiento de 35.8 GOP/s. Con 2000x2000 fue 0.46 segundos y con 3000x3000 fue 1.8 segundos. El rendimiento se mantiene alto, alrededor de 30-35 GOP/s, porque los procesos comparten memoria y no hay latencia de red."

**[2:40 - 3:20] Resultados en el cluster**

"En el cluster AWS con 4 procesos, 1 por nodo, los tiempos fueron: 0.76 segundos para 1000x1000, 6.6 segundos para 2000x2000, y 20 segundos para 3000x3000, con un rendimiento de alrededor de 2.5 GOP/s. Al duplicar a 8 procesos, 2 por nodo, los tiempos bajaron a 0.66, 5.1 y 17.2 segundos respectivamente, con un rendimiento de aproximadamente 3.1 GOP/s. El speedup de 4 a 8 procesos fue entre 1.16x y 1.30x."

**[3:20 - 4:00] Analisis**

"La maquina local es hasta 13 veces mas rapida que el cluster, lo cual se explica por dos factores: primero, el procesador de escritorio es mucho mas potente que las instancias t3.micro que son la opcion mas basica de AWS; segundo, en local no hay latencia de red. El speedup modesto al pasar de 4 a 8 procesos se debe a que con solo 2 vCPUs por nodo, los dos procesos en cada nodo compiten por los mismos recursos. En un escenario real con instancias mas potentes, el cluster mostraria su verdadera ventaja."

---

## PERSONA 3: Practica NFS, Comparativa y Conclusiones (~2 min)

**[4:00 - 4:40] Practica NFS + SSH**

"Adicionalmente implementamos la multiplicacion distribuida sin MPI, usando NFS y SSH. NFS comparte una carpeta entre todos los nodos donde se guardan las matrices. El nodo head genera A y B, lanza por SSH un proceso en cada worker, cada worker calcula sus filas de C, y al final head ensambla el resultado. Los tiempos fueron: 1.96 segundos para 1000x1000, 19.6 segundos para 2000x2000, y 75 segundos para 3000x3000. Se verifico la correctitud comparando contra un calculo secuencial en Python."

**[4:40 - 5:20] Comparativa MPI vs NFS**

"Comparando ambos enfoques en el mismo cluster: MPI con 4 procesos es entre 2.5 y 3.7 veces mas rapido que NFS+SSH. La razon principal es que MPI usa comunicacion optimizada, distribuyendo solo las filas necesarias a cada proceso, mientras que con NFS cada worker debe leer las matrices completas desde el sistema de archivos de red, lo que genera un cuello de botella de entrada/salida. Ademas, MPI maneja la sincronizacion de forma nativa con operaciones colectivas."

**[5:20 - 6:00] Conclusiones**

"En resumen: implementamos exitosamente la multiplicacion de matrices con MPI usando operaciones colectivas en un cluster de 4 nodos AWS. MPI demostro ser significativamente mas eficiente que el enfoque manual con NFS y SSH. Aunque la maquina local fue mas rapida por su hardware superior, el objetivo del cluster es la escalabilidad: con instancias de mayor capacidad y matrices mas grandes, la distribucion del trabajo entre multiples maquinas es lo que permite resolver problemas que una sola maquina no podria. Gracias."

---

## TIPS PARA LA SUSTENTACION
- Tengan abiertas las capturas de pantalla para mostrar los resultados reales.
- Si preguntan por la verificacion, mencionen que se uso semilla 42 y un script Python para confirmar A x B = C.
- Si preguntan por que no se usa MPI_Send/Recv: porque las operaciones colectivas son mas eficientes y simples para este patron de distribucion por filas.
- Si preguntan el comando de ejecucion: `mpirun -f ~/hosts -n 4 /home/cluster/matmul_mpi -n 1000 -l 100`
