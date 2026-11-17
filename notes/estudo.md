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

### Execução 12 (v0.12) - notes/run12_v0.12
- Capturas: Edge, Sarah, (meio da troca). **Previsão confirmada para a Sarah**: +0x3c4 = 2 e
  destaque do painel em x = 636.
- Ajuste: o Edge veio 0 (x = 70) nesta batalha e 1 (x = 353) na execução 11. Os 5 pontos seguem
  x = 70 + 283 × posição. Logo `CBattleMainTask+0x3c4` = **posição do membro controlado na ordem
  do grupo desta batalha, base 0 (0-3)**, não um número fixo por personagem; +0x3c0 = 3 = maior
  posição. `CStatusBoardWindow+0x1f0` é a cópia que a interface usa.
- A captura no meio da troca já tem o valor novo: o campo muda antes da câmera.
- Próximo: (1) achar a lista de membros na ordem desta batalha (posição -> personagem);
  (2) teste de escrita: a sonda grava +0x3c4 e vemos se o controle troca sem R1 (primeira
  escrita na memória do jogo: só com autorização explícita).

### Execução 13 (v0.13) - notes/run13_v0.13
- Teste de escrita (F8): **recusou, nada foi gravado**. CBattleMainTask+0x3c0/+0x3c4 = 0 / 256 nesta
  batalha e não acompanham a troca (capturas Edge, Edge, Arumat, Edge): a coincidência das
  execuções 11-12 era outro estado. **Não usar o F8 da v0.13** (alvo errado; desligar na próxima versão).
- CStatusBoardWindow+0x1f0 continua coerente (x = 70 + 283 × posição), mas na captura #2 já marcava
  1 com a câmera ainda no Edge (igual à captura no meio da troca da execução 12): o painel muda **ao
  apertar R1** e o personagem controlado só muda depois da animação. O painel mostra o membro
  pedido/destino.
- Estático: 6 funções usam +0x1f0 e +0x1f4; o "setter" game+0x3b7e460 grava 4 inteiros em
  +0x1f0..+0x1fc (último = 0) e não tem chamadas diretas (virtual); não bate com o layout do painel.
- Próximo: captura automática (a sonda detecta a troca pela câmera e captura sozinha depois de 1,5 s
  estável) para ter 4 personagens sem depender do tempo do F9.

### Execução 14 (v0.14) - notes/run14_v0.14
- 1ª sessão fechada pelo usuário (23 s). 2ª: F7 ligou a captura automática, mas **nenhuma captura**.
- Causa: o bloco +0x129a0 **não é o personagem controlado**, é o foco da câmera, que passeia entre os
  membros sozinho. Nos dumps feitos ~3 s depois de cada linha "CONTROLLED" do log ele já estava em
  outro membro ou em nenhum (exec. 11: log Edge/Arumat/Edge, dumps Sarah/Myuria/Sarah; exec. 12:
  log Edge/Sarah/-, dumps -/Arumat/Edge; exec. 13: log Edge/Edge/Arumat/Edge, dumps Edge/-/Myuria/Edge).
  Os rótulos "controlado" das execuções 3-13 vinham dele e não são confiáveis.
- O que segue o R1 de forma direta é o painel (CStatusBoardWindow+0x1f0, x = 70 + 283 × posição).
- v0.15: a captura automática dispara e rotula pela posição do painel (estável 1,5 s); o foco da
  câmera fica só como referência; diff_battle.py usa as linhas "POSITION: p".

### Execução 15 (v0.15) - notes/run15_v0.15
- F7 ligado duas vezes (painel na posição 1, depois 0), **nenhuma captura**: com amostras a cada
  250 ms a posição do painel nunca ficou igual por 1,5 s seguidos -> ela também muda sozinha
  (anima ou percorre os membros), não só no R1.
- v0.16: amostra a cada 100 ms, registra cada mudança com o tempo e o foco da câmera (até 400
  linhas) e captura quando uma posição ocupa >= 80% das últimas 15 amostras.
- Observação do usuário na execução 15: o personagem realmente controlado **muda junto com o destaque
  do painel** ao apertar R1. O painel é um bom indicador do membro controlado.

### Execução 16 (v0.16) - notes/run16_v0.16 — **primeiro resultado firme**
- Captura automática funcionou: 8 capturas, duas voltas, posições 1,2,3,0,1,2,3,0. O painel mudou
  exatamente uma vez por R1 (não fica mudando sozinho).
