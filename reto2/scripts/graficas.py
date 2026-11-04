"""
graficas.py - Graficas y tablas del Reto 2 (PI por Monte Carlo).

Lee los CSV de datos/ (separador ';' y coma decimal) generados por
scripts/benchmark.sh y scripts/perfilado.sh, y escribe en graficas/:

  1_serial_variantes.png   muestras/s de cada variante serial (CPU y memoria)
  2_compilador.png         efecto de las banderas -O0/-O2/-O3/-Ofast
  3_speedup.png            speedup y eficiencia vs hilos (escalado fuerte)
  4_variantes_omp.png      las 7 estrategias OpenMP vs hilos
  5_schedule.png           politica de reparto con 12 hilos
  6_calidad.png            dispersion de pi en 20 semillas (rand vs xoshiro)
  7_convergencia.png       error RMS vs N (ley 1/sqrt(N))
  tabla_*.csv              las mismas cifras en forma de tabla

Cada punto de tiempo es la MEDIANA de las repeticiones.

Uso (desde reto2/):   python scripts/graficas.py
"""
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.ticker
import numpy as np
import pandas as pd

RAIZ = Path(__file__).resolve().parent.parent
DATOS = RAIZ / "datos"
SALIDA = RAIZ / "graficas"
SALIDA.mkdir(exist_ok=True)

# Paleta categorica del repositorio, en orden fijo (validada para CVD).
C = ["#2a78d6", "#eb6834", "#1baf7a", "#eda100", "#e87ba4", "#008300", "#6250d6"]
GRIS = "#8a8a86"
TINTA = "#2b2b29"
PI = np.pi

plt.rcParams.update({
    "figure.dpi": 110, "savefig.dpi": 140, "font.size": 10,
    "axes.spines.top": False, "axes.spines.right": False,
    "axes.edgecolor": GRIS, "axes.labelcolor": TINTA, "text.color": TINTA,
    "xtick.color": TINTA, "ytick.color": TINTA,
    "axes.grid": True, "grid.color": "#e4e4e0", "grid.linewidth": 0.8,
    "axes.axisbelow": True, "legend.frameon": False, "lines.linewidth": 2,
})

ALG = {"dartboard": "Dartboard", "needles": "Buffon's Needles"}


def leer(nombre):
    df = pd.read_csv(DATOS / f"{nombre}.csv", sep=";", decimal=",")
    df["alg"] = df["programa"].str.split("_").str[0]
    df["tipo"] = df["programa"].str.split("_").str[1]
    df["Mmps"] = df["N"] / df["tiempo_s"] / 1e6     # millones de muestras/s
    return df


def mediana(df, claves):
    return (df.groupby(claves, sort=False)
              .agg(tiempo_s=("tiempo_s", "median"), Mmps=("Mmps", "median"),
                   N=("N", "first"), pi=("pi", "first"), error_abs=("error_abs", "first"))
              .reset_index())


def guardar(fig, nombre):
    fig.tight_layout()
    fig.savefig(SALIDA / nombre, bbox_inches="tight")
    plt.close(fig)
    print("  ", nombre)


def tabla(df, nombre):
    df.to_csv(SALIDA / nombre, sep=";", decimal=",", index=False, float_format="%.4f")
    print("  ", nombre)


def barras_h(ax, etiquetas, valores, colores, fmt="{:.0f}"):
    y = np.arange(len(etiquetas))
    ax.barh(y, valores, color=colores, height=0.62, edgecolor="white", linewidth=2)
    ax.set_yticks(y, etiquetas)
    ax.invert_yaxis()
    ax.grid(axis="y", visible=False)
    vmax = max(valores)
    for yi, v in zip(y, valores):
        ax.text(v + vmax * 0.01, yi, fmt.format(v), va="center", fontsize=9)
    ax.set_xlim(0, vmax * 1.18)


# ---------------------------------------------------------------- 1. serial
def g1():
    df = mediana(leer("E1_serial"), ["alg", "variante"])
    fig, axs = plt.subplots(1, 2, figsize=(11, 3.6))
    for ax, a in zip(axs, ALG):
        d = df[df.alg == a]
        base = d[d.variante == "rand"].Mmps.iloc[0]
        etiquetas = [f"{v}  (×{m / base:.1f})" for v, m in zip(d.variante, d.Mmps)]
        barras_h(ax, etiquetas, d.Mmps.tolist(), C[:len(d)])
        ax.set_title(f"{ALG[a]} serial (-O2)", loc="left", fontweight="bold")
        ax.set_xlabel("millones de muestras / s  (mayor es mejor)")
    guardar(fig, "1_serial_variantes.png")
    df["ns_por_muestra"] = 1e3 / df.Mmps
    tabla(df[["alg", "variante", "N", "tiempo_s", "Mmps", "ns_por_muestra", "error_abs"]],
          "tabla_serial.csv")


