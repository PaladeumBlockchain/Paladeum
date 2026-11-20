// Copyright (c) 2022 The Paladeum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <unordered_map>

#include <governance/governance.h>
#include <primitives/transaction.h>
#include <utilstrencodings.h>
#include <script/script.h>
#include <chainparams.h>
#include <core_io.h>
#include <amount.h>
#include <base58.h>
#include <chain.h>
#include <hash.h>
#include <util.h>

static const CScript DUMMY_SCRIPT = CScript() << ParseHex("6885777789"); 
static const int DUMMY_TYPE = 0;

static const char DB_NUMBER_AUTHORIZED = 'A';
static const char DB_NUMBER_FROZEN = 'N';
static const char DB_AUTORIZATION = 'p';
static const char DB_FEE_ADDRESS = 'f';
static const char DB_ADDRESS = 'a';
static const char DB_COST = 'c';

static const char DB_GOVERNANCE_INIT  = 'G';

namespace {
    struct FreezeEntry {
        char key;
        CScript script;

        FreezeEntry() : key(DB_ADDRESS), script(DUMMY_SCRIPT) {}
        FreezeEntry(CScript script) : key(DB_ADDRESS), script(script) {}

        template<typename Stream>
        void Serialize(Stream &s) const {
            s << key;
            s << script;
        }

        template<typename Stream>
        void Unserialize(Stream& s) {
            s >> key;
            s >> script;
        }
    };

    struct FreezeDetails {
        bool frozen;

        FreezeDetails() : frozen(true) {}
        FreezeDetails(bool frozen) : frozen(frozen) {}

        template<typename Stream>
        void Serialize(Stream &s) const {
            s << frozen;
        }

        template<typename Stream>
        void Unserialize(Stream& s) {
            s >> frozen;
        }
    };

    struct CostEntry {
        char key;
        int type;
        int height;

        CostEntry() : key(DB_COST), type(DUMMY_TYPE), height(0) {}
        CostEntry(int type, int height) : key(DB_COST), type(type), height(height) {}

        template<typename Stream>
        void Serialize(Stream &s) const {
            s << key;
            s << type;
            s << height;
        }

        template<typename Stream>
        void Unserialize(Stream& s) {
            s >> key;
            s >> type;
            s >> height;
        }
    };

    struct CostDetails {
        CAmount cost;

        CostDetails() : cost(0) {}
        CostDetails(CAmount cost) : cost(cost) {}

        template<typename Stream>
        void Serialize(Stream &s) const {
            s << cost;
        }

        template<typename Stream>
        void Unserialize(Stream& s) {
            s >> cost;
        }
    };

    struct FeeEntry {
        char key;
        int height;

        FeeEntry() : key(DB_FEE_ADDRESS), height(0) {}
        FeeEntry(int height) : key(DB_FEE_ADDRESS), height(height) {}

        template<typename Stream>
        void Serialize(Stream &s) const {
            s << key;
            s << height;
        }

        template<typename Stream>
        void Unserialize(Stream& s) {
            s >> key;
            s >> height;
        }
    };

    struct FeeDetails {
        CScript script;

        FeeDetails() : script(DUMMY_SCRIPT) {}
        FeeDetails(CScript script) : script(script) {}

        template<typename Stream>
        void Serialize(Stream &s) const {
            s << script;
        }

        template<typename Stream>
        void Unserialize(Stream& s) {
            s >> script;
        }
    };

    struct AuthorityEntry {
        char key;
        CScript script;

        AuthorityEntry() : key(DB_AUTORIZATION), script(DUMMY_SCRIPT) {}
        AuthorityEntry(CScript script) : key(DB_AUTORIZATION), script(script) {}

        template<typename Stream>
        void Serialize(Stream &s) const {
            s << key;
            s << script;
        }

        template<typename Stream>
        void Unserialize(Stream& s) {
            s >> key;
            s >> script;
        }
    };

    struct AuthorityDetails {
        bool authorized;

        AuthorityDetails() : authorized(true) {}
        AuthorityDetails(bool authorized) : authorized(authorized) {}

        template<typename Stream>
        void Serialize(Stream &s) const {
            s << authorized;
        }

