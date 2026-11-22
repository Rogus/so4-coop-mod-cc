#!/bin/bash
# Opcao de inicializacao da Steam para jogar o co-op (instalacao real):
#   "/caminho/para/so4-coop/tools/so4play.sh" %command%
# - carrega o so4coop (xinput1_3.dll) e o SO4Fix do Lyall (Ultimate ASI Loader renomeado para wininet.dll:
#   como winmm.dll o Proton carregava o winmm interno e o SO4Fix nunca rodava). O SO4Fix (2024) precisa do
#   msvcp140 >= 14.40; o prefixo tem 14.0 (crash em msvcp140+0x18c34), entao msvcp140/vcruntime140(_1) 14.44
#   ficam na pasta do jogo e sao forcados como nativos;
# - liga o teclado como controle virtual de Xbox 360 (kbpad.py) enquanto o jogo roda (4o jogador).
#   Os controles fisicos devem estar conectados ANTES de abrir o jogo, para o virtual ser o ultimo (controle 4).
DIR="$(cd "$(dirname "$0")" && pwd)"
LOG="$DIR/so4play.log"
export WINEDLLOVERRIDES="${WINEDLLOVERRIDES:+$WINEDLLOVERRIDES;}xinput1_3=n,b;wininet=n,b;msvcp140=n,b;vcruntime140=n,b;vcruntime140_1=n,b"
{ echo; echo "$(date -Is) so4play: $*"; } >> "$LOG"
# diagnostico: com o arquivo tools/proton_log presente, o Proton grava steam-609150.log em tools/
[ -e "$DIR/proton_log" ] && export PROTON_LOG=1 PROTON_LOG_DIR="$DIR"

if [ $# -eq 0 ]; then
    echo "uso (opcao de inicializacao da Steam): $0 %command%" >&2
    exit 1
fi

KB=
if [ "${SO4_KBPAD:-1}" != 0 ]; then
    /usr/bin/python3 -I "$DIR/kbpad.py" >> "$LOG" 2>&1 &
    KB=$!
    sleep 1                                   # o controle virtual aparece para a Steam antes do jogo
    kill -0 "$KB" 2>/dev/null || { echo "$(date -Is) so4play: kbpad nao iniciou (veja acima); jogo segue sem teclado-controle" >> "$LOG"; KB=; }
fi

"$@"
rc=$?
[ -n "$KB" ] && kill "$KB" 2>/dev/null && wait "$KB" 2>/dev/null
echo "$(date -Is) so4play: jogo fechou (codigo $rc)" >> "$LOG"
exit $rc