# ---------------------------------------------------------------- 2. compilador
def g2():
    df = mediana(leer("E2_compilador"), ["alg", "variante", "config"])
    flags = ["O0", "O2", "O3", "Ofastnative"]
    etq = {"O0": "-O0", "O2": "-O2", "O3": "-O3", "Ofastnative": "-Ofast -march=native"}
    fig, axs = plt.subplots(1, 2, figsize=(11, 3.8))
    for ax, a in zip(axs, ALG):
        d = df[df.alg == a]
        vars_ = list(dict.fromkeys(d.variante))
        x = np.arange(len(vars_)); w = 0.2
        for i, fl in enumerate(flags):
            vals = [d[(d.variante == v) & (d.config == fl)].Mmps.iloc[0] for v in vars_]
            ax.bar(x + (i - 1.5) * w, vals, w, color=C[i], label=etq[fl],
                   edgecolor="white", linewidth=1.5)
        ax.set_xticks(x, vars_)
        ax.grid(axis="x", visible=False)
        ax.set_title(f"{ALG[a]} serial", loc="left", fontweight="bold")
        ax.set_ylabel("millones de muestras / s")
    axs[0].legend(ncols=4, loc="upper left", bbox_to_anchor=(0, -0.12))
    guardar(fig, "2_compilador.png")
    t = df.pivot_table(index=["alg", "variante"], columns="config", values="Mmps").reset_index()
    tabla(t, "tabla_compilador.csv")


# ---------------------------------------------------------------- 3. speedup
def g3():
    df = leer("E3_escalado")
    ser = mediana(df[df.tipo == "serial"], ["alg", "variante"])
    omp = mediana(df[df.tipo == "omp"], ["alg", "variante", "hilos"])
    nucleo = {("dartboard", "reduction"): "double", ("dartboard", "opt"): "entero",
              ("needles", "reduction"): "sin", ("needles", "opt"): "rechazo"}
    filas = []
    for (a, v), sv in nucleo.items():
        t1 = ser[(ser.alg == a) & (ser.variante == sv)].tiempo_s.iloc[0]
        d = omp[(omp.alg == a) & (omp.variante == v)].copy()
        d["serial_s"] = t1
        d["speedup"] = t1 / d.tiempo_s
        d["eficiencia"] = d.speedup / d.hilos
        d["serie"] = f"{ALG[a]} · {v}"
        filas.append(d)
    r = pd.concat(filas)
    fig, axs = plt.subplots(1, 2, figsize=(11, 4))
    hmax = r.hilos.max()
    for i, (s, d) in enumerate(r.groupby("serie", sort=False)):
        axs[0].plot(d.hilos, d.speedup, marker="o", ms=6, color=C[i], label=s)
        axs[1].plot(d.hilos, d.eficiencia, marker="o", ms=6, color=C[i], label=s)
    axs[0].plot([1, hmax], [1, hmax], ls="--", color=GRIS, lw=1, label="ideal")
    axs[1].axhline(1, ls="--", color=GRIS, lw=1)
    for ax in axs:
        ax.axvline(6, color=GRIS, lw=1, ls=":")
        ax.set_xlabel("hilos OpenMP")
        ax.set_xticks(sorted(r.hilos.unique()))
    axs[0].text(6.15, 0.6, "6 núcleos físicos\n(a la derecha: SMT)", fontsize=8, color=GRIS)
    axs[0].set_ylabel("speedup  (T serial / T paralelo)")
    axs[1].set_ylabel("eficiencia  (speedup / hilos)")
    axs[1].set_ylim(0, 1.15)
    axs[0].set_title("Speedup (escalado fuerte)", loc="left", fontweight="bold")
    axs[1].set_title("Eficiencia", loc="left", fontweight="bold")
    axs[0].legend(loc="upper left", fontsize=8)
    guardar(fig, "3_speedup.png")
    tabla(r[["alg", "variante", "hilos", "N", "serial_s", "tiempo_s", "speedup", "eficiencia"]],
          "tabla_speedup.csv")


