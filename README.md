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

### Edición

| Comando | Descripción |
|---------|-------------|
| `iv -i file "texto"` | Inserta texto al final (alias: `-insert`) |
| `iv -i file start-end "texto"` | Inserta texto antes de la línea `start` |
| `iv -a file "texto"` | Añade texto al final del archivo |
| `iv -d file [start-end]` | Elimina líneas (alias: `-delete`) |
| `iv -r file [start-end] "texto"` | Reemplaza líneas (alias: `-replace`) |
| `iv -s file "patrón" "reemplazo"` | Sustituye la primera ocurrencia por línea |
| `iv -s file "patrón" "reemplazo" -g` | Sustituye todas las ocurrencias |

### Opciones globales

| Opción | Efecto |
|--------|--------|
| `--dry-run` | Muestra qué se haría sin modificar el archivo |
| `--no-backup` | No crea archivo `.bak` antes de editar |
| `--no-numbers` | Salida sin números de línea (solo con `-v` y `-va`) |

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

## Entrada desde stdin

Usar `-` como texto para leer desde stdin:

```bash
echo "línea nueva" | iv -i file -
cat bloque.txt | iv -a file -
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

`-insert`, `-replace` y `-a` escriben en stdout el texto añadido, de forma similar a `tee`.

## Estructura del código

```
iv.h      — Declaraciones, constantes, IvOpts
main.c    — Entrada, parseo de argumentos, dispatch
view.c    — show_file, show_range, wc_lines
edit.c    — backup, write_with_escapes, apply_patch, search_replace
range.c   — parse_range
```

## Límites

- Máximo 4096 líneas por archivo
- Máximo 1024 caracteres por línea
