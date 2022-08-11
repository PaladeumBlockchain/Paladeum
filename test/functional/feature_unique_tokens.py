#!/usr/bin/env python3
# Copyright (c) 2017 The Bitcoin Core developers
# Copyright (c) 2017-2020 The Paladeum developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""Testing unique token use cases"""

import random
from test_framework.test_framework import PaladeumTestFramework
from test_framework.util import assert_contains, assert_does_not_contain_key, assert_equal, assert_raises_rpc_error


def gen_root_token_name():
    size = random.randint(3, 14)
    name = ""
    for _ in range(1, size + 1):
        ch = random.randint(65, 65 + 25)
        name += chr(ch)
    return name


def gen_unique_token_name(root):
    tag_ab = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789@$%&*()[]{}_.?-:"
    name = root + "#"
    tag_size = random.randint(1, 15)
    for _ in range(1, tag_size + 1):
        tag_c = tag_ab[random.randint(0, len(tag_ab) - 1)]
        name += tag_c
    return name


class UniqueTokenTest(PaladeumTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 3
        self.extra_args = [['-tokenindex'], ['-tokenindex'], ['-tokenindex']]

    def activate_tokens(self):
        self.log.info("Generating PLB for node[0] and activating tokens...")
        n0 = self.nodes[0]
        n0.generate(432)
        self.sync_all()
        assert_equal("active", n0.getblockchaininfo()['bip9_softforks']['tokens']['status'])

    def issue_one(self):
        self.log.info("Issuing a unique token...")
        n0 = self.nodes[0]
        root = gen_root_token_name()
        n0.issue(token_name=root)
        n0.generate(1)
        token_name = gen_unique_token_name(root)
        n0.issue(token_name=token_name)
        n0.generate(1)
        assert_equal(1, n0.listmytokens()[token_name])

    def issue_invalid(self):
        self.log.info("Trying some invalid calls...")
        n0, n1 = self.nodes[0], self.nodes[1]
        n1.generate(10)
        self.sync_all()
        root = gen_root_token_name()
        token_name = gen_unique_token_name(root)

        # no root
        assert_raises_rpc_error(-32600, f"Wallet doesn't have token: {root}!", n0.issue, token_name)

        # don't own root
        n0.sendtoaddress(n1.getnewaddress(), 501)
        n0.generate(1)
        self.sync_all()
        n1.issue(root)
        n1.generate(1)
        self.sync_all()
        assert_contains(root, n0.listtokens())
        assert_raises_rpc_error(-32600, f"Wallet doesn't have token: {root}!", n0.issue, token_name)
        n1.transfer(f"{root}!", 1, n0.getnewaddress())
        n1.generate(1)
        self.sync_all()

        # bad qty
        assert_raises_rpc_error(-8, "Invalid parameters for issuing a unique token.", n0.issue, token_name, 2)

        # bad units
        assert_raises_rpc_error(-8, "Invalid parameters for issuing a unique token.", n0.issue, token_name, 1, "", "", 1)

        # reissuable
        assert_raises_rpc_error(-8, "Invalid parameters for issuing a unique token.", n0.issue, token_name, 1, "", "", 0, True)

        # already exists
        n0.issue(token_name)
        n0.generate(1)
        self.sync_all()
        assert_raises_rpc_error(-8, f"Invalid parameter: token_name '{token_name}' has already been used", n0.issue, token_name)

    def issue_unique_test(self):
        self.log.info("Testing issueunique RPC...")
        n0, n1 = self.nodes[0], self.nodes[1]
        n0.sendtoaddress(n1.getnewaddress(), 501)

        root = gen_root_token_name()
        n0.issue(token_name=root)
        token_tags = ["first", "second"]
        ipfs_hashes = ["QmWWQSuPMS6aXCbZKpEjPHPUZN2NjB3YrhJTHsV4X3vb2t"] * len(token_tags)
        n0.issueunique(root, token_tags, ipfs_hashes)
        block_hash = n0.generate(1)[0]

        token_name = ""
        for tag in token_tags:
            token_name = f"{root}#{tag}"
            assert_equal(1, n0.listmytokens()[token_name])
            assert_equal(1, n0.listtokens(token_name, True)[token_name]['has_ipfs'])
            assert_equal(ipfs_hashes[0], n0.listtokens(token_name, True)[token_name]['ipfs_hash'])

        # invalidate
        n0.invalidateblock(block_hash)
        assert (root in n0.listmytokens())
        assert_does_not_contain_key(token_name, n0.listmytokens(token="*", verbose=False, count=100000, start=0, confs=1))

        # reconsider
        n0.reconsiderblock(block_hash)
        assert_contains(root, n0.listmytokens())
        assert_contains(token_name, n0.listmytokens())

        # root doesn't exist
        missing_token = "VAPOUR"
        assert_raises_rpc_error(-32600, f"Wallet doesn't have token: {missing_token}!", n0.issueunique, missing_token, token_tags)

        # don't own root
        n1.issue(missing_token)
        n1.generate(1)
        self.sync_all()
        assert_contains(missing_token, n0.listtokens())
        assert_raises_rpc_error(-32600, f"Wallet doesn't have token: {missing_token}!", n0.issueunique, missing_token, token_tags)

    def run_test(self):
        self.activate_tokens()
        self.issue_unique_test()
        self.issue_one()
        self.issue_invalid()


if __name__ == '__main__':
    UniqueTokenTest().main()
