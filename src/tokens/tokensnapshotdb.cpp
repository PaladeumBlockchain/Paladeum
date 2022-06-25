// Copyright (c) 2017-2019 The Raven Core developers
// Copyright (c) 2021-2022 The Akila developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "tokensnapshotdb.h"
#include "validation.h"
#include "base58.h"

#include <boost/algorithm/string.hpp>
#include <boost/thread.hpp>

static const char SNAPSHOTCHECK_FLAG = 'C'; // Snapshot Check

CTokenSnapshotDBEntry::CTokenSnapshotDBEntry()
{
    SetNull();
}

CTokenSnapshotDBEntry::CTokenSnapshotDBEntry(
    const std::string & p_tokenName, int p_snapshotHeight,
    const std::set<std::pair<std::string, CAmount>> & p_ownersAndAmounts
)
{
    SetNull();

    height = p_snapshotHeight;
    tokenName = p_tokenName;
    for (auto const & currPair : p_ownersAndAmounts) {
        ownersAndAmounts.insert(currPair);
    }

    heightAndName = std::to_string(height) + tokenName;
}

CTokenSnapshotDB::CTokenSnapshotDB(size_t nCacheSize, bool fMemory, bool fWipe) : CDBWrapper(GetDataDir() / "rewards" / "tokensnapshot", nCacheSize, fMemory, fWipe) {
}

bool CTokenSnapshotDB::AddTokenOwnershipSnapshot(
    const std::string & p_tokenName, int p_height)
{
    LogPrint(BCLog::REWARDS, "AddTokenOwnershipSnapshot: Adding snapshot for '%s' at height %d\n",
        p_tokenName.c_str(), p_height);

    //  Retrieve ownership interest for the token at this height
    if (ptokensdb == nullptr) {
        LogPrint(BCLog::REWARDS, "AddTokenOwnershipSnapshot: Invalid tokens DB!\n");
        return false;
    }

    std::set<std::pair<std::string, CAmount>> ownersAndAmounts;
    std::vector<std::pair<std::string, CAmount>> tempOwnersAndAmounts;
    int totalEntryCount;

    if (!ptokensdb->TokenAddressDir(tempOwnersAndAmounts, totalEntryCount, true, p_tokenName, INT_MAX, 0)) {
        LogPrint(BCLog::REWARDS, "AddTokenOwnershipSnapshot: Failed to retrieve tokens directory for '%s'\n", p_tokenName.c_str());
        return false;
    }

    //  Retrieve all of the addresses/amounts in batches
    const int MAX_RETRIEVAL_COUNT = 100;
    bool errorsOccurred = false;

    for (int retrievalOffset = 0; retrievalOffset < totalEntryCount; retrievalOffset += MAX_RETRIEVAL_COUNT) {
        //  Retrieve the specified segment of addresses
        if (!ptokensdb->TokenAddressDir(tempOwnersAndAmounts, totalEntryCount, false, p_tokenName, MAX_RETRIEVAL_COUNT, retrievalOffset)) {
            LogPrint(BCLog::REWARDS, "AddTokenOwnershipSnapshot: Failed to retrieve tokens directory for '%s'\n", p_tokenName.c_str());
            errorsOccurred = true;
            break;
        }

        //  Verify that some addresses were returned
        if (tempOwnersAndAmounts.size() == 0) {
            LogPrint(BCLog::REWARDS, "AddTokenOwnershipSnapshot: No addresses were retrieved.\n");
            continue;
        }

        //  Move these into the main set
        for (auto const & currPair : tempOwnersAndAmounts) {
            //  Verify that the address is valid
            CTxDestination dest = DecodeDestination(currPair.first);
            if (IsValidDestination(dest)) {
                ownersAndAmounts.insert(currPair);
            }
            else {
                LogPrint(BCLog::REWARDS, "AddTokenOwnershipSnapshot: Address '%s' is invalid.\n", currPair.first.c_str());
            }
        }

        tempOwnersAndAmounts.clear();
    }

    if (errorsOccurred) {
        LogPrint(BCLog::REWARDS, "AddTokenOwnershipSnapshot: Errors occurred while acquiring ownership info for token '%s'.\n", p_tokenName.c_str());
        return false;
    }
    if (ownersAndAmounts.size() == 0) {
        LogPrint(BCLog::REWARDS, "AddTokenOwnershipSnapshot: No owners exist for token '%s'.\n", p_tokenName.c_str());
        return false;
    }

    //  Write the snapshot to the database. We don't care if we overwrite, because it should be identical.
    CTokenSnapshotDBEntry snapshotEntry(p_tokenName, p_height, ownersAndAmounts);

    if (Write(std::make_pair(SNAPSHOTCHECK_FLAG, snapshotEntry.heightAndName), snapshotEntry)) {
        LogPrint(BCLog::REWARDS, "AddTokenOwnershipSnapshot: Successfully added snapshot for '%s' at height %d (ownerCount = %d).\n",
            p_tokenName.c_str(), p_height, ownersAndAmounts.size());
        return true;
    }
    return false;
}

bool CTokenSnapshotDB::RetrieveOwnershipSnapshot(
    const std::string & p_tokenName, int p_height,
    CTokenSnapshotDBEntry & p_snapshotEntry)
{
    //  Load up the snapshot entries at this height
    std::string heightAndName = std::to_string(p_height) + p_tokenName;

    LogPrint(BCLog::REWARDS, "%s : Attempting to retrieve snapshot: heightAndName='%s'\n",
        __func__,
        heightAndName.c_str());

    bool succeeded = Read(std::make_pair(SNAPSHOTCHECK_FLAG, heightAndName), p_snapshotEntry);

    LogPrint(BCLog::REWARDS, "%s : Retrieval of snapshot for '%s' %s!\n",
        __func__,
        heightAndName.c_str(),
        succeeded ? "succeeded" : "failed");

    return succeeded;
}

bool CTokenSnapshotDB::RemoveOwnershipSnapshot(
    const std::string & p_tokenName, int p_height)
{
    //  Load up the snapshot entries at this height
    std::string heightAndName = std::to_string(p_height) + p_tokenName;

    LogPrint(BCLog::REWARDS, "%s : Attempting to remove snapshot: heightAndName='%s'\n",
        __func__,
        heightAndName.c_str());

    bool succeeded = Erase(std::make_pair(SNAPSHOTCHECK_FLAG, heightAndName), true);

    LogPrint(BCLog::REWARDS, "%s : Removal of snapshot for '%s' %s!\n",
        __func__,
        heightAndName.c_str(),
        succeeded ? "succeeded" : "failed");

    return succeeded;
}
