#!/bin/sh
# Generate self-signed SSL certificate for WebRTC signalling server

BASE_DIR=$(dirname $0)

OUTDIR=""
if [ $# -eq 1 ]; then
  OUTDIR=$1/
fi

# Generate certificate with multiple hostnames (127.0.0.1, localhost, example.com)
# Using Subject Alternative Name (SAN) to support multiple hostnames
cat > /tmp/cert_config.cnf <<EOF
[req]
distinguished_name = req_distinguished_name
req_extensions = v3_req
prompt = no

[req_distinguished_name]
CN = 127.0.0.1

[v3_req]
keyUsage = keyEncipherment, dataEncipherment
extendedKeyUsage = serverAuth
subjectAltName = @alt_names

[alt_names]
DNS.1 = localhost
DNS.2 = example.com
IP.1 = 127.0.0.1
IP.2 = ::1
EOF

output=$(openssl req -x509 -newkey rsa:4096 \
    -keyout ${OUTDIR}key.pem \
    -out ${OUTDIR}cert.pem \
    -days 365 \
    -nodes \
    -config /tmp/cert_config.cnf \
    -extensions v3_req 2>&1)

ret=$?
rm -f /tmp/cert_config.cnf

if [ ! $ret -eq 0 ]; then
  echo "${output}" 1>&2
  exit $ret
fi

echo "Certificate generated, supports the following hostnames:"
echo "  - 127.0.0.1"
echo "  - localhost"
echo "  - example.com"
