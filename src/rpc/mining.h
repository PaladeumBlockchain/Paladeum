// Copyright (c) 2017 The Bitcoin Core developers
// Copyright (c) 2021-2022 The Akila developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef AKILA_RPC_MINING_H
#define AKILA_RPC_MINING_H

#include "script/script.h"

#include <univalue.h>

static const bool DEFAULT_GENERATE = false;
static const int DEFAULT_GENERATE_THREADS = 1;

/** Generate blocks (mine) */
UniValue generateBlocks(std::shared_ptr<CReserveScript> coinbaseScript, int nGenerate, uint64_t nMaxTries, bool keepScript);

/** Check bounds on a command line confirm target */
unsigned int ParseConfirmTarget(const UniValue& value);

#endif
