#!/usr/bin/env python3
"""Generate a device-bound perpetual ABM AV Player licence key.
Keep the private signing key offline. Never put it in the app or Git repository.
Requires: pip install cryptography
"""
import argparse
import base64
import getpass
import re
import sys
import uuid
from pathlib import Path
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives.asymmetric.utils import decode_dss_signature

def b64url(data: bytes) -> str:
    return base64.urlsafe_b64encode(data).decode("ascii").rstrip("=")

def main() -> int:
    ap = argparse.ArgumentParser(description="Create a perpetual ABM AV Player key for one device")
    ap.add_argument("--private-key", required=True, help="Path to publisher private PEM key")
    ap.add_argument("--device-id", required=True, help="64-character device ID shown in ABM AV Player")
    ap.add_argument("--license-id", default=str(uuid.uuid4()))
    ap.add_argument("--out", help="Optional output text file")
    args = ap.parse_args()
    device_id = args.device_id.strip().lower()
    if not re.fullmatch(r"[0-9a-f]{64}", device_id):
        ap.error("--device-id must be exactly 64 hexadecimal characters")
    key_bytes = Path(args.private_key).read_bytes()
    password = getpass.getpass("Private-key passphrase: ") if b"ENCRYPTED" in key_bytes else None
    try:
        private_key = serialization.load_pem_private_key(key_bytes, password=password)
    except (TypeError, ValueError) as exc:
        print(f"Could not load private key: {exc}", file=sys.stderr)
        return 2
    if not isinstance(private_key, ec.EllipticCurvePrivateKey) or not isinstance(private_key.curve, ec.SECP256R1):
        print("Private key must be ECDSA P-256 and match the app's embedded public key.", file=sys.stderr)
        return 2
    payload = f"ABM1|{device_id}|{args.license_id}".encode("utf-8")
    der = private_key.sign(payload, ec.ECDSA(hashes.SHA256()))
    r, s = decode_dss_signature(der)
    raw_signature = r.to_bytes(32, "big") + s.to_bytes(32, "big")
    token = f"ABM1.{b64url(payload)}.{b64url(raw_signature)}"
    if args.out:
        Path(args.out).write_text(token + "\n", encoding="ascii")
    print(token)
    print(f"Licence ID: {args.license_id}", file=sys.stderr)
    print("Perpetual licence, bound to the supplied device ID.", file=sys.stderr)
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
