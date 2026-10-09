#!/bin/sh
set -eu
cd "$(dirname "$0")"
openssl req -x509 -newkey rsa:2048 -nodes -days 3650 -keyout ca-key.pem -out ca.pem -subj '/CN=arknet test CA' -addext 'basicConstraints=critical,CA:TRUE' -addext 'keyUsage=critical,keyCertSign,cRLSign'
openssl req -new -newkey rsa:2048 -nodes -keyout server-key.pem -out server.csr -subj '/CN=localhost'
openssl x509 -req -in server.csr -CA ca.pem -CAkey ca-key.pem -set_serial 2 -days 3650 -out server.pem -extfile server.cnf
openssl x509 -req -in server.csr -CA ca.pem -CAkey ca-key.pem -set_serial 3 -days 3650 -out wrong.pem -extfile wrong.cnf
openssl req -new -newkey rsa:2048 -nodes -keyout client-key.pem -out client.csr -subj '/CN=arknet test client'
openssl x509 -req -in client.csr -CA ca.pem -CAkey ca-key.pem -set_serial 4 -days 3650 -out client.pem -extfile client.cnf
rm ca-key.pem server.csr client.csr
