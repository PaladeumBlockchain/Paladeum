#!/usr/bin/env python3
# Copyright (c) 2015-2016 The Bitcoin Core developers
# Copyright (c) 2017-2020 The Paladeum developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""Test listmytokens RPC command."""

from test_framework.test_framework import PaladeumTestFramework
from test_framework.util import assert_equal, assert_contains_pair

class ListMyTokensTest(PaladeumTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 2

    def listmytokens_filter_zeros(self):
        """Sometimes the token cache will contain zero-quantity holdings for some tokens (until they're flushed).
           These shouldn't be returned by listmytokens.
        """

        # activate tokens
        self.nodes[0].generate(500)
        self.sync_all()

        assert_equal(0, len(self.nodes[0].listmytokens()))
        assert_equal(0, len(self.nodes[1].listmytokens()))

        self.nodes[0].issue("FOO", 1000)
        self.nodes[0].generate(10)
        self.sync_all()

        result = self.nodes[0].listmytokens()
        assert_equal(2, len(result))
        assert_contains_pair("FOO", 1000, result)
        assert_contains_pair("FOO!", 1, result)

        address_to = self.nodes[1].getnewaddress()
        self.nodes[0].transfer("FOO", 1000, address_to)
        self.nodes[0].generate(10)
        self.sync_all()

        result = self.nodes[0].listmytokens()
        assert_equal(1, len(result))
        assert_contains_pair("FOO!", 1, result)

        result = self.nodes[1].listmytokens()
        assert_equal(1, len(result))
        assert_contains_pair("FOO", 1000, result)


    def run_test(self):
        self.listmytokens_filter_zeros()

if __name__ == '__main__':
    ListMyTokensTest().main()
