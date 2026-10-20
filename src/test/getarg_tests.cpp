// Copyright (c) 2012-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <util/strencodings.h>
#include <util/system.h>
#include <test/test_bitcoin.h>

#include <string>
#include <sstream>
#include <vector>

#include <boost/algorithm/string.hpp>
#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(getarg_tests, BasicTestingSetup)

static void ResetArgs(const std::string& strArg)
{
    std::vector<std::string> vecArg;
    if (strArg.size())
      boost::split(vecArg, strArg, IsSpace, boost::token_compress_on);

    // Insert dummy executable name:
    vecArg.insert(vecArg.begin(), "testosmanli_altin_coin");

    // Convert to char*:
    std::vector<const char*> vecChar;
    for (const std::string& s : vecArg)
        vecChar.push_back(s.c_str());

    std::string error;
    BOOST_CHECK(gArgs.ParseParameters(vecChar.size(), vecChar.data(), error));
}

static void SetupArgs(const std::vector<std::string>& args)
{
    gArgs.ClearArgs();
    for (const std::string& arg : args) {
        gArgs.AddArg(arg, "", false, OptionsCategory::OPTIONS);
    }
}

BOOST_AUTO_TEST_CASE(boolarg)
{
    SetupArgs({"-foo"});
    ResetArgs("-foo");
    BOOST_CHECK(gArgs.GetBoolArg("-foo", false));
    BOOST_CHECK(gArgs.GetBoolArg("-foo", true));

    BOOST_CHECK(!gArgs.GetBoolArg("-fo", false));
    BOOST_CHECK(gArgs.GetBoolArg("-fo", true));

    BOOST_CHECK(!gArgs.GetBoolArg("-fooo", false));
    BOOST_CHECK(gArgs.GetBoolArg("-fooo", true));

    ResetArgs("-foo=0");
    BOOST_CHECK(!gArgs.GetBoolArg("-foo", false));
    BOOST_CHECK(!gArgs.GetBoolArg("-foo", true));

    ResetArgs("-foo=1");
    BOOST_CHECK(gArgs.GetBoolArg("-foo", false));
    BOOST_CHECK(gArgs.GetBoolArg("-foo", true));

    // New 0.6 feature: auto-map -nosomething to !-something:
    ResetArgs("-nofoo");
    BOOST_CHECK(!gArgs.GetBoolArg("-foo", false));
    BOOST_CHECK(!gArgs.GetBoolArg("-foo", true));

    ResetArgs("-nofoo=1");
    BOOST_CHECK(!gArgs.GetBoolArg("-foo", false));
    BOOST_CHECK(!gArgs.GetBoolArg("-foo", true));

    ResetArgs("-foo -nofoo");  // -nofoo should win
    BOOST_CHECK(!gArgs.GetBoolArg("-foo", false));
    BOOST_CHECK(!gArgs.GetBoolArg("-foo", true));

    ResetArgs("-foo=1 -nofoo=1");  // -nofoo should win
    BOOST_CHECK(!gArgs.GetBoolArg("-foo", false));
    BOOST_CHECK(!gArgs.GetBoolArg("-foo", true));

    ResetArgs("-foo=0 -nofoo=0");  // -nofoo=0 should win
    BOOST_CHECK(gArgs.GetBoolArg("-foo", false));
    BOOST_CHECK(gArgs.GetBoolArg("-foo", true));

    // New 0.6 feature: treat -- same as -:
    ResetArgs("--foo=1");
    BOOST_CHECK(gArgs.GetBoolArg("-foo", false));
    BOOST_CHECK(gArgs.GetBoolArg("-foo", true));

    ResetArgs("--nofoo=1");
    BOOST_CHECK(!gArgs.GetBoolArg("-foo", false));
    BOOST_CHECK(!gArgs.GetBoolArg("-foo", true));

}

BOOST_AUTO_TEST_CASE(stringarg)
{
    SetupArgs({"-foo", "-bar"});
    ResetArgs("");
    BOOST_CHECK_EQUAL(gArgs.GetArg("-foo", ""), "");
    BOOST_CHECK_EQUAL(gArgs.GetArg("-foo", "eleven"), "eleven");

    ResetArgs("-foo -bar");
    BOOST_CHECK_EQUAL(gArgs.GetArg("-foo", ""), "");
    BOOST_CHECK_EQUAL(gArgs.GetArg("-foo", "eleven"), "");

    ResetArgs("-foo=");
    BOOST_CHECK_EQUAL(gArgs.GetArg("-foo", ""), "");
    BOOST_CHECK_EQUAL(gArgs.GetArg("-foo", "eleven"), "");

    ResetArgs("-foo=11");
    BOOST_CHECK_EQUAL(gArgs.GetArg("-foo", ""), "11");
    BOOST_CHECK_EQUAL(gArgs.GetArg("-foo", "eleven"), "11");

    ResetArgs("-foo=eleven");
    BOOST_CHECK_EQUAL(gArgs.GetArg("-foo", ""), "eleven");
    BOOST_CHECK_EQUAL(gArgs.GetArg("-foo", "eleven"), "eleven");

}

