// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2021-2022 The Paladeum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "chainparams.h"
#include "consensus/merkle.h"

#include "tinyformat.h"
#include "util.h"
#include "utilstrencodings.h"
#include "arith_uint256.h"

#include <assert.h>
#include "chainparamsseeds.h"

void GenesisGenerator(CBlock genesis) {
    printf("Searching for genesis block...\n");

    uint256 hash;
    bool fNegative;
    bool fOverflow;
    arith_uint256 bnTarget;
    bnTarget.SetCompact(genesis.nBits, &fNegative, &fOverflow);

    while(true)
    {
        hash = genesis.GetWorkHash();
        if (UintToArith256(hash) <= bnTarget)
            break;
        if ((genesis.nNonce & 0xFFF) == 0)
        {
            printf("nonce %08X: hash = %s (target = %s)\n", genesis.nNonce, hash.ToString().c_str(), bnTarget.ToString().c_str());
        }
        ++genesis.nNonce;
        if (genesis.nNonce == 0)
        {
            printf("NONCE WRAPPED, incrementing time\n");
            ++genesis.nTime;
        }
    }

    printf("block.nNonce = %u \n", genesis.nNonce);
    printf("block.GetIndexHash = %s\n", genesis.GetIndexHash().ToString().c_str());
    printf("block.GetWorkHash = %s\n", hash.ToString().c_str());
    printf("block.MerkleRoot = %s \n", genesis.hashMerkleRoot.ToString().c_str());
}

static CBlock CreateGenesisBlock(const char* pszTimestamp, const CScript& genesisOutputScript, uint32_t nTime, uint64_t nNonce, uint32_t nBits, int32_t nVersion, const CAmount& genesisReward)
{
    CMutableTransaction txNew;
    txNew.nVersion = 1;
    txNew.vin.resize(1);
    txNew.vout.resize(1);
    txNew.vin[0].scriptSig = CScript() << CScriptNum(0) << 486604799 << CScriptNum(4) << std::vector<unsigned char>((const unsigned char*)pszTimestamp, (const unsigned char*)pszTimestamp + strlen(pszTimestamp));
    txNew.vout[0].nValue = genesisReward;
    txNew.vout[0].scriptPubKey = genesisOutputScript;
    txNew.nTime = nTime;

    CBlock genesis;
    genesis.nTime    = nTime;
    genesis.nBits    = nBits;
    genesis.nNonce   = nNonce;
    genesis.nVersion = nVersion;
    genesis.vtx.push_back(MakeTransactionRef(std::move(txNew)));
    genesis.hashPrevBlock.SetNull();
    genesis.hashMerkleRoot = BlockMerkleRoot(genesis);
    return genesis;
}

/**
 * Build the genesis block. Note that the output of its generation
 * transaction cannot be spent since it did not originally exist in the
 * database.
 */
static CBlock CreateGenesisBlock(const char* pszTimestamp, uint32_t nTime, uint64_t nNonce, uint32_t nBits, int32_t nVersion, const CAmount& genesisReward)
{
    const CScript genesisOutputScript = CScript() << ParseHex("04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112de5c384df7ba0b8d578a4c702b6bf11d5f") << OP_CHECKSIG;
    return CreateGenesisBlock(pszTimestamp, genesisOutputScript, nTime, nNonce, nBits, nVersion, genesisReward);
}

void CChainParams::UpdateVersionBitsParameters(Consensus::DeploymentPos d, int64_t nStartTime, int64_t nTimeout)
{
    consensus.vDeployments[d].nStartTime = nStartTime;
    consensus.vDeployments[d].nTimeout = nTimeout;
}

void CChainParams::UpdateLastPOWBlock(int nHeight)
{
    consensus.nLastPOWBlock = nHeight;
}

void CChainParams::UpdateFutureDriftHeight(int nHeight)
{
    consensus.nFutureDriftHeight = nHeight;
}

void CChainParams::UpdateMasterAddress(const std::string& address)
{
    strMasterAddress = address;
}

void CChainParams::TurnOffSegwit() {
	consensus.nSegwitEnabled = false;
}

void CChainParams::TurnOffCSV() {
	consensus.nCSVEnabled = false;
}

void CChainParams::TurnOffBIP34() {
	consensus.nBIP34Enabled = false;
}

void CChainParams::TurnOffBIP65() {
	consensus.nBIP65Enabled = false;
}

void CChainParams::TurnOffBIP66() {
	consensus.nBIP66Enabled = false;
}

