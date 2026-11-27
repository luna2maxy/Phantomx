#!/usr/bin/env bash
set -euo pipefail
# PB-001 operator key generation helper. Generates libsodium Ed25519 crypto_sign keypairs.
# Outputs: pubkeys to stdout, secret key files to output dir (0600). Never prints secret keys.
OUT_DIR=${1:-}
if [[ -z "$OUT_DIR" ]]; then
  echo "usage: $0 <output-dir> [count]" >&2
  exit 2
fi
COUNT=${2:-4}
umask 077
mkdir -p "$OUT_DIR"
chmod 700 "$OUT_DIR"

python3 - "$OUT_DIR" "$COUNT" <<'PY'
import ctypes, ctypes.util, os, sys, binascii
out_dir=sys.argv[1]
count=int(sys.argv[2])
libname=ctypes.util.find_library('sodium') or 'libsodium.so'
sodium=ctypes.CDLL(libname)
if sodium.sodium_init() < 0:
    raise SystemExit('sodium_init failed')
pk_len=sodium.crypto_sign_publickeybytes()
sk_len=sodium.crypto_sign_secretkeybytes()
for i in range(count):
    pk=ctypes.create_string_buffer(pk_len)
    sk=ctypes.create_string_buffer(sk_len)
    if sodium.crypto_sign_keypair(pk, sk) != 0:
        raise SystemExit('crypto_sign_keypair failed')
    pub=binascii.hexlify(pk.raw).decode()
    sec=binascii.hexlify(sk.raw).decode()
    path=os.path.join(out_dir, f'validator-{i+1}.key')
    with open(path,'w',encoding='utf-8') as f:
        f.write(sec+'\n')
    os.chmod(path,0o600)
    print(f'validator_{i+1}_pubkey={pub}')
    print(f'validator_{i+1}_key_file={path}')
    del sec, sk
print('generated_count='+str(count))
print('note=private keys written to files with mode 0600; do not commit or log key file contents')
PY
