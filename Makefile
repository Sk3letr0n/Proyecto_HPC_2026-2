# ============================================================================
#  Makefile - Proyecto HPC 2026-2
#  Multiplicacion de matrices cuadradas de enteros
#
#  Funciona en las tres consolas:
#    PowerShell / CMD      ->  mingw32-make
#    Git Bash / MSYS2      ->  mingw32-make
#    Linux / WSL / macOS   ->  make
# ============================================================================

CC       := gcc
CFLAGS   := -O2 -Wall -Wextra -Wpedantic -std=c11
LDFLAGS  := -lm

SRC_DIR  := src
BIN_DIR  := bin
TARGET   := $(BIN_DIR)/matmul
SOURCES  := $(SRC_DIR)/matmul.c

# ----------------------------------------------------------------------------
#  Deteccion de la consola que usara make.
#
#  GNU Make en Windows busca sh.exe en el PATH:
#    - si lo encuentra, SHELL contiene una ruta completa (con '/') y se pueden
#      usar comandos Unix;
#    - si NO lo encuentra (caso de PowerShell y CMD), SHELL queda como el valor
#      por defecto 'sh.exe' sin ruta y make ejecuta todo con cmd.exe.
#
#  Por eso se comprueba si SHELL contiene una barra: es la forma fiable de
#  saber si hay un shell tipo Unix disponible.
# ----------------------------------------------------------------------------
ifeq ($(findstring /,$(SHELL)),)
    # ---- Modo cmd.exe (PowerShell / CMD sin sh.exe en el PATH) ----
    CREAR_BIN  = if not exist "$(BIN_DIR)" mkdir "$(BIN_DIR)"
    BORRAR_BIN = if exist "$(BIN_DIR)" rmdir /s /q "$(BIN_DIR)"
    EXEC       = $(subst /,\,$(TARGET))
    EXEC_DEBUG = $(subst /,\,$(BIN_DIR)/matmul_debug)
    CONSOLA    = cmd.exe
else
    # ---- Modo shell Unix (Git Bash, MSYS2, Linux, WSL, macOS) ----
    CREAR_BIN  = mkdir -p $(BIN_DIR)
    BORRAR_BIN = rm -rf $(BIN_DIR)
    EXEC       = ./$(TARGET)
    EXEC_DEBUG = ./$(BIN_DIR)/matmul_debug
    CONSOLA    = $(SHELL)
endif

# Parametros por defecto para el target 'run'. Se pueden sobrescribir:
#   mingw32-make run N=2000 L=100 S=1
N ?= 512
L ?= 100
S ?= 42

.PHONY: all run demo debug asan bench clean help info

all: $(TARGET)

$(TARGET): $(SOURCES) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $(SOURCES) $(LDFLAGS)
	@echo Compilado: $(TARGET)

$(BIN_DIR):
	@$(CREAR_BIN)

# Ejecucion parametrica normal
run: $(TARGET)
	$(EXEC) -n $(N) -l $(L) -s $(S)

# Demostracion pequena: imprime las tres matrices
demo: $(TARGET)
	$(EXEC) -n 5 -l 9 -s 42 -p

# Version con simbolos de depuracion, apta para gdb. Funciona en cualquier
# plataforma porque no depende de librerias externas.
debug: $(SOURCES) | $(BIN_DIR)
	$(CC) -O0 -g3 -Wall -Wextra -std=c11 -o $(BIN_DIR)/matmul_debug \
	      $(SOURCES) $(LDFLAGS)
	@echo Compilado: $(BIN_DIR)/matmul_debug

# Version con AddressSanitizer + UndefinedBehaviorSanitizer.
# Requiere libasan/libubsan: disponibles en Linux/WSL y en MSYS2 tras instalar
#   pacman -S mingw-w64-ucrt-x86_64-gcc-libs
# Detecta fugas de memoria y desbordamientos en tiempo de ejecucion.
asan: $(SOURCES) | $(BIN_DIR)
	$(CC) -O0 -g3 -Wall -Wextra -std=c11 -fsanitize=address,undefined \
	      -o $(BIN_DIR)/matmul_asan $(SOURCES) $(LDFLAGS)
	@echo Compilado: $(BIN_DIR)/matmul_asan

# Barrido de tamanos en formato CSV para graficar despues.
# Se escriben las llamadas una por una en vez de usar un bucle, porque la
# sintaxis de bucle de sh y la de cmd.exe son incompatibles entre si.
bench: $(TARGET)
	@echo n,limite,semilla,segundos
	@$(EXEC) -n 64   -l $(L) -s $(S) -c
	@$(EXEC) -n 128  -l $(L) -s $(S) -c
	@$(EXEC) -n 256  -l $(L) -s $(S) -c
	@$(EXEC) -n 512  -l $(L) -s $(S) -c
	@$(EXEC) -n 1024 -l $(L) -s $(S) -c

clean:
	@$(BORRAR_BIN)
	@echo Limpieza completada.

# Muestra que consola detecto make. Util para diagnosticar problemas.
info:
	@echo Consola detectada : $(CONSOLA)
	@echo Ejecutable        : $(EXEC)
	@echo Compilador        : $(CC)

help:
	@echo Targets disponibles:
	@echo   all     - compila el programa en $(TARGET)
	@echo   run     - ejecuta con N=$(N) L=$(L) S=$(S), sobrescribibles
	@echo   demo    - ejecucion 5x5 imprimiendo las matrices
	@echo   debug   - compila con -O0 -g3 para depurar con gdb
	@echo   asan    - compila con AddressSanitizer, requiere libasan
	@echo   bench   - barrido de tamanos con salida CSV
	@echo   info    - muestra la consola detectada
	@echo   clean   - borra la carpeta $(BIN_DIR)