bool CChainParams::BIP34() {
	return consensus.nBIP34Enabled;
}

bool CChainParams::BIP65() {
	return consensus.nBIP65Enabled;
}

bool CChainParams::BIP66() {
	return consensus.nBIP66Enabled;
}

bool CChainParams::CSVEnabled() const{
	return consensus.nCSVEnabled;
}


/**
 * Main network
 */
/**
 * What makes a good checkpoint block?
 * + Is surrounded by blocks with reasonable timestamps
 *   (no blocks before with a timestamp after, none after with
 *    timestamp before)
 * + Contains no strange transactions
 */

class CMainParams : public CChainParams {
public:
    CMainParams() {
        strNetworkID = "main";
        consensus.nBIP34Enabled = true;
        consensus.nBIP65Enabled = true;
        consensus.nBIP66Enabled = true;
        consensus.nSegwitEnabled = false;
        consensus.nCSVEnabled = true;
        consensus.powLimit = uint256S("003fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.posLimit = uint256S("00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nTargetTimespan = 16 * 60; // 16 mins
        consensus.nTargetSpacing = 64;
        consensus.fDiffNoRetargeting = false;
        consensus.fDiffAllowMinDifficultyBlocks = false;
        consensus.nRuleChangeActivationThreshold = 1613; // Approx 80% of 2016
        consensus.nMinerConfirmationWindow = 2016; // nPowTargetTimespan / nTargetSpacing
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 1199145601; // January 1, 2008
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = 1230767999; // December 31, 2008
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nOverrideRuleChangeActivationThreshold = 1814;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nOverrideMinerConfirmationWindow = 2016;

        // The best chain should have at least this much work
        consensus.nMinimumChainWork = uint256S("0x000000000000000000000000000000000000000000000036afb9f21e9ba56ec8"); // block 716000

        // By default assume that the signatures in ancestors of this block are valid.
        consensus.defaultAssumeValid = uint256S("0xcefe3b72cf4f3e2e77159fa34f7935b8988274dff9c7e52a9b9f7bc6204e2bf8"); // block 716000

        // Proof-of-Stake
        consensus.nLastPOWBlock = 150;
        consensus.nTxMessages = 10;
        consensus.nStakeTimestampMask = 0xf; // 15

        consensus.nGovernanceFixHeight = 577090;

        // Fork to enable offline staking and remove the block limiter
        consensus.offlineStakingFork = std::numeric_limits<int>::max();

        // Soft forks that are not scheduled (INT_MAX): the time of a proof-of-stake block in
        // multiples of nStakeTimestampMask + 1, and a smaller allowance for times in the future.
        consensus.nStakeTimestampMaskHeight = std::numeric_limits<int>::max();
        consensus.nFutureDriftHeight = std::numeric_limits<int>::max();
        consensus.nFutureDriftSeconds = 60;

        /**
         * The message start string is designed to be unlikely to occur in normal data.
         * The characters are rarely used upper ASCII, not valid as UTF-8, and produce
         * a large 32-bit integer with any alignment.
         */
        pchMessageStart[0] = 0x50;
        pchMessageStart[1] = 0x4c;
        pchMessageStart[2] = 0x42;
        pchMessageStart[3] = 0x56;
        nDefaultPort = 7576;
        nPruneAfterHeight = 100000;

        const char* pszTimestamp = "Webb Spots Possible Galaxy Merger in Early Universe | Oct 26, 2022 Sci-News";

        genesis = CreateGenesisBlock(pszTimestamp, 1666883415, 18, 0x1f3fffff, 1, 1 * COIN);
        consensus.hashGenesisBlock = genesis.GetIndexHash();

        assert(consensus.hashGenesisBlock == uint256S("0x10390eb26c0a67a9f3ba797ded61077651385523f031c69166c6ebb48c1e0deb"));
        assert(genesis.hashMerkleRoot == uint256S("0x630cb6149c228733eca4835d352557a4022e05b77c76c857c3928ef3ae55cfbf"));

        vSeeds.emplace_back("dns.paladeum.io", false);

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,56);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,51);
        base58Prefixes[SECRET_KEY]     = std::vector<unsigned char>(1,89);
        base58Prefixes[OFFLINE_STAKING_ADDRESS] = std::vector<unsigned char>(1,91);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};

        vFixedSeeds = std::vector<SeedSpec6>(pnSeed6_main, pnSeed6_main + ARRAYLEN(pnSeed6_main));

        fDefaultConsistencyChecks = false;
        fRequireStandard = true;
        fMineBlocksOnDemand = false;
        fMiningRequiresPeers = true;

        // Blocks of the chain that was validated in full (-assumevalid=0) with this program and gave
        // the same set of unspent outputs as Paladeum 0.1.7 (height 716790,
        // hash_serialized_2 df5bfe07a46bb9c828b040263e645db09dd27947a76a97633f6e764fe09588be).
        checkpointData = (CCheckpointData) {
            {
                {     150, uint256S("0xf49e0798b0d129f51cbec02424957a518875c189b866eaa39bcf9e83e4095399")},
                {   50000, uint256S("0x6f3531bd67cb04f24d995573f138f798d38372e677bfc46de8860922dd9959f3")},
                {  100000, uint256S("0x0b6847b24f4b294221ff9554323fc5e050877c8b79377996bf2070a7413b30c2")},
                {  200000, uint256S("0xb4c7ec7708f84e396b0779ede0d3f7f348ead0bfe4c3aa5719b9cc1e9e9cfc8e")},
                {  300000, uint256S("0x6531290a7a682fe448ab76b7a498bcfcfcbe66f055f62f1167730958ea4f54a5")},
                {  400000, uint256S("0xe906870e27485092857b2dc58e70b3e9c502b53bbdc4f3982f7ed59032e5a3c1")},
                {  500000, uint256S("0x42aba1b794f86a2beddd29377a3fdcc8f31b188fe9ec3d3cb85e2a69d88ab080")},
                {  600000, uint256S("0x3ca83cc31723846f0408d216f8adc63729fb344078cb52b6cb28cf4e6a258844")},
                {  700000, uint256S("0x2c5c64ef1cdd402b5b4ecd9efc8c0460521d8bb4a58746257da0e5a759b9b398")},
            }
        };

        // getchaintxstats 4096 at block 716000
        chainTxData = ChainTxData{
            1794204176, // time of block 716000
            1478617,    // transactions up to it
            0.0026      // transactions per second in the 4096 blocks before it
        };

        // The chain state snapshot that this program accepts (see snapshot.h): the state after
        // block 715,913, the last block of the published block archives. The values are the
        // output of getstatehashes of a node that validated every block of the archives
        // (contrib/paladeum/make_snapshot.py prints them).
        snapshot.height = 715913;
        snapshot.block = uint256S("f990001f1fe2ef8aa403dec0d54568bf0f5b128512cf367b939f5bf1c593bd19");
        snapshot.utxo = uint256S("901c6ff11f6330db39c8c40ea940de887ac1936ce65ae715bb7b6d84d389c4ca");
        snapshot.tokens = uint256S("46c5c567049b12e5f69cedf8970da5e1d75a0ad343498c24e814005eb7205923");
        snapshot.governance = uint256S("656a8ff77c016efbc548a9f10a9f499481fdd5e8553ee547aca1994c9cfd6823");
        snapshot.index = uint256S("26edfe068e6ac1acfcab0d86962d7ad0116871808778699ed56ac05604292fd8");
        snapshot.archive = "81cdcb9b4c1d0c112a127551b39e1447c22e89c60feb203197b402c74c880fe7";

        /** PLB Start **/
        // Fee Amounts
        nIssueTokenFeeAmount = 10 * COIN;
        nReissueTokenFeeAmount = 2 * COIN;
        nIssueSubTokenFeeAmount = 5 * COIN;
        nIssueUniqueTokenFeeAmount = 0.2 * COIN;
        nIssueUsernameTokenFeeAmount = 2 * COIN;
        nIssueMsgChannelTokenFeeAmount = 2 * COIN;
        nIssueQualifierTokenFeeAmount = 20 * COIN;
        nIssueSubQualifierTokenFeeAmount = 3 * COIN;
        nIssueRestrictedTokenFeeAmount = 10 * COIN;
        nAddNullQualifierTagFeeAmount = 0.01 * COIN;

        // Global fee address
        strTokenFeeAddress = "PseBvu3oGQyzA11f1pmRAoe95XCVNoUiYA";
        strMasterAddress = "MZNrB7Eim6J9WejZ5XKnH1eii4pwYZeirV";

        nMaxReorganizationDepth = 500;

        init_authorized = {
            "PZmyQWTunep9d9HmYB3nJ9sF6az1H7Dw85",
            "PmXcRhU6apj3v5m4YcRwu9mAJEqE86ytVq",
            "PrgsXc3coJcNj77RBrgot8F1pJ1jjsL4YF",
            "PkNV6QtyrDX7W9SNUS6BgTpsTHMqFYMBYr",
            "PgdehkKsYLAybiwZ3UJVhqGWRdm1RnVohk"
        };

        // BIP44 cointype
        nExtCoinType = 1;
        /** PLB End **/
    }
};

