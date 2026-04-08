"""
TLS certificate generator module.

Public API:
    generate_certs(domain, days, force)  — generate self-signed cert + key
"""

import os
import subprocess
import shutil

_MODULE_DIR = os.path.dirname(os.path.abspath(__file__))
_PROJECT_ROOT = os.path.dirname(os.path.dirname(_MODULE_DIR))
CERT_DIR = os.path.join(_PROJECT_ROOT, "config", "traefik", "certs")

DEFAULT_DOMAIN = "api.localhost"
DEFAULT_DAYS = 365


def generate_certs(domain=DEFAULT_DOMAIN, days=DEFAULT_DAYS, force=False):
    """Generate self-signed TLS certificates for local development.

    Args:
        domain: Primary domain for the certificate CN and SAN.
        days:   Validity period in days.
        force:  If True, overwrite existing certificates.

    Returns:
        dict with 'cert' and 'key' paths on success,
        or dict with 'status'='error' and 'message' on failure.
    """
    if not shutil.which("openssl"):
        return {"status": "error", "message": "openssl not found in PATH"}

    cert_file = os.path.join(CERT_DIR, "cert.pem")
    key_file = os.path.join(CERT_DIR, "key.pem")

    if not force and os.path.isfile(cert_file) and os.path.isfile(key_file):
        print(f"  Certificates already exist in {CERT_DIR}")
        print(f"  Use force=True (or select 'overwrite') to regenerate.")
        return {"cert": cert_file, "key": key_file}

    os.makedirs(CERT_DIR, exist_ok=True)

    san = f"DNS:{domain},DNS:localhost,IP:127.0.0.1"

    cmd = [
        "openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes",
        "-keyout", key_file,
        "-out", cert_file,
        "-days", str(days),
        "-subj", f"/CN={domain}",
        "-addext", f"subjectAltName={san}",
    ]

    try:
        subprocess.run(cmd, check=True, capture_output=True, text=True)
    except subprocess.CalledProcessError as exc:
        return {"status": "error", "message": f"openssl failed: {exc.stderr.strip()}"}

    print(f"  Self-signed TLS certificate generated:")
    print(f"    cert   : {cert_file}")
    print(f"    key    : {key_file}")
    print(f"    domain : {domain} (valid {days} days)")

    return {"cert": cert_file, "key": key_file}
