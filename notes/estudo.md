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

### Execução 4 (v0.5)
- O objeto X do bloco do personagem controlado é `Battle::CCm146` (família de 60 câmeras CCmNNN):
  o bloco +0x129a0 é a **câmera** presa ao personagem controlado (+ juntas do esqueleto
  `JointObject`/`HierarchicalObject`). Bom indicador de "controlado", mas não é o caminho da entrada.
- `CBattleMainTask+0x410` desta vez continha lixo: não é um ponteiro confiável.
- Nenhum objeto já mapeado (gerente, CBtlPad, WindowsPad, táticas, alvo) aponta para o controlado.
- v0.6: procura em toda a memória gravável (inclusive globais do executável) os ponteiros para cada
  personagem do grupo e identifica a classe do objeto que os contém (s<NN>_refs.txt).

### Execução 5 (v0.6) - notes/run5_v0.6
- Edge (F9 #1) -> R1 -> Sarah (F9 #2): a troca foi registrada. Câmera desta batalha: `CCm103`;
  desta vez X (câmera) e Y **não** foram recriados na troca (mesmos endereços).
- Busca de ponteiros (analysis/diff_refs.py): 573/591 referências; quase tudo é
  `MessageDispatcherBlockForList+0x60` (pool de mensagens reciclado = ruído).
  Só 6 campos pulam Edge -> Sarah:
  - `CCameraManager+0x360`, `CCameraUserCtrl+0xc0` (câmera)
  - `CFaceMorphManager+0x540` e um campo no heap ao lado dele (expressão facial)
  - **`0x0000000000C2EC00` (heap baixo, sem RTTI)**: único candidato não explicado.
- Nada da cadeia de entrada (CBtlPad, PadTask, WindowsPad, CBattleMainTask, CTacticsMode)
  aponta para personagem. Os 8 KB de CBattleMainTask, CTacticsMode e CSelectTarget ficaram
  **idênticos** entre Edge e Sarah; CBtlPad/WindowsPad/PadTask só mudaram estado de
  botões/analógico e contadores.
- Conclusão: a entrada provavelmente escolhe o personagem por **índice** do grupo ou por um
  objeto fora dos já mapeados; busca por ponteiro não acha índice.
- Próximo (v0.7): breakpoint de escrita (registradores de depuração) no campo 0xC2EC00 e em
  CCameraUserCtrl+0xc0 para registrar a instrução que troca o personagem ao apertar R1 ->
  essa função é a "troca de personagem controlado", a alavanca real para o co-op.
- v0.7 (src/watch.c): após dois F9 com personagens diferentes, arma até 4 watchpoints de
  hardware (leitura/escrita, 8 bytes) nos campos que pularam para o novo personagem; um VEH
  registra cada instrução do jogo que lê/escreve neles e salva o código da função em
  so4coop_dumps/code_<rva>.bin. F11 = relatório e remove os watchpoints. Autoteste no Wine 11.22
  local: funciona (Dr7 lido de volta, DR6 preenchido). No Proton, o autoteste vai no início do log.

### Execução 6 (v0.7) - notes/run6_v0.7
- Autoteste no Proton 11.0-100: watchpoints de hardware **funcionam**, mas o DR6 vem vazio
  (atribuição só por mudança de valor; leituras ficam como "w?").
- 4 watchpoints armados em 61 threads (Dr7 = 0xBBBB0055, correto), **0 acessos** em 4 min.
- Causa: os "campos do heap sem classe" em 0xC2xxxx (inclusive o 0xC2EC00 da execução 5) são a
  **pilha da própria sonda**: as variáveis locais do F9 guardam o personagem controlado; três
  deles já tinham outro valor no momento de armar. Artefato da sonda, não do jogo.
- Sem esse artefato, só seguem a troca objetos de câmera (CCameraManager+0x360,
  CCameraUserCtrl+0xc0, CCameraAngleLock+0x190) e CFaceMorphManager+0x540.
- find_controlled.py na execução 3 (7 capturas): os 46 candidatos estão todos no bloco da
  câmera (+0x129a0..+0x12a24): o personagem **não** tem flag própria de "controlado".
- v0.8: ignora a pilha da sonda (registra o intervalo no log), vigia os campos de câmera
  (quem escreve neles na troca deve ser a rotina de troca de personagem), não rearma nos F9
  seguintes, limita leituras não atribuídas, e salva as variáveis globais do jogo a cada F9
  (procurar um índice do grupo: Edge 0, Sarah 1, Arumat 2...).

### Execução 7 (v0.8) - notes/run7_v0.8
- Pilha da sonda = 0xB30000-0xC30000: **confirmado** que os campos 0xC2xxxx eram a própria sonda.
- Capturas: #1 Edge, #2 no meio da troca (sem controlado), #3 Sarah, #4 Edge, F11, #5 Edge.
  Bug da v0.8: a #2 sem controlado apagou a referência anterior (corrigido na v0.9).
- Watchpoints em CCameraManager+0x360 e CCameraAngleLock+0x190: 0 acessos (sem troca depois de armar).
- F11 (remover watchpoints = suspender as 61 threads de novo) -> o jogo travou; a sonda continuou viva.
- **Globais (.arch, 7,4 MB)**: só um bloco é igual nas 3 capturas do Edge e diferente na da Sarah:
  - `game+0xf77ed2` Edge 2 / Sarah 3; `game+0xf77ed3` Edge 1 / Sarah 2; `game+0xf77f00` Edge 2 / Sarah 3
  - a captura #2 (no meio da troca) já tem os valores da Sarah: muda antes da câmera.
  - Forte candidato a "índice do membro controlado". Falta um 3º personagem para ver a numeração.
- v0.9: arma uma vez, no 1º F9 em batalha, watchpoints fixos: os dois globais, CBtlPad+0x1180
  (controle: muda sempre) e CBtlPad+0x28 (valores do analógico); F11 só relata, não desarma.

### Execução 8 (v0.9) - notes/run8_v0.9
- O log da execução 8 foi **sobrescrito** pela inicialização seguinte (tela branca); só restaram os
  dumps (2 capturas, 01:12:04 e 01:12:08) -> provável travamento logo após armar os watchpoints.
- Nas duas capturas a Sarah é a controlada, mas o bloco global tem `02 01` (os valores do Edge na
  execução 7): o número **não é o personagem**, parece a posição do controlado na ordem do grupo
  desta batalha (líder = 1 em +0xed3). +0xf00 caiu para 0 na 2ª captura: é outra coisa.
- Início com tela branca (01:15): a sonda carregou, autoteste dos watchpoints ok, chamadas XInput,
  e o log para (nenhum heartbeat de 30 s).
- Watchpoints de hardware no Proton = 2 (talvez 3) travamentos: **desligados na v0.10**.
- v0.10: log em modo acréscimo com cabeçalho de sessão; so4coop_dumps anterior renomeado;
  busca estática no código decifrado (deslocamentos RIP-relativos e endereços absolutos) de
  todas as instruções que apontam para game+0xf77e80..0xf77f40, com dump das funções.
- Som: PulseAudio e PipeWire/WirePlumber rodando juntos; o WirePlumber detém a reserva
  (org.freedesktop.ReserveDevice1) das 4 placas, então as saídas do PulseAudio (que o Proton usa)
  ficam suspensas. Não é problema do jogo nem da sonda.

### Execução 9 (v0.10) - notes/run9_v0.10
- Opções de inicialização: com `WINEDLLOVERRIDES=xinput1_3=n,b` o jogo parava antes do título
  (tela cinza + 1280x720 preto, threads dormindo, sem diálogo). Com só `DXVK_FRAME_RATE=60 %command%`
  iniciou normal. O override do registro do prefixo basta para carregar a sonda.
- Som: resolvido trocando PulseAudio por pipewire-pulse (o WirePlumber detinha as placas).
  analysis/live_check.sh mediu o áudio do jogo em HDMI 2: picos de -17 a -0,2 dBFS (som real).
- Edge (#1) -> Arumat (#2): o bloco global ficou `02 01 / 02` nos dois -> **não** é o membro
  controlado (a coincidência da execução 7 acompanhava outra coisa). Hipótese descartada.
- 14 referências estáticas ao intervalo; 10 leem game+0xf77ea8 = ponteiro global para um alocador
  (alinha endereço e chama métodos +0x58/+0x70 do objeto em +0x2a8). Sem relação com o controle.
- Fato firme que resta: só o personagem controlado tem o bloco da câmera em +0x129a0.
  v0.11: salva a seção de código inteira uma vez (68 MB, fica no disco, nunca no git) para buscar
  offline quem escreve +0x129a0 e subir pelos chamadores até a rotina de troca de personagem.

### Execução 10 (v0.11) - notes/run10_v0.11
- Seção de código salva: game+0x013a2000-0x05829932 (71,8 MB, 0 páginas ilegíveis), confere byte a
  byte com a função salva na execução 9. Fica só no disco (code_full_013a2000.bin), nunca no git.
- analysis/static_xref.py: busca offline de deslocamentos (disp) e de chamadas diretas (E8),
  com limites de função pelo .pdata do exe (não cifrado) e disassembly pelo objdump.
- **Nenhuma instrução usa 0x129a0/0x129c0/0x12a20** como deslocamento a partir do personagem; os
  20 achados vizinhos (0x12984, 0x12998...) são falsos (bytes de saltos ou dados).
- O bloco da câmera é um registro sem vtable em chr+0x12998 (1º campo = dono), logo antes de um
  CFaceMorphManager embutido (vtable em chr+0x12a28). Só o sistema de câmera mexe nele, por ponteiro.
  Sintoma da troca, não o mecanismo. Ele também some poucos segundos depois (dump do Arumat = 0).
- Classes de interface que precisam saber quem é controlado: CArrowCursorManager,
  CMinimapPlayerMarkerElement, CMemberBoardChangeArrowElement. Nenhuma aparece nas buscas de
  ponteiro -> o controlado deve estar guardado como **índice**.
- Próximo: v0.12 com o mapa RTTI de todas as classes Battle:: (não só 24), dump de todas as
  instâncias por F9 e diff de campos inteiros que sigam a sequência Edge, Sarah, Arumat, Edge, Sarah.

### Execução 11 (v0.12) - notes/run11_v0.12
- 542 classes Battle::, ~1210 instâncias por F9 (4,8 MB, < 1 s). Capturas: Edge, Arumat, Edge.
- diff_battle.py (mín. 2 personagens): 4458 campos candidatos; com valores pequenos (0-7) e o
  mesmo mapeamento em objetos independentes, sobram dois:
  - `CBattleMainTask+0x3c4` Edge 1 / Arumat 3 (logo após +0x3c0 = 3 constante)
  - `CStatusBoardWindow+0x1f0` Edge 1 / Arumat 3, seguido da posição do destaque no painel:
    x = 353 (Edge) / 919 (Arumat), y = 544 -> 283 px por posição: **posição no grupo, base 1**
    (Edge 1, Sarah 2, Arumat 3, Myuria 4). Previsão: Sarah 2 / x 636, Myuria 4 / x 1202.
- Ponteiros globais para o CBattleMainTask: game+0xf76940 e game+0x1051840 (sem acesso
  RIP-relativo direto no código).
- O único acesso com deslocamento 0x3c4 (game+0x3704a30) grava a constante 2 num objeto passado
  como argumento (outra classe?). Partes do código têm lixo anti-disassembly depois de saltos
  incondicionais: é preciso seguir os saltos, não ler em sequência.