- diff_battle.py (4 posições, cada uma 2 vezes): 158 campos, com estrutura clara:
  - **`CBattleMainTask+0x3a4` = posição do membro controlado (0-3)**, `+0x3a0` = maior posição (3).
    O bloco +0x3a0..+0x3ac (16 bytes) é copiado por um getter em game+0x3c7adb0.
    (O +0x3c4 das execuções 11-12 era um bloco irmão 0x20 adiante: coincidência.)
  - **Ponteiro para o personagem controlado** em `CThisIs+0x38`, `CTargetSign+0x298`,
    `CHitCounter+0x528`, `CCameraManager+0x390`, `CCameraAngleLock+0x190`.
  - Posição -> personagem nesta batalha: 0 Edge, 1 Sarah, 2 Arumat, 3 Myuria (ordem do grupo).
  - Vários elementos de interface copiam a posição e o x do destaque (70/353/636/919).
- Estático: só 3 funções usam o deslocamento 0x3a4 e 2 usam 0x3a0; o setter do bloco do
  CBattleMainTask não aparece por deslocamento direto (escreve por ponteiro ou cópia embutida).
- Próximo: teste de escrita em +0x3a4 com checagem cruzada (painel e CThisIs) para ver se só
  esse campo troca o controle; depois achar como a entrada do controle chega ao personagem.

### Execução 17 (v0.17) - notes/run17_v0.17
- F8: checagens 1 (+0x3a0 = 3, +0x3a4 = 0) e 2 (painel = 0) passaram; **checagem 3 falhou** (o primeiro
  CThisIs encontrado não apontava para um personagem do grupo pela lista de 24 classes) -> **nada gravado**.
  O usuário relatou "yes it does" (o destaque/câmera mudou?), mas não foi efeito da sonda.
- v0.18: a checagem 3 percorre todas as instâncias de CThisIs e identifica o personagem pelo RTTI
  (class_of), sem depender da lista limitada a 256 objetos; se falhar, registra cada instância e o alvo.

### Execução 18 (v0.18) - travou antes do título
- Tela cinza + 1280x720 preto de novo (02:42), **com só `DXVK_FRAME_RATE=60 %command%`**: a hipótese de
  que o WINEDLLOVERRIDES causava isso estava errada. Terceiro travamento (01:15, 01:27, 02:42) em ~20
  inícios, todos com a sonda. Sem processos restantes da sessão anterior; threads dormindo, áudio em
  silêncio; o heartbeat da sonda continuou.
- Suspeita: a thread da sonda chama GetAsyncKeyState a cada 30 ms desde 0,5 s após carregar, junto com
  a criação da janela/dispositivo do jogo (possível corrida no Wine).
- v0.19: nenhuma chamada de teclado nos primeiros 45 s ("hotkeys active" no log). Se travar de novo,
  o próximo passo é iniciar sem a sonda (WINEDLLOVERRIDES=xinput1_3=b) para descartá-la.

### Execução 19 (v0.19)
- Iniciou normal (sem travar; teclado só depois de 45 s). Uma execução não prova que a causa era essa.
- F8: **checagem 1 falhou**, nada gravado: CBattleMainTask+0x3a0 = 1083249128 (parece float ~4,5), +0x3a4 = 1.
  Ou não estava em combate ativo no momento, ou o bloco muda de batalha para batalha. Próximo: F8 e
  depois F9 em combate ativo, para ver o bloco inteiro nesse momento.
- **Mecânica do jogo (relato do usuário):** em batalha só o **líder** é controlado; L1/R1 troca o
  líder; a câmera e o foco seguem o líder; os outros membros são sempre IA. Logo +0x3a4 = posição do
  **líder** no grupo. Para o co-op: o membro do jogador 2 precisa receber entrada do controle 2 no
  lugar da IA, sem virar líder. Próximos alvos: onde a entrada do controle é aplicada ao líder e onde
  a IA decide as ações dos outros membros.

### Execução 20 (v0.19) - notes/run20_v0.19 — **líder encontrado de forma estável**
- F8 recusou (main+0x3a0 = 0 / +0x3a4 = 0); o F9 logo depois mostrou que a região main+0x380.. tinha
  outro conteúdo: o CBattleMainTask termina antes de +0x3a0 e o dump de 4 KB lia o vizinho.
