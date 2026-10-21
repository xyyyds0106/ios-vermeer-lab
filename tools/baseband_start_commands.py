"""Build a dry-observer start handshake. Pure bytes; no serial/file/device I/O."""


def _identity(text, width):
    if not isinstance(text, str) or len(text) != width or any(c not in "0123456789abcdef" for c in text):
        raise ValueError("identity must be exact-width lowercase hex")
    if not any(bytes.fromhex(text)):
        raise ValueError("zero identity")
    return text


def build_start_commands(run_token, image_sha256):
    token = _identity(run_token, 32)
    digest = _identity(image_sha256, 64)
    commands = [f"bb {token}"]
    commands += [f"bh{i} {digest[i*16:(i+1)*16]} {token}" for i in range(4)]
    commands += [f"bg {token}"]
    assert all(len(command) <= 63 for command in commands)
    return tuple((command+"\n").encode("ascii") for command in commands)
