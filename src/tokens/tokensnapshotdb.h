// Copyright (c) 2017-2019 The Raven Core developers
// Copyright (c) 2021-2022 The Akila developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TOKENSNAPSHOTDB_H
#define TOKENSNAPSHOTDB_H

#include <set>

#include <dbwrapper.h>
#include "amount.h"

class CTokenSnapshotDBEntry
{
public:
    int height;
    std::string tokenName;
    std::set<std::pair<std::string, CAmount>> ownersAndAmounts;

    //  Used as the DB key for the snapshot
    std::string heightAndName;

    CTokenSnapshotDBEntry();
    CTokenSnapshotDBEntry(
        const std::string & p_tokenName, const int p_snapshotHeight,
        const std::set<std::pair<std::string, CAmount>> & p_ownersAndAmounts
    );

    void SetNull()
    {
        height = 0;
        tokenName = "";
        ownersAndAmounts.clear();

        heightAndName = "";
    }

    bool operator<(const CTokenSnapshotDBEntry &rhs) const
    {
        return heightAndName < rhs.heightAndName;
    }

    // Serialization methods
    ADD_SERIALIZE_METHODS;

    template<typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action)
    {
        READWRITE(height);
        READWRITE(tokenName);
        READWRITE(ownersAndAmounts);
        READWRITE(heightAndName);
    }
};

class CTokenSnapshotDB  : public CDBWrapper {
public:
    explicit CTokenSnapshotDB(size_t nCacheSize, bool fMemory = false, bool fWipe = false);

    CTokenSnapshotDB(const CTokenSnapshotDB&) = delete;
    CTokenSnapshotDB& operator=(const CTokenSnapshotDB&) = delete;

    //  Add an entry to the snapshot at the specified height
    bool AddTokenOwnershipSnapshot(
        const std::string & p_tokenName, int p_height);

    //  Read all of the entries at a specified height
    bool RetrieveOwnershipSnapshot(
        const std::string & p_tokenName, int p_height,
        CTokenSnapshotDBEntry & p_snapshotEntry);

    //  Remove the token snapshot at the specified height
    bool RemoveOwnershipSnapshot(
        const std::string & p_tokenName, int p_height);
};


#endif //TOKENSNAPSHOTDB_H
