#!/usr/bin/env python3
# Copyright (c) 2017 The Bitcoin Core developers
# Copyright (c) 2017-2020 The Paladeum developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""Test the -uacomment option."""

from test_framework.test_framework import PaladeumTestFramework
from test_framework.util import assert_equal

class UacommentTest(PaladeumTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True

    def run_test(self):
        self.log.info("test multiple -uacomment")
        subversion = self.nodes[0].getnetworkinfo()["subversion"]
        test_uacomment = subversion[-12:-1]
        assert_equal(test_uacomment, "(testnode0)")

        self.restart_node(0, ["-uacomment=foo"])
        foo_uacomment = self.nodes[0].getnetworkinfo()["subversion"][-17:-1]
        assert_equal(foo_uacomment, "(testnode0; foo)")

        self.log.info("test -uacomment max length")
        self.stop_node(0)
        # the version string with the comments "testnode0" and 256 times "a": "; " and the comment longer
        too_long = len(subversion) + len("; ") + 256
        expected = "Total length of network version string (%d) exceeds maximum length (256). Reduce the number or size of uacomments." % too_long
        self.assert_start_raises_init_error(0, ["-uacomment=" + 'a' * 256], expected)

        self.log.info("test -uacomment unsafe characters")
        for unsafe_char in ['/', ':', '(', ')']:
            expected = "User Agent comment (" + unsafe_char + ") contains unsafe characters"
            self.assert_start_raises_init_error(0, ["-uacomment=" + unsafe_char], expected)

if __name__ == '__main__':
    UacommentTest().main()
