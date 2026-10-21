"""Make the pinned dxb loader reject failed project patches, not skip them."""
from pathlib import Path
import sys

path = Path(sys.argv[1])
text = path.read_text()
marker = '                warning "Skipping patch ${PATCH} (already upstream, previously applied, or malformed): ${APPLY_CHECK}"'
assert text.count(marker) == 1, 'Pinned dxb patch loader changed; inspect before building'
guard = '''                case "${PATCH##*/}" in
                    [0-9][0-9][0-9][0-9]-*)
                        printf 'Required project patch failed: %s\\n%s\\n' "$PATCH" "$APPLY_CHECK" >&2
                        exit 1
                        ;;
                esac
'''
path.write_text(text.replace(marker, guard + marker))
print('Numbered project patches now fail the build if git apply rejects them')