        template<typename Stream>
        void Unserialize(Stream& s) {
            s >> authorized;
        }
    };
}

CGovernance::CGovernance(size_t nCacheSize, bool fMemory, bool fWipe) : CDBWrapper(GetDataDir() / "governance", nCacheSize, fMemory, fWipe) 
{
}

bool CGovernance::Init(bool fWipe, const CChainParams& chainparams) {
    bool init;

    if (fWipe || Read(DB_GOVERNANCE_INIT, init) == false || init == false) {
        LogPrintf("Governance: Creating new database\n");

        CDBBatch batch(*this);

        batch.Write(DB_NUMBER_FROZEN, 0);

        // Add dummy entries will be first for searching the database
        batch.Write(AuthorityEntry(), AuthorityDetails());
        batch.Write(FreezeEntry(), FreezeDetails());
        batch.Write(CostEntry(), CostDetails());

        // Add initial token issuance cost values
        batch.Write(CostEntry(GOVERNANCE_COST_ROOT, 0), CostDetails(chainparams.IssueTokenFeeAmount()));
        batch.Write(CostEntry(GOVERNANCE_COST_REISSUE, 0), CostDetails(chainparams.ReissueTokenFeeAmount()));
        batch.Write(CostEntry(GOVERNANCE_COST_UNIQUE, 0), CostDetails(chainparams.IssueUniqueTokenFeeAmount()));
        batch.Write(CostEntry(GOVERNANCE_COST_SUB, 0), CostDetails(chainparams.IssueUniqueTokenFeeAmount()));
        batch.Write(CostEntry(GOVERNANCE_COST_USERNAME, 0), CostDetails(chainparams.IssueUsernameTokenFeeAmount()));
        batch.Write(CostEntry(GOVERNANCE_COST_MSG_CHANNEL, 0), CostDetails(chainparams.IssueMsgChannelTokenFeeAmount()));
        batch.Write(CostEntry(GOVERNANCE_COST_QUALIFIER, 0), CostDetails(chainparams.IssueQualifierTokenFeeAmount()));
        batch.Write(CostEntry(GOVERNANCE_COST_SUB_QUALIFIER, 0), CostDetails(chainparams.IssueSubQualifierTokenFeeAmount()));
        batch.Write(CostEntry(GOVERNANCE_COST_NULL_QUALIFIER, 0), CostDetails(chainparams.AddNullQualifierTagFeeAmount()));
        batch.Write(CostEntry(GOVERNANCE_COST_RESTRICTED, 0), CostDetails(chainparams.IssueRestrictedTokenFeeAmount()));

        // Init PoS-A addresses
        const std::set<std::string> init_authorized = chainparams.GetInitAuthorized();
        for (auto auth_address : init_authorized) {
            CTxDestination auth_destination = DecodeDestination(auth_address);
            CScript authScript = GetScriptForDestination(auth_destination);
            batch.Write(AuthorityEntry(authScript), AuthorityDetails(true));
        }

        // Add initial token fee address from chainparams
        CTxDestination destination = DecodeDestination(GetParams().TokenFeeAddress());
        CScript feeScript = GetScriptForDestination(destination);
        batch.Write(FeeEntry(), FeeDetails(feeScript));

        batch.Write(DB_GOVERNANCE_INIT, true);
        WriteBatch(batch);
    }

    return true;
}

// ---- Levels of changes (see BeginChanges in governance.h)

void CGovernance::BeginChanges()
{
    LOCK(cs);
    vChanges.emplace_back();
}

void CGovernance::DiscardChanges()
{
    LOCK(cs);
    assert(!vChanges.empty());
    vChanges.pop_back();
}

