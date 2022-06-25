#!/usr/bin/env python3
# Copyright (c) 2017 The Bitcoin Core developers
# Copyright (c) 2017-2020 The Akila developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""
Test sweeping from an address

- 6 nodes
  * node0 will have a collection of AKILA and a few tokens
  * node1 will sweep on a specific token
  * node2 will sweep on a different specific token
  * node3 will sweep on all AKILA
  * node4 will sweep everything else
  * node5 will attempt to sweep, but fail
"""

# Imports should be in PEP8 ordering (std library first, then third party
# libraries then local imports).

from collections import defaultdict

# Avoid wildcard * imports if possible
from test_framework.test_framework import AkilaTestFramework
from test_framework.util import assert_equal, assert_does_not_contain_key, assert_raises_rpc_error, connect_nodes, p2p_port

class FeatureSweepTest(AkilaTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 6
        self.extra_args = [['-tokenindex', '-txindex', '-addressindex'] for _ in range(self.num_nodes)]

    def check_token(self, tokens, token_name, balance):
        token_names = list(tokens.keys())

        assert_equal(token_names.count(token_name), 1)
        assert_equal(balance, tokens[token_name]["balance"])

    def prime_src(self, src_node):
        self.log.info("Priming node to be swept from with some AKILA and 4 different tokens!")

        # Generate the AKILA
        self.log.info("> Generating AKILA...")
        src_node.generate(1)
        self.sync_all()
        src_node.generate(431)
        self.sync_all()
        assert_equal("active", src_node.getblockchaininfo()['bip9_softforks']['tokens']['status'])

        # Issue 3 different tokens
        self.log.info("> Generating 4 different tokens...")
        addr = src_node.getnewaddress()

        src_node.sendtoaddress(addr, 1000)
        src_node.issue(token_name="TOKEN.1", qty=1)
        src_node.issue(token_name="TOKEN.2", qty=2)
        src_node.issue(token_name="TOKEN.3", qty=3)
        src_node.issue(token_name="TOKEN.4", qty=4)

        self.log.info("> Waiting for ten confirmations after issue...")
        src_node.generate(10)
        self.sync_all()

        # Transfer to the address we will be using
        self.log.info("> Transfer tokens to the right address")
        src_node.transfer(token_name="TOKEN.1", qty=1, to_address=addr)
        src_node.transfer(token_name="TOKEN.2", qty=2, to_address=addr)
        src_node.transfer(token_name="TOKEN.3", qty=3, to_address=addr)
        src_node.transfer(token_name="TOKEN.4", qty=4, to_address=addr)

        self.log.info("> Waiting for ten confirmations after transfer...")
        src_node.generate(10)
        self.sync_all()

        # Assert that we have everything correctly set up
        tokens = src_node.listmytokens(token="TOKEN.*", verbose=True)

        assert_equal(100000000000, src_node.getaddressbalance(addr)["balance"])
        self.check_token(tokens, "TOKEN.1", 1)
        self.check_token(tokens, "TOKEN.2", 2)
        self.check_token(tokens, "TOKEN.3", 3)
        self.check_token(tokens, "TOKEN.4", 4)

        # We return the address for getting its private key
        return addr

    def run_test(self):
        """Main test logic"""
        self.log.info("Starting test!")

        # Split the nodes by name
        src_node   = self.nodes[0]
        ast1_node  = self.nodes[1]
        ast2_node  = self.nodes[2]
        akila_node   = self.nodes[3]
        all_node   = self.nodes[4]
        fail_node  = self.nodes[5]

        # Activate tokens and prime the src node
        token_addr = self.prime_src(src_node)
        privkey = src_node.dumpprivkey(token_addr)

        # Sweep single token
        self.log.info("Testing sweeping of a single token")
        txid_token = ast1_node.sweep(privkey=privkey, token_name="TOKEN.2")
        ast1_node.generate(10)
        self.sync_all()

        swept_tokens = ast1_node.listmytokens(token="*", verbose=True)
        swept_keys = list(swept_tokens.keys())

        assert_does_not_contain_key("TOKEN.1", swept_keys)
        self.check_token(swept_tokens, "TOKEN.2", 2)
        assert_does_not_contain_key("TOKEN.2", src_node.listmytokens(token="*", verbose=True).keys())
        assert_does_not_contain_key("TOKEN.3", swept_keys)
        assert_does_not_contain_key("TOKEN.4", swept_keys)

        # Sweep a different single token
        self.log.info("Testing sweeping of a different single token")
        txid_token = ast2_node.sweep(privkey=privkey, token_name="TOKEN.4")
        ast2_node.generate(10)
        self.sync_all()

        swept_tokens = ast2_node.listmytokens(token="*", verbose=True)
        swept_keys = list(swept_tokens.keys())

        assert_does_not_contain_key("TOKEN.1", swept_keys)
        assert_does_not_contain_key("TOKEN.2", swept_keys)
        assert_does_not_contain_key("TOKEN.3", swept_keys)
        self.check_token(swept_tokens, "TOKEN.4", 4)
        assert_does_not_contain_key("TOKEN.4", src_node.listmytokens(token="*", verbose=True).keys())

        # Sweep AKILA
        self.log.info("Testing sweeping of all AKILA")
        txid_akila = akila_node.sweep(privkey=privkey, token_name="AKILA")
        akila_node.generate(10)
        self.sync_all()

        assert (akila_node.getbalance() > 900)

        # Sweep remaining tokens (fail)
        self.log.info("Testing failure of sweeping everything else with insufficient funds")
        assert_raises_rpc_error(-6, f"Please add AKILA to address '{token_addr}' to be able to sweep token ''", all_node.sweep, privkey)

        # Fund the all_node so that we can fund the transaction
        src_node.sendtoaddress(all_node.getnewaddress(), 100)
        src_node.generate(10)
        self.sync_all()

        # Sweep remaining tokens (pass)
        self.log.info("Testing sweeping of everything else")
        txid_all = all_node.sweep(privkey=privkey)
        all_node.generate(10)
        self.sync_all()

        all_tokens = all_node.listmytokens(token="*", verbose=True)

        self.check_token(all_tokens, "TOKEN.1", 1)
        assert_does_not_contain_key("TOKEN.2", all_tokens.keys())
        self.check_token(all_tokens, "TOKEN.3", 3)
        assert_does_not_contain_key("TOKEN.4", all_tokens.keys())

        # Fail with no tokens to sweep
        self.log.info("Testing failure of sweeping an address with no tokens")
        assert_raises_rpc_error(-26, "No tokens to sweep!", all_node.sweep, privkey)

if __name__ == '__main__':
    FeatureSweepTest().main()