# ---------------------------------------------------------------- 4. variantes omp
def g4():
    df = mediana(leer("E4_variantes_omp"), ["alg", "variante", "hilos"])
    orden = ["critical", "atomic", "rand", "falso", "padding", "reduction", "opt"]
    fig, axs = plt.subplots(1, 2, figsize=(11, 4.2), sharey=False)
    for ax, a in zip(axs, ALG):
        for i, v in enumerate(orden):
            d = df[(df.alg == a) & (df.variante == v)]
            ax.plot(d.hilos, d.Mmps, marker="o", ms=6, color=C[i], label=v)
        ax.set_yscale("log")
        ax.set_xticks([1, 2, 4, 6, 12])
        ax.set_xlabel("hilos OpenMP")
        ax.set_ylabel("millones de muestras / s  (log)")
        ax.set_title(ALG[a], loc="left", fontweight="bold")
    axs[0].legend(ncols=7, loc="upper left", bbox_to_anchor=(0, -0.14), fontsize=8)
    guardar(fig, "4_variantes_omp.png")
    t = df.pivot_table(index=["alg", "variante"], columns="hilos", values="tiempo_s").reset_index()
    tabla(t, "tabla_variantes_omp.csv")


# ---------------------------------------------------------------- 5. schedule
def g5():
    df = mediana(leer("E5_schedule"), ["alg", "config"])
    fig, axs = plt.subplots(1, 2, figsize=(11, 3.4))
    for ax, a in zip(axs, ALG):
        d = df[df.alg == a]
        barras_h(ax, d.config.tolist(), d.tiempo_s.tolist(), [C[0]] * len(d), "{:.3f} s")
        ax.set_xlabel("tiempo (s)  (menor es mejor)")
        ax.set_title(f"{ALG[a]} · reduction · 12 hilos", loc="left", fontweight="bold")
    guardar(fig, "5_schedule.png")
    tabla(df[["alg", "config", "N", "tiempo_s", "Mmps"]], "tabla_schedule.csv")


# ---------------------------------------------------------------- 6. calidad
def g6():
    df = leer("E6_calidad")
    df["serie"] = df.tipo + " · " + df.variante
    orden = ["serial · rand", "omp · rand", "omp · reduction", "omp · opt"]
    fig, axs = plt.subplots(1, 2, figsize=(11, 3.6))
    resumen = []
    for ax, a in zip(axs, ALG):
        for i, s in enumerate(orden):
            d = df[(df.alg == a) & (df.serie == s)]
            jit = (np.random.default_rng(i).random(len(d)) - 0.5) * 0.25
            ax.scatter(d.pi, np.full(len(d), i) + jit, s=28, color=C[i],
                       edgecolor="white", linewidth=1, zorder=3)
            resumen.append({"alg": a, "serie": s, "N": d.N.iloc[0],
                            "media_pi": d.pi.mean(), "desv_std": d.pi.std(ddof=1),
                            "sesgo": d.pi.mean() - PI,
                            "rmse": np.sqrt(((d.pi - PI) ** 2).mean())})
        ax.axvline(PI, color=TINTA, lw=1)
        ax.set_yticks(range(len(orden)), orden)
        ax.invert_yaxis()
        ax.grid(axis="y", visible=False)
        ax.xaxis.set_major_locator(matplotlib.ticker.MaxNLocator(5))
        ax.set_xlabel("π estimado (20 semillas, N = 10⁷)")
        ax.set_title(ALG[a], loc="left", fontweight="bold")
    guardar(fig, "6_calidad.png")
    tabla(pd.DataFrame(resumen), "tabla_calidad.csv")