- **`CBattleMainTask+0x68` aponta para o bloco do líder**: [0] = maior posição (3), [4] = posição do
  líder; +0x60 = base comum (pilha de estados?). O bloco muda de lugar (main+0x3a0, +0x3c0, +0x3e0).
- Validado em **19 capturas de 5 batalhas** (exec. 11, 12, 13, 16, 20): sempre max = 3 e líder = painel.
  Explica tudo: o "+0x3c4" das exec. 11-12 era este bloco em +0x3c0; o "0/256" da 13, leitura fixa errada.
- CThisIs/CTargetSign/CHitCounter não são estáveis (na exec. 20 apontam para o CThisIs ou para 0).
- v0.20: F8 grava 1 byte em [bloco+4] só se: (1) bloco válido via +0x68 (até 0x200 após +0x60, max 1-3,
  líder <= max); (2) painel = líder; (3) max + 1 = nº de personagens do grupo na memória (RTTI).
  As 3 checagens passam nas 19 capturas.
- Sessões curtas às 03:02 (30 s) e 03:02:53: inícios extras, sem dados.

### Execução 21 (v0.20) - notes/run21_v0.20 — **primeira escrita, e o jogo seguiu**
- As 3 checagens passaram; 5 escritas (4 na 1ª batalha: 0->1->2->3->0; 1 na 2ª: 0->1).
- Depois de cada escrita: o valor **permaneceu** (3 s), o **destaque do painel deslizou** até a nova posição
  (x 70 -> 295 -> 353 etc., a mesma animação do R1) e o **foco da câmera foi para o novo líder**
  (Sarah, Arumat, Myuria, Edge). CThisIs não apontou para ninguém. Sem travar.
- Na 2ª batalha o bloco estava em outra região (0x149B4D3A0, base +0x60 = 0x149B4D350): o ponteiro
  +0x68 funciona entre batalhas.
- Falta o relato do usuário: o analógico passou a controlar o novo líder?
- **Relato do usuário:** depois de cada F8 o analógico passou a controlar o novo líder. Logo **1 byte
  ([*(CBattleMainTask+0x68) + 4]) troca o líder por completo** (câmera, painel e controle), sem travar.
  Como esperado, continua havendo só um personagem controlável: é o desenho do jogo (1 líder + 3 IA).
- Co-op exige um 2º membro (não líder) dirigido pelo controle 2 no lugar da IA. Próximos alvos:
  (1) como a entrada do controle chega ao líder (começar pelos métodos do CBtlPad, vtable game+0x9d6c78,
  offline); (2) onde a IA decide as ações dos outros membros; (3) leitura do 2º controle (Steam Input,
  2 objetos WindowsPad).

## Fase 3 - entrada do controle (offline, código salvo na execução 10)
- analysis/vtable.py: imagem = exe do disco + seção .xpdata salva; lista a vtable e desmonta o início
  de cada método (para no ret/jmp por causa do lixo anti-disassembly). Saída em notes/cbtlpad_vtable.txt.
- **CBtlPad** (vtable game+0x9d6c78): 19 métodos, a maioria stubs. O [12] (game+0x2f9bae0) percorre 2
  registros de 0x14 bytes em this+0x28 (contador float + flag em +0x10) e, ao zerar, chama
  game+0x75c840 -> **temporizador de vibração de 2 controles** (o 32767.0 visto na execução 5).
- **Camada de entrada**: tabela global de **2 controles** em game+0x1059634 (registros de 0x6e8 bytes,
  1º byte = id 0 e 1). game+0x75c660(id) procura o registro; se o id não existe, faz OR dos botões do
  registro 1 no registro 0 e devolve um registro padrão -> o jogo lê os 2 controles **separados** e os
  **junta** para um jogador só.
- 43 chamadas diretas a game+0x75c660: a maioria pede o controle 0; 4 funções pedem 0 e 1
  (game+0x7b1510, 0x7b2e60, 0x7b7090, 0x7d2870). Todas em .data1 (não cifrada no disco).
- Próximo: descobrir qual chamada do controle 0 alimenta o líder em batalha; para o co-op, o 2º membro
  precisaria receber os dados do controle 1 (exige alterar código em execução, como o SO4Fix faz).
