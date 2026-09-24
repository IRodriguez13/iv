# iv

> Documentación en español. English: [README.md](README.md)

Editor de texto orientado a líneas para la línea de comandos. En tiempo de
ejecución depende solo de libc. Las ediciones se componen con tuberías.

## Licencia

GPL-3.0-or-later. Véase [LICENSE](LICENSE).
Copyright (C) 2026 Iván Ezequiel Rodriguez.

`--help`, `--version` y los diagnósticos están en inglés. Las distros
traducen el manual (`man -L es iv`), no `--version` (los scripts leen la
primera línea).

## Compilación

```bash
make
make test             # smoke + safety + completions + misc
make test-musl        # la misma suite, binario musl-gcc
make bench            # iv vs GNU sed / mawk (1M+3M, equivalencia y luego tiempo)
make install          # PREFIX por defecto: ~/.local
PREFIX=/usr/local make install
./install.sh
make clean
```

## Completions

Las instala `make install`:

| Shell | Ruta (`PREFIX=~/.local`) |
|-------|--------------------------|
| bash  | `~/.local/share/bash-completion/completions/iv` |
| zsh   | `~/.local/share/zsh/site-functions/_iv` |
| fish  | `~/.local/share/fish/vendor_completions.d/iv.fish` |

zsh: añadir ese directorio a `fpath` antes de `compinit`. bash necesita el
paquete `bash-completion`. fish carga `vendor_completions.d` solo.

## Invocación

### Visualización

| Comando | Efecto |
|---------|--------|
| `iv -v archivo` | Imprime el archivo con números de línea |
| `iv -v archivo --no-numbers` | Sin números |
| `iv -va inicio-fin archivo` | Imprime un rango |
| `iv -V`, `iv --version` | Versión y licencia |
| `iv -h`, `iv --help` | Uso |

Contar o buscar con las herramientas de siempre: `iv -v archivo --no-numbers | wc -l`,
`… | grep`.

### Edición

| Comando | Efecto |
|---------|--------|
| `iv -i archivo texto` | Añade *texto* (alias `-insert`) |
| `iv -i archivo inicio-fin texto` | Inserta *texto* antes de la línea *inicio* |
| `iv -a archivo texto` | Añade *texto* |
| `iv -p archivo [archivo...] [rango] contenido` | Parchea uno o más archivos; *rango* opcional |
| `iv -pi archivo [archivo...] línea contenido` | Inserta *contenido* antes de *línea* (no reemplaza) |
| `iv -d archivo [inicio-fin]` | Elimina líneas (alias `-delete`) |
| `iv -d archivo -m patrón` | Elimina líneas que contienen *patrón* |
| `iv -r archivo [inicio-fin] texto` | Reemplaza líneas (alias `-replace`) |
| `iv -r archivo -m patrón texto` | Reemplaza las líneas coincidentes |
| `iv -s archivo patrón reemplazo` | Sustitución literal (primera coincidencia por línea) |
| `iv -s archivo patrón reemplazo -m filtro` | Solo en líneas que contienen *filtro* |
| `iv -s archivo -F ',' 2 X` | Reemplaza el campo 2 (delimitador de un byte; no es CSV con comillas) |
| `iv -s archivo pat repl -e pat2 repl2` | Pares adicionales |
| `iv -s archivo patrón reemplazo -E` | ERE POSIX; el reemplazo admite `\1`–`\9` y `&` |
| `iv -s archivo patrón reemplazo -g` | Todas las coincidencias de la línea |

Un patrón de sustitución vacío se rechaza (salida 1). `-m` usa el mismo
lenguaje que la sustitución (literal, o ERE con `-E`).

### Opciones

| Opción | Efecto |
|--------|--------|
| `--dry-run` | Imprime el resultado; no escribe el archivo |
| `-b` | Backup GNU, método `existing` |
| `--backup[=METHOD]` | Backup GNU. Sin método: `$VERSION_CONTROL`, si no `existing` |
| `-S SUFFIX`, `--suffix=SUFFIX` | Sufijo (también activa el backup). Por defecto `$SIMPLE_BACKUP_SUFFIX` o `~` |
| `--no-numbers` | Sin números de línea (`-v`, `-va`) |
| `-q` | Sin eco tipo tee (`-i`, `-a`, `-r`, `-p`, `-pi`); sin `Replaced N` en `-s` |
| `--stdout` | Escribe el resultado a stdout; no modifica el archivo |
| `-g` | Sustitución global |
| `-E`, `--regex` | ERE POSIX en `-s` y `-m` |

## Rangos

Base 1.

