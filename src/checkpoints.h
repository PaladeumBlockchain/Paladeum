// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2021-2022 The Paladeum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef PLB_CHECKPOINTS_H
#define PLB_CHECKPOINTS_H

#include "uint256.h"

#include <map>

class CBlockIndex;
struct CCheckpointData;

/**
 * Block-chain checkpoints are compiled-in sanity checks.
 * They are updated every release or three.
 */
namespace Checkpoints
{

//! Checks that the block hash at height nHeight matches the expected hardened checkpoint
bool CheckHardened(int nHeight, const uint256& hash, const CCheckpointData& data);

//! Returns last CBlockIndex* in mapBlockIndex that is a checkpoint
CBlockIndex* GetLastCheckpoint(const CCheckpointData& data);

} //namespace Checkpoints

#endif // PLB_CHECKPOINTS_H