- analysis/callgraph.py: RTTI de todas as classes (5201 classes, 3235 vtables, 10091 funções
  rotuladas) + índice de chamadas diretas (122743) -> rotula quem chama uma função, subindo até métodos
  virtuais. Saídas: notes/getpad_callers.txt e notes/getleader_callers.txt.
- Chamadores de game+0x75c660 (pegar controle): quase todos de menus (Camp), campo (Field), título e
  salvar. Em batalha só três:
  - game+0x4fd740 = **StartRumble(CBtlPad, controle, motor1, motor2, duração)**; chamado por
    CPlayerChr::vf107 só quando o personagem é o líder (vibra o controle 0 a 0x7fff).
  - game+0x5152c0 / 0x515690 (CKernel@Battle::vf12): percorrem os 2 controles e reiniciam estado do
    dispositivo, depois consultam o líder (flag leader+0xd46, subsistema em *(main)+0xb0): parece
    preparação/pausa, não o movimento por quadro.
  - CBtlPad::vf12 (temporizador de vibração).
- **Definição do líder no código** (confirma o que achamos em execução):
  - game+0x4f7310(main) = `return *(main+0x68)` (bloco do líder)
  - game+0x4f9990(grupo, i) = `return *(grupo + 0x5a0 + 8*i)`, grupo = *(CBattleMainTask+0x58)
  - **game+0x5000d0 = GetLeader(sistema)** = grupo->membros[bloco[4]]; sistema = [game+0x1051838]
    (1º campo = CBattleMainTask). 109 funções chamam.
  - **game+0x53e390 = IsLeader(personagem)** = (GetLeader(sistema) == personagem): a pergunta "sou o
    controlado pelo jogador?" usada por dezenas de métodos de CPlayerChr/CPlayerThinkChr.
- Hipótese para o co-op: fazer IsLeader responder "sim" também para o personagem do jogador 2 e dar a
  ele a entrada do controle 1 (o movimento do líder não parece vir de game+0x75c660; falta achar a
  interface de entrada por quadro). Isso exige alterar código em execução: pedir autorização antes.
- analysis/users.py: o que vem logo depois de cada chamada a uma função. Nos 31 usos de IsLeader o que
  segue é comportamento (vibração, vozes, despachante de comandos game+0x500b40 com tabela de 10 casos,
  dados de grupo/personagem), **não** a leitura do controle.
- Referências a game+0x1059000..0x105a500 (625 em 337 funções, notes/input_globals_xref.txt): a área
  game+0x1059e78..0x1059ea0 é um registro de eventos/ouvintes (bitmask por categoria + listas de ids,
  game+0x762e40), não controle; o "vf142" marcado era falso positivo (bytes dentro de chamadas).
- Conclusão: estática achou **quem é o líder** (GetLeader/IsLeader), mas não **por onde entra o
  movimento** (deve passar por métodos virtuais dos objetos de controle). Próximo: captura dirigida
  pelo analógico (neutro/cima/direita/baixo/esquerda, repetido) e diff, como foi feito com o líder.
- Validação offline da fórmula do líder: party = *(CBattleMainTask+0x58) = **CChrManager@Battle**,
  membros em +0x5a0 = [Edge, Sarah, Arumat, Myuria] (execução 16), líder = membros[bloco[4]] confere
  em todas as capturas (exec. 16 e 20).
- v0.21: F6 = captura rotulada pelo analógico (neutro, cima, direita, baixo, esquerda, ×2), com objetos
  Battle::, as 24 classes (personagens 256 KB, WindowsPad), globais de entrada game+0x1059000..0x105b000
  e o líder achado pela fórmula do jogo. analysis/diff_stick.py procura eixos X/Y e campos discretos
  (testado com dados sintéticos).

### Execução 22 (v0.21) - notes/run22_v0.21 — **analógico encontrado nos globais**
- 7 de 10 capturas (neutro, cima, direita, baixo, esquerda, neutro, cima); líder Edge; sem travar.
  Houve antes um início curto (03:42, ~30 s) e o jogo fechou ~30 s após a 7ª captura.
- diff_stick.py (critério rígido) não achou eixo no personagem, nos WindowsPad, no PadTask, no CBtlPad:
  os 2 WindowsPad praticamente não mudam. O "cima" saiu torto (1º muito à direita, 2º já solto).