/**
 * Testnet
 */
class CTestNetParams : public CChainParams {
public:
    CTestNetParams() {
        strNetworkID = "test";
        consensus.nBIP34Enabled = true;
        consensus.nBIP65Enabled = true;
        consensus.nBIP66Enabled = true;
        consensus.nSegwitEnabled = false;
        consensus.nCSVEnabled = true;
        consensus.powLimit = uint256S("003fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.posLimit = uint256S("00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nTargetTimespan = 16 * 60; // 16 mins
        consensus.nTargetSpacing = 64; // * 2 is needed for hybrid PoW/PoS (actual block time will be 25)
        consensus.fDiffNoRetargeting = false;
        consensus.fDiffAllowMinDifficultyBlocks = false;
        consensus.nRuleChangeActivationThreshold = 1310; // Approx 65% for testchains
        consensus.nMinerConfirmationWindow = 2016; // nPowTargetTimespan / nTargetSpacing
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 1199145601; // January 1, 2008
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = 1230767999; // December 31, 2008
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nOverrideRuleChangeActivationThreshold = 1310;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nOverrideMinerConfirmationWindow = 2016;

        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x");

        // By default assume that the signatures in ancestors of this block are valid.
        consensus.defaultAssumeValid = uint256S("0x29d575a69da4298782bd3487ba5597e70843aeb67ba845f13ea8e3e2a671320e");

        // Proof-of-Stake
        consensus.nLastPOWBlock = std::numeric_limits<int>::max();
        consensus.nTxMessages = 10;
        consensus.nStakeTimestampMask = 0xf; // 15

        consensus.nGovernanceFixHeight = 1;

        // Soft forks that are not scheduled (INT_MAX): the time of a proof-of-stake block in
        // multiples of nStakeTimestampMask + 1, and a smaller allowance for times in the future.
        consensus.nStakeTimestampMaskHeight = std::numeric_limits<int>::max();
        consensus.nFutureDriftHeight = std::numeric_limits<int>::max();
        consensus.nFutureDriftSeconds = 60;

        // Fork to enable offline staking and remove the block limiter
        consensus.offlineStakingFork = 10;

        pchMessageStart[0] = 0xbb;
        pchMessageStart[1] = 0xab;
        pchMessageStart[2] = 0xaa;
        pchMessageStart[3] = 0xba;
        nDefaultPort = 16465;
        nPruneAfterHeight = 1000;

        const char* pszTimestamp = "Newly-Discovered Cataclysmic Variable Has Extremely Short Orbit | Oct 6, 2022 Sci-News";

        genesis = CreateGenesisBlock(pszTimestamp, 1665084955, 1362, 0x1f3fffff, 1, 1 * COIN);
        consensus.hashGenesisBlock = genesis.GetIndexHash();

        assert(consensus.hashGenesisBlock == uint256S("0x29d575a69da4298782bd3487ba5597e70843aeb67ba845f13ea8e3e2a671320e"));
        assert(genesis.hashMerkleRoot == uint256S("0xebc3610fc8f95a58b26c621e73ca9e694ac28725b467a319dbcf88c7f7e5bfdc"));

        vFixedSeeds.clear();
        vSeeds.clear();

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,83);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,125);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,108);
        base58Prefixes[OFFLINE_STAKING_ADDRESS] = std::vector<unsigned char>(1,115);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};

        // Paladeum BIP44 cointype in testnet
        nExtCoinType = 1;

        vFixedSeeds = std::vector<SeedSpec6>(pnSeed6_test, pnSeed6_test + ARRAYLEN(pnSeed6_test));

        fDefaultConsistencyChecks = false;
        fRequireStandard = false;
        fMineBlocksOnDemand = false;
        fMiningRequiresPeers = false;

        checkpointData = (CCheckpointData) {
            {
                
            }
        };

        chainTxData = ChainTxData{
            0,
            0,
            0
        };

        /** PLB Start **/
        // Fee Amounts
        nIssueTokenFeeAmount = 10 * COIN;
        nReissueTokenFeeAmount = 2 * COIN;
        nIssueSubTokenFeeAmount = 5 * COIN;
        nIssueUniqueTokenFeeAmount = 0.2 * COIN;
        nIssueUsernameTokenFeeAmount = 2 * COIN;
        nIssueMsgChannelTokenFeeAmount = 2 * COIN;
        nIssueQualifierTokenFeeAmount = 20 * COIN;
        nIssueSubQualifierTokenFeeAmount = 3 * COIN;
        nIssueRestrictedTokenFeeAmount = 10 * COIN;
        nAddNullQualifierTagFeeAmount = 0.01 * COIN;

        // Global fee address
        // Testing only: H5HT6QCM37sJ52QMe5Mm3oooDDTgZvqdJoJitZA62DBneguiUYga
        strTokenFeeAddress = "aeqWq9ovJZivVXnZYjTP8WLnJsjKTMybhR";
        strMasterAddress = "aeqWq9ovJZivVXnZYjTP8WLnJsjKTMybhR";

        nMaxReorganizationDepth = 500;

        init_authorized = {
            // Testing only: H1RniRW5Ad64PMgn6mCzAnWZ5bgErwYfCFxoWHXncFMj3VNQm8Zn
            "adWfR3GWw4faVmdcT6He9ztwahEKHRXZYs",
            // Testing only: H17JdgJe5EvWFYHXLLJPgx4Wkq25sBBuLBsmLKdp7t3MtBjiqUbZ
            "abZmftHzCpKtam2V4L27KCrrXpQ4uTeoZm",
            // Testing only: H1Mi9CyYHbRqYiMg3S4Ny1Y6FHsg54GohCjjpbHN9SEsSHY2i668
            "aW3dcTH2HzgKgNMZxJxrPuw4H5NsJoXgZz"
        };

        /** PLB End **/
    }
};

