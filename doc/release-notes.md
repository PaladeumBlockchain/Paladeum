Paladeum Core 2.0.0
===================

Paladeum Core 2.0.0 brings the node and the desktop wallet up to the AokChain
Core 4.0.8 code base. It changes no consensus rule: nodes and wallets on 0.1.7
keep working next to 2.0.0, and nobody has to upgrade on a deadline.

Please report bugs using the issue tracker at GitHub:

  <https://github.com/PaladeumBlockchain/Paladeum/issues>

How to upgrade
==============

Shut down the running node or wallet and wait until it has stopped, then
replace the programs (`paladeumd`, `paladeum-cli`, `paladeum-qt` and the
others) with those of 2.0.0 and start again.

- The block files, the chain state and `wallet.dat` of 0.1.7 are used as they
  are. No reindex is needed.
- Going back to 0.1.7 works the same way: the data folder that 2.0.0 has
  opened still opens with 0.1.7.
- On the first start, 2.0.0 discards the fee estimates file of 0.1.7 and
  builds a new one. The log notes this; it is harmless.

Compatibility with 0.1.7
========================

The consensus code of 0.1.7 is kept as it is. This was checked before the
release in two ways:

- A 2.0.0 node reindexed the main chain from the block files up to block
  716,809. Its best block, its UTXO set hash and its total amount
  (1,007,168,080 PLB) are the same as those of 0.1.7.
- A functional test (`test/functional/feature_compat_017.py`) runs 0.1.7 and
  2.0.0 nodes together. Each version accepts the other's proof-of-work and
  proof-of-stake blocks, plain, multisig, time-locked and message
  transactions, token transactions of every kind and governance commands,
  and both follow each other through chain reorganizations.

Supported systems
=================

Release builds are made for:

- Linux x86_64: node and tools, and the desktop wallet (new)
- Linux ARM64 (aarch64): node and tools (new)
- Windows 64-bit: desktop wallet, and node and tools

Notable changes
===============

Node
----

- The code base of AokChain Core 4.0.8, with its fixes and performance work.
- The staker no longer scans the whole wallet on every round. A wallet with
  hundreds of thousands of transactions now stakes with a small part of one
  processor core instead of most of it.
- `-dbcache` defaults to 1024 MiB on 64-bit computers with 4 GiB of memory or
  more (450 MiB otherwise). A full reindex of the main chain takes a few
  minutes.
- Main network checkpoints, `assumevalid` and chain statistics are updated.
- Changing `-tokenindex` now asks for `-reindex` instead of leaving an
  incomplete index.

Tokens, usernames and mempool
-----------------------------

- Token amounts by address stay right after several blocks are disconnected,
  and a reissued token keeps the height and block of its first issue.
- Username registrations are undone when their block is disconnected.
- The mempool keeps every new token name of a transaction, and keeps mature
  spends of coinbase outputs after a reorganization.
- The miner leaves out transactions whose time is later than the block's.
- The token metadata cache is safe for concurrent readers.

Wallet
------

- New wallets use the standard BIP39 word list. Recovery phrases of 0.1.x
  wallets are still accepted when restoring.
- The desktop wallet has a new design, and a Linux build.

RPC
---

- Help texts, argument names and examples are corrected throughout, including
  `createrawtransaction` (costs and the fee address in force), `sendmany`
  (usernames), `sweep`, `registerusername` and `issueunique`.
- Address balances release a time lock at the block time.
- Optional arguments that are left out are handled as documented
  (`removeaddress`, `listwalletdir` and others).

Tests and build
---------------

- The unit and functional tests are adapted to Paladeum (authorized stakers,
  token fee address and costs, rewards and maturity), and the regtest network
  has its own token fee address.
- `paladeum -version` names the commit a program was built from.
- Qt for Linux is built with `_FORTIFY_SOURCE=2`; at level 3, which recent
  compilers use by default, the desktop wallet stopped right after starting.
