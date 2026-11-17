// Copyright (c) 2026 The Paladeum Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "chainparams.h"
#include "coins.h"
#include "consensus/consensus.h"
#include "consensus/merkle.h"
#include "consensus/validation.h"
#include "governance/governance.h"
#include "key.h"
#include "keystore.h"
#include "policy/policy.h"
#include "pos.h"
#include "pow.h"
#include "primitives/transaction.h"
#include "script/interpreter.h"
#include "script/script.h"
#include "script/script_error.h"
#include "script/sign.h"
#include "script/standard.h"
#include "test/test_paladeum.h"
#include "timedata.h"
#include "validation.h"

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(offlinestake_tests, BasicTestingSetup)

static CScript OfflineScript(const CKey& staking, const CKey& spending)
{
    return GetScriptForDestination(std::make_pair(staking.GetPubKey().GetID(), spending.GetPubKey().GetID()));
}

static bool Verify(const CMutableTransaction& tx, unsigned int nIn, const CScript& scriptPubKey, CAmount amount)
{
    ScriptError error;
    const CTransaction txConst(tx);
    return VerifyScript(tx.vin[nIn].scriptSig, scriptPubKey, nullptr, STANDARD_SCRIPT_VERIFY_FLAGS,
                        TransactionSignatureChecker(&txConst, nIn, amount), &error);
}

// What the script of an offline staking output allows, on its own: the staking key signs in a
// coinstake and nowhere else, the spending key signs everywhere else.
BOOST_AUTO_TEST_CASE(offline_stake_script)
{
    CKey staking, ownerSpending, otherSpending;
    staking.MakeNewKey(true);
    ownerSpending.MakeNewKey(true);
    otherSpending.MakeNewKey(true);

    // the coins of an owner who delegated the staking, and a coin of somebody else with the
    // same staking key
    const CScript ownerScript = OfflineScript(staking, ownerSpending);
    const CScript otherScript = OfflineScript(staking, otherSpending);
    BOOST_CHECK(ownerScript.IsOfflineStaking());
    const CAmount ownerValue = 1000 * COIN, otherValue = 10 * COIN;

    CBasicKeyStore stakingOnly;
    stakingOnly.AddKey(staking);

    // an ordinary transaction: the staking key cannot spend the owner's coin
    {
        CMutableTransaction tx;
        tx.vin.resize(1);
        tx.vin[0].prevout = COutPoint(uint256S("01"), 0);
        tx.vout.push_back(CTxOut(ownerValue, otherScript));
        BOOST_CHECK(!CTransaction(tx).IsCoinStake());
        BOOST_CHECK(!SignSignature(stakingOnly, ownerScript, tx, 0, ownerValue, SIGHASH_ALL, false) || !Verify(tx, 0, ownerScript, ownerValue));
        CBasicKeyStore owner;
        owner.AddKey(ownerSpending);
        BOOST_CHECK(SignSignature(owner, ownerScript, tx, 0, ownerValue, SIGHASH_ALL, false));
        BOOST_CHECK(Verify(tx, 0, ownerScript, ownerValue));
    }

    // A coinstake whose kernel is the other coin and which takes the owner's coin as a second
    // input: the staking key signs both. The script of the owner's coin has no objection to the
    // outputs, which all pay the script of the kernel. Only the validation of the block can refuse
    // this (see offline_stake_other_inputs).
    {
        CMutableTransaction tx;
        tx.vin.resize(2);
        tx.vin[0].prevout = COutPoint(uint256S("02"), 0); // kernel: the other coin
        tx.vin[1].prevout = COutPoint(uint256S("01"), 0); // the owner's coin
        tx.vout.push_back(CTxOut(0, CScript()));          // marks a coinstake
        tx.vout.push_back(CTxOut(ownerValue + otherValue, otherScript));
        BOOST_CHECK(CTransaction(tx).IsCoinStake());
        BOOST_CHECK(SignSignature(stakingOnly, otherScript, tx, 0, otherValue, SIGHASH_ALL, true));
        BOOST_CHECK(SignSignature(stakingOnly, ownerScript, tx, 1, ownerValue, SIGHASH_ALL, true));
        BOOST_CHECK(Verify(tx, 0, otherScript, otherValue));
        BOOST_CHECK(Verify(tx, 1, ownerScript, ownerValue));
    }
}

