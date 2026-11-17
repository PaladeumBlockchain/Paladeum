#!/usr/bin/env python3
# Copyright (c) 2014-2015 The Bitcoin Core developers
# Copyright (c) 2017-2020 The Paladeum developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""Test addressindex generation and fetching"""

import binascii
import time
from test_framework.test_framework import PaladeumTestFramework
from test_framework.util import connect_nodes_bi, assert_equal, assert_raises_rpc_error, bytes_to_hex_str, hex_str_to_bytes
from test_framework.script import CScript, OP_HASH160, OP_EQUAL, OP_DUP, OP_EQUALVERIFY, OP_CHECKSIG
from test_framework.mininode import COIN, CTransaction, CTxIn, CTxOut, COutPoint

# Regtest addresses (version 83; pay-to-script-hash addresses have version 196 as on Bitcoin's testnet)
ADDRESS_P2PKH = "aXgu3VNoUbCGdowBoKubLUAaydzCWLzDLV"  # hash160 53c0307d6851aa0ce7825ba883c6bd9ad242b486
ADDRESS_P2SH = "2N2JD6wb56AfK4tfmM6PwdVmoYk2dCKf4Br"

class AddressIndexTest(PaladeumTestFramework):

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 4

    def setup_network(self):
        self.add_nodes(4, [
            # Nodes 0/1 are "wallet" nodes
            ["-relaypriority=0"],
            ["-addressindex"],
        # Nodes 2/3 are used for testing
            ["-addressindex", "-relaypriority=0"],
            ["-addressindex"]])

        self.start_nodes()

        connect_nodes_bi(self.nodes, 0, 1)
        connect_nodes_bi(self.nodes, 0, 2)
        connect_nodes_bi(self.nodes, 0, 3)

        self.sync_all()

    def run_test(self):
        self.log.info("Mining blocks...")
        self.nodes[0].generate(105)
        self.sync_all()

        chain_height = self.nodes[1].getblockcount()
        assert_equal(chain_height, 105)
        assert_equal(self.nodes[1].getbalance(), 0)
        assert_equal(self.nodes[2].getbalance(), 0)

        # Check that balances are correct
        balance0 = self.nodes[1].getaddressbalance(ADDRESS_P2SH)
        assert_equal(balance0["balance"], 0)

        # Check p2pkh and p2sh address indexes
        self.log.info("Testing p2pkh and p2sh address index...")

        tx_id0 = self.nodes[0].sendtoaddress(ADDRESS_P2PKH, 10)
        self.nodes[0].generate(1)

        tx_idb0 = self.nodes[0].sendtoaddress(ADDRESS_P2SH, 10)
        self.nodes[0].generate(1)

        tx_id1 = self.nodes[0].sendtoaddress(ADDRESS_P2PKH, 15)
        self.nodes[0].generate(1)

        tx_idb1 = self.nodes[0].sendtoaddress(ADDRESS_P2SH, 15)
        self.nodes[0].generate(1)

        tx_id2 = self.nodes[0].sendtoaddress(ADDRESS_P2PKH, 20)
        self.nodes[0].generate(1)

        tx_idb2 = self.nodes[0].sendtoaddress(ADDRESS_P2SH, 20)
        self.nodes[0].generate(1)

        self.sync_all()

        txids = self.nodes[1].getaddresstxids(ADDRESS_P2PKH)
        assert_equal(len(txids), 3)
        assert_equal(txids[0], tx_id0)
        assert_equal(txids[1], tx_id1)
        assert_equal(txids[2], tx_id2)

        tx_idsb = self.nodes[1].getaddresstxids(ADDRESS_P2SH)
        assert_equal(len(tx_idsb), 3)
        assert_equal(tx_idsb[0], tx_idb0)
        assert_equal(tx_idsb[1], tx_idb1)
        assert_equal(tx_idsb[2], tx_idb2)

        # Check that limiting by height works
        self.log.info("Testing querying txids by range of block heights..")
        height_txids = self.nodes[1].getaddresstxids({
            "addresses": [ADDRESS_P2SH],
            "start": 105,
            "end": 110
        })
        assert_equal(len(height_txids), 2)
        assert_equal(height_txids[0], tx_idb0)
        assert_equal(height_txids[1], tx_idb1)

        # Check that multiple addresses works
        multi_tx_ids = self.nodes[1].getaddresstxids({"addresses": [ADDRESS_P2SH, ADDRESS_P2PKH]})
        assert_equal(len(multi_tx_ids), 6)
        assert_equal(multi_tx_ids[0], tx_id0)
        assert_equal(multi_tx_ids[1], tx_idb0)
        assert_equal(multi_tx_ids[2], tx_id1)
        assert_equal(multi_tx_ids[3], tx_idb1)
        assert_equal(multi_tx_ids[4], tx_id2)
        assert_equal(multi_tx_ids[5], tx_idb2)

        # Check that balances are correct
        balance0 = self.nodes[1].getaddressbalance(ADDRESS_P2SH)
        assert_equal(balance0["balance"], 45 * 100000000)

        # Check that outputs with the same address will only return one txid
        self.log.info("Testing for txid uniqueness...")
        address_hash = bytes([99,73,164,24,252,69,120,209,10,55,43,84,180,92,40,12,200,196,56,47])
        script_pub_key = CScript([OP_HASH160, address_hash, OP_EQUAL])
        unspent = self.nodes[0].listunspent()
        tx = CTransaction()
        tx.vin = [CTxIn(COutPoint(int(unspent[0]["txid"], 16), unspent[0]["vout"]))]
        tx.vout = [CTxOut(10, script_pub_key), CTxOut(11, script_pub_key)]
        tx.rehash()

        signed_tx = self.nodes[0].signrawtransaction(binascii.hexlify(tx.serialize()).decode("utf-8"))
        sent_txid = self.nodes[0].sendrawtransaction(signed_tx["hex"], True)

        self.nodes[0].generate(1)
        self.sync_all()

        tx_ids_many = self.nodes[1].getaddresstxids(ADDRESS_P2SH)
        assert_equal(len(tx_ids_many), 4)
        assert_equal(tx_ids_many[3], sent_txid)

        # Check that balances are correct
        self.log.info("Testing balances...")
        balance0 = self.nodes[1].getaddressbalance(ADDRESS_P2SH)
        assert_equal(balance0["balance"], 45 * 100000000 + 21)

        # Check that balances are correct after spending
        self.log.info("Testing balances after spending...")
        privkey2 = "cSdkPxkAjA4HDr5VHgsebAPDEh9Gyub4HK8UJr2DFGGqKKy4K5sG"
        address2 = "aR5CWU6bMiuSeSdxiqCnC1jcZajQB7ycLY"
        address_hash2 = bytes([11,47,10,12,49,191,224,64,107,12,204,19,129,253,190,49,25,70,218,220])
        script_pub_key2 = CScript([OP_DUP, OP_HASH160, address_hash2, OP_EQUALVERIFY, OP_CHECKSIG])
        self.nodes[0].importprivkey(privkey2)

        # (an output that is large enough for the two payments below: the first one that
        # the wallet lists may be a small rest of an earlier payment)
        unspent = sorted(self.nodes[0].listunspent(), key=lambda output: output["amount"], reverse=True)
        tx = CTransaction()
        tx.vin = [CTxIn(COutPoint(int(unspent[0]["txid"], 16), unspent[0]["vout"]))]
        amount = int(unspent[0]["amount"] * 100000000 - 230000)
        tx.vout = [CTxOut(amount, script_pub_key2)]
        signed_tx = self.nodes[0].signrawtransaction(binascii.hexlify(tx.serialize()).decode("utf-8"))
        spending_txid = self.nodes[0].sendrawtransaction(signed_tx["hex"], True)
        self.nodes[0].generate(1)
        self.sync_all()
        balance1 = self.nodes[1].getaddressbalance(address2)
        assert_equal(balance1["balance"], amount)

        tx = CTransaction()
        tx.vin = [CTxIn(COutPoint(int(spending_txid, 16), 0))]
        send_amount = 1 * 100000000 + 12840
        # (the minimum relay fee is 0.01 PLB per kB)
        change_amount = amount - send_amount - 300000
        tx.vout = [CTxOut(change_amount, script_pub_key2), CTxOut(send_amount, script_pub_key)]
        tx.rehash()

        signed_tx = self.nodes[0].signrawtransaction(binascii.hexlify(tx.serialize()).decode("utf-8"))
        self.nodes[0].sendrawtransaction(signed_tx["hex"], True)
        self.nodes[0].generate(1)
        self.sync_all()

        balance2 = self.nodes[1].getaddressbalance(address2)
        assert_equal(balance2["balance"], change_amount)

        # Check that deltas are returned correctly
        deltas = self.nodes[1].getaddressdeltas({"addresses": [address2], "start": 1, "end": 200})
        balance3 = 0
        for delta in deltas:
            balance3 += delta["satoshis"]
        assert_equal(balance3, change_amount)
        assert_equal(deltas[0]["address"], address2)
        assert_equal(deltas[0]["blockindex"], 1)

        # Check that entire range will be queried
        deltas_all = self.nodes[1].getaddressdeltas({"addresses": [address2]})
        assert_equal(len(deltas_all), len(deltas))

        # Check that deltas can be returned from range of block heights
        deltas = self.nodes[1].getaddressdeltas({"addresses": [address2], "start": 113, "end": 113})
        assert_equal(len(deltas), 1)

        # Check that unspent outputs can be queried
        self.log.info("Testing utxos...")
        utxos = self.nodes[1].getaddressutxos({"addresses": [address2]})
        assert_equal(len(utxos), 1)
        assert_equal(utxos[0]["satoshis"], change_amount)

        # Check that indexes will be updated with a reorg
        self.log.info("Testing reorg...")

        best_hash = self.nodes[0].getbestblockhash()
        self.nodes[0].invalidateblock(best_hash)
        self.nodes[1].invalidateblock(best_hash)
        self.nodes[2].invalidateblock(best_hash)
        self.nodes[3].invalidateblock(best_hash)
        self.sync_all()

        balance4 = self.nodes[1].getaddressbalance(address2)
        assert_equal(balance4, balance1)

        utxos2 = self.nodes[1].getaddressutxos({"addresses": [address2]})
        assert_equal(len(utxos2), 1)
        assert_equal(utxos2[0]["satoshis"], amount)

        # Check sorting of utxos
        self.nodes[2].generate(150)

        self.nodes[2].sendtoaddress(address2, 50)
        self.nodes[2].generate(1)
        self.nodes[2].sendtoaddress(address2, 50)
        self.nodes[2].generate(1)
        self.sync_all()

        utxos3 = self.nodes[1].getaddressutxos({"addresses": [address2]})
        assert_equal(len(utxos3), 3)
        assert_equal(utxos3[0]["height"], 114)
        assert_equal(utxos3[1]["height"], 264)
        assert_equal(utxos3[2]["height"], 265)

        # Check mempool indexing
        self.log.info("Testing mempool indexing...")

        priv_key3 = "cVfUn53hAbRrDEuMexyfgDpZPhF7KqXpS8UZevsyTDaugB7HZ3CD"
        address3 = "afc6DuMNEJoKVtDHANhq7LH6NjkxJgLJdX"
        address_hash3 = bytes([170,152,114,181,187,205,181,17,216,158,14,17,170,39,218,115,253,44,63,80])
        script_pub_key3 = CScript([OP_DUP, OP_HASH160, address_hash3, OP_EQUALVERIFY, OP_CHECKSIG])
        #address4 = "2N8oFVB2vThAKury4vnLquW2zVjsYjjAkYQ"
        script_pub_key4 = CScript([OP_HASH160, address_hash3, OP_EQUAL])
        unspent = self.nodes[2].listunspent()

        tx = CTransaction()
        tx.vin = [CTxIn(COutPoint(int(unspent[0]["txid"], 16), unspent[0]["vout"]))]
        amount = int(unspent[0]["amount"] * 100000000 - 230000)
        tx.vout = [CTxOut(amount, script_pub_key3)]
        tx.rehash()
        signed_tx = self.nodes[2].signrawtransaction(binascii.hexlify(tx.serialize()).decode("utf-8"))
        mem_txid1 = self.nodes[2].sendrawtransaction(signed_tx["hex"], True)
        time.sleep(2)

        tx2 = CTransaction()
        tx2.vin = [CTxIn(COutPoint(int(unspent[1]["txid"], 16), unspent[1]["vout"]))]
        amount = int(unspent[1]["amount"] * 100000000 - 400000)
        tx2.vout = [
            CTxOut(int(amount / 4), script_pub_key3),
            CTxOut(int(amount / 4), script_pub_key3),
            CTxOut(int(amount / 4), script_pub_key4),
            CTxOut(int(amount / 4), script_pub_key4)
        ]
        tx2.rehash()
        signed_tx2 = self.nodes[2].signrawtransaction(binascii.hexlify(tx2.serialize()).decode("utf-8"))
        mem_txid2 = self.nodes[2].sendrawtransaction(signed_tx2["hex"], True)
        time.sleep(2)

        mempool = self.nodes[2].getaddressmempool({"addresses": [address3]})
        assert_equal(len(mempool), 3)
        assert_equal(mempool[0]["txid"], mem_txid1)
        assert_equal(mempool[0]["address"], address3)
        assert_equal(mempool[0]["index"], 0)
        assert_equal(mempool[1]["txid"], mem_txid2)
        assert_equal(mempool[1]["index"], 0)
        assert_equal(mempool[2]["txid"], mem_txid2)
        assert_equal(mempool[2]["index"], 1)

        self.nodes[2].generate(1)
        self.sync_all()
        mempool2 = self.nodes[2].getaddressmempool({"addresses": [address3]})
        assert_equal(len(mempool2), 0)

        tx = CTransaction()
        tx.vin = [
            CTxIn(COutPoint(int(mem_txid2, 16), 0)),
            CTxIn(COutPoint(int(mem_txid2, 16), 1))
        ]
        tx.vout = [CTxOut(int(amount / 2 - 500000), script_pub_key2)]
        tx.rehash()
        self.nodes[2].importprivkey(priv_key3)
        signed_tx3 = self.nodes[2].signrawtransaction(binascii.hexlify(tx.serialize()).decode("utf-8"))
        self.nodes[2].sendrawtransaction(signed_tx3["hex"], True)
        time.sleep(2)

        mempool3 = self.nodes[2].getaddressmempool({"addresses": [address3]})
        assert_equal(len(mempool3), 2)
        assert_equal(mempool3[0]["prevtxid"], mem_txid2)
        assert_equal(mempool3[0]["prevout"], 0)
        assert_equal(mempool3[1]["prevtxid"], mem_txid2)
        assert_equal(mempool3[1]["prevout"], 1)

        # sending and receiving to the same address
        privkey1 = "cQY2s58LhzUCmEXN8jtAp1Etnijx78YRZ466w4ikX1V4UpTpbsf8"
        address1 = "ahhawPwj4pSKomT6EJrvWuFBAgxe2uM4qx"
        address1hash = bytes([193,146,191,247,81,175,142,254,193,81,53,212,43,254,237,249,26,111,62,52])
        address1script = CScript([OP_DUP, OP_HASH160, address1hash, OP_EQUALVERIFY, OP_CHECKSIG])

        self.nodes[0].sendtoaddress(address1, 10)
        self.nodes[0].generate(1)
        self.sync_all()

        utxos = self.nodes[1].getaddressutxos({"addresses": [address1]})
        assert_equal(len(utxos), 1)

        tx = CTransaction()
        tx.vin = [
            CTxIn(COutPoint(int(utxos[0]["txid"], 16), utxos[0]["outputIndex"]))
        ]
        # the minimum relay fee of Paladeum is 0.01 PLB per kB
        amount = int(utxos[0]["satoshis"] - 200000)
        tx.vout = [CTxOut(amount, address1script)]
        tx.rehash()
        self.nodes[0].importprivkey(privkey1)
        signed_tx = self.nodes[0].signrawtransaction(binascii.hexlify(tx.serialize()).decode("utf-8"))
        self.nodes[0].sendrawtransaction(signed_tx["hex"], True)

        self.sync_all()
        mempool_deltas = self.nodes[2].getaddressmempool({"addresses": [address1]})
        assert_equal(len(mempool_deltas), 2)

        # Include chaininfo in results
        self.log.info("Testing results with chain info...")

        deltas_with_info = self.nodes[1].getaddressdeltas({
            "addresses": [address2],
            "start": 1,
            "end": 200,
            "chainInfo": True
        })
        start_block_hash = self.nodes[1].getblockhash(1)
        end_block_hash = self.nodes[1].getblockhash(200)
        assert_equal(deltas_with_info["start"]["height"], 1)
        assert_equal(deltas_with_info["start"]["hash"], start_block_hash)
        assert_equal(deltas_with_info["end"]["height"], 200)
        assert_equal(deltas_with_info["end"]["hash"], end_block_hash)

        utxos_with_info = self.nodes[1].getaddressutxos({"addresses": [address2], "chainInfo": True})
        expected_tip_block_hash = self.nodes[1].getblockhash(267)
        assert_equal(utxos_with_info["height"], 267)
        assert_equal(utxos_with_info["hash"], expected_tip_block_hash)

        # One address as a plain string gives what the object form gives
        print("Testing an address as a string...")
        assert_equal(self.nodes[1].getaddressutxos(address2), self.nodes[1].getaddressutxos({"addresses": [address2]}))
        assert_equal(self.nodes[1].getaddressbalance(address2), self.nodes[1].getaddressbalance({"addresses": [address2]}))
        assert_equal(self.nodes[1].getaddresstxids(address2), self.nodes[1].getaddresstxids({"addresses": [address2]}))
        # an address given twice counts once
        assert_equal(self.nodes[1].getaddressbalance({"addresses": [address2, address2]}), self.nodes[1].getaddressbalance(address2))
        # a height range for the transaction ids
        all_txids = self.nodes[1].getaddresstxids({"addresses": [address2]})
        some_txids = self.nodes[1].getaddresstxids({"addresses": [address2], "start": 1, "end": 200})
        assert(0 < len(some_txids) <= len(all_txids))
        assert(set(some_txids) <= set(all_txids))
        assert_raises_rpc_error(-5, "Invalid address", self.nodes[1].getaddressbalance, {"addresses": ["not an address"]})
        assert_raises_rpc_error(-8, "addresses", self.nodes[1].getaddressbalance, {})

        # Pages of unspent outputs
        print("Testing pages of unspent outputs...")
        page_address = self.nodes[0].getnewaddress()
        for i in range(7):
            self.nodes[0].sendtoaddress(page_address, 1 + i)
        self.nodes[0].generate(1)
        self.sync_all()
        everything = self.nodes[1].getaddressutxos({"addresses": [page_address]})
        assert_equal(len(everything), 7)
        collected = []
        after = None
        pages = 0
        while True:
            query = {"addresses": [page_address], "limit": 3}
            if after is not None:
                query["after"] = after
            page = self.nodes[1].getaddressutxos(query)
            assert(len(page["utxos"]) <= 3)
            collected += page["utxos"]
            pages += 1
            after = page["next"]
            if after is None:
                break
        assert_equal(pages, 3)
        assert_equal(collected, everything)
        # "txid:index" of an output that is still unspent continues after it as well
        second = self.nodes[1].getaddressutxos({"addresses": [page_address], "limit": 2,
                                                "after": "%s:%d" % (everything[0]["txid"], everything[0]["outputIndex"])})
        assert_equal(second["utxos"], everything[1:3])
        # the "next" form (height:txid:index) places the page by its order, whether or not that output is still unspent
        cursor = "%d:%s:%d" % (everything[2]["height"], everything[2]["txid"], everything[2]["outputIndex"])
        rest = self.nodes[1].getaddressutxos({"addresses": [page_address], "after": cursor, "limit": 100})
        assert_equal(rest["utxos"], everything[3:])
        assert_equal(rest["next"], None)
        assert_raises_rpc_error(-8, "limit", self.nodes[1].getaddressutxos, {"addresses": [page_address], "limit": 0})
        assert_raises_rpc_error(-8, "after", self.nodes[1].getaddressutxos, {"addresses": [page_address], "after": "nonsense"})

        # A pay-to-pubkey output (every stake pays to one) spent in a block that is disconnected again
        print("Testing pay-to-pubkey outputs and disconnected blocks...")
        pk_address = self.nodes[0].getnewaddress()
        pubkey = self.nodes[0].validateaddress(pk_address)["pubkey"]
        p2pk = CTransaction()
        p2pk.vout = [CTxOut(int(5 * COIN), CScript([hex_str_to_bytes(pubkey), OP_CHECKSIG]))]
        funded = self.nodes[0].fundrawtransaction(bytes_to_hex_str(p2pk.serialize()))["hex"]
        p2pk_txid = self.nodes[0].sendrawtransaction(self.nodes[0].signrawtransaction(funded)["hex"], True)
        self.nodes[0].generate(1)
        self.sync_all()
        p2pk_vout = [o["n"] for o in self.nodes[1].getrawtransaction(p2pk_txid, 1)["vout"] if o["scriptPubKey"]["type"] == "pubkey"][0]

        def has_p2pk_output():
            return any(u["txid"] == p2pk_txid and u["outputIndex"] == p2pk_vout
                       for u in self.nodes[1].getaddressutxos({"addresses": [pk_address]}))

        assert(has_p2pk_output())
        balance_unspent = self.nodes[1].getaddressbalance(pk_address)
        spend = self.nodes[0].createrawtransaction([{"txid": p2pk_txid, "vout": p2pk_vout}], {self.nodes[0].getnewaddress(): 4.99})
        self.nodes[0].sendrawtransaction(self.nodes[0].signrawtransaction(spend)["hex"], True)
        spend_block = self.nodes[0].generate(1)[0]
        self.sync_all()
        assert(not has_p2pk_output())
        balance_spent = self.nodes[1].getaddressbalance(pk_address)
        assert(balance_spent["balance"] < balance_unspent["balance"])
        # the block that spent it goes away: the output is unspent again, in the address index as well
        self.nodes[1].invalidateblock(spend_block)
        assert(has_p2pk_output())
        assert_equal(self.nodes[1].getaddressbalance(pk_address), balance_unspent)
        # and back
        self.nodes[1].reconsiderblock(spend_block)
        self.sync_all()
        assert(not has_p2pk_output())
        assert_equal(self.nodes[1].getaddressbalance(pk_address), balance_spent)

        # Tokens in the address index: an issued and a reissued token, and the owner token,
        # are not time locked. Each time a transfer with a lock time is in the same block:
        # nothing of it may show at the other token outputs.
        print("Testing tokens in the address index...")
        wallet, index = self.nodes[0], self.nodes[1]

        def token_balances(address):
            return {entry["tokenName"]: entry for entry in index.getaddressbalance({"addresses": [address]}, True)}

        wallet.issue("LOCKSOURCE", 100, wallet.getnewaddress())
        wallet.generate(1)
        self.sync_all()
        locked_until = index.getblockcount() + 1000
        locked_at = wallet.getnewaddress()
        holder = wallet.getnewaddress()
        wallet.transfer("LOCKSOURCE", 10, locked_at, locked_until)
        wallet.issue("INDEXTEST", 1000, holder)
        wallet.generate(1)
        self.sync_all()
        assert_equal(token_balances(locked_at)["LOCKSOURCE"]["locked"], 10 * COIN)
        assert_equal(token_balances(locked_at)["LOCKSOURCE"]["balance"], 0)
        issued = token_balances(holder)
        assert_equal(issued["INDEXTEST"]["balance"], 1000 * COIN)
        assert_equal(issued["INDEXTEST"]["locked"], 0)
        assert_equal(issued["INDEXTEST!"]["balance"], 1 * COIN)
        assert_equal(issued["INDEXTEST!"]["locked"], 0)

        reissued_to = wallet.getnewaddress()
        wallet.transfer("LOCKSOURCE", 10, wallet.getnewaddress(), locked_until)
        wallet.reissue("INDEXTEST", 500, reissued_to)
        wallet.generate(1)
        self.sync_all()
        reissued = token_balances(reissued_to)
        assert_equal(reissued["INDEXTEST"]["balance"], 500 * COIN)
        assert_equal(reissued["INDEXTEST"]["locked"], 0)
        owner_at = [address for address in index.listaddressesbytoken("INDEXTEST!")][0]
        assert_equal(token_balances(owner_at)["INDEXTEST!"]["locked"], 0)

        # mediantime of a block is the median time past, as getblockchaininfo has it for the tip
        print("Testing mediantime...")
        tip = self.nodes[1].getbestblockhash()
        assert_equal(self.nodes[1].getblock(tip)["mediantime"], self.nodes[1].getblockchaininfo()["mediantime"])
        assert_equal(self.nodes[1].getblockheader(tip)["mediantime"], self.nodes[1].getblockchaininfo()["mediantime"])

        print("Passed\n")


if __name__ == '__main__':
    AddressIndexTest().main()