bool CGovernance::CommitChanges()
{
    LOCK(cs);
    assert(!vChanges.empty());
    Changes changes = std::move(vChanges.back());
    vChanges.pop_back();

    if (!vChanges.empty()) {
        // to the level below
        Changes& outer = vChanges.back();
        for (const auto& item : changes.freeze)
            outer.freeze[item.first] = item.second;
        for (const auto& item : changes.cost)
            outer.cost[item.first] = item.second;
        for (const auto& item : changes.fee)
            outer.fee[item.first] = item.second;
        if (changes.numberFrozen)
            outer.numberFrozen = changes.numberFrozen;
        for (const auto& item : changes.authority)
            outer.authority[item.first] = item.second;
        if (changes.numberAuthorized)
            outer.numberAuthorized = changes.numberAuthorized;
        return true;
    }

    if (changes.freeze.empty() && changes.cost.empty() && changes.fee.empty() && !changes.numberFrozen &&
        changes.authority.empty() && !changes.numberAuthorized)
        return true;

    // to the database, as one batch
    CDBBatch batch(*this);
    for (const auto& item : changes.freeze)
        batch.Write(FreezeEntry(item.first), FreezeDetails(item.second));
    for (const auto& item : changes.cost) {
        if (item.second)
            batch.Write(CostEntry(item.first.first, item.first.second), CostDetails(*item.second));
        else
            batch.Erase(CostEntry(item.first.first, item.first.second));
    }
    for (const auto& item : changes.fee) {
        if (item.second)
            batch.Write(FeeEntry(item.first), FeeDetails(*item.second));
        else
            batch.Erase(FeeEntry(item.first));
    }
    if (changes.numberFrozen)
        batch.Write(DB_NUMBER_FROZEN, *changes.numberFrozen);
    for (const auto& item : changes.authority)
        batch.Write(AuthorityEntry(item.first), AuthorityDetails(item.second));
    if (changes.numberAuthorized)
        batch.Write(DB_NUMBER_AUTHORIZED, *changes.numberAuthorized);
    return WriteBatch(batch);
}

bool CGovernance::GetFreezeEntry(const CScript& script, bool& frozen) const
{
    AssertLockHeld(cs);
    for (auto level = vChanges.rbegin(); level != vChanges.rend(); ++level) {
        auto it = level->freeze.find(script);
        if (it != level->freeze.end()) {
            frozen = it->second;
            return true;
        }
    }
    FreezeDetails details;
    if (!Read(FreezeEntry(script), details))
        return false;
    frozen = details.frozen;
    return true;
}

unsigned int CGovernance::GetNumberFrozen() const
{
    AssertLockHeld(cs);
    for (auto level = vChanges.rbegin(); level != vChanges.rend(); ++level) {
        if (level->numberFrozen)
            return *level->numberFrozen;
    }
    unsigned int number = 0;
    Read(DB_NUMBER_FROZEN, number);
    return number;
}

bool CGovernance::GetCostEntry(int type, int height, CAmount& cost) const
{
    AssertLockHeld(cs);
    const std::pair<int, int> key(type, height);
    for (auto level = vChanges.rbegin(); level != vChanges.rend(); ++level) {
        auto it = level->cost.find(key);
        if (it != level->cost.end()) {
            if (!it->second)
                return false;
            cost = *it->second;
            return true;
        }
    }
    CostDetails details;
    if (!Read(CostEntry(type, height), details))
        return false;
    cost = details.cost;
    return true;
}

bool CGovernance::GetFeeEntry(int height, CScript& script) const
{
    AssertLockHeld(cs);
    for (auto level = vChanges.rbegin(); level != vChanges.rend(); ++level) {
        auto it = level->fee.find(height);
        if (it != level->fee.end()) {
            if (!it->second)
                return false;
            script = *it->second;
            return true;
        }
    }
    FeeDetails details;
    if (!Read(FeeEntry(height), details))
        return false;
    script = details.script;
    return true;
}

void CGovernance::SetFreezeEntry(CDBBatch& batch, const CScript& script, bool frozen)
{
    AssertLockHeld(cs);
    if (!vChanges.empty())
        vChanges.back().freeze[script] = frozen;
    else
        batch.Write(FreezeEntry(script), FreezeDetails(frozen));
}

void CGovernance::SetNumberFrozen(CDBBatch& batch, unsigned int number)
{
    AssertLockHeld(cs);
    if (!vChanges.empty())
        vChanges.back().numberFrozen = number;
    else
        batch.Write(DB_NUMBER_FROZEN, number);
}

