"""Reject an integrated artifact missing the services required by boot tasks."""
from pathlib import Path
import sys

data = Path(sys.argv[1]).read_bytes()
required = (
    b'SWEET AKS: AppleKeyStore registered', b'AppleKeyStoreUserClient',
    b'SWEET WDT: IOWatchdog registered', b'IOWatchdogUserClient',
    b'CRASH %s reason', b'VERMEER ROOT: first-boot test systembag prepared',
    b'VERMEER USB: CDC ACM debug004', b'K70DBG004',
    b'VERMEER ROOT: experimental clean-root migration baseline:',
)
if '--gui' in sys.argv:
    required += (b'IOMobileFramebufferUserClient', b'IOCoreSurfaceRoot',
                 b'SWEET HID: touch registered', b'SWEET HID: buttons registered',
                 b'SWEET Sandbox: permissive policy', b'VERMEER FB: geometry guard',
                 b'VERMEER FB: first present; muting panel text only',
                 b'VERMEER FB: retained pending frame ownership enabled',
                 b'VERMEER FRAME: submits=', b'VERMEER INPUT: touch-calls=',
                 b'VERMEER TOUCH: physical GT9916 input ready',
                 b'vermeer_touch', b'VERMEER TOUCH: IC-info guard refused',
                 b'VERMEER TOUCH: clock-snapshot', b'VERMEER TOUCH: spi-snapshot',
                 b'VERMEER FB: logical canvas 1125x2436 centered in 1440x3200',
                 b'VERMEER SAMPLE: read-only dyld maps and USB requests enabled',
                 b'IMG %s[%d] base', b'selected tasks only')
missing = [s.decode() for s in required if s not in data]
if missing:
    raise SystemExit('Integrated kernel missing required services: ' + ', '.join(missing))
print('Final integrated kernel contains AKS, watchdog, crash reasons, root preparation and USB debug: PASS')
