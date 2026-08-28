# DoomBF

> Кто может запрограммировать что-то полезное на нём? :) \
> &mdash; Урбан Мюллер, 1993

---

На связи
[Мастерская системного программирования ИТМО](https://t.me/itmosysint)

> **Идея:** собрать *Doom* и запустить его поверх Brainfuck.
> Мы используем оригинальный исходный код Doom, компилируем его в Brainfuck, а затем исполняем BF‑код в собственной среде.

![DoomBF logo](./data/logo_v2.png)

**Репозиторий:** https://github.com/sit-itmo/DoomBF

Проект является совместным творчеством группы энтузиастов в рамках студенческого сообщества ФБИТ ИТМО.

## Статус проекта
Doom успешно компилируется и запускается в brainfuck

## Что было сделано
1. Взят оригинальный код Doom и добавлена С‑обвязка **CrtDoom**, которая сводит API к двум функциям (см. `doom/crt/doom_env.h`).
2. Написаны **два** оптимизированных интерпретатора Brainfuck на C для запуска Doom (см. папку `bf`)
3. Подготовлен ряд примеров на Brainfuck — в папке `b` (спасибо за развитие примеров и добавление варианта `JMP`)
4. Подготовлены GNU Make и CMake-сценарии сборки Doom для RISC-V; CMake-цепочка работает на Linux и Windows
5. Создан компилятор из risc-v в brainfuck (см. папку `RISC-BF`)
6. Реализован фронтенд для отображения игры doom (см. папку `frontend`)

*Код Doom брался изначльно из (https://github.com/NSG650/NtDOOM)
*В Linux система работает заметно быстрее чем в Windows в связи с более грамотным интерпретатором и использовнаием GCC расширения &&.

## Актуальные задачи (Roadmap)
- [x] Добавить сборку Doom/Brainfuck и WinAPI-frontend для Windows (CMake + `build_all.cmd`)
- [ ] Добавить нативный frontend и полный build pipeline для macOS

## Производительность
Doom запускается за несколько минут и выдает примерно один кадр в 40 секунд

## Быстрый старт
Требуется:
- Make
- riscv32-elf-gcc / riscv32-unknown-elf-gcc / riscv64-elf-gcc / riscv64-unknown-elf-gcc
- Python (capstone и pyelftools)
- X11Lib
- GNU Lightning

## Установка зависимостей

### Все дистрибутивы (Nix)

```bash
sh <(curl --proto '=https' --tlsv1.2 -L https://nixos.org/nix/install) --daemon
```

И после завершения, в новом терминале:

```bash
git clone https://github.com/sit-itmo/DoomBF
cd DoomBF
nix develop . --extra-experimental-features 'nix-command flakes'
```

### Debian/Ubuntu

```bash
git clone https://github.com/sit-itmo/DoomBF
cd DoomBF
./install_ubuntu
source .venv/bin/activate
```

## Запуск

```bash
make  # собрать Doom в brainfuck
make run  # собрать Doom в brainfuck и запустить
```

### Дополнительно

Запуск Doom в Linux (не на брейнфаке):
```bash
make lnx_doom
make lnx_run
```

Запуск Doom через фронтенд (но не на брейнфаке):
```bash
make fake_bfk_doom
make fake_bfk_run
```

Запуск тестов BF в Linux:
```bash
make ibf_test
```

### !Если вы запускаете в WSL

Все будет работать как указано ранее, но для запуска используются unix pipes. Поэтому:
1. Надо запускать на WSL 2.0
2. Запускать на Unix файловой системе - не в /mnt/c/... а например в ~/


## Для Windows

Требуется:
- CMake 3.20+ (https://cmake.org/);
- 64-битный host C compiler (рекомендуется Visual Studio Build Tools с workload **Desktop development with C++**; MinGW также поддерживается) (https://visualstudio.microsoft.com/);
- Python 3 (https://www.python.org/);

Полная сборка нативного Doom, `ibf.exe`, WinAPI-frontend, `bfk_doom.elf` и `doom.bpk`:

```bat
build_all.cmd
```

Чистая повторная конфигурация:

```bat
build_all.cmd clean
```

Результаты появляются в `build/windows/bin`. Запуск BF-версии:

```bat
run_bf.cmd
```

`run_bf.cmd` использует `tools/run_pipeline.py`, потому что цепочка двунаправленная: кадры идут `ibf -> frnt`, а события клавиатуры — `frnt -> ibf`. Обычного одностороннего `cmd` pipe для этого недостаточно.

Для MinGW перед чистой конфигурацией задайте генератор:

```bat
set "DOOMBF_GENERATOR=MinGW Makefiles"
build_all.cmd clean
```

### CMake (Windows и Linux)

По умолчанию CMake собирает нативный Doom, `frnt` и переносимую версию `ibf`; тяжёлая BF-цепочка выключена, пока явно не переданы WAD и RISC-V toolchain.

Только нативный Doom:

```sh
cmake -S . -B build/native -DCMAKE_BUILD_TYPE=Release -DDOOMBF_BUILD_BF=OFF
cmake --build build/native --config Release --target doom_native
```

Полная цепочка до `doom.bpk`:

```sh
cmake -S . -B build/full \
  -DCMAKE_BUILD_TYPE=Release \
  -DDOOMBF_BUILD_BF=ON \
  -DRISCV_GCC_EXECUTABLE=/full/path/to/riscv64-unknown-elf-gcc
cmake --build build/full --config Release --target doombf_all
```

Основные targets:
- `doom_native` — `win_doom.exe` на Windows или `lnx_doom` на Linux;
- `frnt` — WinAPI/X11 frontend для BF-протокола;
- `ibf` — industrial Brainfuck interpreter (CMake использует переносимый backend без GNU Lightning);
- `bfk_doom_elf` — RV32I ELF;
- `doom_bpk` — compressed Brainfuck;
- `doombf_all` — все включённые компоненты;
- `run_bf` — запуск двунаправленной BF-цепочки.

Существующий GNU Make workflow для Linux сохранён: корневой `Makefile` и его команды не менялись.

### Особенности сборки для Linux

*Рекомендуем использовать [`nix-shell`](https://nixos.org/download)*
*В этом случае все устанавливается автоматически*

Предварительно надо установить библиотеки. Для Debian/ubuntu:
```bash
sudo apt-get update
sudo apt-get install libx11-dev
```

Для установки GNU lightning см. [инструкцию](https://www.gnu.org/software/lightning/manual/html_node/Installation.html)

Замените riscv32-none-elf-gcc в файле doom/Makefile на ваш установленный risc-v gcc

Сейчас есть проблемы с загрузкой lightning - поэтому мы положили в toolchains/lightning заранее скаченный вариант (https://ftp.yandex.ru/mirrors/gnu/lightning/lightning-2.2.3.tar.gz)

### Особенности сборки для Windows

Основа сборки это CMake
Были трудности с Risc-V компилятором.
В папке toolchains/riscv мы положили заранее скаченный собранный под windwos toolchain (https://xpack-dev-tools.github.io/riscv-none-elf-gcc-xpack/docs/install/)

Но там нет нужной версии libgcc - поэтому используется libgcc из linux версии.

Сейчас в toolchains/ собрано все что нужно


## Как поучаствовать
Присоединяйтесь к обсуждению и обратной связи: https://t.me/itmosysint

## Зачем это всё?
Эзотерические языки программирования — любопытны и трудны для человека; мы исследуем их потенциал на практике и хотим довести до играбельного результата хотя бы одну легендарную игру на одном из самых «жёстких» языков.

## Команда
- Участники сообщества: https://t.me/itmosysint
- Координатор: Алексей Никольский — https://t.me/+2TZRYbxns6tlZjA6

Авторы (по алфавиту):
- Александр Кравченко
- Александр Суров
- Алексей Никольский
- Виталий
- Иван Сакаев

## Полезные ссылки
- https://brainfuck.org/brainfuck.html
- https://github.com/xoreaxeaxeax/movfuscator
- https://esolangs.org/wiki/Brainfuck_algorithms
- https://esolangs.org/wiki/BFFuck
- https://habr.com/ru/companies/badoo/articles/428878/
- https://spiiin.github.io/blog/621874082/
- https://github.com/srorso/SoftFloat
- https://codeberg.org/highghlow/esotope-bfc
- https://github.com/tomhea/c2fj
- https://xpack-dev-tools.github.io/riscv-none-elf-gcc-xpack/docs/install/
- https://github.com/aidantambling/Fuse-Wad-Explorer
- https://people.math.sc.edu/Burkardt/c_src/paranoia/paranoia.html
- https://web.archive.org/web/20260312041643/http://www.jhauser.us/arithmetic/SoftFloat-3/doc/SoftFloat.html

![DoomBF banner](./data/banner_v2.jpg)
