// Copyright (c) 2017-2019 The Raven Core developers
// Copyright (c) 2020 The Paladeum Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <tokens/tokentypes.h>

int IntFromKnownTokenType(KnownTokenType type) {
    return (int)type;
}

KnownTokenType KnownTokenTypeFromInt(int nType) {
    return (KnownTokenType)nType;
}