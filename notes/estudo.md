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

### Execução 1 (sonda v0.2) - notes/run1_v0.2.log
- A sonda carregou pelo override no registro do prefixo (AppDefaults\StarOceanTheLastHope.exe).
- O código real do jogo fica na seção `.data1` (flags RX); `.text` é minúscula.
- O jogo fez UMA chamada de XInput (game+0x3786cd) e não leu controles por XInput depois:
  com o Steam aberto, ele deve ler pela API SteamController005 (Steam Input).
- Erro da sonda: F9/F10 só eram lidos durante chamadas de XInputGetState -> nada foi capturado.
  Corrigido na v0.3 (thread própria para as teclas + registro da 1ª chamada de cada função).

### Execução 2 (v0.3) e 3 (v0.4)
- O jogo não lê controle por XInput com o Steam aberto (nenhum XInputGetState): usa
  `Aska::WindowsPad` (2 instâncias) alimentado pelo Steam Input.
- Grupo: lista ligada de personagens (`+0x8` anterior, `+0x10` próximo).
- `CBattleMainTask` (gerente da batalha): `+0x90` -> `CBtlPad`; `+0x410` -> objeto **X**.
  (O `+0x420` visto na execução 2 era a seleção do menu tático, não o controle.)
- **Personagem controlado** (execução 3: tática "Don't do anything" em todos, troca por L1/R1,
  sequência Edge,Edge,Edge,Sarah,Arumat,Myuria,Edge - batida exata):
  bloco no próprio personagem, zerado nos não controlados:
  - `+0x129a0` e `+0x12a20` -> X (o mesmo apontado por CBattleMainTask+0x410)
  - `+0x129c0` -> objeto **Y**;  `+0x129d8` = 120.0f
  - X e Y são recriados a cada troca (endereços mudam).
- Hipótese para o co-op: X é o "operador do jogador" ligado ao controle; o co-op precisaria de
  um X por jogador, cada um ligado a um controle diferente.
- v0.5: identifica a classe (RTTI) de X, Y e dos objetos para onde apontam, e os copia.
