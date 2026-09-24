# Fish completion for iv(1)

function __iv_complete --description 'Register iv completions'
	set -l cmd $argv[1]

	complete -c $cmd -s h -l help -d 'Show help'
	complete -c $cmd -s V -l version -d 'Show version'
	complete -c $cmd -s v -d 'View file with line numbers'
	complete -c $cmd -s va -d 'View line range'
	complete -c $cmd -s i -l insert -d 'Insert text'
	complete -c $cmd -s a -d 'Append text'
	complete -c $cmd -s p -d 'Patch file(s)'
	complete -c $cmd -s pi -d 'Insert before line (patch)'
	complete -c $cmd -s d -l delete -d 'Delete lines'
	complete -c $cmd -s r -l replace -d 'Replace lines'
	complete -c $cmd -s s -d 'Substitute on lines'
	complete -c $cmd -s b -d 'GNU backup (existing)'
	complete -c $cmd -l backup -d 'GNU backup method' -xa 'none numbered existing simple'
	complete -c $cmd -s S -l suffix -d 'Backup suffix' -r
	complete -c $cmd -l dry-run -d 'Do not modify file'
	complete -c $cmd -l no-numbers -d 'Omit line numbers'
	complete -c $cmd -s g -d 'Global substitute'
	complete -c $cmd -s E -l regex -d 'Regex substitute'
	complete -c $cmd -s q -d 'Quiet'
	complete -c $cmd -l stdout -d 'Write to stdout only'
	complete -c $cmd -s m -d 'Filter lines by pattern' -x
	complete -c $cmd -s F -d 'Replace delimited field' -x
	complete -c $cmd -s e -d 'Extra substitute' -x
	complete -c $cmd -F
end

__iv_complete iv
