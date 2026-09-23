# Fish completion for iv(1)

function __iv_complete --description 'Register iv completions'
	set -l cmd $argv[1]

	complete -c $cmd -s h -l help -d 'Show help'
	complete -c $cmd -s V -l version -d 'Show version'
	complete -c $cmd -s v -d 'View file with line numbers'
	complete -c $cmd -s va -d 'View line range'
	complete -c $cmd -s wc -d 'Count lines'
	complete -c $cmd -s n -d 'Line numbers for pattern'
	complete -c $cmd -s nv -d 'Lines matching pattern'
	complete -c $cmd -s u -d 'Undo from backup slot'
	complete -c $cmd -l diff -d 'Compare with backup'
	complete -c $cmd -s i -l insert -d 'Insert text'
	complete -c $cmd -s a -d 'Append text'
	complete -c $cmd -s p -d 'Patch file(s)'
	complete -c $cmd -s pi -d 'Insert before line (patch)'
	complete -c $cmd -s d -l delete -d 'Delete lines'
	complete -c $cmd -s r -l replace -d 'Replace lines'
	complete -c $cmd -s s -d 'Substitute on lines'
	complete -c $cmd -s l -d 'List backups'
	complete -c $cmd -s lsbak -d 'List backups with metadata'
	complete -c $cmd -s rmbak -s z -d 'Remove backups'
	complete -c $cmd -l persist -l persistence -d 'Persist backup repo'
	complete -c $cmd -l unpersist -l unpersist -d 'Ephemeral backup repo'
	complete -c $cmd -l dry-run -d 'Do not modify file'
	complete -c $cmd -l no-backup -d 'Skip backup'
	complete -c $cmd -l no-numbers -d 'Omit line numbers'
	complete -c $cmd -s g -d 'Global substitute'
	complete -c $cmd -s E -l regex -d 'Regex substitute'
	complete -c $cmd -s q -d 'Quiet'
	complete -c $cmd -l stdout -d 'Write to stdout only'
	complete -c $cmd -l json -d 'JSON output for -n'
	complete -c $cmd -s m -d 'Filter lines by pattern' -x
	complete -c $cmd -s F -d 'Replace delimited field' -x
	complete -c $cmd -s e -d 'Extra substitute' -x
	complete -c $cmd -F
end

__iv_complete iv
