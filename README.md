Basic commands

Requirements:
SDL3
SDL3_image
SDL3_ttf (earlier version used SDL_RenderDebugText)

export DYLD_FRAMEWORK_PATH=./ (here goes path of LD Library on Linux and DYlD Library paths - adjust it's inconsistent)
./joystick_menu

macOS:

Install the SDL3 libraries and `pkg-config` with Homebrew:

brew install pkg-config sdl3 sdl3_image sdl3_ttf sdl3_mixer
brew install --cask pcsx2

Build and run from the project directory:

make
./joystick_menu

The macOS build discovers the SDL include and library paths through `pkg-config`.

To install all macOS dependencies through the project:

make install-macos-deps

Set `PCSX2_BIN` if PCSX2 is installed in a different location.
Set `RPCS3_BIN` if RPCS3 is installed in a different location.
Set `SHADPS4_BIN` if shadPS4 is installed in a different location.
By default the app looks for shadPS4 at `~/Downloads/shadps4-macos-sdl-0/shadps4`.

The dependency target downloads the latest official macOS RPCS3 release and
places `RPCS3.app` in `/Applications` automatically.

Linux:
gcc joystick_menu.c -o joystick_menu -I/usr/local/include -L/usr/local/lib -lSDL3 -lSDL3_image

gcc joystick_menu.c -o joystick_menu -I/usr/local/include -L/usr/local/lib -lSDL3 -lSDL3_ttf -lSDL3_image

gcc joystick_menu.c -o joystick_menu -I/usr/local/include -L/usr/local/lib -lSDL3 -lSDL3_image -lSDL3_mixer -lSDL3_ttf (last to work)

For Windows on Linux:
sudo apt update
sudo apt install mingw-w64

For Windows on macOS with homebrew:
brew install mingw-w64

Download Windows SDL3 Libraries
You must use precompiled Windows versions of SDL3, SDL3_image, SDL3_mixer, SDL3_ttf:
https://github.com/libsdl-org/SDL/releases
https://github.com/libsdl-org/SDL_image/releases
https://github.com/libsdl-org/SDL_mixer/releases
https://github.com/libsdl-org/SDL_ttf/releases

the roms and bios reside in directory:
~/mame/roms in form of .zip files or .rom
For PlayStation 4, place games in `roms/ps4/`; `.elf`, `.bin` and `.pkg` files
are listed, including files inside one level of game folders.


# Metal Slug 3 — MAME / Neo Geo MVS

## Arquivos `.neo` vazios no joystick-menu

O projeto pode utilizar arquivos `.neo` vazios como **placeholders para descoberta de jogos pelo frontend**.

Exemplo:

```bash
touch mslug3.neo
```

O arquivo não contém os dados da ROM. Seu nome funciona como identificador do jogo para o `joystick-menu`.

Fluxo esperado:

```text
mslug3.neo (0 bytes)
      ↓
joystick-menu detecta *.neo
      ↓
Metal Slug 3 aparece no menu
      ↓
seleção do jogo
      ↓
launcher obtém o identificador "mslug3"
      ↓
MAME executa o set mslug3
      ↓
MAME procura os arquivos reais no rompath
```

Portanto, os arquivos `.neo` podem permanecer com **0 bytes**. Os dados reais do cartucho Neo Geo continuam sendo fornecidos pelo ROM set reconhecido pelo MAME.

## Executando Metal Slug 3

Com o ROM set em:

```text
~/Downloads/mslug3/
```

o diretório pai pode ser utilizado como `rompath`:

```bash
mame mslug3 -rompath ~/Downloads
```

O MAME então procura automaticamente:

```text
~/Downloads/mslug3/
```

## Compatibilidade do ROM set

Com **MAME 0.278 no macOS**, o set utilizado possuía:

```text
neo-sma
```

enquanto o MAME esperava:

```text
green.neo-sma
```

O erro era:

```text
green.neo-sma NOT FOUND (tried in mslug3 neogeo)
Fatal error: Required files are missing, the machine cannot be run.
```

Foi possível testar mantendo o original e criando uma cópia com o nome esperado:

```bash
cp ~/Downloads/mslug3/neo-sma \
   ~/Downloads/mslug3/green.neo-sma
```

Depois:

```bash
mame mslug3 -rompath ~/Downloads
```

## Neo Geo MVS — créditos

O jogo roda no MAME como uma máquina arcade **Neo Geo MVS**.

Controles padrão:

```text
5 = Coin 1
1 = Player 1 Start
```

Para começar:

```text
5 → 1
```

As teclas podem ser configuradas em:

```text
TAB
 → Input Settings
 → Input Assignments (this system)
```

Principalmente:

```text
Coin 1
1 Player Start
```

## Diagnóstico

Para verificar problemas com um ROM set:

```bash
mame mslug3 -rompath ~/Downloads -verbose
```

O modo `-verbose` mostra quais arquivos o MAME tentou carregar e onde os procurou.

### Resumo da arquitetura

```text
joystick-menu
     │
     ├── mslug3.neo       ← placeholder / catálogo
     │
     └── usuário seleciona Metal Slug 3
                   │
                   ▼
              MAME: mslug3
                   │
                   ▼
             ROM path real
                   │
                   ▼
        mslug3/256-*.*
                   │
                   ▼
             Neo Geo MVS
                   │
                   ▼
              Coin → Start
```

**Importante:** `.neo` vazio pertence à lógica de descoberta do `joystick-menu`; não é uma ROM e não substitui os arquivos necessários ao MAME.


Thanks to https://pixabay.com/music/ for free roalty music, check it out
Music by <a href="https://pixabay.com/users/jumpingbunny-47869633/?utm_source=link-attribution&utm_medium=referral&utm_campaign=music&utm_content=359782">jumpingbunny</a> from <a href="https://pixabay.com/music//?utm_source=link-attribution&utm_medium=referral&utm_campaign=music&utm_content=359782">Pixabay</a>
