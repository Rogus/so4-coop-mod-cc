# Estudo de viabilidade: co-op local no STAR OCEAN 4 - THE LAST HOPE 4K & Full HD Remaster (Steam)

## Fase 1 - reconhecimento estático (concluída)
- `StarOceanTheLastHope.exe`: x64 nativo (motor "Aska" da tri-Ace), DirectX 11, MSVC 2012 (msvcr110).
- Protegido: SteamStub (seção `.bind` com o ponto de entrada) + código cifrado em `.xpdata` (~68 MB),
  decifrado só em execução. Não removemos nem contornamos o DRM: o mod trabalha em tempo de
  execução, como o SO4Fix (que já altera código do jogo em execução sem problemas).
- Entrada: `XINPUT1_3.dll` carregado dinamicamente (LoadLibraryA) -> um `xinput1_3.dll` na pasta do
  jogo é carregado no lugar (no Proton: `WINEDLLOVERRIDES="xinput1_3=n,b" %command%`).
  Também há a API antiga `SteamController005`.
- RTTI legível (5815 tipos, 545 de batalha). Classes-chave:
  - `Battle::CPlayerEdge/Reimi/Faiz/Lymle/Backus/Arumat/Melicle/Muria/Sarah`
    -> herdam de `CPlayerThinkChr` -> `CPlayerChr` -> ... (a IA é uma camada do próprio personagem;
       o jogo já permite trocar o personagem controlado, então cada um tem código de jogador e de IA)
  - `Battle::CBtlPad` (Task do controle na batalha), `Aska::Pad/PadXe/WindowsPad` (controle; "Xe" = Xbox 360)
  - `Battle::CTacticsMode`, `CReplaceMember`, `CMemberBoard`, `CBattleMainTask`
- Vtables ficam em `.xdata` (não cifrada): o mapa tipo -> vtable funciona (validado em recon/rtti_static.py).

## Fase 2 - sonda em execução (em andamento)
`build/xinput1_3.dll` (src/probe.c + src/scan.c), só leitura:
- repassa o XInput ao do sistema e registra quais slots (0-3) o jogo lê e de onde no código;
- F9: acha na memória os personagens, CBtlPad, Pad e gerenciadores de batalha, registra os
  ponteiros entre eles e salva a memória de cada um em so4coop_dumps/ para comparar;
- F10: marca no log.

Objetivo: achar (1) a variável "personagem controlado" (comparando F9 com Edge e F9 após trocar
de personagem), (2) se o Aska::Pad lê mais de um controle, (3) como o CBtlPad entrega comandos.

## Critério de viabilidade
Protótipo: o controle 2 move um aliado numa batalha enquanto o controle 1 move o líder.