void CGovernance::SetCostEntry(CDBBatch& batch, int type, int height, std::optional<CAmount> cost)
{
    AssertLockHeld(cs);
    if (!vChanges.empty()) {
        vChanges.back().cost[std::make_pair(type, height)] = cost;
    } else if (cost) {
        batch.Write(CostEntry(type, height), CostDetails(*cost));
    } else {
        batch.Erase(CostEntry(type, height));
    }
}

void CGovernance::SetFeeEntry(CDBBatch& batch, int height, std::optional<CScript> script)
{
    AssertLockHeld(cs);
    if (!vChanges.empty()) {
        vChanges.back().fee[height] = script;
    } else if (script) {
        batch.Write(FeeEntry(height), FeeDetails(*script));
    } else {
        batch.Erase(FeeEntry(height));
    }
}

bool CGovernance::GetAuthorityEntry(const CScript& script, bool& authorized) const
{
    AssertLockHeld(cs);
    for (auto level = vChanges.rbegin(); level != vChanges.rend(); ++level) {
        auto it = level->authority.find(script);
        if (it != level->authority.end()) {
            authorized = it->second;
            return true;
        }
    }
    AuthorityDetails details;
    if (!Read(AuthorityEntry(script), details))
        return false;
    authorized = details.authorized;
    return true;
}

unsigned int CGovernance::GetNumberAuthorized() const
{
    AssertLockHeld(cs);
    for (auto level = vChanges.rbegin(); level != vChanges.rend(); ++level) {
        if (level->numberAuthorized)
            return *level->numberAuthorized;
    }
    // (not written when the database is created: no value counts as 0)
    unsigned int number = 0;
    Read(DB_NUMBER_AUTHORIZED, number);
    return number;
}

void CGovernance::SetAuthorityEntry(CDBBatch& batch, const CScript& script, bool authorized)
{
    AssertLockHeld(cs);
    if (!vChanges.empty())
        vChanges.back().authority[script] = authorized;
    else
        batch.Write(AuthorityEntry(script), AuthorityDetails(authorized));
}

void CGovernance::SetNumberAuthorized(CDBBatch& batch, unsigned int number)
{
    AssertLockHeld(cs);
    if (!vChanges.empty())
        vChanges.back().numberAuthorized = number;
    else
        batch.Write(DB_NUMBER_AUTHORIZED, number);
}

std::vector<std::pair<CScript, bool>> CGovernance::AuthorityList() const
{
    AssertLockHeld(cs);
    std::vector<std::pair<CScript, bool>> list;
    std::unique_ptr<CDBIterator> it(const_cast<CGovernance*>(this)->NewIterator());
    for (it->Seek(AuthorityEntry(DUMMY_SCRIPT)); it->Valid(); it->Next()) { // DUMMY_SCRIPT is the lexically first script.
        AuthorityEntry entry;
        if (it->GetKey(entry) && entry.key == DB_AUTORIZATION) {
            AuthorityDetails details;
            it->GetValue(details);
            list.emplace_back(entry.script, details.authorized);
        } else {
            break; // we are done with the scripts.
        }
    }
    if (!vChanges.empty()) {
        // the open levels: their value for a script in the list, new scripts at the end
        std::map<CScript, bool> pending;
        for (const Changes& level : vChanges)
            for (const auto& item : level.authority)
                pending[item.first] = item.second;
        for (auto& item : list) {
            auto found = pending.find(item.first);
            if (found != pending.end()) {
                item.second = found->second;
                pending.erase(found);
            }
        }
        for (const auto& item : pending)
            list.emplace_back(item.first, item.second);
    }
    return list;
}

bool CGovernance::Finish(CDBBatch& batch)
{
    AssertLockHeld(cs);
    if (!vChanges.empty())
        return true;
    return WriteBatch(batch);
}

std::vector<CScript> GovernanceFreezeScripts(const CTransaction& tx)
{
    std::vector<CScript> scripts;
    for (const CTxOut& out : tx.vout) {
        const CScript& script = out.scriptPubKey;
        if (script.size() >= 6 && script[0] == OP_RETURN && script[2] == GOVERNANCE_MARKER && script[3] == GOVERNANCE_ACTION
            && script[4] == GOVERNANCE_FREEZE && script.size() == 6 + (size_t)script[5]) {
            scripts.emplace_back(script.begin() + 6, script.end());
        }
    }
    return scripts;
}

