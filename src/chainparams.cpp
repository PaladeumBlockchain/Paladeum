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
	return consensus.nBIP34Enabled;
}

bool CChainParams::BIP66() {
	return consensus.nBIP34Enabled;
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
        consensus.nMinimumChainWork = uint256S("0x");

        // By default assume that the signatures in ancestors of this block are valid.
        consensus.defaultAssumeValid = uint256S("0xe8b61dd5d266cf5e610520daf9dfa57eb9eafd389a17a0adc81b6d1974eb7540"); // Block 1186833

        // Proof-of-Stake
        consensus.nLastPOWBlock = std::numeric_limits<int>::max();
        consensus.nTxMessages = std::numeric_limits<int>::max();
        consensus.nStakeTimestampMask = 0xf; // 15

        // Fork to enable offline staking and remove the block limiter
        consensus.offlineStakingFork = std::numeric_limits<int>::max();

        /**
         * The message start string is designed to be unlikely to occur in normal data.
         * The characters are rarely used upper ASCII, not valid as UTF-8, and produce
         * a large 32-bit integer with any alignment.
         */
        pchMessageStart[0] = 0x79;
        pchMessageStart[1] = 0x6e;
        pchMessageStart[2] = 0x61;
        pchMessageStart[3] = 0x56;
        nDefaultPort = 6465;
        nPruneAfterHeight = 100000;

        const char* pszTimestamp = "TEST MESSAGE (REPLACE ME)";

        genesis = CreateGenesisBlock(pszTimestamp, 1660202949, 979, 0x1f3fffff, 1, 1 * COIN);
        consensus.hashGenesisBlock = genesis.GetIndexHash();

        assert(consensus.hashGenesisBlock == uint256S("0xe8b61dd5d266cf5e610520daf9dfa57eb9eafd389a17a0adc81b6d1974eb7540"));
        assert(genesis.hashMerkleRoot == uint256S("0xa11738b04ac97e8f71f39d4cf24716ad549f31cf1097e5ade1fd28869757c137"));

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,23);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,26);
        base58Prefixes[SECRET_KEY]     = std::vector<unsigned char>(1,71);
        base58Prefixes[OFFLINE_STAKING_ADDRESS] = std::vector<unsigned char>(1,78);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};

        vFixedSeeds = std::vector<SeedSpec6>(pnSeed6_main, pnSeed6_main + ARRAYLEN(pnSeed6_main));

        fDefaultConsistencyChecks = false;
        fRequireStandard = true;
        fMineBlocksOnDemand = false;
        fMiningRequiresPeers = true;

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
        strTokenFeeAddress = "";
        strMasterAddress = "";

        nMaxReorganizationDepth = 500;

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
        strMasterAddress = "";

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

        // Global fee address
        strTokenFeeAddress = "mmbbmGLSeCpR9VhGp2JMXVkf7xkbjtcEET";
        strMasterAddress = "";

        nMaxReorganizationDepth = 500;

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