- Nos globais de entrada (game+0x1059000+):
  - `+0x476` (s16) = **X do analógico esquerdo**, `+0x478` (s16) = **Y** (bruto −32768..32767):
    direita 32732/−1499, baixo 644/−32760, esquerda −32362/5126, neutro 0/0.
  - cópia em `+0x64a/+0x64c` = **registro do controle 0** (game+0x1059634) **+0x14/+0x18** ->
    cada registro tem o seu analógico; o controle 2 deve estar em game+0x1059d30 (registro 1 + 0x14).
  - `+0x474` = game+0x1059080 + 0x3f4: estado **mesclado** que o jogo usa.
  - bits de direção em +0x6d0..+0x6d8 (direita 8, baixo 2, esquerda 4) e contadores em +0x690..+0x698.
- Quem lê: nenhuma referência direta a esses endereços; game+0x1059080 só é usado por SozApp,
  AppSetupTask (preparação), PadTask@Peripheral::vf12 (atualização por quadro = quem **preenche**) e o
  construtor do PadTask (game+0x75bf90). Nenhum objeto capturado guarda ponteiro para essa área.
  O consumidor (movimento do líder) não aparece na estática.
- v0.22: F5 = rastreio por página-guarda (2 s) da página game+0x1059000: cada acesso cai no VEH, que
  registra (instrução, deslocamento, leitura/escrita), deixa a instrução rodar com TF e rearma a guarda
  no passo seguinte; depois restaura a proteção. Sem suspender threads, sem ptrace, sem gravar dados.
  Autoteste no Wine local (página da própria sonda, 400 leituras): 400 acessos, 2 instruções -> funciona.

### Execução 23 (v0.22) - notes/run23_v0.22 — **página-guarda derrubou o jogo**
- F5 ligou a guarda na página game+0x1059000 (proteção 0x8); o log para em "GUARD TRACE ON" (sem OFF,
  sem relatório, sem heartbeat) e o jogo terminou às 04:01:27: morreu dentro da janela de 2 s.
- Causa provável: código fora do tratamento de exceções do Windows (o lado nativo do Steam Input /
  lsteamclient escrevendo nos registros de controle) tocou a página guardada. **Não usar F5 da v0.22.**
- Busca estática por deslocamentos 0x3f4/0x3f6/0x3f8 (campo do analógico a partir de game+0x1059080):
  ruído demais (offsets comuns e instruções mal decodificadas no código ofuscado).
- Objeto do líder (CPlayerEdge, 256 KB) na execução 22: nenhum float ~0 no neutro e diferente em
  direita/baixo/esquerda -> a intenção de movimento não fica nos primeiros 256 KB do personagem.

## Retomada (após a execução 23)
- v0.23 instalada (F5 desligado). Commit 6333dec.
- PadTask@Peripheral::vf12 (game+0x4e594d0) chama game+0x75c1b0 = **atualização de um controle**
  (notes/pad_poll.txt): o objeto tem o dispositivo em [obj], botões OR em +0x5bc/+0x5be/+0x5c2,
  analógicos em +0x5ca/+0x5cc (esquerdo) e +0x5ce/+0x5d0 (direito), gatilhos +0x5d2/+0x5d3.
  Com obj = game+0x1059080 isso bate com o analógico do controle 0 (game+0x105964a): existem **dois
  objetos de controle** de 0x6e8 bytes, em **game+0x1059080** e **game+0x1059768**; os "registros" são
  a seção de estado deles em +0x5b4; o "mesclado" game+0x1059474 = objeto 0 + 0x3f4.
- Nenhum global aponta para esses objetos; o cabeçalho game+0x1059000..0x105907f tem dezenas de campos
  pequenos lidos por muitas classes, nenhum claramente base do analógico.
- Busca frouxa em tudo da execução 22 (1221 objetos + 24 classes + globais): nenhum float ~0 no neutro
  e não nulo em direita/baixo/esquerda. Estática e diff esgotados para "quem lê o analógico".
- Próxima ferramenta proposta: **ponto de parada de software** (int3) em GetLeader/IsLeader, com o
  manipulador da sonda registrando endereços de retorno e pilha por 1 s, parado vs andando; mexe no
  código do jogo em memória (proteção da página de código), pedir autorização.

