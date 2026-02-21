# bash completion for iv

_iv()
{
    local cur prev
    local words cword

    _init_completion -n : || return

    cur=${COMP_WORDS[COMP_CWORD]}
    prev=${COMP_WORDS[COMP_CWORD-1]}

    local cmds="-h --help -V --version -v -va -wc -n -nv -u -diff -i -insert -a -p -pi -d -delete -r -replace -s -l -lb -lsbak -rmbak -z"
    local opts="--dry-run --no-backup --no-numbers -g -E --regex -q --stdout --json --persist --unpersist -persistence -unpersist -m -F -e"

    # If completing the first argument (the main command/flag)
    if [[ ${COMP_CWORD} -eq 1 ]]; then
        COMPREPLY=( $(compgen -W "${cmds}" -- "${cur}") )
        return
    fi

    # Values expected after some options
    case "${prev}" in
        -m)
            COMPREPLY=()
            return
            ;;
        -F)
            COMPREPLY=()
            return
            ;;
        -e)
            COMPREPLY=()
            return
            ;;
        --persist|--unpersist|-persistence|-unpersist)
            _filedir
            return
            ;;
    esac

    # Slot numbers for commands that accept a slot index
    case "${COMP_WORDS[1]}" in
        -u|-diff|-lsbak)
            if [[ "${cur}" =~ ^[0-9]*$ ]]; then
                COMPREPLY=( $(compgen -W "1 2 3 4 5 6 7 8 9 10" -- "${cur}") )
                return
            fi
            ;;
    esac

    # Complete options anywhere
    if [[ "${cur}" == -* ]]; then
        COMPREPLY=( $(compgen -W "${cmds} ${opts}" -- "${cur}") )
        return
    fi

    # Default: complete files
    _filedir
}

complete -F _iv iv