/**
 * Regression test
 */
class CRegTestParams : public CChainParams {
public:
    CRegTestParams() {
        strNetworkID = "regtest";
        consensus.nBIP34Enabled = true;
        consensus.nBIP65Enabled = true; // 000000000000000004c2b624ed5d7756c508d90fd0da2c7c679febfa6c4735f0
        consensus.nBIP66Enabled = true;
        consensus.nSegwitEnabled = true;
        consensus.nCSVEnabled = true;
        consensus.powLimit = uint256S("7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.posLimit = uint256S("7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nTargetTimespan = 16 * 60; // 16 mins
        consensus.nTargetSpacing = 64;
        consensus.fDiffNoRetargeting = true;
        consensus.fDiffAllowMinDifficultyBlocks = true;
        consensus.nRuleChangeActivationThreshold = 108; // 75% for testchains
        consensus.nMinerConfirmationWindow = 144; // Faster than normal for regtest (144 instead of 2016)
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = 999999999999ULL;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nOverrideRuleChangeActivationThreshold = 108;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nOverrideMinerConfirmationWindow = 144;

        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x00");

        // By default assume that the signatures in ancestors of this block are valid.
        consensus.defaultAssumeValid = uint256S("0x9d7805d5ce13abc52658fe089aa01ea4d6be2594b8d97f65c912030059a6e6b9");

        // Proof-of-Stake
        consensus.nLastPOWBlock = std::numeric_limits<int>::max();
        consensus.nTxMessages = 10;
        consensus.nStakeTimestampMask = 0xf; // 15
        consensus.nStakeTimestampMaskHeight = 0;
        consensus.nFutureDriftHeight = std::numeric_limits<int>::max(); // (-futuredriftheight)
        consensus.nFutureDriftSeconds = 60;

        consensus.nGovernanceFixHeight = 1;

        // Fork to enable offline staking and remove the block limiter
        consensus.offlineStakingFork = 0;

        pchMessageStart[0] = 0x80;
        pchMessageStart[1] = 0x6a;
        pchMessageStart[2] = 0x62;
        pchMessageStart[3] = 0x52;
        nDefaultPort = 26465;
        nPruneAfterHeight = 1000;

        const char* pszTimestamp = "Webb Images Earendel, Farthest Known Star | Aug 9, 2022 Sci-News";

        genesis = CreateGenesisBlock(pszTimestamp, 1524179366, 5, 0x207fffff, 4, 5000 * COIN);
        consensus.hashGenesisBlock = genesis.GetIndexHash();

        assert(consensus.hashGenesisBlock == uint256S("0x9d7805d5ce13abc52658fe089aa01ea4d6be2594b8d97f65c912030059a6e6b9 "));
        assert(genesis.hashMerkleRoot == uint256S("0xe3e448034a4053c8d60567a6f3fe861c4ece9092a70f97a612fe1e0a13aa7b09"));

        vFixedSeeds.clear(); //!< Regtest mode doesn't have any fixed seeds.
        vSeeds.clear();      //!< Regtest mode doesn't have any DNS seeds.

        fDefaultConsistencyChecks = true;
        fRequireStandard = false;
        fMineBlocksOnDemand = true;

        checkpointData = (CCheckpointData) {
            {
            }
        };

        chainTxData = ChainTxData{
            0,
            0,
            0
        };

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,83);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,196);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,239);
        base58Prefixes[OFFLINE_STAKING_ADDRESS] = std::vector<unsigned char>(1,21);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};

        // Paladeum BIP44 cointype in regtest
        nExtCoinType = 1;

        /** PLB Start **/
        // Fee Amounts
        nIssueTokenFeeAmount = 10 * COIN;
        nReissueTokenFeeAmount = 2 * COIN;
        nIssueSubTokenFeeAmount = 5 * COIN;
        nIssueUniqueTokenFeeAmount = 0.2 * COIN;
        nIssueUsernameTokenFeeAmount = 2 * COIN;
        nIssueMsgChannelTokenFeeAmount = 2 * COIN;
        nIssueQualifierTokenFeeAmount = 20 * COIN;
        nIssueSubQualifierTokenFeeAmount = 3 * COIN;
        nIssueRestrictedTokenFeeAmount = 10 * COIN;
        nAddNullQualifierTagFeeAmount = 0.01 * COIN;

        // Global fee address: a regtest address (version 83) that nobody has a key for, the
        // hash160 is RIPEMD160(SHA256("Paladeum regtest token fees")). The address before
        // (mmbbmG...) had Bitcoin's testnet version (111), so the fee output of a token could
        // never match it and no token could be issued on regtest.
        strTokenFeeAddress = "aWcYoTSeLQsdEaWmijA12pUYae5D2jSd93";
        strMasterAddress = "";

        nMaxReorganizationDepth = 500;

        init_authorized = {
            "aSQ4JkAxPdPa4tx3aGJycoAK5fhVamr76G"
        };

        // TODO, we need to figure out what to do with this for regtest. This effects the unit tests
        // For now we can use a timestamp very far away
        /** PLB End **/
    }
};