// ---- Freeze list

unsigned int CGovernance::GetNumberOfFrozenScripts() {
    LOCK(cs);
    return GetNumberFrozen();
}

bool CGovernance::FreezeScript(CScript script) {
    LOCK(cs);
    CDBBatch batch(*this);
    const unsigned int number = GetNumberFrozen();
    bool frozen;

    if (GetFreezeEntry(script, frozen)) {
        if (!frozen) {
            LogPrintf("Governance: Adding script %s back to freeze list\n", HexStr(script));
            SetFreezeEntry(batch, script, true);
            SetNumberFrozen(batch, number + 1);
        } else {
            LogPrintf("Governance: Script %s already frozen\n", HexStr(script));
            SetFreezeEntry(batch, script, true);
        }
    } else {
        LogPrintf("Governance: Freezing previously unknown script %s\n", HexStr(script));
        SetFreezeEntry(batch, script, true);
        SetNumberFrozen(batch, number + 1);
    }

    return Finish(batch);
}

bool CGovernance::UnfreezeScript(CScript script) {
    LOCK(cs);
    CDBBatch batch(*this);
    const unsigned int number = GetNumberFrozen();
    bool frozen;

    if (GetFreezeEntry(script, frozen)) {
        if (frozen) {
            LogPrintf("Governance: Removing script %s from freeze list\n", HexStr(script));
            SetFreezeEntry(batch, script, false);
            SetNumberFrozen(batch, number - 1);
        } else {
            LogPrintf("Governance: Script %s already unfrozen\n", HexStr(script));
            SetFreezeEntry(batch, script, false);
        }
    } else {
        LogPrintf("Governance: Unfreezing previously unknown script %s\n", HexStr(script));
        SetFreezeEntry(batch, script, false);
    }

    return Finish(batch);
}

bool CGovernance::RevertFreezeScript(CScript script) {
    // This is different from unfreezing
    // Reverting immediately removes script from the freeze list,
    // This routine only does so if scrip was only added to the list once
    LOCK(cs);
    CDBBatch batch(*this);
    const unsigned int number = GetNumberFrozen();
    bool frozen;

    if (GetFreezeEntry(script, frozen)) {
        if (frozen) {
            LogPrintf("Governance: Revert adding of script %s to freeze list\n", HexStr(script));
            LogPrintf("Governance: Unfreezing script %s\n", HexStr(script));
            SetNumberFrozen(batch, number - 1);
            SetFreezeEntry(batch, script, false);
        } else {
            LogPrintf("Trying to revert freezing of script, database is corrupted\n");
            return false;
        }
    } else {
        LogPrintf("Trying to revert freezing of unknown script, database is corrupted\n");
        return false;
    }

    return Finish(batch);
}

bool CGovernance::RevertUnfreezeScript(CScript script) {
    // This is different from freezing
    // Reverting immediately adds script to the freeze list,
    // This routine only does so if script was only removed from the list once
    LOCK(cs);
    CDBBatch batch(*this);
    const unsigned int number = GetNumberFrozen();
    bool frozen;

    if (GetFreezeEntry(script, frozen)) {
        if (!frozen) {
            LogPrintf("Governance: Revert disabling of script %s\n", HexStr(script));
            LogPrintf("Governance: Freezing script %s\n", HexStr(script));
            SetNumberFrozen(batch, number + 1);
            SetFreezeEntry(batch, script, true);
        } else {
            LogPrintf("Trying to revert unfreezing of script, database is corrupted\n");
            return false;
        }
    } else {
        LogPrintf("Governance: Trying to revert unfreezing of unknown script, database is corrupted\n");
        return false;
    }

    return Finish(batch);
}

bool CGovernance::ScriptExist(CScript script) {
    LOCK(cs);
    bool frozen;
    return GetFreezeEntry(script, frozen);
}

bool CGovernance::CanSend(CScript script) {
    LOCK(cs);
    bool frozen;
    if (!GetFreezeEntry(script, frozen))
        return true;
    return !frozen;
}