## Modo tag-team (v0.24) — primeiro mod jogável
- src/tagteam.c: cada jogador "é dono" de uma posição do grupo (padrão: jogador 1 = 0, jogador 2 = 1;
  so4coop.ini opcional com p1_position= / p2_position=). **L3 no seu controle** (ou F3 / F4 no teclado)
  torna o seu personagem o líder, gravando o byte do bloco do líder (como na execução 21).
- Controles lidos pelo XInput real (sondagem própria a cada 30 ms, slots 0-3); os dois primeiros slots
  conectados = jogador 1 e 2; o log mostra os slots vistos.
- Checagens antes de gravar, sem varredura completa: CBattleMainTask em cache (verificado por RTTI),
  bloco do líder válido, *(main+0x58) é CChrManager com membros[0..max] todos personagens, alvo existe.
- Limite: o jogo ainda mescla os dois controles, então os dois movem quem for o líder.
- Teste local (Wine do sistema): ini lido (2/3), slot 0 conectado (DualSense) -> jogador 1, sem jogador 2;
  fora de batalha "no battle (nothing written)".

### Execução 24 (v0.24) - notes/run24_v0.24 — tag-team pelo teclado funciona
- **F3/F4 funcionaram**: 7 trocas Edge <-> Sarah com todas as checagens, jogo rodou 6 min sem problema.
- **L3 não pode funcionar ainda**: o XInput não vê nenhum controle no Proton ("slots connected" vazio).
  O jogo lê os controles pela API do Steam Input; o Proton não os expõe ao XInput.
- Correção: ler os botões dos **objetos de controle do próprio jogo** (game+0x1059080 e game+0x1059768,
  palavras de botão em +0x5bc..+0x5c6; game+0x75c660 junta as 6 palavras do controle 2 no 1). Falta
  saber qual bit é o L3: rodada de calibração registrando as mudanças dessas palavras.
- v0.25: o tag-team lê os botões dos objetos de controle do jogo (6 palavras em +0x5bc de
  game+0x1059080 / game+0x1059768) a cada 30 ms; registra toda mudança (até 600 linhas) para calibrar;
  botão de troca configurável no so4coop.ini: take_word=<0-5>, take_mask=<hex>. XInput continua como
  alternativa. Teste local: ini lido (word 2, mask 0x40); fora do jogo os objetos não existem e são ignorados.

### Execução 25 (v0.25) - notes/run25_v0.25 — calibração
- 237 mudanças de botão, **todas no objeto de controle 1** (pad1); o objeto 2 (game+0x1059768) nunca mudou.
  Com Steam Input ligado, os dois controles físicos parecem cair no mesmo objeto do jogo (ou o 2º não foi usado).
- Layout das 6 palavras: [0] = seguradas, [1] = quadro anterior, [3] e [5] = recém-pressionadas.
  Bits pressionados: 0x0001 x3, 0x0004 x2, 0x0008 x1, 0x0080 x11, 0x0400 x8, 0x0800 x4, 0x1000 x8,
  0x2000 x4, 0x4000 x36, 0x8000 x3 -> R3 não identificável (muitos outros botões na sessão).
- L3 = trava de alvo; R3 = recentraliza a câmera no controlado (aceitável para trocar de jogador).
- v0.26: o caminho XInput usa R3 (0x0080). Proposta: **desligar o Steam Input só para este jogo**; aí o
  XInput do Proton vê cada controle num slot próprio e o mod distingue jogador 1 e 2.

### Execução 26 (v0.26) — Steam Input desligado
- Jogo rodou 67 s; o XInput do mod continuou sem controles, o jogo não chamou GetState e os objetos de
  controle do jogo não mudaram: **sem Steam Input o jogo não recebe controle** (religar).
- O exe só referencia XINPUT1_3.dll (checagem/vibração) e "SteamController005": o jogo lê a entrada pela
  **ISteamController**. Não há arquivo de ações do Steam para o jogo no disco; os nomes das ações não
  aparecem no exe nem no código decifrado.
- v0.27 (src/steamctl.c): pega a interface pelo steam_api64.dll do jogo (SteamClient ->
  SteamAPI_ISteamClient_GetISteamController(..., "SteamController005")), lista os controles físicos
  (GetConnectedControllers) e consulta as ações 1..32 em cada um (GetDigitalActionData) a cada 60 ms,
  registrando cada mudança (calibração). take_action=<n> no so4coop.ini: ação n no controle k do Steam
  -> jogador k assume.

