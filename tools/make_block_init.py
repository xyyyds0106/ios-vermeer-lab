"""Build a source-only PID1 with read-only raw-device regression checks."""
from pathlib import Path
import sys

base, extra, dest = map(Path, sys.argv[1:])
source = base.read_text()
assert source.count('\ntick:\n') == 1
source = source.replace('\ntick:\n', '\n\tbl\tblock_test\n\ntick:\n')
source = source.replace('VERMEER userspace:', 'VERMEER BLOCK userspace:')
dest.write_text(source + '\n' + extra.read_text())
