#!/bin/sh
# Makes the key pair that signs pack catalogues (ECDSA P-256), with the openssl on this Mac.
#   tools/packs/make_signing_key.sh <folder outside this repository>
# Writes <folder>/pack-signing-key.pem (the private key: keep it safe, never commit it) and
# prints the public key's entry for effects/store/trusted-keys.json.
set -eu
if [ $# -ne 1 ]; then
    echo "usage: $0 <folder outside this repository>" >&2
    exit 2
fi
folder=$1
mkdir -p "$folder"
folder=$(cd "$folder" && pwd -P)
repo=$(cd "$(dirname "$0")/../.." && pwd -P)
case "$folder/" in
    "$repo"/*) echo "Keep the private key outside the repository ($repo)." >&2; exit 1 ;;
esac
key="$folder/pack-signing-key.pem"
if [ -e "$key" ]; then
    echo "$key already exists; not replacing it." >&2
    exit 1
fi
umask 077
openssl ecparam -name prime256v1 -genkey -noout -out "$key"
public=$(openssl ec -in "$key" -pubout -outform DER 2>/dev/null | base64 | tr -d '\n')
echo "Private key: $key (keep it safe; anyone with it can sign catalogues)"
echo "Add this to the \"keys\" list in effects/store/trusted-keys.json:"
echo "    {\"id\": \"$(date +%Y-%m)\", \"publicKey\": \"$public\"}"
