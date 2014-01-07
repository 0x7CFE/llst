# bash completion for llst(1)

_llst()
{
    local cur prev words cword
    _init_completion -s || return

    case $prev in
        -i|--image)
            _filedir
            return
            ;;
    esac

    if [[ $cur == -* ]]; then
        COMPREPLY=( $( compgen -W '$( _parse_help "$1" )' -- "$cur" ) )
        [[ $COMPREPLY == *= ]] && compopt -o nospace
    fi
} &&
complete -F _llst llst
