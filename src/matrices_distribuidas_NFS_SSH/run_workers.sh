#!/usr/bin/env bash
set -euo pipefail

DIR="/mnt/matrices"
BIN="$DIR/matmul_dist"
N="${1:-}"
LIMIT="${2:-9}"
SEED="${3:-42}"

if [[ -z "$N" ]]; then
  echo "Uso: ./run_workers.sh N [LIMITE] [SEMILLA]"
  echo "Ejemplo: ./run_workers.sh 5 9 42"
  exit 1
fi

# Validacion sencilla de argumentos.
if ! [[ "$N" =~ ^[1-9][0-9]*$ && "$LIMIT" =~ ^[0-9]+$ && "$SEED" =~ ^[0-9]+$ ]]; then
  echo "N debe ser positivo; LIMITE y SEMILLA deben ser enteros no negativos."
  exit 1
fi

cd "$DIR"

echo "== 1. Generando matrices compartidas en NFS =="
"$BIN" init "$N" "$LIMIT" "$SEED" "$DIR"

# Elimina resultados anteriores para evitar mezclar ejecuciones.
rm -f C_wn1.txt C_wn2.txt C_wn3.txt worker_wn1.log worker_wn2.log worker_wn3.log C_final.txt

# Reparto de filas: [inicio, fin), sin solapamientos.
S1=0
E1=$((N / 3))
S2=$E1
E2=$((2 * N / 3))
S3=$E2
E3=$N

echo "== 2. Lanzando los tres workers por SSH =="
echo "WN1: filas [$S1, $E1)"
echo "WN2: filas [$S2, $E2)"
echo "WN3: filas [$S3, $E3)"

ssh -o BatchMode=yes cluster@172.31.2.152 \
  "cd $DIR && $BIN worker $DIR $S1 $E1 $DIR/C_wn1.txt" \
  > worker_wn1.log 2>&1 &
P1=$!

ssh -o BatchMode=yes cluster@172.31.7.1 \
  "cd $DIR && $BIN worker $DIR $S2 $E2 $DIR/C_wn2.txt" \
  > worker_wn2.log 2>&1 &
P2=$!

ssh -o BatchMode=yes cluster@172.31.5.134 \
  "cd $DIR && $BIN worker $DIR $S3 $E3 $DIR/C_wn3.txt" \
  > worker_wn3.log 2>&1 &
P3=$!

STATUS=0
wait "$P1" || STATUS=1
wait "$P2" || STATUS=1
wait "$P3" || STATUS=1

if [[ "$STATUS" -ne 0 ]]; then
  echo "ERROR: al menos un worker fallo. Registros:"
  for f in worker_wn1.log worker_wn2.log worker_wn3.log; do
    echo "----- $f -----"
    cat "$f" || true
  done
  exit 1
fi

echo "== 3. Resultado informado por cada worker =="
for f in worker_wn1.log worker_wn2.log worker_wn3.log; do
  echo "----- $f -----"
  cat "$f"
done

echo "== 4. Ensamblando la matriz C en HEAD =="
"$BIN" merge "$DIR" "$DIR/C_final.txt"

echo "== 5. Ejecucion distribuida terminada =="
echo "Resultado: $DIR/C_final.txt"
