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
| Linux x86_64 | `paladeumd`, `paladeum-cli`, `paladeum-tx`, `paladeum-wallet` (no desktop wallet); Ubuntu 22.04 or later |

Each package has a `README.txt` with what to check before the first start.

Running
-------

### Desktop wallet

Unpack the archive and start `paladeum-qt.exe`. The first start asks for the language and the
data folder, and then for a passphrase and a backup. See
[paladeum/14-first-run-and-wallet-security.md](paladeum/14-first-run-and-wallet-security.md).

2) Install distribution-specific dependencies listed below.

3) Run the GUI wallet or only the Paladeum core deamon

   a. GUI wallet:

   `./paladeum-qt`

   b. Core deamon:

   `./paladeumd -deamon`

#### Ubuntu 16.04, 17.04/17.10 and 18.04

Update apt cache and install general dependencies:

```
./paladeumd -daemon
./paladeum-cli getblockchaininfo
./paladeum-cli stop
```

The wallet requires version 4.8 of the Berkeley DB. The easiest way to get it is to build it with the script contrib/install_db4.sh


```

[paladeum/02-build.md](paladeum/02-build.md) describes the build that the releases use, with
the unit and functional tests. The notes below come from the projects Paladeum Core is derived
from and are kept for the platforms they cover; they may lag behind.

- [Dependencies](dependencies.md)
- [Unix Build Notes](build-unix.md)
- [Windows Build Notes](build-windows.md)
- [OS X Build Notes](build-osx.md)
- [OpenBSD Build Notes](build-openbsd.md)

Documents
---------

Install general dependencies:

`sudo dnf install zeromq libevent boost libdb4-cxx miniupnpc`

The GUI wallet requires the QR Code encoding library and Google's data interchange format Protocol Buffers. Install with:

`sudo dnf install qrencode protobuf`

#### CentOS 7

Add the EPEL repository and install general depencencies:

```
sudo yum install https://dl.fedoraproject.org/pub/epel/epel-release-latest-7.noarch.rpm
sudo yum install zeromq libevent boost libdb4-cxx miniupnpc
```

The GUI wallet requires the QR Code encoding library and Google's data interchange format Protocol Buffers. Install with:

`sudo yum install qrencode protobuf`

### OS X

1) Download Paladeum-Qt.dmg.

2) Double click the DMG to mount it.

3) Drag Paladeum Core icon to the Applications Folder

![alt tag](https://i.imgur.com/GLhBFUV.png)

4) Open the Applications folder and Launch Paladeum Core. The client will begin synchronizing with the network.

![alt tag](https://i.imgur.com/v3962qo.png)

Note: You may get the follow error on first launch:
```
Dyld Error Message:
  Library not loaded: @loader_path/libboost_system-mt.dylib
  Referenced from: /Applications/Paladeum-Qt.app/Contents/Frameworks/libboost_thread-mt.dylib
  Reason: image not found
