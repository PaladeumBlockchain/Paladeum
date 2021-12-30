// Copyright (c) 2017-2019 The Raven Core developers
// Copyright (c) 2021-2022 The Yona developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "tokentypes.h"
#include "hash.h"

int IntFromKnownTokenType(KnownTokenType type) {
    return (int)type;
}

KnownTokenType KnownTokenTypeFromInt(int nType) {
    return (KnownTokenType)nType;
}

uint256 CTokenCacheQualifierAddress::GetHash() {
    return Hash(tokenName.begin(), tokenName.end(), address.begin(), address.end());
}

uint256 CTokenCacheRestrictedAddress::GetHash() {
    return Hash(tokenName.begin(), tokenName.end(), address.begin(), address.end());
}

uint256 CTokenCacheRootQualifierChecker::GetHash() {
    return Hash(rootTokenName.begin(), rootTokenName.end(), address.begin(), address.end());
}