bool CGovernance::DumpFreezeStats(std::vector< std::pair< CScript, bool > > *FreezeVector) {
    LOCK(cs);
    if (IsEmpty())
        LogPrintf("Governance: DB is empty\n");

    std::unique_ptr<CDBIterator> it(NewIterator());
    for (it->Seek(FreezeEntry(DUMMY_SCRIPT)); it->Valid(); it->Next()) { // DUMMY_SCRIPT is the lexically first script.
        FreezeEntry entry;
        if (it->GetKey(entry) && entry.key == DB_ADDRESS) {
            FreezeDetails details;
            it->GetValue(details);

            FreezeVector->emplace_back(entry.script, details.frozen);
        } else {
            break; // we are done with the scripts.
        }
    }

    if (!vChanges.empty()) {
        // the open levels: their value for a script in the list, new scripts at the end
        std::map<CScript, bool> pending;
        for (const Changes& level : vChanges)
            for (const auto& item : level.freeze)
                pending[item.first] = item.second;
        for (auto& item : *FreezeVector) {
            auto found = pending.find(item.first);
            if (found != pending.end()) {
                item.second = found->second;
                pending.erase(found);
            }
        }
        for (const auto& item : pending)
            FreezeVector->emplace_back(item.first, item.second);
    }

    return true;
}

bool CGovernance::GetFrozenScripts(std::vector< CScript > *FreezeVector) {
    std::vector<std::pair<CScript, bool>> all;
    DumpFreezeStats(&all);
    for (const auto& item : all) {
        if (item.second)
            FreezeVector->emplace_back(item.first);
    }
    return true;
}

// ---- Issuing cost

CAmount CGovernance::GetCost(int type) {
    LOCK(cs);
    // every value for this type by height: the database, then the open levels over it
    std::map<int, CAmount> values;

    std::unique_ptr<CDBIterator> it(NewIterator());
    for (it->Seek(CostEntry()); it->Valid(); it->Next()) {
        CostEntry entry;
        if (it->GetKey(entry) && entry.key == DB_COST) {
            if (entry.type == type) {
                CostDetails details = CostDetails();
                it->GetValue(details);
                values[entry.height] = details.cost;
            }
        } else {
            break;
        }
    }
    for (const Changes& level : vChanges) {
        for (const auto& item : level.cost) {
            if (item.first.first != type)
                continue;
            if (item.second)
                values[item.first.second] = *item.second;
            else
                values.erase(item.first.second);
        }
    }

    // the value of the highest height (no value: 0, as before)
    if (values.empty())
        return CostDetails().cost;
    return values.rbegin()->second;
}

static bool CostTypeName(int type, std::string& type_name)
{
    if (type == GOVERNANCE_COST_ROOT) {
        type_name = "root";
    } else if (type == GOVERNANCE_COST_REISSUE) {
        type_name = "reissue";
    } else if (type == GOVERNANCE_COST_UNIQUE) {
        type_name = "unique";
    } else if (type == GOVERNANCE_COST_SUB) {
        type_name = "sub";
    } else if (type == GOVERNANCE_COST_USERNAME) {
        type_name = "username";
    } else {
        return false;
    }
    return true;
}

bool CGovernance::UpdateCost(CAmount cost, int type, int height) {
    std::string type_name;
    if (!CostTypeName(type, type_name)) {
        LogPrintf("Governance: Trying to update issuance cost for unknow type\n");
        return false;
    }

    LOCK(cs);
    CDBBatch batch(*this);
    CAmount existing;
    // (a second command for the same type in the same block does nothing, as before)
    if (!GetCostEntry(type, height, existing)) {
        LogPrintf("Governance: Updating issuance cost for \"%s\" to %s PLB\n", type_name, ValueFromAmountString(cost, 8));
        SetCostEntry(batch, type, height, cost);
    }

    return Finish(batch);
}