### Execução 27 (v0.27) - notes/run27_v0.27 — Steam vê 3 controles
- Interface SteamController005 obtida; **3 controles conectados** (handles 0x1C666D5EC4E5, 0x135371001869A2DE,
  0x2D90895F4DC78F). 130 mudanças, **todas no controle 3 do Steam**: ação 1 x1, 2 x2, 3 x45, 4 x2, 10 x15.
  Controles 1 e 2 não relataram nenhuma ação.
- Hipótese: o Steam só relata ações onde o jogo ativou o seu conjunto de ações; o jogo parece usar só um
  controle (o 3º), o que explica também tudo no objeto de controle 1 na execução 25.
- Ação 10 = candidata a R3 (15 toques no fim); ação 3 = provável ataque.
- Próximo: confirmar com o usuário a ordem e se o 2º controle mexia no jogo; se não, ativar o conjunto de
  ações atual (GetCurrentActionSet do controle 3 -> ActivateActionSet nos outros) para lê-los.
- Relato do usuário (exec. 27): R3 3x no DualSense, depois 3x no outro; saiu com Super+Q. O 3º "controle"
  do Steam não é o teclado (F3/F4 são lidos pelo mod direto); provavelmente um dispositivo virtual/duplicado.
- v0.28: horário em cada linha de ação; por controle, índice de gamepad e conjunto de ações; activate_all=1
  (so4coop.ini criado no jogo só com essa linha): a cada 2 s o conjunto de ações do controle que tem um é
  ativado nos outros. take_action agora é decimal (o teste pegou 10 -> 16 com o leitor hex).

### Execução 28 (v0.28) - notes/run28_v0.28 — **cada controle separado pelo Steam**
- 3 controles do Steam, todos já com o conjunto de ações 1 do jogo (activate_all não precisou ativar nada);
  índice de gamepad -1 em todos.
- **Ação 10 = R3**, vista em cada controle separadamente: controle 3 (61-72 s, 3x), controle 2 (77-98 s, 4x),
  controle 1 (104-121 s, 4x). O DualSense (apertado primeiro) deve ser o controle 3; o outro controle físico
  parece aparecer como duas entradas do Steam (2 e 1). Ação 3 x43 a cada ~0,3 s = botão segurado repetindo.
- v0.29: p1_steam=<n> no so4coop.ini = controle do Steam do jogador 1; qualquer outro controle = jogador 2.
  so4coop.ini proposto: take_action=10, p1_steam=3, activate_all=0, p1_position=0, p2_position=1.
- Controles do usuário: **DualSense, GameSir G7 (estilo Xbox One) e DualShock 4 v1** = os 3 controles do Steam.
  Na exec. 28 o R3 foi apertado nos três: controle 3 = DualSense; 2 e 1 = GameSir / DualShock 4 (ordem a confirmar).
- v0.29 (final): steamN_position=<0-3> no so4coop.ini dá a cada controle do Steam a sua posição no grupo
  (até 4 jogadores); sem essas linhas vale p1_steam (jogador 1) / todos os outros (jogador 2). tt_take dividido
  em tt_take_pos(rótulo, posição). Teste local: posições lidas (3->0, 2->1, 1->2), recusa fora de batalha.
  A ordem dos controles no Steam pode mudar entre sessões (o log lista a cada início).

### Execução 29 (v0.29) - notes/run29_v0.29 — **tag-team pelos controles funcionando**
- R3 (ação 10) em cada controle trocou o líder para o personagem configurado, duas voltas:
  controle 3 -> Edge, controle 2 -> Sarah, controle 1 -> Arumat (6 apertos, todas as checagens ok, sem travar).
- Falta o usuário dizer qual controle físico é o 2 e qual é o 1 (GameSir / DualShock 4) e se o R3 fez algo além
  de recentralizar a câmera.

## Release: so4coop tag-team 1.0
- build_release.sh compila as mesmas fontes com SO4COOP_RELEASE: só F3/F4 + R3 pelos controles do Steam,
  log em so4coop.log. Pacote build/release/so4coop-tagteam-1.0.zip (xinput1_3.dll, so4coop.ini modelo,
  README.txt, LEIA-ME.txt; arquivos-fonte do pacote em release/). Testado no Wine local.
