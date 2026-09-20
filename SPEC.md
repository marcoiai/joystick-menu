# Joystick Menu - Especificacao tecnica

## Objetivo

Aplicacao desktop em C para macOS, Linux e Windows. O programa usa SDL3 para criar a janela, navegar por joystick, listar ROMs por sistema, mostrar capas e iniciar jogos pelo MAME.

O objetivo atual e gerar um binario nativo macOS no Apple Silicon.

## Diagnostico do macOS

- `joystick_menu` e `cover-scraper` existentes eram binarios ELF Linux ARM64; nao rodam no macOS.
- O framework local `SDL3.framework` fornece apenas SDL3.
- O projeto tambem precisa de SDL3_image, SDL3_ttf e SDL3_mixer.
- Homebrew esta instalado em `/opt/homebrew`.
- As formulas instaladas sao `sdl3`, `sdl3_image`, `sdl3_ttf`, `sdl3_mixer` e `pkgconf`.
- Os nomes usados pelo `pkg-config` sao `sdl3`, `sdl3-image`, `sdl3-ttf` e `sdl3-mixer`.
- A instalacao pode pedir confirmacao para atualizar dependencias. Se isso ocorrer, confirme com `y` e aguarde o comando terminar.

## Dependencias

```sh
xcode-select --install
brew install pkg-config sdl3 sdl3_image sdl3_ttf sdl3_mixer
```

Tambem sao necessarios:

- MAME instalado e disponivel no `PATH` como `mame`.
- PCSX2 instalado para executar a entrada PlayStation 2. Por padrao, o programa usa `/Applications/PCSX2-v2.6.3.app/Contents/MacOS/PCSX2`; o caminho pode ser alterado com `PCSX2_BIN`.
- RPCS3 instalado para executar a entrada PlayStation 3. Por padrao, o programa usa `/Applications/RPCS3.app/Contents/MacOS/rpcs3`; o caminho pode ser alterado com `RPCS3_BIN`.
- shadPS4 instalado para executar a entrada PlayStation 4. Por padrao, o programa usa `~/Downloads/shadps4-macos-sdl-0/shadps4`; o caminho pode ser alterado com `SHADPS4_BIN`.
- O alvo `make install-macos-deps` instala SDL3, suas extensoes e PCSX2 via Homebrew; baixa a release oficial mais recente do RPCS3 pela API do GitHub e instala `RPCS3.app` em `/Applications`.
- Assets em `assets/`: `logo.png`, `background.jpg`, `cover.png` e `Roboto-Regular.ttf`.
- ROMs em `roms/<sistema>/` ou `~/mame/roms/<sistema>/`.
- O caminho pode ser sobrescrito com a variável de ambiente `ROM_ROOT`.
- BIOS do MAME conforme a instalacao local.
- Capas opcionais em `covers/`, usando o mesmo nome-base da ROM e `.png` ou `.jpg`.

## Build

O `Makefile` detecta o sistema com `uname`.

### macOS

O alvo macOS usa `clang` e descobre includes e bibliotecas via `pkg-config`:

```sh
make clean
make
file joystick_menu
./joystick_menu
```

Validacao das dependencias:

```sh
pkg-config --modversion sdl3 sdl3-image sdl3-ttf sdl3-mixer
pkg-config --cflags sdl3 sdl3-image sdl3-ttf sdl3-mixer
pkg-config --libs sdl3 sdl3-image sdl3-ttf sdl3-mixer
```

O primeiro comando deve retornar uma versao para cada modulo. O `file joystick_menu` deve identificar um executavel Mach-O.

### Windows

O ramo existente continua usando `x86_64-w64-mingw32-gcc`, `sdl3-win/include`, `sdl3-win/lib` e produz `joystick_menu.exe`.

## Arquitetura

- `main`: inicializa SDL, cria janela/renderizador, carrega fonte e imagens e executa o loop.
- `draw_system_menu`: desenha sistemas, scraper e saida.
- `load_rom_list`: le `ROM_ROOT`, `./roms/` ou `~/mame/roms/`, e aceita extensoes de `SystemEntry`, incluindo `.rom`.
- `draw_rom_menu`: desenha ROMs, barra de rolagem e capa selecionada.
- `handle_events`: trata setas, `Enter` e `ESC`; `ESC` volta do submenu para o menu de sistemas.
- `handle_joystick_input`: trata eixo vertical e botao principal.
- `load_cover_for_rom`: procura capas em `./covers/`.
- `system()`: chama o MAME para os sistemas arcade/retro, o PCSX2 para PlayStation 2, o RPCS3 para PlayStation 3 e o shadPS4 para PlayStation 4.
- `fork`/`execl`: executa `./cover-scraper`.

## Sistemas configurados

| Diretorio | Sistema MAME | Argumento | Extensoes |
| --- | --- | --- | --- |
| `sms1` | `sms1` | `-cart` | `sms`, `bin`, `zip` |
| `genesis` | `genesis` | `-cart` | `md`, `bin`, `zip` |
| `snes` | `snes` | `-cart` | `smc`, `sfc`, `zip` |
| `nes` | `nes` | `-cart` | `nes`, `zip` |
| `segacd` | `segacd` | `-cdrom` | `cue`, `chd`, `iso` |
| `ps1` | `psu` | `-cdrom` | `cue`, `chd`, `iso` |
| `ps2` | `ps2` | `-cdrom` | `cue`, `chd`, `iso`, `bin` |
| `ps3` | `rpcs3` | especial | `iso`, `pkg` |
| `ps4` | `shadps4` | especial | `elf`, `bin`, `pkg` |
| `neogeo` | `neogeo` | especial | `neo` |

## Estrutura esperada

```text
.
├── Makefile
├── SPEC.md
├── README.md
├── joystick_menu.c
├── assets/
├── bios/
├── covers/
├── roms/ ou ~/mame/roms/
│   ├── genesis/
│   ├── nes/
│   ├── neogeo/
│   ├── ps1/
│   ├── ps4/
│   ├── segacd/
│   ├── sms1/
│   └── snes/
└── cover-scraper
```

## Criterios de aceite

1. `pkg-config --modversion` encontra os quatro modulos SDL.
2. `make clean && make` termina com sucesso.
3. `file joystick_menu` mostra um binario Mach-O.
4. `./joystick_menu` abre a janela sem erro.
5. Logo, fundo e fonte sao carregados.
6. O joystick navega e seleciona sistemas.
7. Uma ROM valida e iniciada pelo `mame`.
8. `cover-scraper` so e usado depois de ser recompilado para macOS.

## Pendencias conhecidas

- O codigo nao valida todos os retornos de inicializacao SDL/TTF.
- Assets e ROMs usam caminhos relativos; execute o binario na raiz do projeto.
- `system()` depende do `PATH` e pode falhar com nomes de ROM contendo caracteres especiais.
- A musica esta desativada, embora SDL3_mixer continue no link.
- Nao existem testes automatizados; a validacao atual e build e teste manual.
