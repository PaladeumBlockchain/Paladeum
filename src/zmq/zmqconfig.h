// Copyright (c) 2014 The Bitcoin Core developers
// Copyright (c) 2021-2022 The Paladeum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef PLB_ZMQ_ZMQCONFIG_H
#define PLB_ZMQ_ZMQCONFIG_H

#if defined(HAVE_CONFIG_H)
#include "config/paladeum-config.h"
#endif

#include <stdarg.h>
#include <string>

#if ENABLE_ZMQ
#include <zmq.h>
#endif

#include "primitives/block.h"
#include "primitives/transaction.h"

void zmqError(const char *str);

#endif // PLB_ZMQ_ZMQCONFIG_H
