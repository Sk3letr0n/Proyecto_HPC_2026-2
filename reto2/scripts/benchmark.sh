#!/usr/bin/env bash
# ============================================================================
#  benchmark.sh - Barridos de medicion del Reto 2 (PI por Monte Carlo)
#  Proyecto HPC 2026-2
#
#  Uso (desde reto2/, despues de compilar con mingw32-make):
#      bash scripts/benchmark.sh            # todos los experimentos
#      bash scripts/benchmark.sh E3 E4      # solo algunos
#
#  Cada experimento escribe datos/<experimento>.csv con separador ';' y coma
#  decimal (como el resto de CSV del repositorio). Columnas:
#
#    experimento;config;rep;programa;variante;hilos;N;pi;error_abs;tiempo_s
#
#  'config' guarda la bandera de compilacion, la politica de reparto o la
#  semilla segun el experimento. Se repite cada medicion REPS veces y en el
#  analisis se usa la MEDIANA.
# ============================================================================
set -euo pipefail
cd "$(dirname "$0")/.."

REPS=${REPS:-5}
HILOS=(1 2 3 4 5 6 8 10 12)
mkdir -p datos
CAB="experimento;config;rep;programa;variante;hilos;N;pi;error_abs;tiempo_s"

# corre <archivo> <experimento> <config> <rep> <comando...>
corre() {
    local arch=$1 exp=$2 cfg=$3 rep=$4; shift 4
    echo "$exp;$cfg;$rep;$("$@" -c)" >> "$arch"
}
nuevo() { echo "$CAB" > "datos/$1.csv"; echo ">> $1"; }

# Tamanos: elegidos para que cada medicion serial dure ~1-5 s.
N_D=200000000      # dartboard
N_N=50000000       # needles (sin() es ~30x mas caro que un dardo)

# ---------------------------------------------------------------------------
E1() {  # Variantes seriales (optimizacion de CPU y memoria), -O2
    nuevo E1_serial; local f=datos/E1_serial.csv
    for r in $(seq $REPS); do
        for v in rand double entero arreglo bloques; do corre $f E1 O2 $r bin/dartboard_serial -n $N_D -v $v; done
        for v in rand sin rechazo arreglo bloques;   do corre $f E1 O2 $r bin/needles_serial   -n $N_N -v $v; done
    done
}

# ---------------------------------------------------------------------------
E2() {  # Banderas del compilador
    nuevo E2_compilador; local f=datos/E2_compilador.csv
    for r in $(seq $REPS); do
        for fl in O0 O2 O3 Ofastnative; do
            local s="_$fl"; [ $fl = O2 ] && s=""
            for v in double entero bloques; do corre $f E2 $fl $r bin/dartboard_serial$s -n $N_D -v $v; done
            for v in sin rechazo bloques;   do corre $f E2 $fl $r bin/needles_serial$s   -n $N_N -v $v; done
        done
    done
}

# ---------------------------------------------------------------------------
E3() {  # Escalado fuerte (N fijo) de las versiones OpenMP buenas
    nuevo E3_escalado; local f=datos/E3_escalado.csv
    for r in $(seq $REPS); do
        corre $f E3 serial $r bin/dartboard_serial -n 1e9 -v double
        corre $f E3 serial $r bin/dartboard_serial -n 1e9 -v entero
        corre $f E3 serial $r bin/needles_serial   -n 1e8 -v sin
        corre $f E3 serial $r bin/needles_serial   -n 5e8 -v rechazo
        for t in "${HILOS[@]}"; do
            corre $f E3 O2 $r bin/dartboard_omp -n 1e9 -t $t -v reduction
            corre $f E3 O2 $r bin/dartboard_omp -n 1e9 -t $t -v opt
            corre $f E3 O2 $r bin/needles_omp   -n 1e8 -t $t -v reduction
            corre $f E3 O2 $r bin/needles_omp   -n 5e8 -t $t -v opt
        done
    done
}

# ---------------------------------------------------------------------------
E4() {  # Las 7 estrategias OpenMP frente al numero de hilos
    nuevo E4_variantes_omp; local f=datos/E4_variantes_omp.csv
    for r in $(seq $REPS); do
        for t in 1 2 4 6 12; do
            for v in critical atomic rand falso padding reduction opt; do
                corre $f E4 O2 $r bin/dartboard_omp -n 1e8 -t $t -v $v
                corre $f E4 O2 $r bin/needles_omp   -n 2e7 -t $t -v $v
            done
        done
    done
}

