// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2014-2016 The BlackCoin developers
// Copyright (c) 2021-2022 The Yona developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "pow.h"

#include "arith_uint256.h"
#include "chain.h"
#include "primitives/block.h"
#include "uint256.h"
#include "util.h"
#include "validation.h"
#include "chainparams.h"
#include "tinyformat.h"

namespace {
    // returns a * exp(p/q) where |p/q| is small
    arith_uint256 mul_exp(arith_uint256 a, int64_t p, int64_t q)
    {
        bool isNegative = p < 0;
        uint64_t abs_p = p >= 0 ? p : -p;
        arith_uint256 result = a;
        uint64_t n = 0;
        while (a > 0) {
            ++n;
            a = a * abs_p / q / n;
            if (isNegative && (n % 2 == 1)) {
                result -= a;
            } else {
                result += a;
            }
        }
        return result;
    }
}

unsigned int CalculateNextWorkRequired(const CBlockIndex* pindexLast, int64_t nFirstBlockTime, const Consensus::Params& params, bool fProofOfStake)
{
    // Limit adjustment step
    int64_t nTargetSpacing = params.nTargetSpacing;
    int64_t nActualSpacing = pindexLast->GetBlockTime() - nFirstBlockTime;

    // Retarget
    const arith_uint256 nTargetLimit = UintToArith256(fProofOfStake ? params.posLimit : params.powLimit);

    // ppcoin: target change every block
    // ppcoin: retarget with exponential moving toward target spacing
    arith_uint256 bnNew;
    bnNew.SetCompact(pindexLast->nBits);
    int64_t nInterval = params.DifficultyAdjustmentInterval();

    if (nActualSpacing < 0)
        nActualSpacing = nTargetSpacing;
    if (nActualSpacing > nTargetSpacing * 20)
        nActualSpacing = nTargetSpacing * 20;

    bnNew = mul_exp(bnNew, 2 * (nActualSpacing - nTargetSpacing) / 16, (nInterval + 1) * nTargetSpacing / 16);

    if (bnNew <= 0 || bnNew > nTargetLimit)
        bnNew = nTargetLimit;

    return bnNew.GetCompact();
}

// ppcoin: find last block index up to pindex
const CBlockIndex* GetLastBlockIndexDaa(const CBlockIndex* pindex, bool fProofOfStake)
{
    //CBlockIndex will be updated with information about the proof type later
    while (pindex && pindex->pprev && (pindex->IsProofOfStake() != fProofOfStake))
        pindex = pindex->pprev;
    return pindex;
}

unsigned int GetNextTargetRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock, bool fProofOfStake, const Consensus::Params& params)
{
    assert(pindexLast != nullptr);

    // Regtest rule
    if (params.fDiffNoRetargeting && params.fDiffAllowMinDifficultyBlocks) {
        return pindexLast->nBits;
    }

    unsigned int nTargetLimit = UintToArith256(fProofOfStake ? params.posLimit : params.powLimit).GetCompact();

    // first block
    const CBlockIndex* pindexPrev = GetLastBlockIndexDaa(pindexLast, fProofOfStake);
    if (pindexPrev->pprev == NULL) {
        return nTargetLimit;
    }

    // second block
    const CBlockIndex* pindexPrevPrev = GetLastBlockIndexDaa(pindexPrev->pprev, fProofOfStake);
    if (pindexPrevPrev->pprev == NULL) {
        return nTargetLimit;
    }

    return CalculateNextWorkRequired(pindexPrev, pindexPrevPrev->GetBlockTime(), params, fProofOfStake);
}

bool CheckProofOfWork(uint256 hash, unsigned int nBits, const Consensus::Params& params)
{
    bool fNegative;
    bool fOverflow;
    arith_uint256 bnTarget;

    bnTarget.SetCompact(nBits, &fNegative, &fOverflow);

    // Check range
    if (fNegative || bnTarget == 0 || fOverflow || bnTarget > UintToArith256(params.powLimit))
        return false;

    // Check proof of work matches claimed amount
    if (UintToArith256(hash) > bnTarget)
        return false;

    return true;
}
