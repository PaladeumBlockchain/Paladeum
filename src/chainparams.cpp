// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2021-2022 The Yona developers
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
        hash = genesis.GetBlockHash();
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
    printf("block.GetBlockHash = %s\n", genesis.GetBlockHash().ToString().c_str());
    printf("block.MerkleRoot = %s \n", genesis.hashMerkleRoot.ToString().c_str());
}

static CBlock CreateGenesisBlock(const char* pszTimestamp, const CScript& genesisOutputScript, uint32_t nTime, uint32_t nNonce, uint32_t nBits, int32_t nVersion, const CAmount& genesisReward)
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
static CBlock CreateGenesisBlock(const char* pszTimestamp, uint32_t nTime, uint32_t nNonce, uint32_t nBits, int32_t nVersion, const CAmount& genesisReward)
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
        consensus.posLimit = uint256S("003fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nTargetTimespan = 1000;
        consensus.nTargetSpacing = 20;
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
        consensus.defaultAssumeValid = uint256S("0x0020f74ccfaddbcbbc71041ed0ce985e9b89701847c6b4a824f0a44cdd95e0f5"); // Block 1186833

        // Proof-of-Stake
        consensus.nLastPOWBlock = 1440;
        consensus.nStakeTimestampMask = 0xf; // 15

        /**
         * The message start string is designed to be unlikely to occur in normal data.
         * The characters are rarely used upper ASCII, not valid as UTF-8, and produce
         * a large 32-bit integer with any alignment.
         */
        pchMessageStart[0] = 0x79;
        pchMessageStart[1] = 0x6e;
        pchMessageStart[2] = 0x61;
        pchMessageStart[3] = 0x56;
        nDefaultPort = 7768;
        nPruneAfterHeight = 100000;

        const char* pszTimestamp = "New Species of Mouse Opossum Discovered in Panama | Dec 10, 2021 Sci-News";

        genesis = CreateGenesisBlock(pszTimestamp, 1640816880, 703, 0x1f3fffff, 1, 1 * COIN);
        consensus.hashGenesisBlock = genesis.GetBlockHash();

        assert(consensus.hashGenesisBlock == uint256S("0x003d14f2fa1aba21f7b40711601a66736f283c041633a5b7ee10420d18702719"));
        assert(genesis.hashMerkleRoot == uint256S("0xa8361e55f50f1764c1d25f82ae29caed27f0fff3ca83f1af36532787e4abd9b2"));

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,78);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,81);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,46);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x88, 0xB2, 0x1E};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x88, 0xAD, 0xE4};

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

        /** YONA Start **/
        // Burn Amounts
        nIssueTokenBurnAmount = 1 * COIN;
        nReissueTokenBurnAmount = 1 * COIN;
        nIssueSubTokenBurnAmount = 1 * COIN;
        nIssueUniqueTokenBurnAmount = 1 * COIN;
        nIssueUsernameTokenBurnAmount = 1 * COIN;
        nIssueMsgChannelTokenBurnAmount = 1 * COIN;
        nIssueQualifierTokenBurnAmount = 1 * COIN;
        nIssueSubQualifierTokenBurnAmount = 1 * COIN;
        nIssueRestrictedTokenBurnAmount = 1 * COIN;
        nAddNullQualifierTagBurnAmount = 1 * COIN;

        // Burn Addresses
        strIssueTokenBurnAddress = "YmvWrhCZBVZ69NbrMZB4qycs4h3Zho7zrW";
        strReissueTokenBurnAddress = "YmvWrhCZBVZ69NbrMZB4qycs4h3Zho7zrW";
        strIssueSubTokenBurnAddress = "YmvWrhCZBVZ69NbrMZB4qycs4h3Zho7zrW";
        strIssueUniqueTokenBurnAddress = "YmvWrhCZBVZ69NbrMZB4qycs4h3Zho7zrW";
        strIssueUsernameTokenBurnAddress = "YmvWrhCZBVZ69NbrMZB4qycs4h3Zho7zrW";
        strIssueMsgChannelTokenBurnAddress = "YmvWrhCZBVZ69NbrMZB4qycs4h3Zho7zrW";
        strIssueQualifierTokenBurnAddress = "YmvWrhCZBVZ69NbrMZB4qycs4h3Zho7zrW";
        strIssueSubQualifierTokenBurnAddress = "YmvWrhCZBVZ69NbrMZB4qycs4h3Zho7zrW";
        strIssueRestrictedTokenBurnAddress = "YmvWrhCZBVZ69NbrMZB4qycs4h3Zho7zrW";
        strAddNullQualifierTagBurnAddress = "YmvWrhCZBVZ69NbrMZB4qycs4h3Zho7zrW";

        // Global Burn Address
        strGlobalBurnAddress = "YmvWrhCZBVZ69NbrMZB4qycs4h3Zho7zrW";

        nMaxReorganizationDepth = 180; // 180 at 20 seconds block timespan is +/- 60 minutes.
        nMinReorganizationPeers = 4;
        nMinReorganizationAge = 60 * 60 * 12; // 12 hours

        // BIP44 cointype
        nExtCoinType = 1;
        /** YONA End **/
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
        consensus.nSegwitEnabled = true;
        consensus.nCSVEnabled = true;

        consensus.powLimit = uint256S("003fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.posLimit = uint256S("0000000000ffffffffffffffffffffffffffffffffffffffffffffffffffffff");

        consensus.nTargetTimespan = 16 * 60;
        consensus.nTargetSpacing = 64;
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
        consensus.nMinimumChainWork = uint256S("0x000000000000000000000000000000000000000000000000000168050db560b4");

        // By default assume that the signatures in ancestors of this block are valid.
        consensus.defaultAssumeValid = uint256S("0x000000006272208605c4df3b54d4d5515759105e7ffcb258e8cd8077924ffef1");

        // Proof-of-Stake
        consensus.nLastPOWBlock = 1440;
        consensus.nStakeTimestampMask = 0xf; // 15

        pchMessageStart[0] = 0x79;
        pchMessageStart[1] = 0x6e;
        pchMessageStart[2] = 0x61;
        pchMessageStart[3] = 0x54;
        nDefaultPort = 5566;
        nPruneAfterHeight = 1000;

        const char* pszTimestamp = "New Species of Mouse Opossum Discovered in Panama | Dec 10, 2021 Sci-News";

        genesis = CreateGenesisBlock(pszTimestamp, 1639241695, 2153, 0x1f3fffff, 1, 1 * COIN);
        consensus.hashGenesisBlock = genesis.GetBlockHash();
        // assert(consensus.hashGenesisBlock == uint256S("0x000c8bc3b01f2fce94b55bab848eeaed66ef3d8481eaf0cf23763bca2cc43f31"));
        // assert(genesis.hashMerkleRoot == uint256S("0x756f73dccc4a501e0b51c619ebfa9b0abf9f81a1a48a9809047394bbf70dd47e"));

        vFixedSeeds.clear();
        vSeeds.clear();

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,140);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,143);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,108);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};

        // Yona BIP44 cointype in testnet
        nExtCoinType = 1;

        vFixedSeeds = std::vector<SeedSpec6>(pnSeed6_test, pnSeed6_test + ARRAYLEN(pnSeed6_test));

        fDefaultConsistencyChecks = false;
        fRequireStandard = false;
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

        /** YONA Start **/
        // Burn Amounts
        nIssueTokenBurnAmount = 500 * COIN;
        nReissueTokenBurnAmount = 100 * COIN;
        nIssueSubTokenBurnAmount = 100 * COIN;
        nIssueUniqueTokenBurnAmount = 5 * COIN;
        nIssueUsernameTokenBurnAmount = 5 * COIN;
        nIssueMsgChannelTokenBurnAmount = 100 * COIN;
        nIssueQualifierTokenBurnAmount = 1000 * COIN;
        nIssueSubQualifierTokenBurnAmount = 100 * COIN;
        nIssueRestrictedTokenBurnAmount = 1500 * COIN;
        nAddNullQualifierTagBurnAmount = .1 * COIN;

        // Burn Addresses
        strIssueTokenBurnAddress = "YmvWrhCZBVZ69NbrMZB4qycs4h3Zho7zrW";
        strReissueTokenBurnAddress = "YmvWrhCZBVZ69NbrMZB4qycs4h3Zho7zrW";
        strIssueSubTokenBurnAddress = "YmvWrhCZBVZ69NbrMZB4qycs4h3Zho7zrW";
        strIssueUniqueTokenBurnAddress = "YmvWrhCZBVZ69NbrMZB4qycs4h3Zho7zrW";
        strIssueUsernameTokenBurnAddress = "YmvWrhCZBVZ69NbrMZB4qycs4h3Zho7zrW";
        strIssueMsgChannelTokenBurnAddress = "YmvWrhCZBVZ69NbrMZB4qycs4h3Zho7zrW";
        strIssueQualifierTokenBurnAddress = "YmvWrhCZBVZ69NbrMZB4qycs4h3Zho7zrW";
        strIssueSubQualifierTokenBurnAddress = "YmvWrhCZBVZ69NbrMZB4qycs4h3Zho7zrW";
        strIssueRestrictedTokenBurnAddress = "YmvWrhCZBVZ69NbrMZB4qycs4h3Zho7zrW";
        strAddNullQualifierTagBurnAddress = "YmvWrhCZBVZ69NbrMZB4qycs4h3Zho7zrW";

        // Global Burn Address
        strGlobalBurnAddress = "YmvWrhCZBVZ69NbrMZB4qycs4h3Zho7zrW";

        nMaxReorganizationDepth = 180; // 180 at 20 seconds block timespan is +/- 60 minutes.
        nMinReorganizationPeers = 4;
        nMinReorganizationAge = 60 * 60 * 12; // 12 hours
        /** YONA End **/
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
        consensus.nTargetTimespan = 16 * 60;
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
        consensus.defaultAssumeValid = uint256S("0x00");

        // Proof-of-Stake
        consensus.nLastPOWBlock = 1440;
        consensus.nStakeTimestampMask = 0xf; // 15

        pchMessageStart[0] = 0x79;
        pchMessageStart[1] = 0x6e;
        pchMessageStart[2] = 0x61;
        pchMessageStart[3] = 0x54;
        nDefaultPort = 5566;
        nPruneAfterHeight = 1000;

        const char* pszTimestamp = "New Species of Mouse Opossum Discovered in Panama | Dec 10, 2021 Sci-News";

        genesis = CreateGenesisBlock(pszTimestamp, 1524179366, 1, 0x207fffff, 4, 5000 * COIN);
        consensus.hashGenesisBlock = genesis.GetBlockHash();

        // assert(consensus.hashGenesisBlock == uint256S("0x0b2c703dc93bb63a36c4e33b85be4855ddbca2ac951a7a0a29b8de0408200a3c "));
        // assert(genesis.hashMerkleRoot == uint256S("0x28ff00a867739a352523808d301f504bc4547699398d70faf2266a8bae5f3516"));

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

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,111);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,196);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,239);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};

        // Yona BIP44 cointype in regtest
        nExtCoinType = 1;

        /** YONA Start **/
        // Burn Amounts
        nIssueTokenBurnAmount = 500 * COIN;
        nReissueTokenBurnAmount = 100 * COIN;
        nIssueSubTokenBurnAmount = 100 * COIN;
        nIssueUniqueTokenBurnAmount = 5 * COIN;
        nIssueUsernameTokenBurnAmount = 5 * COIN;
        nIssueMsgChannelTokenBurnAmount = 100 * COIN;
        nIssueQualifierTokenBurnAmount = 1000 * COIN;
        nIssueSubQualifierTokenBurnAmount = 100 * COIN;
        nIssueRestrictedTokenBurnAmount = 1500 * COIN;
        nAddNullQualifierTagBurnAmount = .1 * COIN;

        // Burn Addresses
        strIssueTokenBurnAddress = "YmvWrhCZBVZ69NbrMZB4qycs4h3Zho7zrW";
        strReissueTokenBurnAddress = "YmvWrhCZBVZ69NbrMZB4qycs4h3Zho7zrW";
        strIssueSubTokenBurnAddress = "YmvWrhCZBVZ69NbrMZB4qycs4h3Zho7zrW";
        strIssueUniqueTokenBurnAddress = "YmvWrhCZBVZ69NbrMZB4qycs4h3Zho7zrW";
        strIssueUsernameTokenBurnAddress = "YmvWrhCZBVZ69NbrMZB4qycs4h3Zho7zrW";
        strIssueMsgChannelTokenBurnAddress = "YmvWrhCZBVZ69NbrMZB4qycs4h3Zho7zrW";
        strIssueQualifierTokenBurnAddress = "YmvWrhCZBVZ69NbrMZB4qycs4h3Zho7zrW";
        strIssueSubQualifierTokenBurnAddress = "YmvWrhCZBVZ69NbrMZB4qycs4h3Zho7zrW";
        strIssueRestrictedTokenBurnAddress = "YmvWrhCZBVZ69NbrMZB4qycs4h3Zho7zrW";
        strAddNullQualifierTagBurnAddress = "YmvWrhCZBVZ69NbrMZB4qycs4h3Zho7zrW";

        // Global Burn Address
        strGlobalBurnAddress = "YmvWrhCZBVZ69NbrMZB4qycs4h3Zho7zrW";

        nMaxReorganizationDepth = 60; // 60 at 1 minute block timespan is +/- 60 minutes.
        nMinReorganizationPeers = 4;
        nMinReorganizationAge = 60 * 60 * 12; // 12 hours

        // TODO, we need to figure out what to do with this for regtest. This effects the unit tests
        // For now we can use a timestamp very far away
        // If you are looking to test the kawpow hashing function in regtest. You will need to change this number
        /** YONA End **/
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
