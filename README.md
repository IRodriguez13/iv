# iv

Editor de texto orientado a líneas para la línea de comandos. Sin dependencias externas. Pensado para composición con pipes y scripts.

## Compilación

```bash
make
make install   # instala en /usr/bin
make clean
```

## Comandos

### Visualización

| Comando | Descripción |
|---------|-------------|
| `iv -v file` | Muestra el archivo completo con números de línea |
| `iv -v file --no-numbers` | Muestra el archivo sin números de línea |
| `iv -va start-end file` | Muestra el rango de líneas indicado |
| `iv -wc file` | Cuenta las líneas del archivo |
| `iv -n file "pattern"` | Números de línea donde aparece el patrón |
| `iv -n file "pattern" --json` | Salida JSON: `{"lines":[1,5,7]}` (para jq, Python, etc.) |
| `iv -u file [N]` | Deshace: restaura desde el backup N (por defecto 1); N=1..10 |
| `iv -diff [-u] [N] file` | Compara backup N vs actual; `-u` = diff unificado |
| `iv -l [file] [--persist]` | Lista backups: solo ruta y tamaño |
| `iv -lsbak [file] [N] [--persist]` | Lista backups **con metadatos** (fecha y usuario); si indicas N, muestra el contenido de ese slot |
| `iv -rmbak [file] [--persist]` | Elimina backups (alias: `-z`). Sin archivo: todos; con archivo: solo los de ese archivo |
| `iv --persist file` | Mueve el repo de backups de ese archivo desde `/tmp` a `~/.local/share/iv/` |
| `iv --unpersist file` | Mueve el repo de backups de ese archivo desde `~/.local/share/iv/` a `/tmp` |
| `iv -V` / `iv --version` | Muestra versión |

### Edición

| Comando | Descripción |
|---------|-------------|
| `iv -i file "texto"` | Inserta texto al final (alias: `-insert`) |
| `iv -i file start-end "texto"` | Inserta texto antes de la línea `start` |
| `iv -a file "texto"` | Añade texto al final del archivo |
| `iv -p file [file...] [range] content` | Parchea uno o más archivos; range opcional |
| `iv -pi file [file...] line content` | Patch insert: inserta antes de la línea indicada (no reemplaza); la línea y el resto bajan |
| `iv -d file [start-end]` | Elimina líneas (alias: `-delete`) |
| `iv -d file -m "pattern"` | Elimina solo líneas que coinciden con el patrón |
| `iv -r file [start-end] "texto"` | Reemplaza líneas (alias: `-replace`) |
| `iv -r file -m "pattern" "texto"` | Reemplaza solo líneas que coinciden |
| `iv -s file patrón reemplazo` | Sustituye (literal) |
| `iv -s file patrón reemplazo -m "filter"` | Sustituye solo en líneas que contienen "filter" |
| `iv -s file -F ',' 2 "X"` | Sustituye campo 2 con "X" (CSV/TSV) |
| `iv -s file patrón reemplazo -e pat2 repl2` | Múltiples sustituciones (como sed -e) |
| `iv -s file patrón reemplazo -E` | Sustituye con regex |
| `iv -s file patrón reemplazo -g` | Sustituye todas las ocurrencias |

### Opciones globales

| Opción | Efecto |
|--------|--------|
| `--dry-run` | Muestra qué se haría sin modificar el archivo |
| `--no-backup` | No crea archivo `.bak` antes de editar |
| `--no-numbers` | Salida sin números de línea (solo con `-v` y `-va`) |
| `-q` | Suprime la salida tipo tee en `-i`, `-a`, `-r`, `-p` |
| `--stdout` | Escribe resultado a stdout sin modificar el archivo (composable en pipelines) |

## Rangos

Los rangos son 1-based. Sintaxis:

| Formato | Significado |
|--------|-------------|
| `1-5` | Líneas 1 a 5 |
| `5` | Línea 5 |
| `-3` | Tercera línea contando desde el final |
| `-3--1` | Últimas tres líneas |
| `-5-` | Últimas cinco líneas |
| `2-` | Desde la línea 2 hasta el final |

## Entrada: stdin o archivo

El argumento de texto en `-i`, `-a` y `-r` admite tres formas:

