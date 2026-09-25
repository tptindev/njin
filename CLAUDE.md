# njin

## Line endings: LF only

Every text file in this repo uses LF (`\n`). Never write CRLF (`\r\n`, shows as `^M`).
`.gitattributes` and `.editorconfig` already enforce this for git and editors;
these rules cover scripted edits, which bypass both.

- Prefer the Edit/Write tools for file changes.
- Python on Windows writes CRLF in text mode. Always pass `newline='\n'`:
  `open(path, 'w', newline='\n')`. Same for `Path.write_text(..., newline='\n')`.
- Do not use PowerShell `Set-Content`/`Out-File` to write source files.
- After any scripted edit, check that no CR slipped in:
  `grep -rlI $'\r' src CMakeLists.txt` must print nothing. Fix with
  `sed -i 's/\r$//' <file>`.
