"""
graficas.py - Graficas y tabla de speedup de la Entrega 2 (optimizacion).

Lee los CSV que generan los comandos de commands.txt (separador ';' y coma
decimal, como los espera Excel en espanol) y produce en la carpeta graficas/:

  1_compilador_niveles.png   GOP/s vs N para -O0, -O1, -O2, -O3, -Ofast, -Os
  2_compilador_flags.png     GOP/s vs N para -O3 y sus combinaciones de flags
  3_codigo_traspuesta.png    clasico vs ikj vs traspuesta en -O0/-O2/-O3/-Ofast
  4_comparacion_speedup.png  las 4 curvas: mejor compilador, mejor codigo,
                             mejor numero de hilos y mejor numero de procesos
  tabla_speedup.csv          tiempo, speedup y eficiencia de esas 4 curvas
  tabla_paralelizacion.csv   speedup y eficiencia de cada numero de hilos/procesos
  tabla_optimizacion.csv     mediana de tiempo/GOP/s/speedup de cada combinacion

Cada punto es la MEDIANA de los turnos medidos (robusta frente a turnos
perturbados por el sistema operativo). El speedup se calcula contra la misma
linea base que la paralelizacion: matmul.exe compilado con -O2 (orden i-k-j),
es decir resultados_secuencial.csv.

Uso (desde la raiz del repositorio):
    python scripts/graficas.py
"""

from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import pandas as pd

RAIZ = Path(__file__).resolve().parent.parent
SALIDA = RAIZ / "graficas"

# Paleta categorica en orden fijo (nunca ciclada).
COLORES = ["#2a78d6", "#eb6834", "#1baf7a", "#eda100",
           "#e87ba4", "#008300", "#6250d6", "#e34948"]

NIVELES = ["O0", "O1", "O2", "O3", "Ofast", "Os"]
COMBINACIONES = ["O3", "O3native", "O3unroll", "Ofastfull"]
ALGORITMOS = ["clasico", "ikj", "traspuesta"]
FLAGS_CODIGO = ["O0", "O2", "O3", "Ofast"]

ETIQUETA_FLAGS = {
    "O0": "-O0", "O1": "-O1", "O2": "-O2", "O3": "-O3", "Ofast": "-Ofast",
    "Os": "-Os",
    "O3native": "-O3 -march=native",
    "O3unroll": "-O3 -funroll-loops",
    "Ofastfull": "-Ofast -march=native -funroll-loops",
}
ETIQUETA_ALG = {"clasico": "clásico i-j-k (fila × columna)",
                "ikj": "i-k-j (original)",
                "traspuesta": "traspuesta (fila × fila)"}


def leer(nombre):
    df = pd.read_csv(RAIZ / nombre, sep=";", decimal=",", encoding="utf-8-sig")
    df.columns = [c.strip() for c in df.columns]
    return df.rename(columns={"Tiempo(s)": "Tiempo",
                              "Rendimiento(GOP/s)": "GOPS"})


def medianas(df, claves):
    return (df.groupby(claves + ["Orden"], as_index=False)[["Tiempo", "GOPS"]]
              .median())


def con_speedup(df, base):
    df = df.merge(base, on="Orden", how="inner")
    df["Speedup"] = df["TiempoBase"] / df["Tiempo"]
    return df


def mejor(df, clave):
    """Configuracion con mayor speedup medio sobre todos los tamanos."""
    return df.groupby(clave)["Speedup"].mean().idxmax()


def estilo(ax, titulo, ylabel):
    ax.set_title(titulo, loc="left", fontsize=11)
    ax.set_xlabel("Orden de la matriz (N)")
    ax.set_ylabel(ylabel)
    ax.grid(True, color="#e4e3df", linewidth=0.8)
    ax.set_axisbelow(True)
    ax.set_ylim(bottom=0)
    for lado in ("top", "right"):
        ax.spines[lado].set_visible(False)
    ax.spines["left"].set_color("#a3a29d")
    ax.spines["bottom"].set_color("#a3a29d")


def curva(ax, x, y, i, etiqueta):
    ax.plot(x, y, color=COLORES[i], linewidth=2, marker="o", markersize=5,
            markeredgecolor="white", markeredgewidth=1.2, label=etiqueta)


def grafica_lineas(df, clave, series, etiquetas, titulo, archivo):
    fig, ax = plt.subplots(figsize=(11, 5))
    for i, s in enumerate(series):
        d = df[df[clave] == s].sort_values("Orden")
        if not d.empty:
            curva(ax, d["Orden"], d["GOPS"], i, etiquetas[s])
    estilo(ax, titulo, "Rendimiento (GOP/s)")
    ax.legend(frameon=False, fontsize=9, loc="upper left", bbox_to_anchor=(1.01, 1))
    fig.tight_layout()
    fig.savefig(SALIDA / archivo, dpi=150)
    plt.close(fig)


