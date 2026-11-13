# U1 Factory Display

Pixel-art factory dashboard for the Snapmaker U1 on ESP32-S3 touch displays (Waveshare 7"). Live print status with a 4-head spool view and full Spoolman inventory via Moonraker. Companion to [Spool Factory](https://github.com/caiotmoraes/spool-factory).

## Monitor da fábrica — Snapmaker U1 no Waveshare ESP32-S3-Touch-LCD-7

Firmware de um painel 800×480 (LCD RGB ST7262 + toque GT911, 8 MB de PSRAM)
para acompanhar a Snapmaker U1 (Klipper / Moonraker). Visual inspirado no
app [Spool Factory](https://github.com/caiotmoraes/spool-factory) e na máquina
de estados do [klippyface](https://github.com/luke-kearney/klippyface).

Nesta versão a cena da **fábrica** está completa (estados, HUD em português,
U1 centrada com cabeçote alinhado, rolo ativo via Spoolman). O **armazém de estoque** é só um
placeholder: o mapeamento do app e a proposta de navegação touch estão
documentados abaixo; **nenhuma escrita** no Spoolman ou na U1 foi implementada.

## Por que PlatformIO + Arduino + LVGL 8 (e não ESPHome)

- A Waveshare documenta o bring-up oficial com `ESP32_Display_Panel` + API
  `Board` (`new Board(); init(); begin(); lvgl_port_init(lcd, touch)`). Este
  projeto segue esse caminho (LVGL 8.4, port oficial CC0, `avoid-tearing`
  desligado, dois buffers de 20 linhas na SRAM).
- Controle total de sprites, recoloração do filamento em runtime, animações
  de 2–4 quadros e cena customizada — mais direto em C/C++ do que em YAML
  ESPHome.
- O cliente precisa de HTTP GET, POST do proxy Spoolman, varredura da /24 e
  portal cativo. Tudo isso é código nosso, portátil de testar no host.
- No Windows a gravação é um `pio run -t upload` pela porta UART (CH343).

O ESPHome tem placa de primeira classe (`ESP32-S3-TOUCH-LCD-7-800X480`), e
serviria para um painel simples. Para esta cena, o PlatformIO é a escolha
certa.

Pilha: pioarduino 53.03.11, Arduino-ESP32 3.1.1, ESP32_Display_Panel,
ESP32_IO_Expander 1.1.0, esp-lib-utils 0.2.0, LVGL 8.4.0, ArduinoJson 7.
Macros da placa: `-DESP_PANEL_BOARD_DEFAULT_USE_SUPPORTED=1
-DBOARD_WAVESHARE_ESP32_S3_TOUCH_LCD_7`. CDC no boot desligado
(`ARDUINO_USB_CDC_ON_BOOT=0`): o serial é o chip CH343 da porta UART.

## O que esta versão faz (v0.4.3)

v0.4.3 (em cima da v0.4.2):

- **Cena só com a U1**: saíram da fábrica os últimos sprites, frames, timers
  e a lógica de estado que ainda animava figuras na frente da gaveta.
  Essas imagens deixam de ser ligadas em `images.S`. Ficam a impressora
  centrada, o cabeçote alinhado (carro + bico com o mesmo `dx`) e os 4
  compartimentos de baixo com número, rolo e material.

## O que esta versão faz (v0.4.2)

v0.4.2 (em cima da v0.4.1):

- **Alinhamento do cabeçote**: o gantry do teto (`img_u1_carriage`) estava
  **12 px à direita** do bico (`img_u1_toolhead`). Os eixos visuais
  (círculo amarelo e bico) passam a coincidir: `U1_CARRIAGE_X = U1_TOOLHEAD_X`,
  com o mesmo `dx` em todos os pontos do curso.
- **Impressora centrada na cena**: `SCENE_CROP_X` passou de 240 para **206**,
  para o centro do corpo (orig ≈ 190–623, centro 406,5) cair no meio dos
  400 px direitos. A máquina tem 434 px de largura, então ~16–17 px das
  faces laterais do pedestal ficam de fora; o corpo superior e os 4
  compartimentos de baixo entram inteiros. Todos os X locais (compartimentos,
  bico, carro, peça, curso) acompanham o deslocamento de **+34 px**.

## O que esta versão faz (v0.4.1)

v0.4.1 (em cima da v0.4):

- **Filament Detection**: `GET /firmware-config/api/settings` vai à **porta 80**
  (nginx). No Moonraker `:7125` a rota dá 404 — era isso que impedia a
  verificação. O JSON real tem ~30 066 bytes; `http_buf` já tem 192 KB em
  PSRAM, suficiente. O parse usa filtro ArduinoJson e só lê o item `rfid`.
  O valor atual da U1 é `"openrfid"` (não External): associar/carregar
  mostra a 2ª mensagem do app (*Selecione Filament Detection = External...*).
  A regra de segurança não mudou.
- **Rolos na gaveta de baixo**: números 1–4, rolo colorido e plaquinha de
  material nos **4 compartimentos já existentes** da gaveta (abaixo da
  câmara). A arte não foi redesenhada. As baias do topo voltam ao original.
  O extrusor ativo leva o contorno menta.

## O que esta versão faz (v0.4)

v0.4 (em cima da v0.3), correções do ecrã real:

- **Carro em duas sprites**: o gantry do teto (`img_u1_carriage` 43×40) e o
  bico da câmara (`img_u1_toolhead` 41×37) voltam a ser camadas curtas,
  movidas com o **mesmo dx**. A faixa 55×141 quase toda transparente
  sujava o painel RGB (lixo cinza/azul a atravessar a fascia). Movimento
  com easing (smoothstep) a cada 50 ms.
- **Cores escuras na recoloração**: o marcador (alpha 254) fica só no
  enrolamento rosa do filamento. O firmware ignora pixels marrons/quase-pretos
  com a=254, por segurança. Filamento preto nas baias permanece preto
  (flanges escuros, nunca marcados).
- **Monitor de FPS/CPU**: `LV_USE_PERF_MONITOR` desligado na versão de
  lançamento. As regiões sujas são bem menores (sprites curtas, sem
  retângulos de LED a pulsar).
- **Baias do topo ao vivo**: cada um dos 4 alimentadores mostra o número
  1–4, um rolo pixel na cor do canal (`print_task_config` / Spoolman) e
  uma plaquinha com o material (`PLA`, `PETG`, ou `--` se vazio). O
  extrusor ativo ganha um contorno menta. Bico 3 = canal 2 (confirmado).
- **LEDs menta**: os retângulos `led_panel` (ecrã frontal, à direita de
  "snapmaker") e `led_front` (baía de baixo) foram removidos — a arte de
  fundo já tem esses indicadores.

A imagem de app `build/u1_factory_0x10000.bin` grava só em `0x10000`
(preserva o NVS do Wi-Fi). A imagem mesclada DIO 16 MB 80 MHz continua
em `build/u1_factory_merged_v2.bin`. Não gravar com a placa a imprimir
(DTR no COM3 reinicia o ESP32).

## O que esta versão faz (v0.3)

v0.3 (em cima da v0.2):

- **Layout**: metade esquerda (400×480) é o painel de dados — estado grande,
  progresso %, ETA, camada, temperaturas do bico *ativo* e da mesa, rolo
  ativo (cor/material/resto em g, nome do arquivo) e os 4 feeders. Metade
  direita é a cena da fábrica, recortada (crop x=240) para caber em 400×480.
- **Carro completo**: o bloco branco com o círculo amarelo no trilho de
  cima (fora da câmara) e o bico de dentro agora são **uma só camada** e
  se movem juntos.
- **Rolo ativo**: o HUD lê `print_task_config.filament_spool_id` do
  extrusor atual, não o `spool_id` global do Moonraker (que na U1 vem
  `null` mesmo com rolos associados). Se o filamento está no bico mas
  sem id, mostra *SEM ROLO ASSOCIADO* (não esconde os outros 3).
- **Ativar rolo**: o botão CONFIRMAR do diálogo de confirmação realmente
  dispara a ação (na v0.2 `ui_overlay_box` zerava o callback). Associação
  é só metadado e pode ser feita durante a impressão; carga/descarga
  física continua bloqueada.
- A imagem de app `build/u1_factory_0x10000.bin` grava só em `0x10000`
  (preserva o NVS do Wi-Fi). A imagem mesclada DIO 16 MB 80 MHz continua
  em `build/u1_factory_merged_v2.bin`.

### O que já vinha da v0.2


- Máquina de estados: offline, ociosa, aquecendo, imprimindo, pausada,
  concluída (até o `idle_timeout` virar Idle), erro, ocupada. Cancelada
  aparece como CONCLUÍDA com o subtítulo CANCELADA, sem festa.
- Cena 800×480: U1 gigante no centro (cabeçote se move no X enquanto imprime,
  a peça cresce com o progresso, LEDs pulsam/piscando), HUD em português,
  abas **WIFI / FABRICA / ARMAZEM / DIARIO**.
- Rolo ativo via Moonraker (não direto na VM do Spoolman): cor real (swatch
  + recoloração do rolo na gaveta), material, fabricante, peso restante; alerta
  **FILAMENTO BAIXO** abaixo de 100 g (o filtro do armazém é 200 g).
- Toque na cena ou no HUD abre a página de detalhes. As abas não abrem
  detalhes (`lv_event_stop_bubbling`).
- **Armazém completo** via proxy Spoolman (lista, busca, filtros, ficha,
  edição, cadastro, exclusão, vínculo NFC, ativar rolo, carga/descarga).
  Toda escrita pede confirmação na tela. HTTP só numa task de fundo, JSON
  em PSRAM.
- **Menu Wi-Fi na tela**: varredura (2,4 GHz), SSID+RSSI+cadeado, senha
  com teclado LVGL, grava NVS `u1wifi` (ssid/pass) e reinicia. O portal
  `U1-DISPLAY` continua como fallback. Descoberta do IP da U1 (NVS +
  semente `192.168.0.4` + varredura da /24 na porta 7125).
- **Diário** local (SPIFFS, 60 linhas), busca e filtro por tipo.

## Wi-Fi (nunca grave credenciais de verdade no repositório)

1. Copie `include/secrets.h.example` para `include/secrets.h`.
2. Preencha `WIFI_SSID` e `WIFI_PASSWORD`. O arquivo está no `.gitignore`.
3. Se o SSID ficar vazio (ou a conexão falhar), a placa sobe o ponto de
   acesso **`U1-DISPLAY`**, senha **`u1display`**. No celular, abra
   `http://192.168.4.1`, grave a rede e a placa reinicia.
4. Na própria tela, a aba **WIFI** varre as redes 2,4 GHz (em modo portal
   a placa sobe AP+STA), mostra RSSI e cadeado, pede a senha e grava no
   mesmo namespace NVS. A placa só fala 2,4 GHz — isso está escrito no
   menu.

`MOONRAKER_SEED_IP` (padrão `192.168.0.4`) e `MOONRAKER_PORT` (7125) também
ficam nesse arquivo — são só a primeira dica; a descoberta substitui o IP.

## Compilar e gravar no Windows

A placa tem **dois** Type-C. Use o da **UART** (chip CH343), não o USB-OTG.

1. Instale o [PlatformIO Core](https://platformio.org/install/cli) (ou a
   extensão no VS Code / Cursor) e o driver **CH343**.
2. Abra um terminal na pasta `firmware/`. Na primeira vez o PlatformIO baixa
   a toolchain (alguns minutos).
3. Prepare o Wi-Fi (`secrets.h` ou deixe vazio e use o portal depois).
4. Conecte o Type-C UART. Se o Windows não mostrar a porta: segure **BOOT**,
   plugue o cabo, solte, e grave; depois aperte **RESET**.
5. Grave:

```bat
pio run -t upload
```

   Ou, com o binário já pronto desta pasta, sem recompilar:

```bat
pio run -t upload --upload-port COMx
```

   Os artefatos pré-compilados estão em `build/` (veja também `build/OFFSETS.txt`):

   - **`u1_factory_merged_v2.bin`** — imagem desta versão (DIO, 16 MB, 80 MHz). Grave em `0x0`.
   - `u1_factory_merged_fix.bin` — a mesma imagem (cópia, mantida por compatibilidade).
   - `bootloader_0x0.bin` — 2º estágio (`bootloader_qio_80m`), offset `0x0`, **header DIO**.
   - `partitions_0x8000.bin` — tabela 16 MB, offset `0x8000`.
   - **`u1_factory_0x10000.bin`** — aplicação, offset `0x10000`. **Use esta**
     para atualizar o firmware sem apagar o Wi-Fi já gravado no NVS:
     `esptool --chip esp32s3 --port COMx write-flash 0x10000 u1_factory_0x10000.bin`.
   - `u1_factory_merged.bin` — **NÃO USE.** É a imagem antiga (header QIO + 8 MB) que causou o boot loop.

   Comando (esptool 5.4.0; o padrão `--flash-mode keep` preserva o DIO do header — **não passe `--flash-mode qio`**):

```bat
esptool --chip esp32s3 --port COMx --baud 921600 write-flash 0x0 u1_factory_merged_v2.bin
```

6. Monitor serial: `pio device monitor` (115200 baud). Filtros
   `esp32_exception_decoder` já estão no `platformio.ini`. Depois do reset
   o ROM deve mostrar `mode:DIO`, não `mode:QIO`.

### Por que a primeira imagem deu boot loop (ets_loader.c 78)

O `bootloader.bin` que o Arduino-ESP32 gera para esta placa já vem com
**header DIO**. Isso é de propósito: o ROM da ESP32-S3 carrega o 2º estágio
no modo do header; em QIO a 80 MHz (`mode:QIO, clock div:1`) a leitura
falha em `ets_loader.c:78` e o watchdog dá `TG0WDT_SYS_RST` (PC no ROM,
`0x400454d5`), antes de qualquer código nosso. A definição oficial da
placa (`boards.txt`) confirma o split: `flash_mode=dio` no header,
`boot=qio` para escolher o ELF do 2º estágio (`bootloader_qio_80m`), que
é quem liga o QIO **depois** que o ROM terminou. O `memory_type` do app
continua `qio_opi` (QIO + PSRAM octal).

A imagem `u1_factory_merged.bin` foi juntada com `merge-bin --flash-mode qio`,
que reescreveu o byte do header (DIO → QIO) e atualizou o SHA. Por isso o
ROM tentou QIO e entrou em loop. O `scripts/merge_flash.sh` agora força
`--flash-mode dio --flash-size 16MB`.

### Flash de 16 MB (N16R8)

A placa real tem **16 MB** de flash (quad no eFuse) e 8 MB de PSRAM OPI.
A tabela de partições passou a 16 MB: app `factory` de ~6,94 MB em
`0x10000`, SPIFFS de 9 MB em `0x700000`. O header das imagens diz 16 MB.

## Moonraker: HTTP a cada 1 s, não WebSocket

- A descoberta na /24 já é um `GET /printer/info`. O mesmo cliente HTTP
  serve para o status, o Spoolman e o POST do proxy.
- Reconectar um WebSocket em cada IP candidato é mais frágil.
- 1 segundo basta para um painel (progresso, temperaturas, estado).

Um upgrade para WebSocket pode reutilizar `printer_classify()` depois.

### Descoberta do IP

A U1 não tem IP estático, o roteador não reserva DHCP e `lava.local` não
resolve. A ordem é:

1. Último IP gravado na NVS (chave `mr_ip`, namespace `u1face`).
2. Semente `MOONRAKER_SEED_IP` (`192.168.0.4`).
3. Varredura do último octeto 1–254 da /24 do IP local da placa, TCP +
   `GET /printer/info`. O IP que responder é salvo na NVS.

Consulta periódica:

```
GET /printer/objects/query?print_stats&virtual_sdcard&display_status&extruder&heater_bed&idle_timeout&webhooks
GET /server/spoolman/status          → spoolman_connected, spool_id
POST /server/spoolman/proxy          → ficha do rolo ativo
```

O parser aceita tanto o JSON cru quanto o envelope `{"result": ...}`.

### Proxy Spoolman (confirmado no Moonraker)

O Spoolman da Caio vive numa VM só alcançável por Tailscale. A placa fala
**somente** com a U1 (`192.168.0.4:7125`). Formato do proxy
(`Arksine/moonraker`, `spoolman.py`):

```json
POST /server/spoolman/proxy
{
  "request_method": "GET",
  "path": "/v1/spool/40",
  "use_v2_response": true
}
```

- O `path` começa em `/v1/…` (sem o prefixo `/api` que o app Android usa).
- Com `use_v2_response`, a resposta é `{ "response": {…}, "error": { "status_code", "message" } }`
  (pode vir dentro de `result`). Se o v2 falhar, o firmware tenta o formato
  antigo. `request_method` também aceita `POST`, `PATCH` e `DELETE` — a
  usar quando as escritas do armazém forem implementadas.

## Máquina de estados

Inspirada no klippyface, em C portátil (`src/printer_state.cpp`), testável
no PC sem Arduino.

| Condição | Estado | Rótulo |
|---|---|---|
| Sem conexão | `OFFLINE` | OFFLINE |
| `webhooks` shutdown/error, ou `print_stats` error | `ERROR` | ERRO |
| `print_stats=printing` e `filament_used ≤ 0,05` | `HEATING` | AQUECENDO |
| `print_stats=printing` | `PRINTING` | IMPRIMINDO |
| `print_stats=paused` | `PAUSED` | PAUSADA |
| `complete` ou `cancelled`, até `idle_timeout=Idle` | `COMPLETE` | CONCLUÍDA (subtítulo CANCELADA se for o caso) |
| Depois disso, `idle_timeout=Idle` | `IDLE` | OCIOSA |
| Sem job, mas `idle_timeout=Printing` (home, macros) | `BUSY` | OCUPADA |
| Demais casos com conexão | `IDLE` | OCIOSA |

## Telas atuais (implementadas)

- **Fábrica** — cena, HUD, abas, U1 animada (cabeçote e peça), overlays
  (noite na ociosidade/offline, véu vermelho no erro, confete na conclusão).
- **Detalhes** — toque na fábrica; arquivo, temps atuais e alvo, progresso,
  camada, IP, ficha do rolo.
- **Armazém** — lista rolável (2 colunas ou compacta), busca (NFD, todas
  as palavras), filtros de material/marca/local, ordenação, arquivados,
  ≤ 200 g, limpar, atualizar, + Novo rolo. Toque no cartão abre a **ficha**
  (editar local/comentário/peso opcional, vincular NFC, ativar no feeder,
  carregar/retirar, excluir com o id digitado). Todas as escritas pedem
  confirmação. A lista é buscada ao abrir a aba e no botão, não em loop.
- **Diário** — 60 eventos locais, busca e filtro (problema / associação /
  leitura / outros). Compartilhar diagnóstico não existe neste painel.
- **Wi-Fi** — rede e IP atuais, varredura, senha, salvar e reiniciar.

## Mapeamento do app Spool Factory → APIs

Fonte: código Java do app clonado em `/workspace/u1-screen/spool-factory`
(versão 1.8.0, MIT). O app fala com **duas** bases: a U1 (Moonraker) e o
Spoolman **direto**. No painel Waveshare o Spoolman tem que passar pelo
proxy acima (`/api/v1/…` do app vira `/v1/…` no proxy).

Há um *Inventário* (`InventoryManager`) que **nunca** chama a impressora, e
um conjunto de chamadas U1 para associação e alimentação.

### Telas e funções do app

| Tela / diálogo | O que faz | Endpoints |
|---|---|---|
| **Fábrica** | Cena animada, 4 feeders, ler NFC, associar na inserção | `GET /printer/objects/query?filament_feed%20left&filament_feed%20right&filament_detect&print_task_config&print_stats&webhooks`; inventário Spoolman (GET) só para identificar o rolo pela tag ou pelo `filament_spool_id` |
| **Armazém** | Lista de rolos; busca (rolo, material, marca, cor, local); filtros material / marca / local; ordenar por nº, menor peso, material ou marca; “Mostrar arquivados”; “Até 200 g”; lista compacta; atualizar; + Novo rolo / NFC | `GET /api/v1/spool?allow_archived=true`. Filtros e ordenação são **locais** (não há query string de filtro). **Não existe escrita de arquivar** no código — só o filtro de visualização |
| **Ficha do rolo** | Detalhe (cor, peso, local, comentário, NFC); editar peso e local; vincular outra tag; escolher feeder; excluir | `GET /api/v1/spool/{id}`; `PATCH /api/v1/spool/{id}` (`location`, `comment`, `remaining_weight` opcional — se omitido, não sobrescreve o consumo ao vivo); `PATCH` com `extra.card_uids` no modo `bind`; `DELETE /api/v1/spool/{id}` (com confirmação do número e checagem de identidade) |
| **Novo rolo / Recepção** | Cadastrar com NFC (UID 4, 7 ou 10 bytes, único); fabricante e filamento novos ou existentes | `GET /api/v1/vendor`, `POST /api/v1/vendor`; `GET /api/v1/filament`, `GET /api/v1/filament/{id}`, `POST /api/v1/filament`; `POST /api/v1/spool` (`filament_id`, `initial_weight`, `used_weight`, `location`, `comment`, `extra.card_uids`); `GET /api/v1/field/spool` e `POST /api/v1/field/spool/card_uids` para garantir o campo customizado (tipo texto) |
| **Troca manual** | Escolher feeder (1–4) e rolo; associar; carregar / retirar filamento (bloqueado durante impressão ou pausa; exige Filament Detection = External) | `GET /firmware-config/api/settings` (modo External); `POST /printer/spoollink/set` `{channel, info, message}` — **esta é a “ativação”**, não o `POST /server/spoolman/spool_id` do Moonraker; `POST /printer/gcode/script` com `AUTO_FEEDING EXTRUDER=n LOAD=1` ou `UNLOAD=1` (timeout 240 s, pode aquecer e mover a U1) |
| **Leitura NFC (fábrica)** | Identificar tag, eventualmente injetar UID no detector da U1 | `POST /printer/filament_detect/set` `{channel, info: {CARD_UID, CARD_TYPE}}` |
| **Histórico (“Escritório”)** | Diário de trocas, leituras NFC e alertas | **Só local no celular.** Não consulta Spoolman nem a U1. Busca e filtro (associações / NFC / problemas); compartilhar diagnóstico |
| **Configurações** | Endereços e chaves da U1 e do Spoolman; “Testar conexões” | No app, dois bases. No Waveshare: só o IP do Moonraker; o Spoolman entra pelo proxy |

Resumo importante para a próxima etapa:

- **Ativar rolo** no app = `POST /printer/spoollink/set` na U1, não um POST
  de escrita no Spoolman e não `/server/spoolman/spool_id`.
- **Arquivar** não é uma API usada pelo app (só filtro). Excluir é `DELETE`.
- **Peso** é `PATCH` com `remaining_weight` opcional.
- **Histórico** não tem endpoint — se for reproduzido no painel, seria log
  local na NVS/SPIFFS.

### Tradução para o proxy

| App (base Spoolman) | Waveshare (`POST /server/spoolman/proxy`) |
|---|---|
| `GET /api/v1/spool?allow_archived=true` | `request_method=GET`, `path=/v1/spool`, `query=allow_archived=true` |
| `GET /api/v1/spool/{id}` | `GET`, `path=/v1/spool/{id}` (já usado no HUD do rolo ativo) |
| `PATCH /api/v1/spool/{id}` | `PATCH`, `path=/v1/spool/{id}`, `body={…}` — **não nesta versão** |
| `POST /api/v1/spool` | `POST`, `path=/v1/spool` — **não nesta versão** |
| `DELETE /api/v1/spool/{id}` | `DELETE`, `path=/v1/spool/{id}` — **não nesta versão** |
| `GET/POST /api/v1/vendor`, `/filament` | idem, path `/v1/vendor`, `/v1/filament` — **não nesta versão** |
| Chamadas `/printer/…` e `/firmware-config/…` | Direto na U1 (já é Moonraker), sem proxy — **não nesta versão** |

## Proposta de navegação touch 800×480 (armazém)

Não implementada. Pré-visualizações: `../preview/proposta-armazem.png` e
`../preview/proposta-ficha.png` (faixa dourada “PROPOSTA — não implementado”).

1. **Abas** já existentes, canto superior direito: `FABRICA` | `ARMAZEM`.
   O armazém ocupa a tela inteira (sem a U1 gigante), fundo navy da paleta.
2. **Barra de busca** no topo (material, marca, cor, local, nº) e botão
   dourado **LER NFC** (futuro PN532 — ver nota abaixo).
3. **Chips de filtro** numa única linha rolável: material (PLA, PETG, …),
   “≤ 200 g”, “Arquivados”, ordenação (nº / peso / material / marca).
   Tudo client-side sobre o `GET /v1/spool?allow_archived=true`.
4. **Lista em duas colunas**, cartões de ~376×96, rolagem vertical. Cada
   cartão: swatch da cor, `#id`, material, fabricante, gramas, local;
   borda alerta e “POUCO FILAMENTO” se `< 100 g` (o app usa 200 g no
   filtro; o HUD da fábrica já alerta em 100 g — manter os dois limiares
   explícitos na UI).
5. **Toque no cartão** abre a **Ficha**: swatch grande, peso restante /
   inicial, local, comentário, UIDs NFC, feeder atual (consulta
   `print_task_config`, só leitura). Botões para a *próxima* versão:
   - **Editar peso** — teclado numérico + `PATCH /v1/spool/{id}`.
   - **Associar feeder** — escolhe 1–4 e, com confirmação, `POST /printer/spoollink/set`.
   - **Vincular NFC** — espera o PN532 (ou aceita UID digitado) e `PATCH` em `extra.card_uids`.
   - **Excluir** — confirmação digitando o número, `DELETE`.
6. **Novo rolo** (botão `+`, ainda futuro): fluxo da Recepção do app,
   em páginas empilhadas (fabricante → filamento → pesos → NFC).
7. **Histórico** não cabe como terceira aba permanente neste retângulo;
   se entrar, seria uma página secundária a partir da ficha ou de um
   ícone discreto, alimentada por log local.

Nesta versão a aba ARMAZEM já lista, filtra e escreve (com confirmação), no mesmo espírito do app.

## Nota: módulo NFC PN532 (só planejamento, nada ligado)

A placa traz o barramento I2C do GT911 e do expansor CH422G nos GPIOs
**8 (SDA)** e **9 (SCL)**, e o **mesmo** barramento sai no conector
PH2.0 rotulado I2C. O scan do wiki ocupa **0x20–0x27** (CH422G em 0x20) e
**0x30–0x3F**; o GT911 fica em **0x5D**.

O PN532 em I2C usa o endereço **0x24 por padrão**, que **conflita** com a
faixa do CH422G. **Não ligue o PN532 em I2C no conector I2C da placa.**
O endereço do PN532 em geral não é alterável com confiança.

Caminhos seguros para uma próxima etapa (escolher um e documentar os
pinos no esquemático antes de soldar):

1. **PN532 em SPI** — MOSI / SCK / MISO nos GPIOs **11 / 12 / 13** (header
   SPI, se estiver livre no seu lote). O CS **não** pode ser o EXIO4 do
   CH422G, que já é o CS do cartão SD. Escolher um GPIO livre ou outro
   EXIO claramente disponível.
2. **PN532 em UART** — UART1 da ESP32-S3 em dois GPIOs livres (fora da
   matriz RGB: VSYNC 3, HSYNC 46, DE 5, PCLK 7, DATA0–15 = 14, 38, 18,
   17, 10, 39, 0, 45, 48, 47, 21, 1, 2, 42, 41, 40; fora também de 8, 9 e
   4, que é o IRQ do GT911).
3. **Segundo host I2C** da ESP32-S3 em **dois GPIOs livres**, não 8/9. Aí
   o 0x24 do PN532 não esbarra no CH422G.

No firmware, o encaixe natural é um `nfc.cpp` em task própria: lê o UID,
consulta `GET /v1/spool?allow_archived=true`, casa com `extra.card_uids` e
abre a ficha (ou “cadastrar esta tag”). A primeira fatia NFC pode ser só
leitura; o `PATCH` de vínculo fica junto das outras escritas.

## Testes (não usam a U1 real nem o PC do Caio)

```bash
# na pasta firmware/: gcc, g++-14 (parser) e Python 3
./tests/run.sh
```

- `tests/test_printer_state.c` — 17 casos da máquina de estados (incluindo
  cancelada, busy, maiúsculas, limiar de 0,05 mm de filamento).
- `tests/test_pixels.py` — decodifica bins TRUE_COLOR_ALPHA (lo,hi,α) e simula o recolor do firmware (cabelo escuro, flanges, sprites curtas).
- `tests/test_spool_parse.cpp` — parser v2/array cru, filtros, busca com
  acento, histórico, `spool_build_link_info`. Compila com `g++-14` e
  ArduinoJson (já no `.pio/libdeps`).
- `scripts/fake_moonraker.py` — objetos, feeders, firmware-config, proxy
  v2 (GET/PATCH/POST/DELETE), spoollink e gcode. `scripts/test_protocol.py`
  sobe esse servidor e valida o JSON.

Para brincar com cenários: `python3 scripts/fake_moonraker.py --port 7125 --scenario printing --spool 40`.

Pré-visualizações (composição Python fiel a `layout.h` / `ui.cpp`, fonte
Press Start 2P OFL) em `/workspace/u1-screen/preview/` e `docs/preview/`:

| Arquivo | Conteúdo |
|---|---|
| `estado-offline.png` | Noite, SEM CONEXAO, U1 centrada |
| `estado-ociosa.png` | Noite, FABRICA DORMINDO, gaveta com rolos |
| `estado-aquecendo.png` | Cabeçote alinhado, AQUECENDO O BICO |
| `estado-imprimindo.png` | 67 %, peça crescendo, cabeçote, RESTAM 1h 23min |
| `estado-pausada.png` | Job pausado, cabeçote congelado |
| `estado-concluida.png` | Confete, peça completa, 100 % |
| `estado-erro.png` | Véu vermelho, estado ERRO |
| `estado-pouco-filamento.png` | Alerta laranja, PETG 72 g |
| `estado-troca-rolo.png` | Noite, gaveta com rolos |
| `anim-imprimindo.gif` | Cabeçote alinhado (carro + bico) em ping-pong |
| `tela-detalhes.png` | Página de detalhes |
| `tela-armazem.png` | Lista com filtros, cartões e EM USO |
| `tela-wifi.png` | Menu Wi-Fi (varredura 2,4 GHz) |
| `tela-diario.png` | Histórico local |
| `indice.png` | Folha de contato |

Regenerar: `python3 scripts/render_preview.py`.

## Uso de memória (build real neste repositório, v0.4.3)

```
RAM:   24.3 %   79 784 / 327 680 bytes   (DRAM interna)
Flash: 28.7 %    2 086 774 / 7 274 496 bytes   (partição app de ~6,94 MB, flash de 16 MB)
```

`build/u1_factory.bin` e `build/u1_factory_0x10000.bin` têm 2 087 136 bytes
(header `e9 05 02 4f`). A imagem mesclada para gravar em 0x0 é
`build/u1_factory_merged_v2.bin` (2 152 672 bytes, header `e9 04 02 4f` =
DIO, 16 MB, 80 MHz). Para atualizar sem perder o Wi-Fi: grave só o app em
`0x10000`. Snapshots do estoque, buffer HTTP (~192 KB) e recoloração das
bobinas da gaveta ficam na PSRAM, fora da task do LVGL (stack de 6 KB).
Em relação à v0.4.1, o flash caiu ~1,41 MB (sprites que não entram mais
na imagem). Compilação: `[SUCCESS]`, sem warnings no código deste projeto.
**Não grave a placa a partir desta máquina** — a imagem está pronta para
o Caio gravar.

## O que não foi verificado

- **Não gravamos a placa.** Sem teste do painel RGB, do GT911, do CH422G,
  da PSRAM em runtime (buffers LVGL e recoloração), do backlight ou do
  toque.
- **Não falamos com a U1 de verdade a partir desta máquina** (regra da
  tarefa). O protocolo foi exercitado só contra o FakeMoonraker.
- Portal Wi-Fi, varredura da /24 e gravação NVS: implementados, não
  testados em hardware / LAN reais.
- A recoloração das bobinas na gaveta só troca pixels marcados com
  alpha 254. O swatch do HUD é o sinal principal da cor do rolo.
- O PN532 ainda não está ligado (gancho `nfc_hook` pronto; nunca no I2C
  GPIO 8/9). O UID é digitado à mão até o módulo chegar.
- Compartilhar diagnóstico do app não existe neste painel (não há share
  sheet). O restante do Armazém cobre o que o Spool Factory faz, via
  proxy.


## Paridade com o Spool Factory (app → painel)

| Recurso do app | Painel v0.2 | Nota |
|---|---|---|
| URLs / chaves API nas configurações | N/A | Proxy Moonraker + descoberta + menu Wi-Fi |
| Idioma | parcial | Só português |
| Lista `GET /v1/spool?allow_archived=true` | feito | Filtro de arquivados é local |
| Busca NFD, todas as palavras | feito | |
| Filtros material / marca / local | feito | |
| Ordenar id / peso / material / marca | feito | |
| Mostrar arquivados | feito | Não há escrita de arquivar (o app também não escreve) |
| Filtro ≤ 200 g | feito | Alerta do HUD continua 100 g |
| Lista compacta | feito | |
| Limpar filtros | feito | |
| Atualizar (não em loop) | feito | Ao abrir a aba e no botão |
| Ficha do rolo | feito | |
| Editar local, comentário, peso opcional | feito | Sem peso / desmarcado = não sobrescreve o consumo |
| Novo rolo (vendor/filament find-or-create, UID, pesos) | feito | UID 4/7/10 bytes, unicidade, remaining ≤ initial |
| Excluir com id digitado + identidade | feito | Bloqueado se o rolo está num feeder |
| Vínculo NFC `extra.card_uids` | feito | Formato JSON-quoted; `tagField`; preserva extra |
| Entrada manual de UID | feito | PN532 ainda é um gancho |
| Associação automática (EdgeDetector) | parcial | Inspect por UID + gancho; sem hardware e sem auto na fábrica |
| Ativar rolo (`/printer/spoollink/set`) | feito | Checagens (External, filamento, idle, outro feeder) + confirmação |
| Carga / descarga (`AUTO_FEEDING`, 240 s) | feito | Mesmas checagens + confirmação |
| Estado dos 4 feeders | feito | Poll Moonraker |
| Filament Detection = External | feito | `GET /firmware-config/api/settings` |
| Histórico local 60 linhas | feito | SPIFFS, busca e tipo |
| Compartilhar diagnóstico | falta | Sem share sheet no painel |
| Confirmação antes de toda escrita | feito | |
| Teclado / campos de edição | feito | `lv_keyboard` + textareas (Montserrat 14) |

## Licenças

- Press Start 2P — SIL Open Font License (já no app Spool Factory).
- Port LVGL (`lvgl_v8_port.*`) — CC0, exemplo oficial ESP32_Display_Panel.
- Spool Factory — MIT; este firmware não copia o app, só o visual e o
  mapa de APIs.