| Argumento | Comportamiento |
|-----------|----------------|
| `-` | Lee desde stdin |
| Ruta a archivo existente | Lee el contenido del archivo |
| Cualquier otro texto | Se usa como literal |

```bash
echo "línea nueva" | iv -p file           # sin arg = stdin
iv -p main.c snippet.c                    # append archivo
iv -p main.c 5 snippet.c                   # insertar en línea 5
iv -p main.c 1-3 plantilla.txt             # reemplazar líneas 1-3
iv -pi main.c 1 "#include <foo.h>"         # insertar línea 1 sin reemplazar (baja el resto)
iv -p f1.c f2.c snippet.c                 # parchear múltiples archivos
iv -s file "[0-9]+" "X" -E                 # regex
iv -s file "a" "b" -e "c" "d"             # múltiples sustituciones
cat file | iv -s - "old" "new" --stdout    # pipeline sin modificar archivo
```

## Secuencias de escape

En el texto de inserción o reemplazo se interpretan:

| Secuencia | Carácter |
|-----------|----------|
| `\n` | Nueva línea |
| `\t` | Tabulador |
| `\\` | Barra invertida |
| `\r` | Retorno de carro |

Ejemplo:

```bash
iv -i file "línea1\nlínea2\nlínea3"
```

## Comportamiento tipo tee

`-insert`, `-replace`, `-a`, `-p` y `-pi` escriben en stdout el texto añadido, de forma similar a `tee`. Usa `-q` para suprimir esta salida.

## Pipelines con --stdout

Con `--stdout`, el resultado se escribe a stdout sin modificar el archivo. Permite encadenar operaciones:

```bash
iv -s file "a" "b" --stdout | iv -s - "b" "c" --stdout
```

## Estructura del código

```
iv.h      — Declaraciones, constantes, IvOpts
main.c    — Entrada, parseo de argumentos, dispatch
view.c    — show_file, show_range, wc_lines, find_line_numbers, stream_file_with_numbers
edit.c    — backup, apply_patch, search_replace, search_replace_regex, list_backups
range.c   — parse_range
```

## Formato de diff

`iv -diff file` (o `iv -diff 2 file` para comparar con el backup 2) muestra antes y después con numeración de línea. Con `-u` usa formato unificado (compatible con `diff -u`):

```
--- /tmp/iv_demo.c.bak (anterior)
   1 | #include <stdio.h>
   2 | int main(void) {
   3 |     return 0;
   4 | }

--- demo.c (actual)
   1 | // v1.0
   2 | #include <stdio.h>
   3 | int main(void) {
   4 |     return 0;
   5 | }
```

## Backup

- El repo de backups puede ser **efímero** (por defecto) o **persistido**.
- Repo efímero: root en `/tmp/iv_<user>/` por defecto. Se puede cambiar con la variable de entorno `IV_BACKUP_DIR`.
- Repo persistido: root en `$XDG_DATA_HOME/iv` o `~/.local/share/iv/`.
- Los backups se guardan **por archivo** dentro de un subdirectorio derivado del nombre del repo y el path del archivo.
- Cada slot se guarda como `N.bak` (por ejemplo `1.bak`, `2.bak`, ...), y el archivo `N.meta` (si existe) guarda `epoch` + `usuario`.
- `iv -lsbak [file] [--persist]` lista backups mostrando fecha y usuario cuando hay `.meta`.
- `iv -lsbak file N [--persist]` muestra el contenido del slot N y sus metadatos.
- `iv -u file` restaura desde el backup 1; `iv -u file 2` desde el backup 2.
- `iv -diff file` compara con el backup 1; `iv -diff 2 file` con el backup 2.
- `iv -l [file] [--persist]` lista todos los backups; con `file` filtra por archivo.
- `iv -rmbak` (o `-z`) elimina todos los backups (y sus `.meta`); `iv -rmbak file` solo los de ese archivo.
- `iv --persist file` mueve el repo de backups de ese archivo al repo persistido; `iv --unpersist file` lo devuelve al efímero.

## Seguridad

- **Archivos binarios**: iv rechaza editar archivos que contienen bytes nulos para evitar corrupción.

## Códigos de salida

- `0`: éxito
- `1`: error (archivo binario, rango inválido, uso incorrecto, etc.)

## Límites

- Líneas: array dinámico (sin límite fijo)
- Longitud de línea: sin límite (usa `getline` POSIX)
