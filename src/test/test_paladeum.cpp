// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2021-2022 The Paladeum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "test_paladeum.h"
#include "base58.h"
#include "chainparams.h"
#include "consensus/consensus.h"
#include "consensus/merkle.h"
#include "consensus/validation.h"
#include "crypto/sha256.h"
#include "fs.h"
#include "hash.h"
#include "key.h"
#include "validation.h"
#include "governance/governance.h"
#include "miner.h"
#include "net_processing.h"
#include "pubkey.h"
#include "random.h"
#include "txdb.h"
#include "txmempool.h"
#include "ui_interface.h"
#include "streams.h"
#include "rpc/server.h"
#include "rpc/register.h"
#include "script/sigcache.h"
#include "script/standard.h"

#include <memory>

uint256 insecure_rand_seed = GetRandHash();
FastRandomContext insecure_rand_ctx(insecure_rand_seed);


extern void noui_connect();

BasicTestingSetup::BasicTestingSetup(const std::string &chainName)
{
    SHA256AutoDetect();
    RandomInit();
    ECC_Start();
    SetupEnvironment();
    SetupNetworking();
    InitSignatureCache();
    InitScriptExecutionCache();
    //  If additional debug information is desired, uncomment 'cout' section in VerifyScript method in the script/interpreter.cpp file (warning - this will cause the logger window to spew messages).
    fPrintToConsole = false;
    fPrintToDebugLog = false; // don't want to write to debug.log file
    fCheckBlockIndex = true;
    SelectParams(chainName);
    noui_connect();
}

BasicTestingSetup::~BasicTestingSetup()
{
    ECC_Stop();
}

TestingSetup::TestingSetup(const std::string &chainName) : BasicTestingSetup(chainName)
{
    const CChainParams &chainparams = GetParams();
    // Ideally we'd move all the RPC tests to the functional testing framework
    // instead of unit tests, but for now we need these here.
    RegisterAllCoreRPCCommands(tableRPC);
    ClearDatadirCache();
    pathTemp = fs::temp_directory_path() / strprintf("test_paladeum_%lu_%i", (unsigned long) GetTime(), (int) (InsecureRandRange(100000)));
    fs::create_directories(pathTemp);
    gArgs.ForceSetArg("-datadir", pathTemp.string());

    // Note that because we don't bother running a scheduler thread here,
    // callbacks via CValidationInterface are unreliable, but that's OK,
    // our unit tests aren't testing multiple parts of the code at once.
    GetMainSignals().RegisterBackgroundSignalScheduler(scheduler);

    mempool.setSanityCheck(1.0);
    pblocktree = new CBlockTreeDB(1 << 20, true);
    pcoinsdbview = new CCoinsViewDB(1 << 23, true);
    pcoinsTip = new CCoinsViewCache(pcoinsdbview);
    // (the node always has the governance state; without it every check of a transaction's inputs
    // crashed here once governance is active, which is height 10 on regtest)
    governance = new CGovernance(1 << 20, true, false);
    governance->Init(false, chainparams);
    if (!LoadGenesisBlock(chainparams))
    {
        throw std::runtime_error("LoadGenesisBlock failed.");
    }

    ptokens = new CTokensCache();
    {
        CValidationState state;
        if (!ActivateBestChain(state, chainparams))
        {
            throw std::runtime_error("ActivateBestChain failed.");
        }
    }
    nScriptCheckThreads = 3;
    for (int i = 0; i < nScriptCheckThreads - 1; i++)
        threadGroup.create_thread(&ThreadScriptCheck);
    g_connman = std::unique_ptr<CConnman>(new CConnman(0x1337, 0x1337)); // Deterministic randomness for tests.
    connman = g_connman.get();
    peerLogic.reset(new PeerLogicValidation(connman, scheduler));
}

TestingSetup::~TestingSetup()
{
    threadGroup.interrupt_all();
    threadGroup.join_all();
    GetMainSignals().FlushBackgroundCallbacks();
    GetMainSignals().UnregisterBackgroundSignalScheduler();
    g_connman.reset();
    peerLogic.reset();
    UnloadBlockIndex();
    delete pcoinsTip;
    pcoinsTip = nullptr;
    delete pcoinsdbview;
    delete pblocktree;
    pblocktree = nullptr;
    delete ptokens;
    delete governance;
    governance = nullptr;
    fs::remove_all(pathTemp);
}

TestChain100Setup::TestChain100Setup() : TestingSetup(CBaseChainParams::REGTEST)
{
    // Generate a 100-block chain:
    coinbaseKey.MakeNewKey(true);
    CScript scriptPubKey = CScript() << ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG;
    for (int i = 0; i < COINBASE_MATURITY; i++)
    {
        std::vector<CMutableTransaction> noTxns;
        CBlock b = CreateAndProcessBlock(noTxns, scriptPubKey);
        coinbaseTxns.push_back(*b.vtx[0]);
    }
}

