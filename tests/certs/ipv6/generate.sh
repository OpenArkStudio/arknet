#!/bin/sh
set -eu
cd "$(dirname "$0")"
openssl req -x509 -newkey rsa:2048 -nodes -days 3650 \
    -keyout server-key.pem -out server.pem -subj '/CN=arknet IPv6 test server' \
    -addext 'subjectAltName=DNS:localhost,IP:127.0.0.1,IP:::1' \
    -addext 'basicConstraints=critical,CA:FALSE' \
    -addext 'keyUsage=critical,digitalSignature,keyEncipherment' \
    -addext 'extendedKeyUsage=serverAuth'
cp server.pem ca.pem
