# Multiplicación de matrices distribuida con NFS + SSH

## Arquitectura

- `head` genera A y B una sola vez en `/mnt/matrices`.
- NFS hace visibles esos archivos en los cuatro nodos.
- `head` inicia por SSH un proceso en cada worker.
- Cada worker calcula un rango distinto de filas de C.
- Cada worker escribe su parcial en `/mnt/matrices`.
- `head` reúne los tres parciales y crea `C_final.txt`.

No utiliza MPI. NFS comparte archivos; SSH inicia los procesos remotos.

## Archivos

- `matmul_dist.c`: programa C con los comandos `init`, `worker` y `merge`.
- `run_workers.sh`: coordinador que se ejecuta en HEAD.
- `A.txt`, `B.txt`: matrices de entrada.
- `C_wn1.txt`, `C_wn2.txt`, `C_wn3.txt`: resultados parciales.
- `C_final.txt`: matriz final ensamblada.

## Paso 1. Comprobar los nodos

Asegúrate de que `/mnt/matrices` esté montado por NFS en HEAD y en los tres workers.
La cuenta `cluster` debe poder leer y escribir allí desde todos los nodos.

Desde `cluster@head`, prueba SSH:

```bash
ssh cluster@172.31.2.152 hostname
ssh cluster@172.31.7.1 hostname
ssh cluster@172.31.5.134 hostname
```

Los tres comandos deben responder sin pedir contraseña (la automatización usa `BatchMode=yes`).

## Paso 2. Copiar los dos archivos a NFS

Coloca `matmul_dist.c` y `run_workers.sh` en `/mnt/matrices` de HEAD.
luego, desde `cluster@head`, verifica:

```bash
ls -l /mnt/matrices/matmul_dist.c /mnt/matrices/run_workers.sh
```

## Paso 3. Compilar en HEAD

Ejecuta como `cluster@head`:

```bash
cd /mnt/matrices
gcc -O2 -Wall -Wextra -std=c11 matmul_dist.c -o matmul_dist
chmod u+x run_workers.sh
```

El ejecutable queda en la carpeta NFS y los workers lo ven en la misma ruta. Los cuatro nodos deben tener la misma arquitectura/ABI de Linux (por ejemplo, Ubuntu x86-64).

## Paso 4. Primera ejecución pequeña

Desde `cluster@head`:

```bash
cd /mnt/matrices
./run_workers.sh 5 9 42
```

- `5`: dimensión de las matrices (5 x 5).
- `9`: valores aleatorios entre 0 y 9.
- `42`: semilla para repetir la misma generación.

El programa imprime el rango de filas asignado a cada worker y crea `C_final.txt`.

---------------------------------------------------------------------

Resultados de este paso:

== 1. Generando matrices compartidas en NFS ==
Matrices A y B creadas: N=5, limite=9, semilla=42
== 2. Lanzando los tres workers por SSH ==
WN1: filas [0, 1)
WN2: filas [1, 3)
WN3: filas [3, 5)
== 3. Resultado informado por cada worker ==
----- worker_wn1.log -----
Worker: filas [0, 1), tiempo de calculo: 0.003641 s
----- worker_wn2.log -----
Worker: filas [1, 3), tiempo de calculo: 0.003442 s
----- worker_wn3.log -----
Worker: filas [3, 5), tiempo de calculo: 0.003445 s
== 4. Ensamblando la matriz C en HEAD ==
Matriz C ensamblada correctamente en /mnt/matrices/C_final.txt
== 5. Ejecucion distribuida terminada ==
Resultado: /mnt/matrices/C_final.txt

---------------------------------------------------------------------


## Paso 5. Ver el resultado

Desde `cluster@head`:

```bash
cat /mnt/matrices/C_final.txt
```

El archivo comienza con el tamaño N y luego contiene N filas con los valores de C.


---------------------------------------------------------------------

Resultados de este paso:

cluster@head:/mnt/matrices$ cat /mnt/matrices/C_final.txt
5
174 144 96 113 111
150 113 87 73 101
198 160 97 92 108
78 60 36 65 88
165 127 72 71 104

---------------------------------------------------------------------


## Importante

- No ejecutes el script como `ubuntu`; ejecútalo como `cluster@head`, que es el usuario configurado para SSH y escritura en NFS.
- Los IP del script corresponden a esta práctica: WN1 `172.31.2.152`, WN2 `172.31.7.1`, WN3 `172.31.5.134`.
- Si cambian las IP, edita las tres líneas `ssh` de `run_workers.sh`.
- Para una dimensión menor que 3, alguno de los workers recibirá cero filas; seguirá participando y generará un parcial vacío válido.
- Los archivos de matrices y resultados son texto plano para facilitar su inspección.
- La salida final usa enteros de 64 bits. El programa detecta si un elemento calculado excede ese rango.
- Esta es una primera versión distribuida por filas. Los workers leen las matrices completas A y B desde NFS, pero cada uno calcula solo sus filas de C.