/**
 * A regtest chain with coins for proof-of-stake blocks, mature for staking:
 *  - owner1 (1000 PLB) and owner2 (500 PLB): an owner who delegated the staking of his coins
 *    to the staking key (offline staking script of the staking key and the owner's key);
 *  - other (10 PLB): a coin of somebody else, who delegated to the same staking key;
 *  - stakerCoin (10 PLB): an ordinary coin of the staker (pay to the staking key).
 * The scripts that the proof-of-stake blocks name are authorized to stake (governance).
 */
struct OfflineStakeSetup : public TestChain100Setup {
    CKey staking, ownerSpending, otherSpending;
    CScript ownerScript, otherScript, stakerScript, stakerRewardScript;
    COutPoint owner1, owner2, other, stakerCoin;
    CBasicKeyStore stakerKeys; // the staker has the staking key only

    OfflineStakeSetup()
    {
        staking.MakeNewKey(true);
        ownerSpending.MakeNewKey(true);
        otherSpending.MakeNewKey(true);
        ownerScript = OfflineScript(staking, ownerSpending);
        otherScript = OfflineScript(staking, otherSpending);
        stakerScript = CScript() << ToByteVector(staking.GetPubKey()) << OP_CHECKSIG;
        stakerRewardScript = GetScriptForDestination(staking.GetPubKey().GetID());
        stakerKeys.AddKey(staking);

        // The coins, paid from the premine of block 1
        CMutableTransaction fund;
        fund.nVersion = 1;
        fund.nTime = chainActive.Tip()->GetMedianTimePast() + 1;
        fund.vin.push_back(CTxIn(COutPoint(coinbaseTxns[0].GetHash(), 0)));
        fund.vout.push_back(CTxOut(1000 * COIN, ownerScript));
        fund.vout.push_back(CTxOut(500 * COIN, ownerScript));
        fund.vout.push_back(CTxOut(10 * COIN, otherScript));
        fund.vout.push_back(CTxOut(10 * COIN, stakerScript));
        fund.vout.push_back(CTxOut(coinbaseTxns[0].vout[0].nValue - 1521 * COIN, coinbaseTxns[0].vout[0].scriptPubKey));
        CBasicKeyStore coinbaseKeys;
        coinbaseKeys.AddKey(coinbaseKey);
        BOOST_REQUIRE(SignSignature(coinbaseKeys, CTransaction(coinbaseTxns[0]), fund, 0, SIGHASH_ALL));
        const uint256 txid = fund.GetHash();
        owner1 = COutPoint(txid, 0);
        owner2 = COutPoint(txid, 1);
        other = COutPoint(txid, 2);
        stakerCoin = COutPoint(txid, 3);
        CreateAndProcessBlock({fund}, coinbaseTxns[0].vout[0].scriptPubKey);
        {
            LOCK(cs_main);
            BOOST_REQUIRE(pcoinsTip->HaveCoin(owner1));
        }

        // A coin can be staked COINSTAKE_MATURITY blocks after the block that has it
        for (int i = 0; i < COINSTAKE_MATURITY - 1; i++)
            CreateAndProcessBlock({}, coinbaseTxns[0].vout[0].scriptPubKey);

        // The scripts that the blocks below name in vout[1] of their coinstake (a pay to key is
        // authorized by its key hash)
        BOOST_REQUIRE(governance->AuthorizeScript(ownerScript));
        BOOST_REQUIRE(governance->AuthorizeScript(otherScript));
        BOOST_REQUIRE(governance->AuthorizeScript(stakerRewardScript));
    }

    /** The reward of a block on the tip: the subsidy (the coinstake pays no fee, the block has no
     *  other transaction). */
    CAmount Reward()
    {
        LOCK(cs_main);
        return GetBlockSubsidy(chainActive.Height() + 1, GetParams().GetConsensus());
    }

