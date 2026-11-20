Paladeum Core
=============

Paladeum Core is the full node of the Paladeum network, with the wallet built into it. It
checks every block and transaction against the consensus rules itself. This directory holds
the documentation that ships with the source code. The guides for users are at
https://paladeum.io/doc/.

Getting it
----------

The release files and their SHA-256 hashes are listed at https://paladeum.io/download/ and
on the [GitHub release page](https://github.com/PaladeumBlockchain/Paladeum/releases). Compare the hash
of the file you downloaded with `SHA256SUMS.txt` before you start it.

| Package | Contents |
|---|---|
| Windows 64-bit, wallet | `paladeum-qt.exe`: the desktop wallet, which is also a full node |
| Windows 64-bit, tools | `paladeumd.exe`, `paladeum-cli.exe`, `paladeum-tx.exe`, `paladeum-wallet.exe` |
| Linux x86_64, wallet | `paladeum-qt` |
| Linux x86_64 | `paladeumd`, `paladeum-cli`, `paladeum-tx`, `paladeum-wallet` |
| Linux ARM64 | `paladeumd`, `paladeum-cli`, `paladeum-tx`, `paladeum-wallet` |

Running
-------

### Desktop wallet

Unpack the archive and start `paladeum-qt` (`paladeum-qt.exe` on Windows). The first start asks
for the language and the data folder, and then for a passphrase and a backup.

### Node

```
./paladeumd -daemon
./paladeum-cli getblockchaininfo
./paladeum-cli stop
```

Building from source
--------------------

The notes below come from the projects Paladeum Core is derived from and are kept for the
platforms they cover; they may lag behind.

- [Dependencies](dependencies.md)
- [Unix Build Notes](build-unix.md)
- [Windows Build Notes](build-windows.md)
- [OS X Build Notes](build-osx.md)
- [OpenBSD Build Notes](build-openbsd.md)

Documents
---------

- [Release Notes](release-notes.md)
- [Developer Notes](developer-notes.md)
- [Release Process](release-process.md)
- [Translation Process](translation_process.md)
- [Translation Strings Policy](translation_strings_policy.md)
- [Unauthenticated REST Interface](REST-interface.md)
- [Shared Libraries](shared-libraries.md)
- [BIPS](bips.md)
- [Dnsseed Policy](dnsseed-policy.md)
- [Benchmarking](benchmarking.md)
- [Files](files.md)
- [Fuzz-testing](fuzzing.md)
- [Reduce Traffic](reduce-traffic.md)
- [Tor Support](tor.md)
- [Init Scripts (systemd/upstart/openrc)](init.md)
- [ZMQ](zmq.md)

License
-------

Distributed under the [MIT software license](../COPYING).
This product includes software developed by the OpenSSL Project for use in the
[OpenSSL Toolkit](https://www.openssl.org/). This product includes cryptographic software
written by Eric Young ([eay@cryptsoft.com](mailto:eay@cryptsoft.com)), and UPnP software written
by Thomas Bernard.
