# Loopback TLS fixtures

`localhost-cert.pem` is a self-signed RSA certificate for `localhost` and
`127.0.0.1`, valid from January 2025 through January 2045. The matching
`localhost-key.pem` is a **public, test-only private key**, never a production
credential.

`MuffinWindowsTlsTest` starts a local TLS server, trusts this certificate only
on its test client, verifies the peer, and exchanges application data. It does
not contact an external server or change the operating system's trust store.
CTest runs the handshake separately with OpenSSL and Schannel to cover both
backends, including native Windows ARM64 builds.