```
To resolve, you will need to copy libboost_system.dylib to libboost_system-mt.dylib in the /Applications/Paladeum-Qt.app/Contents/Frameworks folder

### Windows

1) Download windows-x86_64.zip and unpack executables to desired folder.

2) Double click the paladeum-qt.exe to launch it.

### Need Help?

- See the documentation at the [Paladeum Wiki](https://paladeum.wiki/wiki/Paladeum_Wiki)
for help and more information.
- Ask for help on [Discord](https://discord.gg/DUkcBst), [Telegram](https://t.me/PaladeumDev) or [Reddit](https://www.reddit.com/r/Paladeum/).

Building from source
---------------------
The following are developer notes on how to build the Paladeum core software on your native platform. They are not complete guides, but include notes on the necessary libraries, compile flags, etc.

- [Dependencies](https://github.com/PaladeumBlockchain/Paladeum/tree/master/doc/dependencies.md)
- [OS X Build Notes](https://github.com/PaladeumBlockchain/Paladeum/tree/master/doc/build-osx.md)
- [Unix Build Notes](https://github.com/PaladeumBlockchain/Paladeum/tree/master/doc/build-unix.md)
- [Windows Build Notes](https://github.com/PaladeumBlockchain/Paladeum/tree/master/doc/build-windows.md)
- [OpenBSD Build Notes](https://github.com/PaladeumBlockchain/Paladeum/tree/master/doc/build-openbsd.md)
- [Gitian Building Guide](https://github.com/PaladeumBlockchain/Paladeum/tree/master/doc/gitian-building.md)

Development
---------------------
Paladeum repo's [root README](https://github.com/PaladeumBlockchain/Paladeum/blob/master/README.md) contains relevant information on the development process and automated testing.

- [Developer Notes](https://github.com/PaladeumBlockchain/Paladeum/blob/master/doc/developer-notes.md)
- [Release Notes](https://github.com/PaladeumBlockchain/Paladeum/blob/master/doc/release-notes.md)
- [Release Process](https://github.com/PaladeumBlockchain/Paladeum/blob/master/doc/release-process.md)
- [Source Code Documentation (External Link)](https://dev.visucore.com/paladeum/doxygen/) -- 2018-05-11 -- Broken link
- [Translation Process](https://github.com/PaladeumBlockchain/Paladeum/blob/master/doc/translation_process.md)
- [Translation Strings Policy](https://github.com/PaladeumBlockchain/Paladeum/blob/master/doc/translation_strings_policy.md)
- [Travis CI](https://github.com/PaladeumBlockchain/Paladeum/blob/master/doc/travis-ci.md)
- [Unauthenticated REST Interface](https://github.com/PaladeumBlockchain/Paladeum/blob/master/doc/REST-interface.md)
- [Shared Libraries](https://github.com/PaladeumBlockchain/Paladeum/blob/master/doc/shared-libraries.md)
- [BIPS](https://github.com/PaladeumBlockchain/Paladeum/blob/master/doc/bips.md)
- [Dnsseed Policy](https://github.com/PaladeumBlockchain/Paladeum/blob/master/doc/dnsseed-policy.md)
- [Benchmarking](https://github.com/PaladeumBlockchain/Paladeum/blob/master/doc/benchmarking.md)

### Resources
- Discuss on chat [Discord](https://discord.gg/jn6uhur), [Telegram](https://t.me/PaladeumDev) or [Reddit](https://www.reddit.com/r/Paladeum/).
- Find out more on the [Paladeum Wiki](https://paladeum.wiki/wiki/Paladeum_Wiki)
- Visit the project home [Paladeum.org](https://paladeum.io)

### Miscellaneous
- [Tokens Attribution](https://github.com/PaladeumBlockchain/Paladeum/blob/master/doc/tokens-attribution.md)
- [Files](https://github.com/PaladeumBlockchain/Paladeum/blob/master/doc/files.md)
- [Fuzz-testing](https://github.com/PaladeumBlockchain/Paladeum/blob/master/doc/fuzzing.md)
- [Reduce Traffic](https://github.com/PaladeumBlockchain/Paladeum/blob/master/doc/reduce-traffic.md)
- [Tor Support](https://github.com/PaladeumBlockchain/Paladeum/blob/master/doc/tor.md)
- [Init Scripts (systemd/upstart/openrc)](https://github.com/PaladeumBlockchain/Paladeum/blob/master/doc/init.md)
- [ZMQ](https://github.com/PaladeumBlockchain/Paladeum/blob/master/doc/zmq.md)

License
-------

Distributed under the [MIT software license](../COPYING).
This product includes software developed by the OpenSSL Project for use in the
[OpenSSL Toolkit](https://www.openssl.org/). This product includes cryptographic software
written by Eric Young ([eay@cryptsoft.com](mailto:eay@cryptsoft.com)), and UPnP software written
by Thomas Bernard.