---------------------------------------------------------------------

RESULTADOS CON MATRICES DE TAMAÑO 1000, 2000 y 3000:

cluster@head:/mnt/matrices$ cd /mnt/matrices

python3 - <<'PY'
def leer_matriz(nombre):
    with open(nombre) as f:
        datos = list(map(int, f.read().split()))
    n = datos[0]
    valores = datos[1:]
    assert len(valores) == n * n, f"Dimensiones incorrectas en {nombre}"
    return [valores[i*n:(i+1)*n] for i in range(n)]

A = leer_matriz("A.txt")
B = leer_matriz("B.txt")
C = leer_matriz("C_final.txt")

n = len(A)
esperada = [
    [sum(A[i][k] * B[k][j] for k in range(n)) for j in range(n)]
    for i in range(n)
]

if C == esperada:
    print("CORRECTO: C es igual a A x B")
else:
    print("ERROR: el resultado no coincide")
    print("Matriz esperada:", esperada)
    print("Matriz obtenida:", C)
PY
CORRECTO: C es igual a A x B
cluster@head:/mnt/matrices$ cd /mnt/matrices

for N in 1000 2000 3000
do
    echo ""
    echo "=================================="
    echo "PRUEBA CON MATRICES ${N}x${N}"
    echo "=================================="

    /usr/bin/time -f "Tiempo total: %e segundos" ./run_workers.sh "$N" 9 42

    if [ "$(head -n 1 C_final.txt)" = "$N" ]; then
        echo "Verificacion: matriz C de ${N}x${N} generada correctamente."
    else
        echo "ERROR: dimension incorrecta."
        break
    fi
done

==================================
PRUEBA CON MATRICES 1000x1000
==================================
== 1. Generando matrices compartidas en NFS ==
Matrices A y B creadas: N=1000, limite=9, semilla=42
== 2. Lanzando los tres workers por SSH ==
WN1: filas [0, 333)
WN2: filas [333, 666)
WN3: filas [666, 1000)
== 3. Resultado informado por cada worker ==
----- worker_wn1.log -----
Worker: filas [0, 333), tiempo de calculo: 0.566419 s
----- worker_wn2.log -----
Worker: filas [333, 666), tiempo de calculo: 0.624165 s
----- worker_wn3.log -----
Worker: filas [666, 1000), tiempo de calculo: 0.551616 s
== 4. Ensamblando la matriz C en HEAD ==
Matriz C ensamblada correctamente en /mnt/matrices/C_final.txt
== 5. Ejecucion distribuida terminada ==
Resultado: /mnt/matrices/C_final.txt
Tiempo total: 1.96 segundos
Verificacion: matriz C de 1000x1000 generada correctamente.

==================================
PRUEBA CON MATRICES 2000x2000
==================================
== 1. Generando matrices compartidas en NFS ==
Matrices A y B creadas: N=2000, limite=9, semilla=42
== 2. Lanzando los tres workers por SSH ==
WN1: filas [0, 666)
WN2: filas [666, 1333)
WN3: filas [1333, 2000)
== 3. Resultado informado por cada worker ==
----- worker_wn1.log -----
Worker: filas [0, 666), tiempo de calculo: 17.538457 s
----- worker_wn2.log -----
Worker: filas [666, 1333), tiempo de calculo: 17.432315 s
----- worker_wn3.log -----
Worker: filas [1333, 2000), tiempo de calculo: 17.081237 s
== 4. Ensamblando la matriz C en HEAD ==
Matriz C ensamblada correctamente en /mnt/matrices/C_final.txt
== 5. Ejecucion distribuida terminada ==
Resultado: /mnt/matrices/C_final.txt
Tiempo total: 19.64 segundos
Verificacion: matriz C de 2000x2000 generada correctamente.

==================================
PRUEBA CON MATRICES 3000x3000
==================================
== 1. Generando matrices compartidas en NFS ==
Matrices A y B creadas: N=3000, limite=9, semilla=42
== 2. Lanzando los tres workers por SSH ==
WN1: filas [0, 1000)
WN2: filas [1000, 2000)
WN3: filas [2000, 3000)
== 3. Resultado informado por cada worker ==
----- worker_wn1.log -----
Worker: filas [0, 1000), tiempo de calculo: 70.184776 s
----- worker_wn2.log -----
Worker: filas [1000, 2000), tiempo de calculo: 70.620094 s
----- worker_wn3.log -----
Worker: filas [2000, 3000), tiempo de calculo: 69.714235 s
== 4. Ensamblando la matriz C en HEAD ==
Matriz C ensamblada correctamente en /mnt/matrices/C_final.txt
== 5. Ejecucion distribuida terminada ==
Resultado: /mnt/matrices/C_final.txt
Tiempo total: 75.09 segundos
Verificacion: matriz C de 3000x3000 generada correctamente.