bool CGovernance::RevertUpdateCost(int type, int height) {
    std::string type_name;
    CostTypeName(type, type_name);

    LOCK(cs);
    CDBBatch batch(*this);
    CAmount existing;
    if (GetCostEntry(type, height, existing)) {
        LogPrintf("Governance: Revert updating issuance cost for \"%s\" to %s PLB\n", type_name, ValueFromAmountString(existing, 8));
        SetCostEntry(batch, type, height, std::nullopt);
    } else {
        LogPrintf("Governance: Trying to revert unknown issuance cost update, database is corrupted\n");
        return false;
    }

    return Finish(batch);
}

// ---- Fee address

CScript CGovernance::GetFeeScript() {
    LOCK(cs);
    std::map<int, CScript> values;

    std::unique_ptr<CDBIterator> it(NewIterator());
    for (it->Seek(FeeEntry()); it->Valid(); it->Next()) {
        FeeEntry entry;
        if (it->GetKey(entry) && entry.key == DB_FEE_ADDRESS) {
            FeeDetails details = FeeDetails();
            it->GetValue(details);
            values[entry.height] = details.script;
        } else {
            break;
        }
    }
    for (const Changes& level : vChanges) {
        for (const auto& item : level.fee) {
            if (item.second)
                values[item.first] = *item.second;
            else
                values.erase(item.first);
        }
    }

    if (values.empty())
        return FeeDetails().script;
    return values.rbegin()->second;
}

bool CGovernance::UpdateFeeScript(CScript script, int height) {
    LOCK(cs);
    CDBBatch batch(*this);
    CScript existing;
    if (!GetFeeEntry(height, existing)) {
        LogPrintf("Governance: Updating fee script to %s\n", HexStr(script));
        SetFeeEntry(batch, height, script);
    }

    return Finish(batch);
}

bool CGovernance::RevertUpdateFeeScript(int height) {
    LOCK(cs);
    CDBBatch batch(*this);
    CScript existing;
    if (GetFeeEntry(height, existing)) {
        LogPrintf("Governance: Revert updating fee script to %s\n", HexStr(existing));
        SetFeeEntry(batch, height, std::nullopt);
    } else {
        LogPrintf("Governance: Trying to revert unknown fee script update, database is corrupted\n");
        return false;
    }

    return Finish(batch);
}

namespace {
/** Reads whatever is left of a key or a value. */
struct RawRecord {
    std::vector<unsigned char> bytes;
    template <typename Stream> void Unserialize(Stream& s)
    {
        bytes.resize(s.size());
        if (!bytes.empty())
            s.read((char*)bytes.data(), bytes.size());
    }
};
} // namespace

uint256 CGovernance::ContentHash()
{
    CHashWriter ss(SER_GETHASH, 0);
    std::unique_ptr<CDBIterator> it(NewIterator());
    for (it->SeekToFirst(); it->Valid(); it->Next()) {
        RawRecord key, value;
        if (!it->GetKey(key) || !it->GetValue(value))
            return uint256();
        ss << key.bytes << value.bytes;
    }
    return ss.GetHash();
}

// ---- Authorization list (proof-of-stake authorization)

unsigned int CGovernance::GetNumberOfAuthorizedScripts() {
    LOCK(cs);
    return GetNumberAuthorized();
}

bool CGovernance::AuthorizeScript(CScript script) {
    LOCK(cs);
    CDBBatch batch(*this);
    const unsigned int number = GetNumberAuthorized();
    bool authorized;

    if (GetAuthorityEntry(script, authorized)) {
        if (!authorized) {
            LogPrintf("Governance: Adding script %s back to authorized list\n", HexStr(script));
            SetAuthorityEntry(batch, script, true);
            SetNumberAuthorized(batch, number + 1);
        } else {
            LogPrintf("Governance: Script %s already authorized\n", HexStr(script));
            SetAuthorityEntry(batch, script, true);
        }
    } else {
        LogPrintf("Governance: Authorizing previously unknown script %s\n", HexStr(script));
        SetAuthorityEntry(batch, script, true);
        SetNumberAuthorized(batch, number + 1);
    }

    return Finish(batch);
}