def main():
    SALIDA.mkdir(exist_ok=True)

    base = medianas(leer("resultados_secuencial.csv"), [])
    base = base.rename(columns={"Tiempo": "TiempoBase"})[["Orden", "TiempoBase"]]

    opt = con_speedup(medianas(leer("resultados_optimizacion.csv"),
                               ["Flags", "Algoritmo"]), base)
    hil = con_speedup(medianas(leer("resultados_hilos.csv"), ["NumHilos"]), base)
    pro = con_speedup(medianas(leer("resultados_procesos.csv"),
                               ["NumProcesos"]), base)

    # --- 1 y 2: optimizacion por compilador (codigo original i-k-j) ---------
    ikj = opt[opt["Algoritmo"] == "ikj"]
    grafica_lineas(ikj, "Flags", NIVELES, ETIQUETA_FLAGS,
                   "Optimización por compilador: niveles -O (código i-k-j)",
                   "1_compilador_niveles.png")
    grafica_lineas(ikj, "Flags", COMBINACIONES, ETIQUETA_FLAGS,
                   "Optimización por compilador: flags adicionales (código i-k-j)",
                   "2_compilador_flags.png")

    # --- 3: optimizacion por codigo (traspuesta) ----------------------------
    fig, ejes = plt.subplots(2, 2, figsize=(11, 8), sharex=True)
    for ax, flag in zip(ejes.flat, FLAGS_CODIGO):
        for i, alg in enumerate(ALGORITMOS):
            d = opt[(opt["Flags"] == flag) & (opt["Algoritmo"] == alg)]
            d = d.sort_values("Orden")
            if not d.empty:
                curva(ax, d["Orden"], d["GOPS"], i, ETIQUETA_ALG[alg])
        estilo(ax, ETIQUETA_FLAGS[flag], "Rendimiento (GOP/s)")
    manejadores, etiquetas = ejes.flat[0].get_legend_handles_labels()
    fig.legend(manejadores, etiquetas, loc="upper left", ncol=3, frameon=False,
               fontsize=9, bbox_to_anchor=(0.01, 0.95))
    fig.suptitle("Optimización por código: multiplicación con traspuesta",
                 x=0.01, ha="left", fontsize=12)
    fig.tight_layout(rect=(0, 0, 1, 0.92))
    fig.savefig(SALIDA / "3_codigo_traspuesta.png", dpi=150)
    plt.close(fig)

    # --- 4: las cuatro mejores curvas ---------------------------------------
    f_comp = mejor(ikj, "Flags")
    tras = opt[opt["Algoritmo"] == "traspuesta"]
    f_cod = mejor(tras, "Flags")
    h = mejor(hil, "NumHilos")
    p = mejor(pro, "NumProcesos")

    curvas = [
        (f"Compilador: {ETIQUETA_FLAGS[f_comp]} (i-k-j)", ikj[ikj["Flags"] == f_comp], 1),
        (f"Código: traspuesta {ETIQUETA_FLAGS[f_cod]}", tras[tras["Flags"] == f_cod], 1),
        (f"Hilos: {h} hilos (-O2)", hil[hil["NumHilos"] == h], h),
        (f"Procesos: {p} procesos (-O2)", pro[pro["NumProcesos"] == p], p),
    ]

    fig, ax = plt.subplots(figsize=(11, 5))
    for i, (nombre, d, _) in enumerate(curvas):
        d = d.sort_values("Orden")
        curva(ax, d["Orden"], d["Speedup"], i, nombre)
    ax.axhline(1, color="#a3a29d", linewidth=1, linestyle="--")
    estilo(ax, "Speedup frente al secuencial (-O2, i-k-j): optimización vs paralelización",
           "Speedup (×)")
    ax.legend(frameon=False, fontsize=9, loc="upper left", bbox_to_anchor=(1.01, 1))
    fig.tight_layout()
    fig.savefig(SALIDA / "4_comparacion_speedup.png", dpi=150)
    plt.close(fig)

    # --- Tablas -------------------------------------------------------------
    tabla = base.rename(columns={"TiempoBase": "Secuencial -O2 (s)"})
    for nombre, d, nucleos in curvas:
        # Eficiencia = speedup / unidades de computo usadas. Las versiones
        # optimizadas corren en un solo nucleo, asi que su eficiencia es su
        # propio speedup.
        d = d[["Orden", "Tiempo", "Speedup"]].assign(
            Eficiencia=d["Speedup"] / nucleos).rename(
            columns={"Tiempo": f"{nombre} (s)", "Speedup": f"{nombre} speedup",
                     "Eficiencia": f"{nombre} eficiencia"})
        tabla = tabla.merge(d, on="Orden", how="left")
    tabla = tabla.sort_values("Orden")
    tabla.to_csv(SALIDA / "tabla_speedup.csv", sep=";", decimal=",",
                 index=False, float_format="%.4f", encoding="utf-8-sig")

    paral = pd.concat([
        hil.rename(columns={"NumHilos": "Unidades"}).assign(Version="hilos"),
        pro.rename(columns={"NumProcesos": "Unidades"}).assign(Version="procesos"),
    ])
    paral["Eficiencia"] = paral["Speedup"] / paral["Unidades"]
    paral = paral[["Version", "Unidades", "Orden", "Tiempo", "GOPS",
                   "Speedup", "Eficiencia"]]
    paral.sort_values(["Version", "Unidades", "Orden"]).to_csv(
        SALIDA / "tabla_paralelizacion.csv", sep=";", decimal=",", index=False,
        float_format="%.4f", encoding="utf-8-sig")

    opt.sort_values(["Algoritmo", "Flags", "Orden"]).to_csv(
        SALIDA / "tabla_optimizacion.csv", sep=";", decimal=",", index=False,
        float_format="%.4f", encoding="utf-8-sig")

    pd.set_option("display.width", 200)
    print("Tabla de speedup (mediana de los turnos):\n")
    print(tabla.to_string(index=False, float_format=lambda v: f"{v:.3f}"))
    print(f"\nGraficas y tablas escritas en {SALIDA}")


if __name__ == "__main__":
    main()
