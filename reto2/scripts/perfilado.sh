#!/usr/bin/env bash
# ============================================================================
#  perfilado.sh - Perfilado de CPU y memoria del Reto 2
#  Proyecto HPC 2026-2
#
#  1. bin/perfil.exe (../scripts/perfil.c): ejecuta el programa completo en un
#     Job Object de Windows y mide tiempo de pared, CPU de usuario y de kernel,
#     nucleos ocupados en promedio, pico de memoria comprometida, working set
#     y fallos de pagina.  ->  datos/perfilado.csv  (mediana de 3 corridas en
#     el analisis)
#
#  2. gprof sobre las versiones seriales compiladas con -pg -fno-inline:
#     reparte el tiempo entre el generador, el nucleo y el bucle de conteo.
#     Las funciones de la DLL de C (rand, sin) NO se muestrean: el tiempo que
#     falta respecto al total de pared es el que se pasa dentro de ucrt.
#     ->  perfilado/gprof_<alg>_<variante>.txt
#
#  Uso (desde reto2/):  bash scripts/perfilado.sh
# ============================================================================
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p datos perfilado

# ---------------------------------------------------------------- 1. perfil
f=datos/perfilado.csv
echo "programa;variante;hilos;N;rep;Pared(s);CPUUsuario(s);CPUKernel(s);UsoCPU(nucleos);MemoriaPico(MiB);WorkingSet(MiB);FallosPagina" > $f
mide() {  # mide <prog> <variante> <hilos> <N> <rep>
    local p=$1 v=$2 t=$3 n=$4 r=$5 extra=""
    [[ $p == *_omp ]] && extra="-t $t"
    echo "$p;$v;$t;$n;$r;$(bin/perfil.exe "bin\\$p.exe" -n $n -v $v $extra)" >> $f
}
for r in 1 2 3; do
    for v in rand double entero arreglo bloques; do mide dartboard_serial $v 1 1e8 $r; done
    for v in rand sin rechazo arreglo bloques;   do mide needles_serial   $v 1 2e7 $r; done
    for v in critical atomic rand falso padding reduction opt; do
        mide dartboard_omp $v 12 1e8 $r
        mide needles_omp   $v 12 2e7 $r
    done
    for t in 1 6 12; do
        mide dartboard_omp reduction $t 1e9 $r
        mide needles_omp   reduction $t 1e8 $r
    done
done
echo ">> $f"

# ---------------------------------------------------------------- 2. gprof
for v in rand double entero bloques; do
    bin/dartboard_serial_pg -n 1e8 -v $v > /dev/null
    gprof -b -p bin/dartboard_serial_pg.exe gmon.out > perfilado/gprof_dartboard_$v.txt
done
for v in rand sin rechazo bloques; do
    bin/needles_serial_pg -n 2e7 -v $v > /dev/null
    gprof -b -p bin/needles_serial_pg.exe gmon.out > perfilado/gprof_needles_$v.txt
done
rm -f gmon.out

# Informacion del equipo
{
    echo "CPU   : $(powershell -NoProfile -Command '(Get-CimInstance Win32_Processor).Name' | tr -d '\r')"
    echo "Nucleos fisicos / logicos: $(powershell -NoProfile -Command '$p=Get-CimInstance Win32_Processor; "$($p.NumberOfCores) / $($p.NumberOfLogicalProcessors)"' | tr -d '\r')"
    echo "RAM   : $(powershell -NoProfile -Command '[math]::Round((Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory/1GB,1)' | tr -d '\r') GiB"
    echo "SO    : $(powershell -NoProfile -Command '(Get-CimInstance Win32_OperatingSystem).Caption' | tr -d '\r')"
    echo "GCC   : $(gcc --version | head -1)"
} > perfilado/equipo.txt
echo ">> perfilado/"
