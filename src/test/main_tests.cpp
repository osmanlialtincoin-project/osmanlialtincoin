// Copyright (c) 2014-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <validation.h>
#include <net.h>

#include <test/test_bitcoin.h>

#include <boost/signals2/signal.hpp>
#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(main_tests, TestingSetup)

static void TestBlockSubsidyHalvings(const Consensus::Params& consensusParams)
{
    int maxHalvings = 64;
    CAmount nInitialSubsidy = 100 * COIN;

    CAmount nPreviousSubsidy = nInitialSubsidy * 2; // for height == 0
    BOOST_CHECK_EQUAL(nPreviousSubsidy, nInitialSubsidy * 2);
    for (int nHalvings = 0; nHalvings < maxHalvings; nHalvings++) {
        int nHeight = nHalvings * consensusParams.nSubsidyHalvingInterval;
        CAmount nSubsidy = GetBlockSubsidy(nHeight, consensusParams);
        BOOST_CHECK(nSubsidy <= nInitialSubsidy);
        BOOST_CHECK_EQUAL(nSubsidy, nPreviousSubsidy / 2);
        nPreviousSubsidy = nSubsidy;
    }
    BOOST_CHECK_EQUAL(GetBlockSubsidy(maxHalvings * consensusParams.nSubsidyHalvingInterval, consensusParams), 0);
}

static void TestBlockSubsidyHalvings(int nSubsidyHalvingInterval)
{
    Consensus::Params consensusParams;
    consensusParams.nSubsidyHalvingInterval = nSubsidyHalvingInterval;
    TestBlockSubsidyHalvings(consensusParams);
}

BOOST_AUTO_TEST_CASE(block_subsidy_test)
{
    // Short intervals never reach the supply cap, so every halving is visible.
    TestBlockSubsidyHalvings(150); // As in regtest
    TestBlockSubsidyHalvings(1000); // Just another interval

    const auto chainParams = CreateChainParams(CBaseChainParams::MAIN);
    const Consensus::Params& params = chainParams->GetConsensus();
    BOOST_CHECK_EQUAL(GetBlockSubsidy(1, params), 8400000 * COIN);
    BOOST_CHECK_EQUAL(GetBlockSubsidy(2, params), 100 * COIN);
    BOOST_CHECK_EQUAL(GetBlockSubsidy(419999, params), 100 * COIN);
    BOOST_CHECK_EQUAL(GetBlockSubsidy(420000, params), 50 * COIN);
    BOOST_CHECK_EQUAL(GetBlockSubsidy(840000, params), 25 * COIN);
    BOOST_CHECK_EQUAL(GetBlockSubsidy(1260000, params), 1250000000);
    BOOST_CHECK_EQUAL(GetBlockSubsidy(1428015, params), 1250000000); // last block with a subsidy
    BOOST_CHECK_EQUAL(GetBlockSubsidy(1428016, params), 0);
    BOOST_CHECK_EQUAL(GetBlockSubsidy(1680000, params), 0);
}

BOOST_AUTO_TEST_CASE(subsidy_limit_test)
{
    const auto chainParams = CreateChainParams(CBaseChainParams::MAIN);
    const Consensus::Params& params = chainParams->GetConsensus();
    const int nMaxHeight = 64 * params.nSubsidyHalvingInterval;
    const int nCapHeight = 1428015;
    CAmount nSum = 0;
    for (int nHeight = 1; nHeight <= nMaxHeight; nHeight++) {
        CAmount nSubsidy = GetBlockSubsidy(nHeight, params);
        if (nHeight < nCapHeight) {
            // Rewards before the cap are unchanged from the original schedule.
            CAmount nOriginal = (nHeight == 1 ? 8400000 * COIN : 100 * COIN) >> (nHeight / params.nSubsidyHalvingInterval);
            BOOST_CHECK_EQUAL(nSubsidy, nOriginal);
        }
        if (nHeight > nCapHeight) {
            BOOST_CHECK_EQUAL(nSubsidy, 0);
            break;
        }
        nSum += nSubsidy;
        BOOST_CHECK(MoneyRange(nSum));
    }
    BOOST_CHECK_EQUAL(nSum, MAX_MONEY);
    for (int nHeight = nCapHeight + 1; nHeight <= nMaxHeight; nHeight += 1000) {
        BOOST_CHECK_EQUAL(GetBlockSubsidy(nHeight, params), 0);
    }
}

static bool ReturnFalse() { return false; }
static bool ReturnTrue() { return true; }

BOOST_AUTO_TEST_CASE(test_combiner_all)
{
    boost::signals2::signal<bool (), CombinerAll> Test;
    BOOST_CHECK(Test());
    Test.connect(&ReturnFalse);
    BOOST_CHECK(!Test());
    Test.connect(&ReturnTrue);
    BOOST_CHECK(!Test());
    Test.disconnect(&ReturnFalse);
    BOOST_CHECK(Test());
    Test.disconnect(&ReturnTrue);
    BOOST_CHECK(Test());
}
BOOST_AUTO_TEST_SUITE_END()
