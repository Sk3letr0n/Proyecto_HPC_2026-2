# ============================================================================
#  Makefile - Reto 2: PI por Monte Carlo (Dartboard y Buffon's Needles)
#  Proyecto HPC 2026-2
#
#  Uso:  mingw32-make  (Git Bash / MSYS2 en Windows)  o  make  (Linux)
#
#  Genera en bin/:
#    dartboard_serial, needles_serial          serial, -O2 (linea base)
#    dartboard_omp,    needles_omp             OpenMP, -O2
#    *_serial_{O0,O3,Ofastnative}              barrido de banderas del compilador
#    *_omp_Ofastnative                         OpenMP con la mejor bandera
#    *_serial_pg                               instrumentados para gprof
#    perfil                                    medidor de CPU/memoria (Windows)
# ============================================================================
CC      := gcc
WARN    := -Wall -Wextra -Wpedantic -std=c11
LDLIBS  := -lm
BIN     := bin
SRC     := src
HDRS    := $(SRC)/comun.h $(SRC)/nucleos.h

ALGS    := dartboard needles
FLAGS_O0          := -O0
FLAGS_O2          := -O2
FLAGS_O3          := -O3
FLAGS_Ofastnative := -Ofast -march=native

SERIAL  := $(foreach a,$(ALGS),$(BIN)/$(a)_serial)
OMP     := $(foreach a,$(ALGS),$(BIN)/$(a)_omp)
BARRIDO := $(foreach a,$(ALGS),$(foreach f,O0 O3 Ofastnative,$(BIN)/$(a)_serial_$(f)))
OMPFAST := $(foreach a,$(ALGS),$(BIN)/$(a)_omp_Ofastnative)
PG      := $(foreach a,$(ALGS),$(BIN)/$(a)_serial_pg)

.PHONY: all clean bench perfilado graficas

all: $(SERIAL) $(OMP) $(BARRIDO) $(OMPFAST) $(PG) $(BIN)/perfil

$(BIN):
	mkdir -p $(BIN)

$(BIN)/%_serial: $(SRC)/%_serial.c $(HDRS) | $(BIN)
	$(CC) $(FLAGS_O2) $(WARN) -o $@ $< $(LDLIBS)

$(BIN)/%_omp: $(SRC)/%_omp.c $(HDRS) | $(BIN)
	$(CC) $(FLAGS_O2) -fopenmp $(WARN) -o $@ $< $(LDLIBS)

# Barrido de banderas: bin/<alg>_serial_<flag>
define REGLA_FLAG
$(BIN)/%_serial_$(1): $(SRC)/%_serial.c $(HDRS) | $(BIN)
	$(CC) $(FLAGS_$(1)) $(WARN) -o $$@ $$< $(LDLIBS)
endef
$(foreach f,O0 O3 Ofastnative,$(eval $(call REGLA_FLAG,$(f))))

$(BIN)/%_omp_Ofastnative: $(SRC)/%_omp.c $(HDRS) | $(BIN)
	$(CC) $(FLAGS_Ofastnative) -fopenmp $(WARN) -o $@ $< $(LDLIBS)

# gprof: -pg -no-pie (sin -no-pie MinGW no toma muestras) y sin inlining
# para que cada nucleo y el generador aparezcan como funciones propias
$(BIN)/%_serial_pg: $(SRC)/%_serial.c $(HDRS) | $(BIN)
	$(CC) -O2 -pg -no-pie -fno-inline $(WARN) -o $@ $< $(LDLIBS)

$(BIN)/perfil: ../scripts/perfil.c | $(BIN)
	$(CC) -O2 -Wall -Wextra -std=c11 -o $@ $< -lpsapi

bench: all
	bash scripts/benchmark.sh

perfilado: all
	bash scripts/perfilado.sh

graficas:
	python scripts/graficas.py

clean:
	rm -rf $(BIN) gmon.out
