// Copyright (c) 2012-2016 The Bitcoin Core developers
// Copyright (c) 2021-2022 The Yona developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef YONA_VERSION_H
#define YONA_VERSION_H

/**
 * network protocol versioning
 */

static const int PROTOCOL_VERSION = 70028;

//! initial proto version, to be increased after version/verack negotiation
static const int INIT_PROTO_VERSION = 209;

//! demand canonical block signatures starting from this version
static const int CANONICAL_BLOCK_SIG_VERSION = 60018;

//! In this version, 'getheaders' was introduced.
static const int GETHEADERS_VERSION = CANONICAL_BLOCK_SIG_VERSION;

//! tokendata network request is allowed for this version
static const int TOKENDATA_VERSION = 70017;

//! disconnect from peers older than this proto version
//!!! Anytime this value is changed please also update the "MY_VERSION" value to match in the
//!!! ./test/functional/test_framework/mininode.py file. Not doing so will cause verack to fail!
static const int MIN_PEER_PROTO_VERSION = TOKENDATA_VERSION;

//! nTime field added to CAddress, starting with this version;
//! if possible, avoid requesting addresses nodes older than this
static const int CADDR_TIME_VERSION = 31402;

//! BIP 0031, pong message, is enabled for all versions AFTER this one
static const int BIP0031_VERSION = 60000;

//! "filter*" commands are disabled without NODE_BLOOM after and including this version
static const int NO_BLOOM_VERSION = 70011;

//! "sendheaders" command and announcing blocks with headers starts with this version
static const int SENDHEADERS_VERSION = 70012;

//! "feefilter" tells peers to filter invs to you by fee starts with this version
static const int FEEFILTER_VERSION = 70013;

//! short-id-based block download starts with this version
static const int SHORT_IDS_BLOCKS_VERSION = 70014;

//! not banning for invalid compact blocks starts with this version
static const int INVALID_CB_NO_BAN_VERSION = 70015;

//! gettokendata reutrn asstnotfound, and tokendata doesn't have blockhash in the data
static const int TOKENDATA_VERSION_UPDATED = 70020;

//! In this version messaging and restricted tokens was introduced
static const int MESSAGING_RESTRICTED_TOKENS_VERSION = 70026;


#endif // YONA_VERSION_H