//
// Create a new block with just given transactions, coinbase paying to
// scriptPubKey, and try to add it to the current chain.
//
CBlock
TestChain100Setup::CreateAndProcessBlock(const std::vector<CMutableTransaction> &txns, const CScript &scriptPubKey)
{
    const CChainParams &chainparams = GetParams();
    std::unique_ptr<CBlockTemplate> pblocktemplate = BlockAssembler(chainparams).CreateNewBlock(scriptPubKey);
    CBlock &block = pblocktemplate->block;

    // Replace mempool-selected txns with just coinbase plus passed-in txns:
    block.vtx.resize(1);
    for (const CMutableTransaction &tx : txns)
        block.vtx.push_back(MakeTransactionRef(tx));
    // The witness commitment of the coinbase (regtest has segwit from the first block) was made
    // for the transactions of the template: make it again for these.
    {
        CMutableTransaction coinbase(*block.vtx[0]);
        for (auto it = coinbase.vout.begin(); it != coinbase.vout.end();) {
            const CScript& script = it->scriptPubKey;
            if (script.size() >= 38 && script[0] == OP_RETURN && script[1] == 0x24 && script[2] == 0xaa &&
                script[3] == 0x21 && script[4] == 0xa9 && script[5] == 0xed)
                it = coinbase.vout.erase(it);
            else
                ++it;
        }
        block.vtx[0] = MakeTransactionRef(std::move(coinbase));
        GenerateCoinbaseCommitment(block, chainActive.Tip(), chainparams.GetConsensus());
    }
    // IncrementExtraNonce creates a valid coinbase and merkleRoot
    unsigned int extraNonce = 0;
    IncrementExtraNonce(&block, chainActive.Tip(), extraNonce);

    while (!CheckProofOfWork(block.GetWorkHash(), block.nBits, chainparams.GetConsensus())) ++block.nNonce;

    std::shared_ptr<const CBlock> shared_pblock = std::make_shared<const CBlock>(block);
    ProcessNewBlock(chainparams, shared_pblock, true, nullptr, shared_pblock->GetIndexHash());

    CBlock result = block;
    return result;
}

TestChain100Setup::~TestChain100Setup()
{
}


CTxMemPoolEntry TestMemPoolEntryHelper::FromTx(const CMutableTransaction &tx)
{
    CTransaction txn(tx);
    return FromTx(txn);
}

CTxMemPoolEntry TestMemPoolEntryHelper::FromTx(const CTransaction &txn)
{
    return CTxMemPoolEntry(MakeTransactionRef(txn), nFee, nTime, nHeight,
                           spendsCoinbase, sigOpCost, lp);
}

CKey TestKey(unsigned char n)
{
    unsigned char secret[32];
    memset(secret, n, sizeof(secret));
    CKey key;
    key.Set(secret, secret + sizeof(secret), true);
    assert(key.IsValid());
    return key;
}

CMutableTransaction TestTx(const COutPoint& prevout, const CScript& scriptSig, const CScript& scriptPubKey)
{
    CMutableTransaction tx;
    tx.nVersion = 1;
    tx.nTime = 1600000000;
    tx.vin.resize(1);
    tx.vin[0].prevout = prevout;
    tx.vin[0].scriptSig = scriptSig;
    tx.vout.resize(1);
    tx.vout[0].nValue = 1 * COIN;
    tx.vout[0].scriptPubKey = scriptPubKey;
    return tx;
}

CBlock TestBlock(const std::vector<CMutableTransaction>& txs)
{
    CBlock block;
    block.nVersion = 1;
    block.nTime = 1600000016;
    block.nBits = 0x207fffff;
    CMutableTransaction coinbase;
    coinbase.nVersion = 1;
    coinbase.nTime = 1600000016;
    coinbase.vin.resize(1);
    coinbase.vin[0].prevout.SetNull();
    coinbase.vin[0].scriptSig = CScript() << 1 << OP_0;
    coinbase.vout.resize(1);
    coinbase.vout[0].nValue = 4 * COIN;
    coinbase.vout[0].scriptPubKey = CScript() << ToByteVector(TestKey(1).GetPubKey()) << OP_CHECKSIG;
    block.vtx.push_back(MakeTransactionRef(coinbase));
    for (const CMutableTransaction& tx : txs)
        block.vtx.push_back(MakeTransactionRef(tx));
    block.hashMerkleRoot = BlockMerkleRoot(block);
    return block;
}

CBlock TestBlock9()
{
    // Each transaction spends an output that is not in the block and pays to
    // the address of its own key.
    std::vector<CMutableTransaction> txs;
    for (unsigned char n = 2; n < 10; n++) {
        const CPubKey pubkey = TestKey(n).GetPubKey();
        uint256 prev;
        memset(prev.begin(), n, 32);
        txs.push_back(TestTx(COutPoint(prev, 0),
                             CScript() << std::vector<unsigned char>(71, n) << ToByteVector(pubkey),
                             GetScriptForDestination(pubkey.GetID())));
    }
    return TestBlock(txs);
}

std::string ExtKeyWithNetworkPrefix(const std::string& strBase58, CChainParams::Base58Type type)
{
    std::vector<unsigned char> data;
    if (!DecodeBase58(strBase58, data) || data.size() != 4 + BIP32_EXTKEY_SIZE + 4)
        return "";
    const uint256 checksum = Hash(data.begin(), data.end() - 4);
    if (memcmp(checksum.begin(), &data[data.size() - 4], 4) != 0)
        return "";
    std::vector<unsigned char> out = GetParams().Base58Prefix(type);
    out.insert(out.end(), data.begin() + 4, data.end() - 4);
    return EncodeBase58Check(out);
}
