"""Reject an integrated artifact missing the services required by boot tasks."""
from pathlib import Path
import sys

data = Path(sys.argv[1]).read_bytes()
required = (
    b'SWEET AKS: AppleKeyStore registered', b'AppleKeyStoreUserClient',
    b'SWEET WDT: IOWatchdog registered', b'IOWatchdogUserClient',
    b'CRASH %s reason', b'VERMEER ROOT: first-boot test systembag prepared',
)
missing = [s.decode() for s in required if s not in data]
if missing:
    raise SystemExit('Integrated kernel missing required services: ' + ', '.join(missing))
print('Final integrated kernel contains AKS, watchdog, crash reasons and root preparation: PASS')