# ---------------------------------------------------------------- 7. convergencia
def g7():
    df = leer("E7_convergencia")
    df["serie"] = df.alg.map(ALG) + " · " + df.variante
    g = (df.groupby(["serie", "N"]).pi.apply(lambda p: np.sqrt(((p - PI) ** 2).mean()))
           .rename("rmse").reset_index())
    fig, ax = plt.subplots(figsize=(7.5, 4.2))
    for i, (s, d) in enumerate(g.groupby("serie", sort=True)):
        ax.plot(d.N, d.rmse, marker="o", ms=6, color=C[i], label=s)
    n = np.array(sorted(g.N.unique()), dtype=float)
    # Desviaciones teoricas: dartboard 4*sqrt(p(1-p)/N), p = pi/4;
    # needles: pi^2 * sqrt(q(1-q)/N) con q = 1/pi (metodo delta).
    sd_d = 4 * np.sqrt((PI / 4) * (1 - PI / 4) / n)
    sd_n = PI ** 2 * np.sqrt((1 / PI) * (1 - 1 / PI) / n)
    ax.plot(n, sd_n, ls=":", color=GRIS, lw=1.2, label="teórico needles ≈ 2.37/√N")
    ax.plot(n, sd_d, ls="--", color=GRIS, lw=1.2, label="teórico dartboard ≈ 1.64/√N")
    ax.set_xscale("log"); ax.set_yscale("log")
    ax.set_xlabel("muestras N"); ax.set_ylabel("error RMS |π̂ − π| (10 semillas)")
    ax.set_title("Convergencia O(1/√N)", loc="left", fontweight="bold")
    ax.legend(fontsize=8)
    guardar(fig, "7_convergencia.png")
    tabla(g.pivot(index="N", columns="serie", values="rmse").reset_index(), "tabla_convergencia.csv")


# ---------------------------------------------------------------- 8. afinidad
def g8():
    p = DATOS / "E9_afinidad.csv"
    if not p.exists():
        return
    e9 = mediana(leer("E9_afinidad"), ["alg", "variante", "hilos", "config"])
    e3 = leer("E3_escalado")
    ser = mediana(e3[e3.tipo == "serial"], ["alg", "variante"])
    nucleo = {("dartboard", "reduction"): "double", ("dartboard", "opt"): "entero",
              ("needles", "reduction"): "sin", ("needles", "opt"): "rechazo"}
    # tiempo serial por muestra (E3) escalado al N de E9
    e9["serial_s"] = [ser[(ser.alg == a) & (ser.variante == nucleo[(a, v)])].iloc[0]
                      .pipe(lambda s: s.tiempo_s / s.N * n)
                      for a, v, n in zip(e9.alg, e9.variante, e9.N)]
    e9["speedup"] = e9.serial_s / e9.tiempo_s
    confs = [(6, "03F", "6 hilos en 3 núcleos (SMT)"), (6, "FFF", "6 hilos, sin afinidad"),
             (6, "555", "6 hilos, 1 por núcleo"), (12, "FFF", "12 hilos (6 núcleos × 2 SMT)")]
    series = [(a, v) for a in ALG for v in ("reduction", "opt")]
    fig, ax = plt.subplots(figsize=(10, 4))
    x = np.arange(len(series)); w = 0.2
    for i, (h, m, etq) in enumerate(confs):
        vals = [e9[(e9.alg == a) & (e9.variante == v) & (e9.hilos == h) & (e9.config == m)]
                .speedup.iloc[0] for a, v in series]
        b = ax.bar(x + (i - 1.5) * w, vals, w, color=C[i], label=etq, edgecolor="white", linewidth=1.5)
        ax.bar_label(b, fmt="%.1f", fontsize=8, padding=2)
    ax.axhline(6, ls="--", color=GRIS, lw=1)
    ax.text(2.5, 6.15, "6 = núcleos físicos", fontsize=8, color=GRIS, ha="center")
    ax.set_xticks(x, [f"{ALG[a]}\n{v}" for a, v in series])
    ax.grid(axis="x", visible=False)
    ax.set_ylabel("speedup vs serial")
    ax.set_title("Afinidad y SMT (máscara de CPU del proceso)", loc="left", fontweight="bold")
    ax.legend(ncols=2, fontsize=8, loc="upper left")
    ax.set_ylim(0, 12.5)
    guardar(fig, "8_afinidad.png")
    tabla(e9[["alg", "variante", "hilos", "config", "N", "tiempo_s", "serial_s", "speedup"]],
          "tabla_afinidad.csv")


# ---------------------------------------------------------------- tablas extra
def t_extra():
    p = DATOS / "perfilado.csv"
    if p.exists():
        df = pd.read_csv(p, sep=";", decimal=",")
        cols = [c for c in df.columns if "(" in c or c == "FallosPagina"]
        tabla(df.groupby(["programa", "variante", "hilos", "N"], sort=False)[cols]
                .median().reset_index(), "tabla_perfilado.csv")
    e8 = DATOS / "E8_mejor.csv"
    if e8.exists():
        tabla(mediana(leer("E8_mejor"), ["alg", "variante", "config"]), "tabla_mejor.csv")


if __name__ == "__main__":
    print("Generando en", SALIDA)
    for f in (g1, g2, g3, g4, g5, g6, g7, g8, t_extra):
        f()
