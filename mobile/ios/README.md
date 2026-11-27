# PhantomX iOS Wallet (Foundation)

Light-client architecture. This directory contains a foundation skeleton; production app must use Keychain/Secure Enclave where available, validate RPC/storage responses, and never transmit private keys or seed phrases.

## Security
- iOS Keychain/Secure Enclave for key protection; no plaintext seed storage.
- No private key/seed transmission.
- Validate transaction and RPC responses; protect against malicious RPC.
- Secure encrypted backup flows only.
