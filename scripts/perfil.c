/*
 * perfil.c - Perfilado de CPU y memoria de un programa (solo Windows).
 *
 * Lanza el comando indicado dentro de un Job Object, espera a que termine y
 * mide sobre TODO el arbol de procesos (importante para mmPROCESOS, cuyos
 * procesos hijos heredan el Job):
 *
 *   Pared(s)          tiempo real transcurrido (de lanzar a terminar)
 *   CPUUsuario(s)     tiempo de CPU en modo usuario, sumado sobre todos los
 *                     hilos y procesos
 *   CPUKernel(s)      tiempo de CPU en modo kernel (reservas, creacion de
 *                     hilos/procesos, fallos de pagina...)
 *   UsoCPU(nucleos)   (usuario + kernel) / pared: cuantos nucleos estuvieron
 *                     ocupados en promedio. 1,0 = un nucleo entero.
 *   MemoriaPico(MiB)  pico de memoria comprometida (commit) del Job entero
 *   WorkingSet(MiB)   pico de memoria fisica (working set) del proceso
 *                     principal
 *   FallosPagina      fallos de pagina del proceso principal
 *
 * Se mide el programa completo (reserva, llenado aleatorio, multiplicacion y
 * liberacion), no solo la region que cronometran los propios programas.
 * La salida estandar del programa medido se descarta; perfil imprime una
 * unica linea CSV (separador ';' y coma decimal, como los demas CSV):
 *
 *   Pared(s);CPUUsuario(s);CPUKernel(s);UsoCPU(nucleos);MemoriaPico(MiB);WorkingSet(MiB);FallosPagina
 *
 * Compilacion:
 *   gcc -O2 -Wall -Wextra -std=c11 -o bin/perfil scripts/perfil.c -lpsapi
 *
 * Uso:
 *   perfil.exe <programa> [argumentos...]
 *   perfil.exe bin\mmHILOS.exe -n 1000 -l 100 -t 8 -c
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#include <stdio.h>
#include <string.h>

#define MIB (1024.0 * 1024.0)

/* FILETIME / LARGE_INTEGER de 100 ns a segundos. */
static double a_segundos(LONGLONG t100ns)
{
    return (double)t100ns / 1e7;
}

/* Imprime un double con coma decimal. */
static void imprimir(double v, int decimales, const char *sep)
{
    char buf[64];
    snprintf(buf, sizeof buf, "%.*f", decimales, v);
    for (char *p = buf; *p; p++)
        if (*p == '.')
            *p = ',';
    printf("%s%s", buf, sep);
}

/* Agrega un argumento a la linea de comandos, entre comillas si hace falta. */
static int agregar_arg(char *dst, size_t cap, const char *arg)
{
    size_t len = strlen(dst);
    int comillas = strchr(arg, ' ') != NULL || arg[0] == '\0';
    int n = snprintf(dst + len, cap - len, "%s%s%s%s", len ? " " : "",
                     comillas ? "\"" : "", arg, comillas ? "\"" : "");
    return n > 0 && (size_t)n < cap - len;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "Uso: %s <programa> [argumentos...]\n", argv[0]);
        return 1;
    }

    char cmd[32768] = "";
    for (int i = 1; i < argc; i++) {
        if (!agregar_arg(cmd, sizeof cmd, argv[i])) {
            fprintf(stderr, "Error: linea de comandos demasiado larga.\n");
            return 1;
        }
    }

    HANDLE job = CreateJobObjectA(NULL, NULL);
    if (job == NULL) {
        fprintf(stderr, "Error: CreateJobObject (%lu)\n", GetLastError());
        return 1;
    }

    /* La salida del programa medido se manda a NUL. */
    SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, TRUE };
    HANDLE nul = CreateFileA("NUL", GENERIC_WRITE, FILE_SHARE_WRITE, &sa,
                             OPEN_EXISTING, 0, NULL);

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof si);
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = nul;
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);

    LARGE_INTEGER frec, t0, t1;
    QueryPerformanceFrequency(&frec);

    /* Se crea suspendido para meterlo en el Job antes de que ejecute nada:
     * asi todos sus hijos quedan dentro del Job desde el principio. */
    QueryPerformanceCounter(&t0);
    if (!CreateProcessA(NULL, cmd, NULL, NULL, TRUE, CREATE_SUSPENDED, NULL,
                        NULL, &si, &pi)) {
        fprintf(stderr, "Error: no se pudo lanzar '%s' (%lu)\n", cmd,
                GetLastError());
        return 1;
    }
    if (!AssignProcessToJobObject(job, pi.hProcess)) {
        fprintf(stderr, "Error: AssignProcessToJobObject (%lu)\n",
                GetLastError());
        TerminateProcess(pi.hProcess, 1);
        return 1;
    }
    ResumeThread(pi.hThread);
    WaitForSingleObject(pi.hProcess, INFINITE);
    QueryPerformanceCounter(&t1);

    DWORD codigo = 0;
    GetExitCodeProcess(pi.hProcess, &codigo);

    JOBOBJECT_BASIC_ACCOUNTING_INFORMATION cuenta;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limites;
    PROCESS_MEMORY_COUNTERS mem;
    ZeroMemory(&cuenta, sizeof cuenta);
    ZeroMemory(&limites, sizeof limites);
    ZeroMemory(&mem, sizeof mem);
    QueryInformationJobObject(job, JobObjectBasicAccountingInformation,
                              &cuenta, sizeof cuenta, NULL);
    QueryInformationJobObject(job, JobObjectExtendedLimitInformation,
                              &limites, sizeof limites, NULL);
    GetProcessMemoryInfo(pi.hProcess, &mem, sizeof mem);

    double pared = (double)(t1.QuadPart - t0.QuadPart) / (double)frec.QuadPart;
    double usuario = a_segundos(cuenta.TotalUserTime.QuadPart);
    double kernel = a_segundos(cuenta.TotalKernelTime.QuadPart);

    imprimir(pared, 6, ";");
    imprimir(usuario, 6, ";");
    imprimir(kernel, 6, ";");
    imprimir((usuario + kernel) / pared, 3, ";");
    imprimir((double)limites.PeakJobMemoryUsed / MIB, 2, ";");
    imprimir((double)mem.PeakWorkingSetSize / MIB, 2, ";");
    printf("%lu\n", (unsigned long)mem.PageFaultCount);

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    CloseHandle(nul);
    CloseHandle(job);

    if (codigo != 0)
        fprintf(stderr, "Aviso: el programa termino con codigo %lu\n",
                (unsigned long)codigo);
    return (int)codigo;
}