# ---------------------------------------------------------------------------
E5() {  # Politica de reparto (schedule) con 12 hilos
    nuevo E5_schedule; local f=datos/E5_schedule.csv
    for r in $(seq $REPS); do
        for s in static "static,1" "dynamic" "dynamic,1000" "dynamic,100000" "guided"; do
            OMP_SCHEDULE=$s corre $f E5 "$s" $r bin/dartboard_omp -n 1e8 -t 12 -v reduction
            OMP_SCHEDULE=$s corre $f E5 "$s" $r bin/needles_omp   -n 2e7 -t 12 -v reduction
        done
    done
}

# ---------------------------------------------------------------------------
E6() {  # Calidad estadistica del generador: 20 semillas, N = 1e7, 12 hilos
    nuevo E6_calidad; local f=datos/E6_calidad.csv
    for s in $(seq 1 20); do
        for v in rand reduction opt; do
            corre $f E6 "$s" 1 bin/dartboard_omp -n 1e7 -t 12 -s $s -v $v
            corre $f E6 "$s" 1 bin/needles_omp   -n 1e7 -t 12 -s $s -v $v
        done
        corre $f E6 "$s" 1 bin/dartboard_serial -n 1e7 -s $s -v rand
        corre $f E6 "$s" 1 bin/needles_serial   -n 1e7 -s $s -v rand
    done
}

# ---------------------------------------------------------------------------
E7() {  # Convergencia del error con N (10 semillas), version opt 12 hilos
    nuevo E7_convergencia; local f=datos/E7_convergencia.csv
    for n in 1e3 1e4 1e5 1e6 1e7 1e8 1e9; do
        for s in $(seq 1 10); do
            corre $f E7 "$s" 1 bin/dartboard_omp -n $n -t 12 -s $s -v opt
            corre $f E7 "$s" 1 bin/needles_omp   -n $n -t 12 -s $s -v opt
            corre $f E7 "$s" 1 bin/needles_omp   -n $n -t 12 -s $s -v reduction
            corre $f E7 "$s" 1 bin/dartboard_omp -n $n -t 12 -s $s -v reduction
        done
    done
}

# ---------------------------------------------------------------------------
E8() {  # Mejor combinacion: OpenMP opt + -Ofast -march=native, 12 hilos
    nuevo E8_mejor; local f=datos/E8_mejor.csv
    for r in $(seq $REPS); do
        for fl in O2 Ofastnative; do
            local s="_$fl"; [ $fl = O2 ] && s=""
            corre $f E8 $fl $r bin/dartboard_omp$s -n 1e9 -t 12 -v opt
            corre $f E8 $fl $r bin/needles_omp$s   -n 5e8 -t 12 -v opt
        done
    done
}

# ---------------------------------------------------------------------------
E9() {  # Afinidad: libgomp de MinGW ignora OMP_PLACES/OMP_PROC_BIND
        # ("Affinity not supported on this configuration"), asi que se fija la
        # mascara de CPUs del PROCESO con  cmd /c start /affinity <hex>.
        # En el Ryzen 5600G los CPU logicos 2k y 2k+1 son hermanos SMT:
        #   FFF = 12 CPU logicos (libre)     555 = 1 CPU logico por nucleo (6)
        #   03F = 3 nucleos fisicos x 2 SMT
    nuevo E9_afinidad; local f=datos/E9_afinidad.csv
    afin() {  # afin <mascara> <exe> <args...>
        local m=$1 exe=$2; shift 2
        cmd //c "start /affinity $m /wait /b bin\\$exe.exe $* -c" | tr -d '\r'
    }
    for r in $(seq $REPS); do
        for m in FFF 555 03F; do
            for v in reduction opt; do
                echo "E9;$m;$r;$(afin $m dartboard_omp -n 1e9 -t 6 -v $v)" >> $f
                echo "E9;$m;$r;$(afin $m needles_omp -n 1e8 -t 6 -v $v)" >> $f
            done
        done
        for v in reduction opt; do
            echo "E9;FFF;$r;$(afin FFF dartboard_omp -n 1e9 -t 12 -v $v)" >> $f
            echo "E9;FFF;$r;$(afin FFF needles_omp -n 1e8 -t 12 -v $v)" >> $f
        done
    done
}

EXPS=("$@"); [ ${#EXPS[@]} -eq 0 ] && EXPS=(E1 E2 E3 E4 E5 E6 E7 E8 E9)
for e in "${EXPS[@]}"; do "$e"; done
echo "Listo. Resultados en datos/"
