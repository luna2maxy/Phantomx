# PhantomX Android Wallet (Foundation)

Light-client architecture. This directory contains a foundation skeleton; production app must connect to trusted/configured PhantomX RPC and storage APIs, validate responses, and use Android Keystore for key material.

## Security
- Keys/seeds stored via Android Keystore-backed encryption; no plaintext seed storage.
- No private key/seed transmission.
- Validate transaction and RPC responses; handle malicious RPC data.
- Secure backup/export flows must be explicit and encrypted.
