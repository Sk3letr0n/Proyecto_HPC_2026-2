/* ============================================================================
 *  nucleos.h - Nucleos (una muestra) de los dos metodos de Monte Carlo
 *  Proyecto HPC 2026-2 - Reto 2
 *
 *  Se comparten entre la version serial y la version OpenMP para que ambas
 *  ejecuten EXACTAMENTE el mismo calculo por muestra: el speedup medido es
 *  entonces atribuible solo al paralelismo.
 *
 *  ---------------------------------------------------------------------------
 *  DARTBOARD (tablero de dardos)
 *    Se lanzan dardos uniformes en el cuadrado [0,1) x [0,1). La fraccion que
 *    cae dentro del cuarto de circulo x^2 + y^2 <= 1 tiende a (pi/4).
 *        pi ~= 4 * dentro / N
 *    Coste por muestra: 2 numeros aleatorios, 2 mult, 1 suma, 1 comparacion.
 *
 *  BUFFON'S NEEDLES (agujas de Buffon)
 *    Agujas de longitud l sobre un piso con lineas paralelas separadas t
 *    (aguja corta, l <= t). Con x = distancia del centro de la aguja a la
 *    linea mas cercana ~ U[0, t/2] y theta = angulo ~ U[0, pi/2], la aguja
 *    cruza una linea si  x <= (l/2) sin(theta).  P(cruce) = 2l / (t pi).
 *    Aqui l = 1, t = 2  =>  P = 1/pi   =>   pi ~= N / cruces
 *    Coste por muestra (version clasica): 2 aleatorios + sin(). En MinGW-w64
 *    sin() es __sinl_internal (x87, long double): ~50 ns = ~200 ciclos.
 * ========================================================================== */
#ifndef NUCLEOS_H
#define NUCLEOS_H

#include "comun.h"

/* ------------------------------ DARTBOARD -------------------------------- */

/* Clasico con libc rand(): 15 bits por coordenada en MinGW (RAND_MAX=32767). */
static inline int dardo_rand(void) {
    double x = rand() / ((double)RAND_MAX + 1.0);
    double y = rand() / ((double)RAND_MAX + 1.0);
    return x * x + y * y <= 1.0;
}

/* Clasico en coma flotante con xoshiro256**: 2 llamadas al generador. */
static inline int dardo_double(xoshiro_t *r) {
    double x = xo_double(r);
    double y = xo_double(r);
    return x * x + y * y <= 1.0;
}

/* OPTIMIZADO (CPU): un solo numero de 64 bits da las DOS coordenadas
 * (31 bits cada una) y la prueba se hace en aritmetica entera:
 *     x, y en [0, 2^31)  ->  x^2 + y^2 < 2^63 (no desborda uint64)
 *     dentro  <=>  x^2 + y^2 < (2^31)^2 = 2^62
 * Ventajas: la mitad de llamadas al generador, sin conversiones int->double
 * y sin unidades de coma flotante. 31 bits >> 15 bits de rand().          */
static inline int dardo_entero(xoshiro_t *r) {
    uint64_t v = xo_siguiente(r);
    uint64_t x = v >> 33;               /* 31 bits altos  */
    uint64_t y = v & 0x7FFFFFFFULL;     /* 31 bits bajos  */
    return x * x + y * y < (UINT64_C(1) << 62);
}

/* ------------------------------- NEEDLES --------------------------------- */

/* Clasico con libc rand() + sin(). Usa pi para muestrear el angulo. */
static inline int aguja_rand(void) {
    double x     = rand() / ((double)RAND_MAX + 1.0);              /* U[0,1)    */
    double theta = rand() / ((double)RAND_MAX + 1.0) * (M_PI / 2); /* U[0,pi/2) */
    return x <= 0.5 * sin(theta);
}

/* Clasico con xoshiro256** + sin(). */
static inline int aguja_sin(xoshiro_t *r) {
    double x     = xo_double(r);
    double theta = xo_double(r) * (M_PI / 2);
    return x <= 0.5 * sin(theta);
}

/* OPTIMIZADO (CPU) y sin "trampa": no usa pi ni sin() ni sqrt().
 * El angulo se obtiene de un vector (u,v) uniforme dentro del cuarto de
 * circulo (muestreo por rechazo). Su direccion es uniforme en [0, pi/2] y
 *     sin(theta) = v / sqrt(u^2 + v^2)
 * La condicion x <= 0.5 * v / sqrt(u^2+v^2), con todo >= 0, se eleva al
 * cuadrado y queda sin raiz:
 *     x^2 * (u^2 + v^2) <= 0.25 * v^2
 * Coste: ~1.27 pares (u,v) por aguja (tasa de aceptacion pi/4) + mults,
 * frente a una llamada a sin() de decenas de ciclos.                      */
static inline int aguja_rechazo(xoshiro_t *r) {
    double u, v, d;
    do {
        u = xo_double(r);
        v = xo_double(r);
        d = u * u + v * v;
    } while (d > 1.0 || d == 0.0);
    double x = xo_double(r);
    return x * x * d <= 0.25 * v * v;
}

#endif /* NUCLEOS_H */
