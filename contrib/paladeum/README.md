# contrib/paladeum

## make_bootstrap.py — block archives for the fast first synchronization

Writes the blocks of a synced node as archives that other nodes fetch with
`-bootstrapurl=<address>` or import with `-bootstrapdir=<folder>`.

```
contrib/paladeum/make_bootstrap.py --datadir ~/.paladeum --out /srv/www/bootstrap
```

- Reads the active chain through RPC (`getblockhash`, `getblock <hash> 0`) of a running, synced node.
  `--datadir` takes the RPC cookie or `rpcuser`/`rpcpassword` from there; `--rpcurl http://user:pass@host:port` works too.
- Writes `blocks-<first>-<last>.dat.gz` (bootstrap.dat format, gzip), `manifest.json` and `SHA256SUMS`
  into one flat folder. A chunk has 250,000 heights at most and ends earlier when its file reaches
  190 MB (`--chunk-blocks`, `--max-file-mb`). It stops 1,000 blocks below the tip (`--margin`).
- Run it again later: chunks that are listed in the manifest are never rewritten, only new ones are
  added, and `manifest.json` and `SHA256SUMS` are replaced.
- The folder is ready to publish as it is: serve it over HTTP, copy it to mirrors, upload the files as
  release assets, or make a torrent of the folder.

The host of the files does not have to be trusted: a node validates every block of an archive like a
block from a peer. The SHA-256 of the manifest only detects damaged downloads early.

## make_snapshot.py — chain state snapshot for the fast start

Packages the databases of a stopped node (chainstate, block index, tokens, governance) as
`snapshot-<height>.partNN` files of 190 MB at most plus `snapshot.json`, into the folder of the
block archives, and prints the values that `src/chainparams.cpp` must hold for a program to
accept this snapshot.

```
paladeum-cli getstatehashes > hashes.json && paladeum-cli stop
contrib/paladeum/make_snapshot.py --datadir ~/snapshot-node --hashes hashes.json --out /srv/www/bootstrap
```

- The node must have been synchronized from the block archives only (`connect=0`, `txindex=0`),
  to the height where they end. A snapshot of any other node is rejected by the check of the
  node that fetches it.
- The files are not trusted by anyone: a node computes the hashes of what it unpacked and compares
  them with the values of its program. Publishing a snapshot therefore always goes with a program
  release that has its values.

## history_scan.py — candidate rules against the whole chain

Reads every block of a synced node with -txindex through RPC and lists the blocks and
transactions that a candidate soft-fork rule would reject (proof-of-stake block times
not in multiples of 16 seconds, token amounts and supplies out of range, token outputs
with a PLB value, token amounts not in their units, tokens in a coinbase or coinstake).
Read only.

```
contrib/paladeum/history_scan.py --datadir ~/.paladeum --out scan.json
```

## build-release.sh — release archives for Linux, Windows and macOS

```bash
contrib/paladeum/build-release.sh --macos-sdk ~/MacOSX14.4.sdk.tar.gz           # the commit that is checked out
contrib/paladeum/build-release.sh --ref 2.0.0 --macos-sdk ~/MacOSX14.4.sdk.tar.gz
```

Builds the programs (with the GUI) with `depends/` on one Linux machine and writes, into
`~/paladeum-release/<version>/`, `paladeum-<version>-<platform>.zip` for `linux-x86_64`,
`windows-x86_64`, `macos-x86_64` and `macos-arm64`, `paladeum-<version>-linux-x86_64.tar.gz`
and `SHA256SUMS`, ready to attach to a GitHub release. Platforms can be named on the command
line; `linux-aarch64` is one more.

- An archive holds the folder `paladeum-<version>-<platform>` with the programs (`paladeumd`,
  `paladeum-cli`, `paladeum-tx`, `paladeum-wallet`, `paladeum-qt`) and `COPYING`; for macOS
  `Paladeum-Qt.app` instead of `paladeum-qt`.
- Everything is built in Docker containers, from two images the script makes the first time:
  Ubuntu 22.04 for Linux (the programs run on glibc 2.35 or newer: Ubuntu 22.04, Debian 12 and
  later) and Ubuntu 24.04 with MinGW-w64, clang 18 and lld for Windows and macOS. The machine needs
  docker, git and about 30 GB of disk. `DOCKER="sudo docker"` when the user is not in the docker
  group; `DOCKER=podman` works too.
- The depends cache stays in `~/paladeum-release/cache/`: the next release only builds the packages
  whose recipes changed. Each platform's log is in `~/paladeum-release/logs/`.
- The macOS SDK is not in the tree, because Apple's license does not allow passing it around. Copy it
  from a Mac (the command is in `--help`) or extract it from Xcode (`contrib/macdeploy/README.md`).
- The macOS programs have no Developer ID signature and are not notarized: macOS asks before it opens
  them the first time (right click > Open). The arm64 programs carry an ad hoc signature, without which
  Apple silicon starts no program.
- Without `--ref` it builds the commit that is checked out; uncommitted changes are left out unless
  `--worktree` is given (a test build before a commit).
- The same tree gives the same archives: every file is stamped with the time of the commit and the
  entries are sorted.
