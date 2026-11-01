#!/bin/sh
# Signs a site's catalogue.json with the private key, writing catalogue.json.sig beside it.
#   tools/packs/sign_catalogue.sh <site> <private key .pem>
set -eu
if [ $# -ne 2 ]; then
    echo "usage: $0 <site> <private key .pem>" >&2
    exit 2
fi
openssl dgst -sha256 -sign "$2" -out "$1/catalogue.json.sig" "$1/catalogue.json"
echo "Signed $1/catalogue.json"
