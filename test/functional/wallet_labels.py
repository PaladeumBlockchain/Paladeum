#!/usr/bin/env python3
# Copyright (c) 2016 The Bitcoin Core developers
# Copyright (c) 2017-2020 The Paladeum developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""
Test account RPCs.
RPCs tested are:
    - getaccountaddress
    - getaddressesbyaccount
    - listaddressgroupings
    - setaccount
    - sendfrom (with account arguments)
    - move (with account arguments)
"""

from test_framework.test_framework import PaladeumTestFramework
from test_framework.util import assert_equal

# A coinbase output can be spent once it has COINBASE_MATURITY + 1 confirmations
COINBASE_MATURITY = 10

class WalletAccountsTest(PaladeumTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1
        self.extra_args = [[]]

    def run_test(self):
        node = self.nodes[0]
        # Check that there's no UTXO on any of the nodes
        assert_equal(len(node.listunspent()), 0)

        # Note each time we call generate, all generated coins go into
        # the same address, so we call twice to get two addresses with a
        # mature reward each: 1,000,000,000 PLB of block 1, 10 PLB of block 2
        node.generate(1)
        node.generate(COINBASE_MATURITY + 1)
        assert_equal(node.getbalance(), 1000000010)

        # there should be 2 address groups
        # each with 1 address with the balance of its block
        address_groups = node.listaddressgroupings()
        assert_equal(len(address_groups), 2)
        # the addresses aren't linked now, but will be after we send to the
        # common address
        linked_addresses = set()
        for address_group in address_groups:
            assert_equal(len(address_group), 1)
            assert_equal(len(address_group[0]), 2)
            linked_addresses.add(address_group[0][0])
        assert_equal(sorted(address_group[0][1] for address_group in address_groups), [10, 1000000000])

        # send the reward of each address to a third address not in this wallet
        # (a regtest address of a key that this wallet does not have)
        # There's some fee that will come back to us when the miner reward
        # matures.
        common_address = "aRZAhLKRtnEhzcWBfS7k5TJVbRc9uhXEeA"
        txid = node.sendmany(
            fromaccount="",
            amounts={common_address: 1000000010},
            subtractfeefrom=[common_address],
            minconf=1,
        )
        tx_details = node.gettransaction(txid)
        fee = -tx_details['details'][0]['fee']
        # there should be 1 address group, with the previously
        # unlinked addresses now linked (they both have 0 balance)
        address_groups = node.listaddressgroupings()
        assert_equal(len(address_groups), 1)
        assert_equal(len(address_groups[0]), 2)
        assert_equal(set([a[0] for a in address_groups[0]]), linked_addresses)
        assert_equal([a[1] for a in address_groups[0]], [0, 0])

        node.generate(1)

        # we want to reset so that the "" account has what's expected.
        # otherwise we're off by exactly the fee amount as that's mined
        # and matures in the next 10 blocks
        node.sendfrom("", common_address, fee)
        accounts = ["a", "b", "c", "d", "e"]
        amount_to_send = 1.0
        account_addresses = dict()
        for account in accounts:
            address = node.getaccountaddress(account)
            account_addresses[account] = address
            
            node.getnewaddress(account)
            assert_equal(node.getaccount(address), account)
            assert(address in node.getaddressesbyaccount(account))
            
            node.sendfrom("", address, amount_to_send)
        
        node.generate(1)
        
        for i in range(len(accounts)):
            from_account = accounts[i]
            to_account = accounts[(i+1) % len(accounts)]
            to_address = account_addresses[to_account]
            node.sendfrom(from_account, to_address, amount_to_send)
        
        node.generate(1)
        
        for account in accounts:
            address = node.getaccountaddress(account)
            assert(address != account_addresses[account])
            assert_equal(node.getreceivedbyaccount(account), 2)
            node.move(account, "", node.getbalance(account))

        node.generate(COINBASE_MATURITY + 1)

        # The rewards of the mature blocks (10 PLB each) but those of blocks 1 and 2,
        # which went to the common address; all fees paid have matured as rewards.
        expected_balance = 10 * (node.getblockcount() - COINBASE_MATURITY - 2)
        assert_equal(expected_balance, 140)
        expected_account_balances = {"": expected_balance}
        for account in accounts:
            expected_account_balances[account] = 0
        
        assert_equal(node.listaccounts(), expected_account_balances)
        
        assert_equal(node.getbalance(""), expected_balance)
        
        for account in accounts:
            address = node.getaccountaddress("")
            node.setaccount(address, account)
            assert(address in node.getaddressesbyaccount(account))
            assert(address not in node.getaddressesbyaccount(""))
        
        for account in accounts:
            addresses = []
            for x in range(10):
                addresses.append(node.getnewaddress())
            multisig_address = node.addmultisigaddress(5, addresses, account)
            node.sendfrom("", multisig_address, 10)
        
        node.generate(1)
        
        for account in accounts:
            assert_equal(node.getbalance(account), 10)

if __name__ == '__main__':
    WalletAccountsTest().main()