BOOST_AUTO_TEST_CASE(intarg)
{
    SetupArgs({"-foo", "-bar"});
    ResetArgs("");
    BOOST_CHECK_EQUAL(gArgs.GetArg("-foo", 11), 11);
    BOOST_CHECK_EQUAL(gArgs.GetArg("-foo", 0), 0);

    ResetArgs("-foo -bar");
    BOOST_CHECK_EQUAL(gArgs.GetArg("-foo", 11), 0);
    BOOST_CHECK_EQUAL(gArgs.GetArg("-bar", 11), 0);

    ResetArgs("-foo=11 -bar=12");
    BOOST_CHECK_EQUAL(gArgs.GetArg("-foo", 0), 11);
    BOOST_CHECK_EQUAL(gArgs.GetArg("-bar", 11), 12);

    ResetArgs("-foo=NaN -bar=NotANumber");
    BOOST_CHECK_EQUAL(gArgs.GetArg("-foo", 1), 0);
    BOOST_CHECK_EQUAL(gArgs.GetArg("-bar", 11), 0);
}

BOOST_AUTO_TEST_CASE(doubledash)
{
    SetupArgs({"-foo", "-bar"});
    ResetArgs("--foo");
    BOOST_CHECK_EQUAL(gArgs.GetBoolArg("-foo", false), true);

    ResetArgs("--foo=verbose --bar=1");
    BOOST_CHECK_EQUAL(gArgs.GetArg("-foo", ""), "verbose");
    BOOST_CHECK_EQUAL(gArgs.GetArg("-bar", 0), 1);
}

BOOST_AUTO_TEST_CASE(boolargno)
{
    SetupArgs({"-foo", "-bar"});
    ResetArgs("-nofoo");
    BOOST_CHECK(!gArgs.GetBoolArg("-foo", true));
    BOOST_CHECK(!gArgs.GetBoolArg("-foo", false));

    ResetArgs("-nofoo=1");
    BOOST_CHECK(!gArgs.GetBoolArg("-foo", true));
    BOOST_CHECK(!gArgs.GetBoolArg("-foo", false));

    ResetArgs("-nofoo=0");
    BOOST_CHECK(gArgs.GetBoolArg("-foo", true));
    BOOST_CHECK(gArgs.GetBoolArg("-foo", false));

    ResetArgs("-foo --nofoo"); // --nofoo should win
    BOOST_CHECK(!gArgs.GetBoolArg("-foo", true));
    BOOST_CHECK(!gArgs.GetBoolArg("-foo", false));

    ResetArgs("-nofoo -foo"); // foo always wins:
    BOOST_CHECK(gArgs.GetBoolArg("-foo", true));
    BOOST_CHECK(gArgs.GetBoolArg("-foo", false));
}

BOOST_AUTO_TEST_CASE(mining_secret_arguments_do_not_echo_values)
{
    const std::string secret = "TEST_ONLY_SECRET_MUST_NOT_APPEAR";
    for (const std::string option : {"-miningprivkey", "--miningprivkey", "-nominingprivkey", "-main.nominingprivkey", "-regtest.miningprivkey"}) {
        ArgsManager args;
        args.AddArg("-miningprivkey", "", false, OptionsCategory::OPTIONS);
        const std::string argument = option + "=" + secret;
        const char* argv[] = {"test", argument.c_str()};
        std::string error;
        BOOST_CHECK(!args.ParseParameters(2, argv, error));
        BOOST_CHECK(!error.empty());
        BOOST_CHECK(error.find(secret) == std::string::npos);
    }
}

BOOST_AUTO_TEST_CASE(mining_secret_config_errors_do_not_echo_values)
{
    struct ConfigArgs : ArgsManager { using ArgsManager::ReadConfigStream; };
    const std::string secret = "TEST_ONLY_SECRET_MUST_NOT_APPEAR";
    for (const std::string text : {
        "nominingprivkey=" + secret,
        "main.nominingprivkey=" + secret,
        "[regtest]\nnominingprivkey=" + secret,
        "-miningprivkey=" + secret,
        "miningprivkey " + secret,
        "MININGPRIVKEY " + secret,
        "MiningPrivKey=" + secret,
        "miningprivkey " + secret + "=value",
        "[miningprivkey " + secret + "]"}) {
        ConfigArgs args;
        args.AddArg("-miningprivkey", "", false, OptionsCategory::OPTIONS);
        std::istringstream config(text);
        std::string error;
        BOOST_CHECK(!args.ReadConfigStream(config, error));
        BOOST_CHECK(!error.empty());
        BOOST_CHECK(error.find(secret) == std::string::npos);
    }
    for (const std::string text : {"regtest.miningprivkey=" + secret, "[regtest]\nminingprivkey=" + secret}) {
        ConfigArgs args;
        args.AddArg("-miningprivkey", "", false, OptionsCategory::OPTIONS);
        args.SelectConfigNetwork("regtest");
        std::istringstream config(text);
        std::string error;
        BOOST_REQUIRE(args.ReadConfigStream(config, error));
        BOOST_CHECK_EQUAL(args.GetArg("-miningprivkey", ""), secret);
    }
}

BOOST_AUTO_TEST_SUITE_END()