    /** A proof-of-stake block on the tip whose coinstake spends the inputs (the first one is the
     *  kernel) and pays the outputs. The staker signs the inputs with the staking key, and the
     *  block with the staking key, as the wallet does. */
    CBlock StakeBlock(const std::vector<COutPoint>& inputs, const std::vector<CTxOut>& outputs)
    {
        LOCK(cs_main);
        const Consensus::Params& consensus = GetParams().GetConsensus();
        const CBlockIndex* prev = chainActive.Tip();

        CBlock block;
        block.nVersion = ComputeBlockVersion(prev, consensus);
        block.hashPrevBlock = prev->GetIndexHash();
        // after the tip, in a slot of the stake timestamp mask
        block.nTime = (std::max<int64_t>(prev->GetBlockTime(), GetAdjustedTime()) + consensus.nStakeTimestampMask + 1) & ~(int64_t)consensus.nStakeTimestampMask;
        block.nBits = GetNextTargetRequired(prev, &block, true, consensus);

        CMutableTransaction coinbase;
        coinbase.nTime = block.nTime;
        coinbase.vin.resize(1);
        coinbase.vin[0].prevout.SetNull();
        coinbase.vin[0].scriptSig = CScript() << (prev->nHeight + 1) << OP_0;
        coinbase.vout.resize(1);
        coinbase.vout[0].SetEmpty();

        CMutableTransaction coinstake;
        coinstake.nTime = block.nTime;
        for (const COutPoint& input : inputs)
            coinstake.vin.push_back(CTxIn(input));
        coinstake.vout.push_back(CTxOut(0, CScript()));
        for (const CTxOut& output : outputs)
            coinstake.vout.push_back(output);
        for (unsigned int i = 0; i < inputs.size(); i++) {
            const Coin& coin = pcoinsTip->AccessCoin(inputs[i]);
            BOOST_REQUIRE(!coin.IsSpent());
            BOOST_REQUIRE(SignSignature(stakerKeys, coin.out.scriptPubKey, coinstake, i, coin.out.nValue, SIGHASH_ALL, true));
        }

        block.vtx.push_back(MakeTransactionRef(std::move(coinbase)));
        block.vtx.push_back(MakeTransactionRef(std::move(coinstake)));
        BOOST_REQUIRE(block.IsProofOfStake());
        block.hashMerkleRoot = BlockMerkleRoot(block);
        BOOST_REQUIRE(staking.Sign(block.GetIndexHash(), block.vchBlockSig));
        return block;
    }

    /** What the validation of the block says (TestBlockValidity: the checks of the block,
     *  ConnectBlock() included): empty if it is valid, else the reason. */
    std::string Validate(const CBlock& block)
    {
        LOCK(cs_main);
        CValidationState state;
        if (TestBlockValidity(state, GetParams(), block, chainActive.Tip(), false, true, true))
            return "";
        return state.GetRejectReason().empty() ? "invalid" : state.GetRejectReason();
    }
};

