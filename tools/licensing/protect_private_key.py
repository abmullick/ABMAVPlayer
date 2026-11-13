#!/usr/bin/env python3
"""Encrypt the ABM AV Player publisher ECDSA private key with a passphrase."""
import argparse
import getpass
from pathlib import Path
from cryptography.hazmat.primitives import serialization

def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--input", required=True, help="Unencrypted publisher PEM key")
    ap.add_argument("--output", required=True, help="Encrypted PEM output path")
    args = ap.parse_args()
    source = Path(args.input)
    key = serialization.load_pem_private_key(source.read_bytes(), password=None)
    first = getpass.getpass("Choose a strong private-key passphrase: ")
    second = getpass.getpass("Confirm passphrase: ")
    if not first or first != second:
        ap.error("Passphrases must be non-empty and match")
    encrypted = key.private_bytes(
        serialization.Encoding.PEM,
        serialization.PrivateFormat.PKCS8,
        serialization.BestAvailableEncryption(first.encode("utf-8")),
    )
    target = Path(args.output)
    if target.exists():
        ap.error(f"Refusing to overwrite existing file: {target}")
    target.write_bytes(encrypted)
    print(f"Encrypted signing key written to: {target}")
    print("Verify the encrypted key can issue a test licence before securely archiving the original.")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