static std::unique_ptr<CChainParams> globalChainParams;

const CChainParams &GetParams() {
    assert(globalChainParams);
    return *globalChainParams;
}

std::unique_ptr<CChainParams> CreateChainParams(const std::string& chain)
{
    if (chain == CBaseChainParams::MAIN)
        return std::unique_ptr<CChainParams>(new CMainParams());
    else if (chain == CBaseChainParams::TESTNET)
        return std::unique_ptr<CChainParams>(new CTestNetParams());
    else if (chain == CBaseChainParams::REGTEST)
        return std::unique_ptr<CChainParams>(new CRegTestParams());
    throw std::runtime_error(strprintf("%s: Unknown chain %s.", __func__, chain));
}

void SelectParams(const std::string& network, bool fForceBlockNetwork)
{
    SelectBaseParams(network);
    if (fForceBlockNetwork) {
        bNetwork.SetNetwork(network);
    }
    globalChainParams = CreateChainParams(network);
}

void UpdateVersionBitsParameters(Consensus::DeploymentPos d, int64_t nStartTime, int64_t nTimeout)
{
    globalChainParams->UpdateVersionBitsParameters(d, nStartTime, nTimeout);
}

void UpdateLastPOWBlock(int nHeight)
{
    globalChainParams->UpdateLastPOWBlock(nHeight);
}

void UpdateFutureDriftHeight(int nHeight)
{
    globalChainParams->UpdateFutureDriftHeight(nHeight);
}

void UpdateMasterAddress(const std::string& address)
{
    globalChainParams->UpdateMasterAddress(address);
}

void UpdateSnapshotParams(const CChainParams::SnapshotParams& params)
{
    globalChainParams->SetSnapshot(params);
}

void UpdateOldPeerCutoffHeight(int nHeight)
{
    globalChainParams->SetOldPeerCutoffHeight(nHeight);
}

void TurnOffSegwit(){
	globalChainParams->TurnOffSegwit();
}

void TurnOffCSV() {
	globalChainParams->TurnOffCSV();
}

void TurnOffBIP34() {
	globalChainParams->TurnOffBIP34();
}

void TurnOffBIP65() {
	globalChainParams->TurnOffBIP65();
}

void TurnOffBIP66() {
	globalChainParams->TurnOffBIP66();
}