bool CGovernance::UnauthorizeScript(CScript script) {
    LOCK(cs);
    CDBBatch batch(*this);
    const unsigned int number = GetNumberAuthorized();
    bool authorized;

    if (GetAuthorityEntry(script, authorized)) {
        if (authorized) {
            LogPrintf("Governance: Removing script %s from authorization list\n", HexStr(script));
            SetAuthorityEntry(batch, script, false);
            SetNumberAuthorized(batch, number - 1);
        } else {
            LogPrintf("Governance: Script %s already unauthorized\n", HexStr(script));
            SetAuthorityEntry(batch, script, false);
        }
    } else {
        LogPrintf("Governance: Unauthorizing previously unknown script %s\n", HexStr(script));
        SetAuthorityEntry(batch, script, false);
    }

    return Finish(batch);
}

bool CGovernance::RevertAuthorizeScript(CScript script) {
    // This is different from unauthorizing
    // Reverting immediately removes script from the authorization list,
    // This routine only does so if script was only added to the list once
    LOCK(cs);
    CDBBatch batch(*this);
    const unsigned int number = GetNumberAuthorized();
    bool authorized;

    if (GetAuthorityEntry(script, authorized)) {
        if (authorized) {
            LogPrintf("Governance: Revert adding of script %s to authorized list\n", HexStr(script));
            LogPrintf("Governance: Unauthorizing script %s\n", HexStr(script));
            SetNumberAuthorized(batch, number - 1);
            SetAuthorityEntry(batch, script, false);
        } else {
            LogPrintf("Trying to revert authorization of script, database is corrupted\n");
            return false;
        }
    } else {
        LogPrintf("Trying to revert authorization of unknown script, database is corrupted\n");
        return false;
    }

    return Finish(batch);
}

bool CGovernance::RevertUnauthorizeScript(CScript script) {
    // This is different from authorizing
    // Reverting immediately adds script to the authorize list,
    // This routine only does so if script was only removed from the list once
    LOCK(cs);
    CDBBatch batch(*this);
    const unsigned int number = GetNumberAuthorized();
    bool authorized;

    if (GetAuthorityEntry(script, authorized)) {
        if (!authorized) {
            LogPrintf("Governance: Revert unauthorization of script %s\n", HexStr(script));
            LogPrintf("Governance: Authorizing script %s\n", HexStr(script));
            SetNumberAuthorized(batch, number + 1);
            SetAuthorityEntry(batch, script, true);
        } else {
            LogPrintf("Trying to revert unauthorization of script, database is corrupted\n");
            return false;
        }
    } else {
        LogPrintf("Governance: Trying to revert unauthorization of unknown script, database is corrupted\n");
        return false;
    }

    return Finish(batch);
}

bool CGovernance::AuthorityExist(CScript script) {
    LOCK(cs);
    bool authorized;
    return GetAuthorityEntry(script, authorized);
}

bool CGovernance::CanStake(CScript script) {
    // Handle pay-to-public-key outputs properly
    if (script.IsPayToPublicKey()) {
        uint160 hashBytes(Hash160(script.begin() + 1, script.end() - 1));
        script = CScript() << OP_DUP << OP_HASH160 << ToByteVector(hashBytes) << OP_EQUALVERIFY << OP_CHECKSIG;
    }

    LOCK(cs);
    bool authorized;
    if (!GetAuthorityEntry(script, authorized))
        return false;
    return authorized;
}

bool CGovernance::GetActiveValidators(std::vector< std::string > *ValidatorsVector) {
    LOCK(cs);
    if (IsEmpty())
        LogPrintf("Governance: DB is empty\n");

    for (const auto& item : AuthorityList()) {
        if (item.second) {
            CTxDestination authorizedDestination;
            ExtractDestination(item.first, authorizedDestination);
            CPaladeumAddress validatorAddress(authorizedDestination);

            ValidatorsVector->emplace_back(validatorAddress.ToString());
        }
    }

    return true;
}


bool CGovernance::GetActiveValidatorsScript(std::vector< CScript > *ValidatorsVector) {
    LOCK(cs);
    if (IsEmpty())
        LogPrintf("Governance: DB is empty\n");

    for (const auto& item : AuthorityList()) {
        if (item.second)
            ValidatorsVector->emplace_back(item.first);
    }

    return true;
}