| Forma | Significado |
|-------|-------------|
| `1-5` | Líneas 1–5 |
| `5` | Línea 5 |
| `-3` | Tercera línea desde el final |
| `-3--1` | Últimas tres líneas |
| `-5-` | Últimas cinco líneas |
| `2-` | Desde la línea 2 hasta EOF |

## Argumentos de texto

En `-i`, `-a`, `-r` y el contenido de un parche:

| Argumento | Significado |
|-----------|-------------|
| `-` | Lee stdin |
| Ruta de un archivo existente | Lee ese archivo |
| Cualquier otra cadena | Texto literal |

Un archivo cuyo nombre es `-` se pasa como `./-`.

```bash
echo "línea nueva" | iv -p file
iv -p main.c snippet.c
iv -p main.c 5 snippet.c
iv -p main.c 1-3 plantilla.txt
iv -pi main.c 1 "#include <foo.h>"
iv -s file "[0-9]+" "X" -E
iv -s file a b -e c d
cat file | iv -s - old new --stdout
iv -s file a b --stdout | iv -s - b c --stdout
```

## Escapes

En texto de inserción/reemplazo: `\n` nueva línea, `\t` tabulador, `\\`
barra invertida, `\r` retorno de carro.

## Stdout

`-i`, `-a`, `-r`, `-p` y `-pi` escriben en stdout el texto añadido (como
`tee`) salvo `-q`.

`--stdout` escribe el archivo transformado a stdout. Si el lector cierra la
tubería, iv sale 0 y no emite `Broken pipe`.

```bash
iv -s huge foo bar --stdout | head -n 1
```

## Backups

Las ediciones in-place no escriben backup salvo que se pida (`-b`,
`--backup` o `-S`). Nombres y métodos son los de GNU Coreutils
(`cp`, `mv`, `install`):

| Método | También | Efecto |
|--------|---------|--------|
| `none` | `off` | Sin backup, aunque se haya dado `-b` antes |
| `numbered` | `t` | `archivo.~1~`, `archivo.~2~`, … |
| `existing` | `nil` | Numerado si ya existe `archivo.~N~`; si no, simple |
| `simple` | `never` | `archivo` + sufijo (`~` salvo `-S` / `SIMPLE_BACKUP_SUFFIX`) |

`-b` es `--backup=existing`. Se aceptan abreviaturas únicas.
`--stdout` y `--dry-run` no escriben backup. Una sustitución sin
coincidencias no sobrescribe el archivo y no escribe backup.

Restaurar y comparar con el filesystem: `mv archivo~ archivo`,
`diff -u archivo~ archivo`.

## Escritura in-place

leer → transformar → temporal exclusivo (`openat` + `O_EXCL` en el
directorio padre) → `fsync` → misma inode → `renameat`.

Si la edición falla, la ruta original no cambia. Si se pidió backup, es
una copia del original tomada justo antes del rename.

- Solo se editan archivos regulares. Directorios, FIFOs y dispositivos se
  rechazan antes de abrir.
- Un archivo con un byte NUL se rechaza.
- Enlace simbólico: se reemplaza el referente; el inode del enlace se conserva.
- Enlace simbólico colgante: se rechaza. iv no lo sustituye por un archivo
  regular.
- Enlace duro: este pathname recibe un inode nuevo; los demás nombres
  conservan los bytes viejos.
- Se copian modo y owner del referente cuando el sistema lo permite. No se
  copian xattrs ni ACL.
- `SIGINT` / `SIGTERM` / `SIGHUP` borran un temporal `.iv.*` residual.
  `SIGKILL` puede dejar uno; la ruta original no se renombra.
- Esto no es un límite de seguridad en un directorio escribible por un
  tercero.

## Modelo de texto

iv es deliberadamente orientado a bytes. Fija `LC_ALL=C` y usa ERE POSIX
en locale C. Eso es la interfaz, no un detalle de implementación oculto.

- Una línea son bytes hasta `\n`, o hasta EOF si la última no tiene nueva línea.
- `-F` usa el primer byte del argumento delimitador.
- Las ERE POSIX (`-E`) se compilan y emparejan como bytes en locale C.
- UTF-8 inválido es dato. No se repara.

## Estado de salida

| Estado | Significado |
|--------|-------------|
| 0 | Éxito, incluido `EPIPE` en stdout |
| 1 | Error (uso, archivo inexistente, binario, rango inválido, escritura, patrón vacío, no regular) |

## Manuales

| Idioma | Ruta |
|--------|------|
| Inglés | `iv.1` → `man iv` |
| Español | `man/es/iv.1` → `man -L es iv` |
