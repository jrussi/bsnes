# Sprite pack (substituição de sprites por imagens em alta resolução)

Este fork do [bsnes](https://github.com/bsnes-emu/bsnes) (v115) acrescenta ao renderizador `ppu-fast` um recurso estilo *texture pack*:
sprites do jogo são agrupados em personagens, e cada grupo reconhecido é trocado por uma imagem PNG em alta resolução
desenhada num quadro ampliado (por padrão 4x: 1024x960), respeitando a prioridade/camadas do jogo. O pack incluído
(`sprite-pack/lontra`) é um personagem original, a **Lontra**, que imita as poses de caminhada e as poses paradas do
jogador em **Goof Troop (USA)**.

> **Atenção: é necessário ter a ROM de Goof Troop (USA).** Ela **não** está incluída neste repositório nem no release,
> e não será fornecida. Use a ROM do seu próprio cartucho. O pack só reconhece essa versão (USA) do jogo.

## Como rodar (Windows)

### Opção A: release pronto (sem compilar)

1. Baixe `bsnes-sprite-pack-v1.zip` na página de [Releases](https://github.com/jrussi/bsnes/releases) e extraia numa pasta.
2. Tenha em mãos a sua ROM de **Goof Troop (USA)** (`.sfc` ou `.smc`). Sem ela não há o que rodar.
3. Dê dois cliques em `rodar_com_pack.bat`. **Não abra o `bsnes.exe` direto**: sem o `.bat` o pack não é carregado.
4. No bsnes, use o menu *Sistema > Carregar Jogo...* (*System > Load Game...*) e escolha a ROM.
   Ou arraste a ROM sobre o `rodar_com_pack.bat`.
5. Dentro do jogo, quando o personagem do jogador (lado/frente/costas, andando ou parado) for reconhecido, ele aparece como a Lontra em alta resolução.
   O renderizador padrão (`ppu-fast`) já é o usado; não precisa mudar nada nas configurações.

### Opção B: compilar

1. Compile o bsnes (MSYS2 MINGW64):
   ```
   pacman -S --needed mingw-w64-x86_64-gcc make
   cd bsnes
   make -j8 openmp=false
   ```
   O executável sai em `bsnes/out/bsnes.exe`.
2. Tenha a ROM de **Goof Troop (USA)**. Dê dois cliques em `sprite-pack/rodar_com_pack.bat` e carregue a ROM pelo menu, ou rode pelo terminal passando a ROM:
   ```
   sprite-pack\rodar_com_pack.bat "C:\caminho\Goof Troop (USA).sfc"
   ```
   O batch só define a variável `BSNES_SPRITE_PACK` apontando para `sprite-pack\lontra` e abre o `bsnes.exe`.

Sem a variável, o bsnes se comporta como o original.

## Limitações

- Só funciona com o renderizador `ppu-fast` (o padrão) e em modo normal (sem hires, interlace ou HD Mode 7).
- Com o pack ativo o quadro sai ampliado (escala do pack) e os filtros de software do bsnes ficam desativados.
- Os ids dos grupos são hashes das peças de sprite do jogo, então o pack incluído só reconhece Goof Troop (USA).
  Poses que não estão no pack aparecem com o sprite original.
- É uma prova de conceito: o personagem substitui só as poses mapeadas; inimigos e itens não são trocados.

## Variáveis de ambiente

- `BSNES_SPRITE_PACK`: pasta com o `pack.txt` e os PNGs. Cada linha do `pack.txt`: `id png escala largura altura margem`
  (`largura`/`altura` são as dimensões do PNG em pixels; `margem` é a borda extra em pixels nativos, 8 nas imagens do pack; todas as
  linhas devem ter a mesma escala, de 2 a 8). Com o pack ativo o emulador grava nessa pasta os logs `recognized-*.csv` e
  `unrecognized-*.csv` (grupos vistos que estão ou não no pack), que servem para descobrir quais poses faltam.
- `BSNES_SPRITE_DUMP`: pasta que já exista; grava cada peça de sprite em PNG, um `manifest.csv` e `frames-*.csv` (as peças de cada quadro).
  Serve para montar novos packs.

## Criar ou regenerar um pack

Em `sprite-pack/tools` (Python 3, `librsvg` com `rsvg-convert` e Pillow):

- `listar_grupos.py frames.csv grupos.json` lista os grupos de sprites vistos num dump.
- `render_pack.py tabela.json grupos.json pasta_saida [escala]` renderiza os SVGs (em `svg/`) para PNG com a margem certa e escreve `pack.txt`.
  A `tabela.json` liga o id de cada grupo ao SVG que o substitui. O viewBox do SVG deve ter o tamanho do grupo em pixels nativos.
- `lontra.py` e `lontra2.py` geram os SVGs da lontra (pose de lado, de frente e de costas).

O pack incluído foi gerado com `python3 render_pack.py tabela.json grupos.json ../lontra 4`.

## Arquivos do emulador alterados

- `bsnes/sfc/ppu-fast/spritedump.cpp` (novo): dump, agrupamento, leitura do pack e composição.
- `bsnes/sfc/ppu-fast/ppu.cpp`, `ppu.hpp`, `line.cpp`: ganchos para o dump e uma segunda passada sem sprites (`skipObjects`/`renderRedirect`).
- `nall/arithmetic.hpp`: `#include <stdexcept>` (necessário para compilar com GCC recente).

Licença: GPLv3 ou posterior, como o bsnes. As imagens da lontra são originais deste fork.