// Paladeum's rule for a coinstake whose kernel is an offline staking coin (ConnectBlock()): what
// it pays to the script of the kernel, less the value of the kernel, must be at least 90% of the
// block reward, and what it pays to other scripts at most 10% of the reward (the staker's share).
BOOST_FIXTURE_TEST_CASE(offline_stake_block_rules, OfflineStakeSetup)
{
    const CAmount reward = Reward();
    BOOST_CHECK_EQUAL(reward, 10 * COIN);
    const CAmount share = reward / 10; // the staker's share: 1 PLB

    // What the wallet builds: the kernel and the reward less the staker's share back to the
    // owner's script, the share to the staker.
    BOOST_CHECK_EQUAL(Validate(StakeBlock({owner1}, {CTxOut(1000 * COIN + reward - share, ownerScript), CTxOut(share, stakerRewardScript)})), "");
    // all of the reward to the owner
    BOOST_CHECK_EQUAL(Validate(StakeBlock({owner1}, {CTxOut(1000 * COIN + reward, ownerScript)})), "");
    // 90% of the reward to the owner and nothing to the staker (a coinstake may pay less than the reward)
    BOOST_CHECK_EQUAL(Validate(StakeBlock({owner1}, {CTxOut(1000 * COIN + reward - share, ownerScript)})), "");
    // more coins of the owner combined into the stake, and paid back to his script
    BOOST_CHECK_EQUAL(Validate(StakeBlock({owner1, owner2}, {CTxOut(1500 * COIN + reward - share, ownerScript), CTxOut(share, stakerRewardScript)})), "");
    // an ordinary coin as kernel: the reward goes where the staker wants
    BOOST_CHECK_EQUAL(Validate(StakeBlock({stakerCoin}, {CTxOut(10 * COIN + reward, stakerScript)})), "");

    // the owner gets less than 90% of the reward
    BOOST_CHECK_EQUAL(Validate(StakeBlock({owner1}, {CTxOut(1000 * COIN + reward - 2 * share, ownerScript), CTxOut(2 * share, stakerRewardScript)})), "bad-offline-stake");
    // the coin of the kernel goes to another script
    BOOST_CHECK_EQUAL(Validate(StakeBlock({owner1}, {CTxOut(1000 * COIN + reward, otherScript)})), "bad-offline-stake");
    BOOST_CHECK_EQUAL(Validate(StakeBlock({owner1}, {CTxOut(reward, ownerScript), CTxOut(1000 * COIN, stakerRewardScript)})), "bad-offline-stake");
    // a second coin of the owner taken by the staker: more than the staker's share
    BOOST_CHECK_EQUAL(Validate(StakeBlock({owner1, owner2}, {CTxOut(1000 * COIN + reward - share, ownerScript), CTxOut(500 * COIN + share, stakerRewardScript)})), "bad-offline-stake-greed");
    // more than the reward
    BOOST_CHECK_EQUAL(Validate(StakeBlock({owner1}, {CTxOut(1000 * COIN + reward + 1, ownerScript)})), "bad-cs-amount");

    // The block the wallet builds is accepted into the chain.
    const CBlock block = StakeBlock({owner1}, {CTxOut(1000 * COIN + reward - share, ownerScript), CTxOut(share, stakerRewardScript)});
    BOOST_CHECK(ProcessNewBlock(GetParams(), std::make_shared<const CBlock>(block), true, nullptr, block.GetIndexHash()));
    {
        LOCK(cs_main);
        BOOST_CHECK(chainActive.Tip()->GetIndexHash() == block.GetIndexHash());
        BOOST_CHECK(chainActive.Tip()->IsProofOfStake());
        BOOST_CHECK(pcoinsTip->AccessCoin(COutPoint(block.vtx[1]->GetHash(), 1)).out.scriptPubKey == ownerScript);
    }
}

// Offline staking coins that are not the kernel of the coinstake. The staking key can sign them in
// any coinstake (offline_stake_script), and the rule above looks at the kernel only. A coinstake
// whose kernel is another coin of the staker (offline staking or not) takes the owner's coin and
// pays it to the staker: that must not be a valid block. This test fails while ConnectBlock()
// checks the kernel only. (AokChain refuses these blocks with CheckOfflineStakeScripts(), a rule
// for every input and output of a coinstake with offline staking coins.)
BOOST_FIXTURE_TEST_CASE(offline_stake_other_inputs, OfflineStakeSetup)
{
    const CAmount reward = Reward();

    // kernel: a coin of another offline staking script with the same staking key
    BOOST_CHECK_MESSAGE(Validate(StakeBlock({other, owner1}, {CTxOut(1010 * COIN + reward, otherScript)})) != "",
                        "a coinstake with another offline staking kernel moved the owner's coin to the other script");
    // kernel: an ordinary coin of the staker
    BOOST_CHECK_MESSAGE(Validate(StakeBlock({stakerCoin, owner1}, {CTxOut(1010 * COIN + reward, stakerScript)})) != "",
                        "a coinstake with an ordinary kernel moved the owner's offline staking coin to the staker");
}

BOOST_AUTO_TEST_SUITE_END()
