# ============================================================================
#  Makefile - Proyecto HPC 2026-2
#  Multiplicacion de matrices cuadradas de enteros
#
#  Uso en Windows (MSYS2 / MinGW):   mingw32-make
#  Uso en Linux / WSL / macOS:       make
# ============================================================================

CC       := gcc
CFLAGS   := -O2 -Wall -Wextra -Wpedantic -std=c11
LDFLAGS  := -lm

SRC_DIR  := src
BIN_DIR  := bin
TARGET   := $(BIN_DIR)/matmul
SOURCES  := $(SRC_DIR)/matmul.c

# Parametros por defecto para el target 'run' (se pueden sobrescribir):
#   mingw32-make run N=2000 L=100 S=1
N ?= 512
L ?= 100
S ?= 42

.PHONY: all run demo debug asan bench clean help

all: $(TARGET)

$(TARGET): $(SOURCES) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $(SOURCES) $(LDFLAGS)
	@echo "Compilado: $(TARGET)"

$(BIN_DIR):
	@mkdir -p $(BIN_DIR)

# Ejecucion parametrica normal
run: $(TARGET)
	./$(TARGET) -n $(N) -l $(L) -s $(S)

# Demostracion pequena: imprime las tres matrices
demo: $(TARGET)
	./$(TARGET) -n 5 -l 9 -s 42 -p

# Version con simbolos de depuracion, apta para gdb. Funciona en cualquier
# plataforma porque no depende de librerias externas.
debug: $(SOURCES) | $(BIN_DIR)
	$(CC) -O0 -g3 -Wall -Wextra -std=c11 -o $(BIN_DIR)/matmul_debug \
	      $(SOURCES) $(LDFLAGS)
	@echo "Compilado: $(BIN_DIR)/matmul_debug"

# Version con AddressSanitizer + UndefinedBehaviorSanitizer.
# Requiere libasan/libubsan: disponibles en Linux/WSL y en MSYS2 tras instalar
#   pacman -S mingw-w64-ucrt-x86_64-gcc-libs
# Detecta fugas de memoria y desbordamientos en tiempo de ejecucion.
asan: $(SOURCES) | $(BIN_DIR)
	$(CC) -O0 -g3 -Wall -Wextra -std=c11 -fsanitize=address,undefined \
	      -o $(BIN_DIR)/matmul_asan $(SOURCES) $(LDFLAGS)
	@echo "Compilado: $(BIN_DIR)/matmul_asan"

# Barrido de tamanos en formato CSV para graficar despues
bench: $(TARGET)
	@echo "n,limite,semilla,segundos"
	@for size in 64 128 256 512 1024; do ./$(TARGET) -n $$size -l $(L) -s $(S) -c; done

clean:
	@rm -rf $(BIN_DIR)
	@echo "Limpieza completada."

help:
	@echo "Targets disponibles:"
	@echo "  all     - compila el programa en $(TARGET)"
	@echo "  run     - ejecuta con N=$(N) L=$(L) S=$(S) (sobrescribibles)"
	@echo "  demo    - ejecucion 5x5 imprimiendo las matrices"
	@echo "  debug   - compila con -O0 -g3 para depurar con gdb"
	@echo "  asan    - compila con AddressSanitizer (requiere libasan)"
	@echo "  bench   - barrido de tamanos con salida CSV"
	@echo "  clean   - borra la carpeta $(BIN_DIR)"
