// Copyright (c) 2021-2022 The Paladeum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <tokens/tokens.h>

#include <test/test_paladeum.h>

#include <boost/test/unit_test.hpp>
#include "core_write.cpp"

#include <amount.h>
#include <base58.h>
#include <chainparams.h>

#include "LibBoolEE.h"

BOOST_FIXTURE_TEST_SUITE(token_tests, BasicTestingSetup)

    BOOST_AUTO_TEST_CASE(name_validation_tests)
    {
        BOOST_TEST_MESSAGE("Running Name Validation Test");

        KnownTokenType type;

        // regular
        BOOST_CHECK(IsTokenNameValid("MIN", type));
        BOOST_CHECK(type == KnownTokenType::ROOT);
        BOOST_CHECK(IsTokenNameValid("MAX_TOKEN_IS_30_CHARACTERS_LNG", type));
        BOOST_CHECK(!IsTokenNameValid("MAX_TOKEN_IS_31_CHARACTERS_LONG", type));
        BOOST_CHECK(type == KnownTokenType::INVALID);
        BOOST_CHECK(IsTokenNameValid("A_BCDEFGHIJKLMNOPQRSTUVWXY.Z", type));
        BOOST_CHECK(IsTokenNameValid("0_12345678.9", type));

        BOOST_CHECK(!IsTokenNameValid("NO", type));
        BOOST_CHECK(!IsTokenNameValid("nolower", type));
        BOOST_CHECK(!IsTokenNameValid("NO SPACE", type));
        BOOST_CHECK(!IsTokenNameValid("(#&$(&*^%$))", type));

        BOOST_CHECK(!IsTokenNameValid("_ABC", type));
        BOOST_CHECK(!IsTokenNameValid("_ABC", type));
        BOOST_CHECK(!IsTokenNameValid("ABC_", type));
        BOOST_CHECK(!IsTokenNameValid(".ABC", type));
        BOOST_CHECK(!IsTokenNameValid("ABC.", type));
        BOOST_CHECK(!IsTokenNameValid("AB..C", type));
        BOOST_CHECK(!IsTokenNameValid("A__BC", type));
        BOOST_CHECK(!IsTokenNameValid("A._BC", type));
        BOOST_CHECK(!IsTokenNameValid("AB_.C", type));

        //- Versions of PLBCOIN NOT allowed
        BOOST_CHECK(!IsTokenNameValid("PLB", type));
        BOOST_CHECK(!IsTokenNameValid("PLB", type));
        BOOST_CHECK(!IsTokenNameValid("PLBCOIN", type));

        //- Versions of PLBCOIN ALLOWED
        BOOST_CHECK(IsTokenNameValid("PLB.COIN", type));
        BOOST_CHECK(IsTokenNameValid("PLB_COIN", type));
        BOOST_CHECK(IsTokenNameValid("PLBSPYDER", type));
        BOOST_CHECK(IsTokenNameValid("SPYDERPLB", type));
        BOOST_CHECK(IsTokenNameValid("PLBSPYDER", type));
        BOOST_CHECK(IsTokenNameValid("SPYDEPLB", type));
        BOOST_CHECK(IsTokenNameValid("BLACK_PLBS", type));
        BOOST_CHECK(IsTokenNameValid("SEPLBOT", type));

        // subs
        BOOST_CHECK(IsTokenNameValid("ABC/A", type));
        BOOST_CHECK(type == KnownTokenType::SUB);
        BOOST_CHECK(IsTokenNameValid("ABC/A/1", type));
        BOOST_CHECK(IsTokenNameValid("ABC/A_1/1.A", type));
        BOOST_CHECK(IsTokenNameValid("ABC/AB/XYZ/STILL/MAX/30/123456", type));

        BOOST_CHECK(!IsTokenNameValid("ABC//MIN_1", type));
        BOOST_CHECK(!IsTokenNameValid("ABC/", type));
        BOOST_CHECK(!IsTokenNameValid("ABC/NOTRAIL/", type));
        BOOST_CHECK(!IsTokenNameValid("ABC/_X", type));
        BOOST_CHECK(!IsTokenNameValid("ABC/X_", type));
        BOOST_CHECK(!IsTokenNameValid("ABC/.X", type));
        BOOST_CHECK(!IsTokenNameValid("ABC/X.", type));
        BOOST_CHECK(!IsTokenNameValid("ABC/X__X", type));
        BOOST_CHECK(!IsTokenNameValid("ABC/X..X", type));
        BOOST_CHECK(!IsTokenNameValid("ABC/X_.X", type));
        BOOST_CHECK(!IsTokenNameValid("ABC/X._X", type));
        BOOST_CHECK(!IsTokenNameValid("ABC/nolower", type));
        BOOST_CHECK(!IsTokenNameValid("ABC/NO SPACE", type));
        BOOST_CHECK(!IsTokenNameValid("ABC/(*#^&$%)", type));
        BOOST_CHECK(!IsTokenNameValid("ABC/AB/XYZ/STILL/MAX/30/OVERALL/1234", type));

        // unique
        BOOST_CHECK(IsTokenNameValid("ABC#AZaz09", type));
        BOOST_CHECK(type == KnownTokenType::UNIQUE);
        BOOST_CHECK(IsTokenNameValid("ABC#abc123ABC@$%&*()[]{}-_.?:", type));
        BOOST_CHECK(!IsTokenNameValid("ABC#no!bangs", type));
        BOOST_CHECK(IsTokenNameValid("ABC/THING#_STILL_31_MAX-------_", type));

        BOOST_CHECK(!IsTokenNameValid("MIN#", type));
        BOOST_CHECK(!IsTokenNameValid("ABC#NO#HASH", type));
        BOOST_CHECK(!IsTokenNameValid("ABC#NO SPACE", type));
        BOOST_CHECK(!IsTokenNameValid("ABC#RESERVED/", type));
        BOOST_CHECK(!IsTokenNameValid("ABC#RESERVED~", type));
        BOOST_CHECK(!IsTokenNameValid("ABC#RESERVED^", type));

        // Check function that creates unique tags for the user return empty string when nessecary
        BOOST_CHECK(GetUniqueTokenName("_.INVALID", "TAG") == "");
        BOOST_CHECK(GetUniqueTokenName("#TAG", "TAG") == "");

        // channel
        BOOST_CHECK(IsTokenNameValid("ABC~1", type));
        BOOST_CHECK(type == KnownTokenType::MSGCHANNEL);
        BOOST_CHECK(IsTokenNameValid("ABC~MAX_OF_12_CR", type));
        BOOST_CHECK(!IsTokenNameValid("ABC~MAX_OF_12_CHR", type));
        BOOST_CHECK(IsTokenNameValid("TEST/TEST~CHANNEL", type));
        BOOST_CHECK(type == KnownTokenType::MSGCHANNEL);

        BOOST_CHECK(!IsTokenNameValid("MIN~", type));
        BOOST_CHECK(!IsTokenNameValid("ABC~NO~TILDE", type));
        BOOST_CHECK(!IsTokenNameValid("ABC~_ANN", type));
        BOOST_CHECK(!IsTokenNameValid("ABC~ANN_", type));
        BOOST_CHECK(!IsTokenNameValid("ABC~.ANN", type));
        BOOST_CHECK(!IsTokenNameValid("ABC~ANN.", type));
        BOOST_CHECK(!IsTokenNameValid("ABC~X__X", type));
        BOOST_CHECK(!IsTokenNameValid("ABC~X._X", type));
        BOOST_CHECK(!IsTokenNameValid("ABC~X_.X", type));
        BOOST_CHECK(!IsTokenNameValid("ABC~X..X", type));

        // owner
        BOOST_CHECK(IsTokenNameAnOwner("ABC!"));
        BOOST_CHECK(!IsTokenNameAnOwner("ABC"));
        BOOST_CHECK(!IsTokenNameAnOwner("ABC!COIN"));
        BOOST_CHECK(IsTokenNameAnOwner("MAX_TOKEN_IS_30_CHARACTERS_LNG!"));
        BOOST_CHECK(!IsTokenNameAnOwner("MAX_TOKEN_IS_31_CHARACTERS_LONG!"));
        BOOST_CHECK(IsTokenNameAnOwner("ABC/A!"));
        BOOST_CHECK(IsTokenNameAnOwner("ABC/A/1!"));
        BOOST_CHECK(IsTokenNameValid("ABC!", type));
        BOOST_CHECK(type == KnownTokenType::OWNER);

        // vote
        BOOST_CHECK(IsTokenNameValid("ABC^VOTE"));
        BOOST_CHECK(!IsTokenNameValid("ABC^"));
        BOOST_CHECK(IsTokenNameValid("ABC^VOTING"));
        BOOST_CHECK(IsTokenNameValid("ABC^VOTING_IS_30_CHARACTERS_LN"));
        BOOST_CHECK(!IsTokenNameValid("ABC^VOTING_IS_31_CHARACTERS_LN!"));
        BOOST_CHECK(IsTokenNameValid("ABC/SUB/SUB/SUB/SUB^VOTE"));
        BOOST_CHECK(IsTokenNameValid("ABC/SUB/SUB/SUB/SUB/SUB/30^VOT"));
        BOOST_CHECK(IsTokenNameValid("ABC/SUB/SUB/SUB/SUB/SUB/31^VOTE"));
        BOOST_CHECK(!IsTokenNameValid("ABC/SUB/SUB/SUB/SUB/SUB/32X^VOTE"));
        BOOST_CHECK(IsTokenNameValid("ABC/SUB/SUB^VOTE", type));
        BOOST_CHECK(type == KnownTokenType::VOTE);

        // Check type for different type of sub tokens
        BOOST_CHECK(IsTokenNameValid("TEST/UYTH#UNIQUE", type));
        BOOST_CHECK(type == KnownTokenType::UNIQUE);

        BOOST_CHECK(IsTokenNameValid("TEST/UYTH/SUB#UNIQUE", type));
        BOOST_CHECK(type == KnownTokenType::UNIQUE);

        BOOST_CHECK(IsTokenNameValid("TEST/UYTH/SUB~CHANNEL", type));
        BOOST_CHECK(type == KnownTokenType::MSGCHANNEL);

        BOOST_CHECK(!IsTokenNameValid("TEST/UYTH/SUB#UNIQUE^VOTE", type));
        BOOST_CHECK(!IsTokenNameValid("TEST/UYTH/SUB#UNIQUE#UNIQUE", type));
        BOOST_CHECK(!IsTokenNameValid("TEST/UYTH/SUB~CHANNEL^VOTE", type));
        BOOST_CHECK(!IsTokenNameValid("TEST/UYTH/SUB~CHANNEL^UNIQUE", type));
        BOOST_CHECK(!IsTokenNameValid("TEST/UYTH/SUB~CHANNEL!", type));
        BOOST_CHECK(!IsTokenNameValid("TEST/UYTH/SUB^VOTE!", type));

        // Check ParentName function
        BOOST_CHECK(GetParentName("TEST!") == "TEST!");
        BOOST_CHECK(GetParentName("TEST") == "TEST");
        BOOST_CHECK(GetParentName("TEST/SUB") == "TEST");
        BOOST_CHECK(GetParentName("TEST/SUB#UNIQUE") == "TEST/SUB");
        BOOST_CHECK(GetParentName("TEST/TEST/SUB/SUB") == "TEST/TEST/SUB");
        BOOST_CHECK(GetParentName("TEST/SUB^VOTE") == "TEST/SUB");
        BOOST_CHECK(GetParentName("TEST/SUB/SUB~CHANNEL") == "TEST/SUB/SUB");
        BOOST_CHECK(GetParentName("#TEST/#HELLO") == "#TEST");
        BOOST_CHECK(GetParentName("#TEST") == "#TEST");
        BOOST_CHECK(GetParentName("$RESTRICTED") == "$RESTRICTED");
        BOOST_CHECK(GetParentName("._INVALIDNAME") == "");

        // Qualifier
        BOOST_CHECK(IsTokenNameValid("#ABC"));
        BOOST_CHECK(IsTokenNameValid("#ABC_TEST"));
        BOOST_CHECK(IsTokenNameValid("#ABC.TEST"));
        BOOST_CHECK(IsTokenNameValid("#ABC_IS_31_CHARACTERS_LENGTH_31", type));
        BOOST_CHECK(type == KnownTokenType::QUALIFIER);
        BOOST_CHECK(!IsTokenNameValid("#ABC_IS_32_CHARACTERS_LEN_GTH_32"));
        BOOST_CHECK(!IsTokenNameValid("#ABC^"));
        BOOST_CHECK(!IsTokenNameValid("#ABC_.A"));
        BOOST_CHECK(!IsTokenNameValid("#A"));
        BOOST_CHECK(!IsTokenNameValid("#ABC!"));
        BOOST_CHECK(!IsTokenNameValid("#_ABC"));
        BOOST_CHECK(!IsTokenNameValid("#.ABC"));
        BOOST_CHECK(!IsTokenNameValid("#ABC_"));
        BOOST_CHECK(!IsTokenNameValid("#ABC."));


        // Sub Qualifier
        BOOST_CHECK(IsTokenNameValid("#ABC/#TESTING"));
        BOOST_CHECK(IsTokenNameValid("#ABC/#TESTING_THIS"));
        BOOST_CHECK(IsTokenNameValid("#ABC/#SUB_IS_31_CHARACTERS_LENG"));
        BOOST_CHECK(IsTokenNameValid("#ABC/#A", type));
        BOOST_CHECK(type == KnownTokenType::SUB_QUALIFIER);
        BOOST_CHECK(!IsTokenNameValid("#ABC/TEST_"));
        BOOST_CHECK(!IsTokenNameValid("#ABC/TEST."));
        BOOST_CHECK(!IsTokenNameValid("#ABC/TEST"));
        BOOST_CHECK(!IsTokenNameValid("#ABC/#SUB_IS_32_CHARACTERS_LEN32"));


        // Restricted
        BOOST_CHECK(IsTokenNameValid("$ABC"));
        BOOST_CHECK(IsTokenNameValid("$ABC_A"));
        BOOST_CHECK(IsTokenNameValid("$ABC_A"));
        BOOST_CHECK(IsTokenNameValid("$ABC_IS_30_CHARACTERS_LENGTH30", type));
        BOOST_CHECK(type == KnownTokenType::RESTRICTED);
        BOOST_CHECK(!IsTokenNameValid("$ABC_IS_32_CHARACTERSA_LENGTH_32"));
        BOOST_CHECK(!IsTokenNameValid("$ABC/$NO"));
        BOOST_CHECK(!IsTokenNameValid("$ABC/NO"));
        BOOST_CHECK(!IsTokenNameValid("$ABC/#NO"));
        BOOST_CHECK(!IsTokenNameValid("$ABC^NO"));
        BOOST_CHECK(!IsTokenNameValid("$ABC~#NO"));
        BOOST_CHECK(!IsTokenNameValid("$ABC#NO"));
    }

    BOOST_AUTO_TEST_CASE(transfer_token_coin_test)
    {
        BOOST_TEST_MESSAGE("Running Transfer Token Coin Test");

        SelectParams(CBaseChainParams::MAIN);

        // Create the token scriptPubKey
        CTokenTransfer token("PLB", 1000);
        CScript scriptPubKey = GetScriptForDestination(DecodeDestination(GetParams().GlobalFeeAddress()));
        token.ConstructTransaction(scriptPubKey);

        CTxOut txOut;
        txOut.nValue = 0;
        txOut.scriptPubKey = scriptPubKey;


        Coin coin(txOut, 0, 0);

        BOOST_CHECK_MESSAGE(coin.IsToken(), "Transfer Token Coin isn't as token");
    }

    BOOST_AUTO_TEST_CASE(new_token_coin_test)
    {
        BOOST_TEST_MESSAGE("Running Token Coin Test");

        SelectParams(CBaseChainParams::MAIN);

        // Create the token scriptPubKey
        CNewToken token("PLB", 1000, 8, 1, 0, "");
        CScript scriptPubKey = GetScriptForDestination(DecodeDestination(GetParams().GlobalFeeAddress()));
        token.ConstructTransaction(scriptPubKey);

        CTxOut txOut;
        txOut.nValue = 0;
        txOut.scriptPubKey = scriptPubKey;

        Coin coin(txOut, 0, 0);

        BOOST_CHECK_MESSAGE(coin.IsToken(), "New Token Coin isn't as token");
    }

    BOOST_AUTO_TEST_CASE(new_token_is_null_test)
    {
        BOOST_TEST_MESSAGE("Running Token Coin is Null Test");

        SelectParams(CBaseChainParams::MAIN);

        // Create the token scriptPubKey
        CNewToken token1("", 1000);

        BOOST_CHECK_MESSAGE(token1.IsNull(), "New Token isn't null when it should be");

        CNewToken token2("NOTNULL", 1000);

        BOOST_CHECK_MESSAGE(!token2.IsNull(), "New Token is null when it shouldn't be");
    }

    BOOST_AUTO_TEST_CASE(new_token_to_string_test)
    {
        BOOST_TEST_MESSAGE("Running Token To String test");

        std::string success_print = "Printing an token\n"
                                    "name : TOKEN\n"
                                    "amount : 1000\n"
                                    "units : 4\n"
                                    "reissuable : 1\n"
                                    "has_ipfs : 1\n"
                                    "ipfs_hash : QmTqu3Lk3gmTsQVtjU7rYYM37EAW4xNmbuEAp2Mjr4AV7E";



        SelectParams(CBaseChainParams::MAIN);

        // Create the token scriptPubKey
        CNewToken token("TOKEN", 1000, 4, 1, 1, "QmTqu3Lk3gmTsQVtjU7rYYM37EAW4xNmbuEAp2Mjr4AV7E");
        std::string strToken = token.ToString();

        BOOST_CHECK_MESSAGE(strToken == success_print, "Token to string failed check");
    }

    BOOST_AUTO_TEST_CASE(dwg_version_test)
    {
        BOOST_TEST_MESSAGE("Running DWG Version Test");

        int32_t version = 0x30000000;
        int32_t mask = 0xF0000000;
        int32_t new_version = version & mask;
        int32_t shifted = new_version >> 28;

        BOOST_CHECK_MESSAGE(shifted == 3, "New version didn't equal 3");
    }

    BOOST_AUTO_TEST_CASE(token_formatting_test)
    {
        BOOST_TEST_MESSAGE("Running Token Formatting Test");

        CAmount amount = 50000010000;
        BOOST_CHECK(ValueFromAmountString(amount, 4) == "500.0001");

        amount = 100;
        BOOST_CHECK(ValueFromAmountString(amount, 6) == "0.000001");

        amount = 1000;
        BOOST_CHECK(ValueFromAmountString(amount, 6) == "0.000010");

        amount = 50010101010;
        BOOST_CHECK(ValueFromAmountString(amount, 8) == "500.10101010");

        amount = 111111111;
        BOOST_CHECK(ValueFromAmountString(amount, 8) == "1.11111111");

        amount = 1;
        BOOST_CHECK(ValueFromAmountString(amount, 8) == "0.00000001");

        amount = 40000000;
        BOOST_CHECK(ValueFromAmountString(amount, 8) == "0.40000000");

    }

    BOOST_AUTO_TEST_CASE(boolean_expression_evaluator_test)
    {
        BOOST_TEST_MESSAGE("Running Boolean Expression Evaluator Test");

        LibBoolEE::Vals vals = { { "#KY_C", true }, { "#CI.A", false } };
        BOOST_CHECK(LibBoolEE::resolve("#KY_C & !#CI.A", vals));
        BOOST_CHECK_THROW(LibBoolEE::resolve("#KY_C|#MISS", vals), std::runtime_error);
        BOOST_CHECK_THROW(LibBoolEE::resolve("BAD -- EXPRESSION -- BUST", vals), std::runtime_error);

        //! Check for valid syntax
        std::string valid_verifier_syntax = "((#KYC & !#ABC) | #DEF & #GHI & #RET) | (#TEST)";

        std::set<std::string> setQualifiers;
        std::string stripped_valid_verifier_syntax  = GetStrippedVerifierString(valid_verifier_syntax);
        ExtractVerifierStringQualifiers(stripped_valid_verifier_syntax, setQualifiers);

        vals.clear();

        for (auto item : setQualifiers) {
            vals.insert(make_pair(item, true));
        }

        BOOST_CHECK(LibBoolEE::resolve(stripped_valid_verifier_syntax, vals));

        // Clear the vals, and insert them with false, this should make the resolve return false
        vals.clear();
        for (auto item : setQualifiers) {
            vals.insert(make_pair(item, false));
        }

        BOOST_CHECK(!LibBoolEE::resolve(stripped_valid_verifier_syntax, vals));

        vals.clear();
        setQualifiers.clear();

        //! Check for invalid syntax #DEF#XXX won't work
        std::string not_valid_qualifier = "((#KYC & !#ABC) | #DEF$XXX & #GHI & #RET)";
        std::string stripped_not_valid_qualifier  = GetStrippedVerifierString(not_valid_qualifier);
        ExtractVerifierStringQualifiers(stripped_not_valid_qualifier, setQualifiers);
        for (auto item : setQualifiers) {
            vals.insert(make_pair(item, true));
        }

        BOOST_CHECK_THROW(LibBoolEE::resolve(stripped_not_valid_qualifier, vals), std::runtime_error);

        vals.clear();
        setQualifiers.clear();

        //! Check for invalid syntax, missing a parenthesis '(' at the beginning
        std::string not_valid_missing_parenthesis = "(#KYC & !#ABC) | #DEF & #GHI & #RET)";
        std::string stripped_not_valid_missing_parenthesis  = GetStrippedVerifierString(not_valid_missing_parenthesis);
        ExtractVerifierStringQualifiers(stripped_not_valid_missing_parenthesis, setQualifiers);
        for (auto item : setQualifiers) {
            vals.insert(make_pair(item, true));
        }

        BOOST_CHECK_THROW(LibBoolEE::resolve(stripped_not_valid_missing_parenthesis, vals), std::runtime_error);

        vals.clear();
        setQualifiers.clear();

        //! Check for invalid syntax, has two & in a row
        std::string not_valid_double_and = "((#KYC && !#ABC) | #DEF & #GHI & #RET)";
        std::string stripped_not_valid_double_and = GetStrippedVerifierString(not_valid_double_and);
        ExtractVerifierStringQualifiers(stripped_not_valid_double_and, setQualifiers);
        for (auto item : setQualifiers) {
            vals.insert(make_pair(item, true));
        }

        BOOST_CHECK_THROW(LibBoolEE::resolve(stripped_not_valid_double_and, vals), std::runtime_error);

        vals.clear();
        setQualifiers.clear();

        //! Check for invalid syntax, has two | in a row
        std::string not_valid_double_or = "((#KYC & !#ABC) || #DEF & #GHI & #RET)";
        std::string stripped_not_valid_double_or = GetStrippedVerifierString(not_valid_double_or);
        ExtractVerifierStringQualifiers(stripped_not_valid_double_or, setQualifiers);
        for (auto item : setQualifiers) {
            vals.insert(make_pair(item, true));
        }

        BOOST_CHECK_THROW(LibBoolEE::resolve(stripped_not_valid_double_or, vals), std::runtime_error);

        vals.clear();
        setQualifiers.clear();

        //! Check for invalid syntax, has & | without a qualifier inbetween
        std::string not_valid_missing_qualifier = "((#KYC & | !#ABC) | #DEF & #GHI & #RET)";
        std::string stripped_not_valid_missing_qualifier = GetStrippedVerifierString(not_valid_missing_qualifier);
        ExtractVerifierStringQualifiers(stripped_not_valid_missing_qualifier, setQualifiers);
        for (auto item : setQualifiers) {
            vals.insert(make_pair(item, true));
        }

        BOOST_CHECK_THROW(LibBoolEE::resolve(stripped_not_valid_missing_qualifier, vals), std::runtime_error);

        vals.clear();
        setQualifiers.clear();

        //! Check for invalid syntax, has () without qualifier inside them
        std::string not_valid_open_close = "()((#KYC & !#ABC) | #DEF & #GHI & #RET)";

        std::string stripped_not_valid_open_close = GetStrippedVerifierString(not_valid_open_close);
        ExtractVerifierStringQualifiers(stripped_not_valid_open_close, setQualifiers);

        for (auto item : setQualifiers) {
            vals.insert(make_pair(item, true));
        }

        BOOST_CHECK_THROW(LibBoolEE::resolve(stripped_not_valid_open_close, vals), std::runtime_error);

        vals.clear();
        setQualifiers.clear();

        //! Check for invalid syntax, has (#YES) followed by no comparator
        std::string not_valid_open_close_no_comparator = "((#KYC & !#ABC) | (#YES) #DEF & #GHI & #RET)";
        std::string stripped_not_valid_open_close_no_comparator = GetStrippedVerifierString(not_valid_open_close_no_comparator);
        ExtractVerifierStringQualifiers(stripped_not_valid_open_close_no_comparator, setQualifiers);

        ExtractVerifierStringQualifiers(stripped_not_valid_open_close_no_comparator, setQualifiers);
        for (auto item : setQualifiers) {
            vals.insert(make_pair(item, true));
        }

        BOOST_CHECK_THROW(LibBoolEE::resolve(stripped_not_valid_open_close_no_comparator, vals), std::runtime_error);

        vals.clear();
        setQualifiers.clear();

        //! Check for invalid syntax, could return true on the #KYC, but is missing a ending parenthesis
        std::string bad_syntax_after_true_statement = "((#KYC) | #GHI";

        std::string stripped_bad_syntax_after_true_statement = GetStrippedVerifierString(bad_syntax_after_true_statement);
        ExtractVerifierStringQualifiers(stripped_bad_syntax_after_true_statement, setQualifiers);
        for (auto item : setQualifiers) {
            vals.insert(make_pair(item, true));
        }

        BOOST_CHECK_THROW(LibBoolEE::resolve(stripped_bad_syntax_after_true_statement, vals), std::runtime_error);
    }

    BOOST_AUTO_TEST_CASE(token_valid_check_tests)
    {
        BOOST_TEST_MESSAGE("Running Valid CheckNewToken Tests");

        std::string error = "";

        // Check all units
        for (int i = MIN_UNIT; i <= MAX_UNIT; i++) {
            CNewToken token_unit("VALID", 1000 * COIN, i, 0, 0, "");
            BOOST_CHECK_MESSAGE(CheckNewToken(token_unit, error), "CheckNewToken: Test Unit " + std::to_string(i) + " Failed - " + error);
        }

        // Check normal token creation
        CNewToken token1("VALID", 1000 * COIN);
        BOOST_CHECK_MESSAGE(CheckNewToken(token1, error), "CheckNewToken: Test 1 Failed - " + error);

        // Check message channel
        CNewToken message_channel("VALID~MSG_CHANNEL", 1 * COIN, MIN_UNIT, 0, 0, "");
        BOOST_CHECK_MESSAGE(CheckNewToken(message_channel, error), "CheckNewToken: Message Channel Test Failed - " + error);

        // Check qualifier
        CNewToken qualifier("#QUALIFIER", 1 * COIN, MIN_UNIT, 0, 0, "");
        BOOST_CHECK_MESSAGE(CheckNewToken(message_channel, error), "CheckNewToken: Qualifier Test Failed - " + error);

        // Check sub_qualifier
        CNewToken sub_qualifier("#QUALIFIER/#SUB", 1 * COIN, MIN_UNIT, 0, 0, "");
        BOOST_CHECK_MESSAGE(CheckNewToken(sub_qualifier, error), "CheckNewToken: Sub Qualifier Test Failed - " + error);

        // Check restricted
        CNewToken restricted_min_money("$RESTRICTED", 1 * COIN, MIN_UNIT, 0, 0, "");
        CNewToken restricted_max_money("$RESTRICTED", MAX_MONEY, MAX_UNIT, 0, 0, "");
        BOOST_CHECK_MESSAGE(CheckNewToken(restricted_min_money, error), "CheckNewToken: Restricted Min Money Test Failed - " + error);
        BOOST_CHECK_MESSAGE(CheckNewToken(restricted_max_money, error), "CheckNewToken: Restricted Max Money Test Failed - " + error);
    }

    BOOST_AUTO_TEST_CASE(token_invalid_check_tests)
    {
        BOOST_TEST_MESSAGE("Running Not Valid CheckNewToken Tests");

        std::string error = "";

        /// Generic Amount Tests ///
        {
            CNewToken invalid_amount_less_zero("INVALID", -1);
            CNewToken invalid_amount_over_max("INVALID", MAX_MONEY + 1);

            BOOST_CHECK_MESSAGE(!CheckNewToken(invalid_amount_less_zero, error), "CheckNewToken: Invalid Amount Test 1 should fail");
            BOOST_CHECK_MESSAGE(!CheckNewToken(invalid_amount_over_max, error), "CheckNewToken: Invalid Amount Test 2 should fail");
        }

        /// Generic Units Tests ///
        {
            // Check with invalid units (-1, an 9)
            CNewToken invalid_unit_1("INVALID", 1000 * COIN, -1, 0, 0, "");
            CNewToken invalid_unit_2("INVALID", 1000 * COIN, 9, 0, 0, "");
            BOOST_CHECK_MESSAGE(!CheckNewToken(invalid_unit_1, error), "CheckNewToken: Invalid Unit Test 1 should fail");
            BOOST_CHECK_MESSAGE(!CheckNewToken(invalid_unit_2, error), "CheckNewToken: Invalid Unit Test 2 should fail");
        }

        /// Generic Reissuable Flag Tests ///
        {
            // Check with invalid reissue flag
            CNewToken invalid_ressiue_1("INVALID", 1000 * COIN, MAX_UNIT, -1, 0, "");
            CNewToken invalid_ressiue_2("INVALID", 1000 * COIN, MAX_UNIT, 2, 0, "");
            BOOST_CHECK_MESSAGE(!CheckNewToken(invalid_ressiue_1, error), "CheckNewToken: Invalid Reissue Test 1 should fail");
            BOOST_CHECK_MESSAGE(!CheckNewToken(invalid_ressiue_2, error), "CheckNewToken: Invalid Reissue Test 2 should fail");
        }

        /// Generic IPFS Flag Tests ///
        {
            // Check with invalid ipfs flag
            CNewToken invalid_ipfsflag_1("INVALID", 1000 * COIN, MAX_UNIT, 0, -1, "");
            CNewToken invalid_ipfsflag_2("INVALID", 1000 * COIN, MAX_UNIT, 0, 2, "");
            BOOST_CHECK_MESSAGE(!CheckNewToken(invalid_ipfsflag_1, error), "CheckNewToken: Invalid Ipfs Flag Test 1 should fail");
            BOOST_CHECK_MESSAGE(!CheckNewToken(invalid_ipfsflag_2, error), "CheckNewToken: Invalid Ipfs Flag Test 2 should fail");
        }

        /// Message Channel Tests ///
        {
            // Check that units must be zero for message channels
            CNewToken invalid_channel_units("INVALID~CHANNEL", 1 * COIN, MAX_UNIT, 0, 0, "");
            BOOST_CHECK_MESSAGE(!CheckNewToken(invalid_channel_units, error), "CheckNewToken: Invalid Channel Units Test should fail");

            // Check that the amount can't be bigger than 1 * COIN
            CNewToken invalid_channel_amount("INVALID~CHANNEL", 2 * COIN, MIN_UNIT, 0, 0, "");
            BOOST_CHECK_MESSAGE(!CheckNewToken(invalid_channel_amount, error), "CheckNewToken: Invalid Channel Amount Test should fail");

            // Check that reissue flag must be 0
            CNewToken invalid_channel_resissue_flag("INVALID~CHANNEL", 1 * COIN, MIN_UNIT, 1, 0, "");
            BOOST_CHECK_MESSAGE(!CheckNewToken(invalid_channel_resissue_flag, error), "CheckNewToken: Invalid Channel Reissue Flag Test should fail");
        }

        /// Unique Tests ///
        {
            // Check that units must be zero for message channels
            CNewToken invalid_unique_units("TEST#INVALID_UNIQUE", 1 * COIN, MAX_UNIT, 0, 0, "");
            BOOST_CHECK_MESSAGE(!CheckNewToken(invalid_unique_units, error), "CheckNewToken: Invalid Unique Units Test should fail");

            // Check that the amount can't be bigger than 1 * COIN
            CNewToken invalid_unique_amount("TEST#INVALID_UNIQUE", 2 * COIN, MIN_UNIT, 0, 0, "");
            BOOST_CHECK_MESSAGE(!CheckNewToken(invalid_unique_amount, error), "CheckNewToken: Invalid Unique Amount Test should fail");

            // Check that reissue flag must be 0
            CNewToken invalid_unique_resissue_flag("TEST#INVALID_UNIQUE", 1 * COIN, MIN_UNIT, 1, 0, "");
            BOOST_CHECK_MESSAGE(!CheckNewToken(invalid_unique_resissue_flag, error), "CheckNewToken: Invalid Unique Reissue Flag Test should fail");
        }

        /// Qualifier Tests ///
        {
            // Check that units must be zero for message channels
            CNewToken invalid_qualifier_units("#INVALID_QUALIFIER", 1 * COIN, MAX_UNIT, 0, 0, "");
            BOOST_CHECK_MESSAGE(!CheckNewToken(invalid_qualifier_units, error), "CheckNewToken: Invalid Qualifier Units Test should fail");

            // Check that the amount can't be bigger than 1 * COIN
            CNewToken invalid_qualifier_amount("#INVALID_QUALIFIER", 11 * COIN, MIN_UNIT, 0, 0, "");
            BOOST_CHECK_MESSAGE(!CheckNewToken(invalid_qualifier_amount, error), "CheckNewToken: Invalid Qualifier Amount Test should fail");

            // Check that reissue flag must be 0
            CNewToken invalid_qualifier_resissue_flag("#INVALID_QUALIFIER", 1 * COIN, MIN_UNIT, 1, 0, "");
            BOOST_CHECK_MESSAGE(!CheckNewToken(invalid_qualifier_resissue_flag, error), "CheckNewToken: Invalid Qualifier Reissue Flag Test should fail");
        }

        /// Sub Qualifier Tests ///
        {
            // Check that units must be zero for message channels
            CNewToken invalid__subqualifier_units("#INVALID/#SUBQUALIFIER", 1 * COIN, MAX_UNIT, 0, 0, "");
            BOOST_CHECK_MESSAGE(!CheckNewToken(invalid__subqualifier_units, error), "CheckNewToken: Invalid Sub Qualifier Units Test should fail");

            // Check that the amount can't be bigger than 1 * COIN
            CNewToken invalid_subqualifier_amount("#INVALID/#SUBQUALIFIER", 11 * COIN, MIN_UNIT, 0, 0, "");
            BOOST_CHECK_MESSAGE(!CheckNewToken(invalid_subqualifier_amount, error), "CheckNewToken: Invalid Sub Qualifier Amount Test should fail");

            // Check that reissue flag must be 0
            CNewToken invalid_subqualifier_resissue_flag("#INVALID/#SUBQUALIFIER", 1 * COIN, MIN_UNIT, 1, 0, "");
            BOOST_CHECK_MESSAGE(!CheckNewToken(invalid_subqualifier_resissue_flag, error), "CheckNewToken: Invalid Sub Qualifier Reissue Flag Test should fail");
        }
    }

    BOOST_AUTO_TEST_CASE(reissue_token_valid_check_tests)
    {
        BOOST_TEST_MESSAGE("Running Valid CheckReissueToken Tests");

        std::string error = "";

        /// Generic Amount Tests ///
        {
            CReissueToken valid_amount_1("VALID", 1 * COIN, -1, 1, "");
            CReissueToken valid_amount_2("INVALID", MAX_MONEY - 1, -1, 1, "");

            BOOST_CHECK_MESSAGE(CheckReissueToken(valid_amount_1, error), "CheckReissueToken: Valid Amount Test 1 failed - " + error);
            BOOST_CHECK_MESSAGE(CheckReissueToken(valid_amount_2, error), "CheckReissueToken: Valid Amount Test 2 failed - " + error);
        }

        /// Generic Units Tests ///
        {
            // Check all units (-1 -> 8)
            for (int i = -1; i <= MAX_UNIT; i++) {
                CReissueToken reissue_unit("VALID", 1000 * COIN, i, 0, "");
                BOOST_CHECK_MESSAGE(CheckReissueToken(reissue_unit, error), "CheckReissueToken: Test Unit " + std::to_string(i) + " Failed - " + error);
            }
        }

        /// Generic Reissuable Flag Tests ///
        {
            // Check with invalid reissue flag
            CReissueToken valid_ressiue_1("VALID", 1000 * COIN, MAX_UNIT, 1, "");
            CReissueToken valid_ressiue_2("VALID", 1000 * COIN, MAX_UNIT, 0, "");
            BOOST_CHECK_MESSAGE(CheckReissueToken(valid_ressiue_1, error), "CheckReissueToken: Valid Reissue Test 1 failed - " + error);
            BOOST_CHECK_MESSAGE(CheckReissueToken(valid_ressiue_2, error), "CheckReissueToken: Valid Reissue Test 2 failed - " + error);
        }
    }

    BOOST_AUTO_TEST_CASE(reissue_token_invalid_check_tests)
    {
        BOOST_TEST_MESSAGE("Running Not Valid CheckReissueToken Tests");

        std::string error = "";

        /// Generic Amount Tests ///
        {
            CReissueToken invalid_amount_less_zero("INVALID", -1, -1, 1, "");
            CReissueToken invalid_amount_over_max("INVALID", MAX_MONEY, -1, 1, "");

            BOOST_CHECK_MESSAGE(!CheckReissueToken(invalid_amount_less_zero, error), "CheckReissueToken: Invalid Amount Test 1 should fail");
            BOOST_CHECK_MESSAGE(!CheckReissueToken(invalid_amount_over_max, error), "CheckReissueToken: Invalid Amount Test 2 should fail");
        }

        /// Generic Units Tests ///
        {
            // Check with invalid units (-1, an 9)
            CReissueToken invalid_unit_1("INVALID", 1000 * COIN, -2, 0, "");
            CReissueToken invalid_unit_2("INVALID", 1000 * COIN, 9, 0, "");
            BOOST_CHECK_MESSAGE(!CheckReissueToken(invalid_unit_1, error), "CheckReissueToken: Invalid Unit Test 1 should fail");
            BOOST_CHECK_MESSAGE(!CheckReissueToken(invalid_unit_2, error), "CheckReissueToken: Invalid Unit Test 2 should fail");
        }

        /// Generic Reissuable Flag Tests ///
        {
            // Check with invalid reissue flag
            CReissueToken invalid_ressiue_1("INVALID", 1000 * COIN, MAX_UNIT, -1, "");
            CReissueToken invalid_ressiue_2("INVALID", 1000 * COIN, MAX_UNIT, 2, "");
            BOOST_CHECK_MESSAGE(!CheckReissueToken(invalid_ressiue_1, error), "CheckReissueToken: Invalid Reissue Test 1 should fail");
            BOOST_CHECK_MESSAGE(!CheckReissueToken(invalid_ressiue_2, error), "CheckReissueToken: Invalid Reissue Test 2 should fail");
        }
    }

    BOOST_AUTO_TEST_CASE(tag_address_burn_check)
    {
        BOOST_TEST_MESSAGE("Tag Address Burn Check");

        SelectParams(CBaseChainParams::MAIN);
        std::string error = "";

        // Create mutable transaction
        CMutableTransaction muttx;

        // Create the script for addinga  tag to an address
        CNullTokenTxData addTagData("#TAG", 1);
        CScript addTagScript = GetScriptForDestination(DecodeDestination(GetParams().GlobalFeeAddress()));
        addTagData.ConstructTransaction(addTagScript);

        // Create the txOut and add it to the mutable transaction
        CTxOut txOut(0, addTagScript);
        muttx.vout.push_back(txOut);

        // Check without burn fee added
        CTransaction txNoFee(muttx);
        BOOST_CHECK_MESSAGE(!txNoFee.CheckAddingTagBurnFee(1), "CheckAddingTagBurnFee: Test 1 Didn't fail with no burn fee");

        // Create the script that adds the correct burn fee
        CScript addTagBurnFeeScript = GetScriptForDestination(DecodeDestination(GetBurnAddress(KnownTokenType::NULL_ADD_QUALIFIER)));
        CTxOut txBurnFee(GetBurnAmount(KnownTokenType::NULL_ADD_QUALIFIER), addTagBurnFeeScript);
        muttx.vout.push_back(txBurnFee);

        // Check with burn fee added
        CTransaction txWithFee(muttx);
        BOOST_CHECK_MESSAGE(txWithFee.CheckAddingTagBurnFee(1), "CheckAddingTagBurnFee: Test 2 Failed with the correct burn tx added");

        // Create the script that adds the burn fee twice
        muttx.vout.pop_back();
        CScript addDoubleTagBurnFeeScript = GetScriptForDestination(DecodeDestination(GetBurnAddress(KnownTokenType::NULL_ADD_QUALIFIER)));
        CTxOut txDoubleBurnFee(GetBurnAmount(KnownTokenType::NULL_ADD_QUALIFIER) * 2, addTagBurnFeeScript);
        muttx.vout.push_back(txDoubleBurnFee);

        // Check with double burn fee added
        CTransaction txWithDoubleFee(muttx);
        BOOST_CHECK_MESSAGE(!txWithDoubleFee.CheckAddingTagBurnFee(1), "CheckAddingTagBurnFee: Test 3 Didn't fail with double burn fee");
    }


BOOST_AUTO_TEST_SUITE_END()