- Pedido do usuário: os outros controles jogarem com os outros membros **ao mesmo tempo** na batalha.
  Ainda não é possível: o jogo só tem um personagem controlado (líder) e não sabemos como injetar comandos
  na IA dos outros membros. Passo seguinte proposto: interceptar as chamadas do jogo à ISteamController
  (tabela virtual da interface) para separar a entrada por controle (só o controle do líder dirige; a
  entrada dos outros fica disponível para alimentar outro personagem) e então estudar o "pensar" da IA.

## Fase 4 - separar a entrada por controle
- Release 1.0 instalada no jogo (xinput1_3.dll f244b3d6…), so4coop.ini do usuário mantido.
- Desmontagem dos thunks planos do steam_api64.dll do jogo: RunFrame +0x10, GetConnectedControllers +0x18,
  ActivateActionSet +0x30, GetCurrentActionSet +0x38, GetDigitalActionData +0x48, GetAnalogActionData +0x60.
  Os dois "Get…Data" são métodos MSVC que devolvem struct por ponteiro oculto: (this=rcx, ret=rdx,
  controle=r8, ação=r9) -> rax = ret. Analógico = {int modo; float x; float y; bool ativo} (16 bytes).
- v0.30 (src/steamhook.c, hook_steam=1 no ini): copia a tabela virtual do objeto da interface, troca esses
  6 métodos por invólucros que só contam e repassam, e aponta o objeto para a cópia (uma escrita de 8 bytes
  no objeto do Steam; nenhum código alterado). Relatório a cada 10 s (2 min) e depois a cada 60 s: chamadas
  por método, por handle de controle (ou "todos"), ações consultadas e quem chamou no jogo. Chamadas da
  própria sonda não contam.
- Autoteste no Wine local com um objeto falso de mesma ABI: contagens certas, repasse correto (digital e
  analógico), chamadas próprias ignoradas. A release também tem o código, desligado por padrão.

### Execução 30 (v0.30, hook_steam=1) - notes/run30_v0.30 — **o jogo pergunta por controle**
- Gancho estável (~2,5 min, tag-team pelo R3 funcionando junto).
- Em 100 s: RunFrame 30864, GetConnectedControllers 12346, **GetDigitalActionData 889056 = 296352 em cada um dos
  3 handles**, **GetAnalogActionData 74088 = 24696 por handle**. Nenhuma chamada com "todos os controles".
  Ações digitais 0..16, analógicas 1 e 2.
- Chamadores no jogo (camada de entrada Aska): game+0x7a535a (GetConnected), 0x7a556d e 0x7a55a3 (digital),
  0x7a55f8 (analógico), 0x7a5c59 (RunFrame). O jogo junta os controles depois.
- Logo: o invólucro pode neutralizar os controles que não são do líder (estado solto, analógico zerado) e a
  entrada de cada controle continua disponível para o mod (as consultas da própria sonda não são filtradas).
- v0.31: leader_only_input=1 (com hook_steam=1): nos invólucros, as consultas do jogo num controle que não é
  o do líder recebem "solto" / analógico zerado (bActive mantido); as consultas da própria sonda nunca são
  filtradas (o R3 de todos continua funcionando). O controle do líder é recalculado a cada 60 ms: posição do
  líder lida pelo global que o GetLeader do jogo usa ([game+0x1051838] -> CBattleMainTask, sem varredura) e
  mapeada pelos steamN_position; fora de batalha, ou se o líder não tem controle mapeado, nada é filtrado.
  tt_find_main também usa esse caminho rápido antes da varredura.
- Autoteste (objeto falso): líder passa, outro controle zerado (botão e analógico), consulta da sonda não filtrada.

### Execução 31 (v0.31, leader_only_input=1) - notes/run31_v0.31
- Até a batalha (~45 s) handle do líder = 0 (sem filtro). Em batalha: Edge líder -> handle do DualSense; após R3 no
  controle 1 -> handle 1 (Arumat); R3 no 2 -> Sarah; R3 no DualSense -> Edge; controle 1 -> Arumat.
- ~6300 consultas neutralizadas por segundo = 2/3 do total (os dois controles que não são do líder). Sem travar.
- **Usuário confirmou: funcionou** (só o controle do líder move o líder; R3 troca; menus ok).
