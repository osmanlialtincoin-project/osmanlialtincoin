// Copyright (c) 2026 The Osmanli Altin Coin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <consensus/validation.h>
#include <key.h>
#include <key_io.h>
#include <mining_auth.h>
#include <primitives/block.h>
#include <streams.h>
#include <util/strencodings.h>
#include <test/test_bitcoin.h>
#include <boost/test/unit_test.hpp>

namespace {
CKey TestKey(unsigned char scalar = 1)
{
    unsigned char bytes[32] = {};
    bytes[31] = scalar;
    CKey key;
    key.Set(bytes, bytes + 32, true);
    return key;
}
Consensus::Params TestConsensus()
{
    auto params = Params().GetConsensus();
    params.nSecureMiningHeight = 505400;
    const auto pubkey = TestKey().GetPubKey();
    params.miningAuthPubKey.assign(pubkey.begin(), pubkey.end());
    return params;
}
bool Authorized(const CBlock& block, const Consensus::Params& params, int height)
{
    CValidationState state;
    return CheckBlockMiningAuth(block, state, params, height);
}
}

BOOST_FIXTURE_TEST_SUITE(mining_auth_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(exact_activation_boundary)
{
    const auto params = TestConsensus();
    CBlock block;
    BOOST_CHECK(Authorized(block, params, 505399));
    BOOST_CHECK(!Authorized(block, params, 505400));
    BOOST_CHECK(!Authorized(block, params, 505401));
    BOOST_CHECK(!IsSecureMiningActive(505399, params));
    BOOST_CHECK(IsSecureMiningActive(505400, params));
    BOOST_CHECK_EQUAL(CreateChainParams(CBaseChainParams::MAIN)->GetConsensus().nSecureMiningHeight, 505400);
}

BOOST_AUTO_TEST_CASE(signature_binds_header_height_and_network)
{
    auto params = TestConsensus();
    CBlock block;
    block.nVersion = 1;
    block.nTime = 123456;
    const auto hash = block.GetHash();
    const auto pow = block.GetPoWHash();
    BOOST_REQUIRE(SignBlockMiningAuth(block, params, 505400, TestKey()));
    BOOST_CHECK(Authorized(block, params, 505400));
    BOOST_CHECK(block.GetHash() == hash);
    BOOST_CHECK(block.GetPoWHash() == pow);
    BOOST_CHECK(!Authorized(block, params, 505401));
    auto foreign = params;
    foreign.hashGenesisBlock = uint256S("01");
    BOOST_CHECK(!Authorized(block, foreign, 505400));
    for (int field = 0; field < 5; ++field) {
        auto changed = block;
        if (field == 0) ++changed.nNonce;
        if (field == 1) ++changed.nTime;
        if (field == 2) ++changed.nVersion;
        if (field == 3) changed.hashPrevBlock = uint256S("02");
        if (field == 4) changed.hashMerkleRoot = uint256S("03");
        BOOST_CHECK(!Authorized(changed, params, 505400));
    }
}

BOOST_AUTO_TEST_CASE(invalid_signature_is_recoverable_even_when_checked)
{
    const auto params = TestConsensus();
    CBlock block;
    BOOST_REQUIRE(SignBlockMiningAuth(block, params, 505400, TestKey()));
    const auto signature = block.vchMiningSignature;
    block.fChecked = true;
    block.vchMiningSignature.back() ^= 1;
    CValidationState state;
    BOOST_CHECK(!CheckBlockMiningAuth(block, state, params, 505400));
    BOOST_CHECK(state.CorruptionPossible());
    block.vchMiningSignature = signature;
    BOOST_CHECK(Authorized(block, params, 505400));
    block.vchMiningSignature.push_back(0);
    BOOST_CHECK(!Authorized(block, params, 505400));
    block.vchMiningSignature.assign(73, 0);
    BOOST_CHECK(!Authorized(block, params, 505400));
}

BOOST_AUTO_TEST_CASE(reject_high_s_and_wrong_authority)
{
    const auto params = TestConsensus();
    CBlock block;
    BOOST_REQUIRE(SignBlockMiningAuth(block, params, 505400, TestKey()));
    const auto signature = block.vchMiningSignature;
    const size_t r_size = signature[3];
    const size_t s_size = signature[5 + r_size];
    std::vector<unsigned char> low_s(32, 0);
    std::copy(signature.end() - s_size, signature.end(), low_s.end() - s_size);
    auto high_s = ParseHex("fffffffffffffffffffffffffffffffebaaedce6af48a03bbfd25e8cd0364141");
    int borrow = 0;
    for (int i = 31; i >= 0; --i) {
        const int difference = high_s[i] - low_s[i] - borrow;
        high_s[i] = difference & 255;
        borrow = difference < 0;
    }
    block.vchMiningSignature.assign(signature.begin(), signature.begin() + 4 + r_size);
    block.vchMiningSignature.push_back(2);
    block.vchMiningSignature.push_back(33);
    block.vchMiningSignature.push_back(0);
    block.vchMiningSignature.insert(block.vchMiningSignature.end(), high_s.begin(), high_s.end());
    block.vchMiningSignature[1] = block.vchMiningSignature.size() - 2;
    BOOST_CHECK(!Authorized(block, params, 505400));
    block.vchMiningSignature = signature;
    auto wrong = params;
    const auto pubkey = TestKey(2).GetPubKey();
    wrong.miningAuthPubKey.assign(pubkey.begin(), pubkey.end());
    BOOST_CHECK(!Authorized(block, wrong, 505400));
}

BOOST_AUTO_TEST_CASE(legacy_and_signed_record_roundtrip)
{
    const auto params = TestConsensus();
    CBlock block;
    CDataStream legacy(SER_NETWORK, PROTOCOL_VERSION);
    legacy << block;
    const auto legacy_size = legacy.size();
    CBlock decoded;
    ReadBlockWithAuth(legacy, decoded);
    BOOST_CHECK(decoded.vchMiningSignature.empty());
    BOOST_REQUIRE(SignBlockMiningAuth(block, params, 505400, TestKey()));
    CDataStream signed_stream(SER_NETWORK, PROTOCOL_VERSION);
    signed_stream << block;
    BOOST_CHECK_EQUAL(signed_stream.size(), legacy_size + 5 + block.vchMiningSignature.size());
    ReadBlockWithAuth(signed_stream, decoded);
    BOOST_CHECK(decoded.vchMiningSignature == block.vchMiningSignature);
    BOOST_CHECK(Authorized(decoded, params, 505400));
}

BOOST_AUTO_TEST_CASE(signature_length_does_not_change_consensus_weight)
{
    const auto params = TestConsensus();
    const auto key = TestKey();
    CBlock block;
    const auto legacy_weight = GetBlockWeight(block);
    BOOST_REQUIRE(SignBlockMiningAuth(block, params, 505400, key));
    const auto fixed_weight = GetBlockWeight(block);
    BOOST_CHECK_EQUAL(fixed_weight, legacy_weight + MAX_MINING_AUTH_TRAILER_SIZE * WITNESS_SCALE_FACTOR);
    const auto first_size = GetSerializeSize(block, PROTOCOL_VERSION);
    bool different_length = false;
    for (uint32_t nonce = 1; nonce < 100; ++nonce) {
        BOOST_REQUIRE(key.Sign(GetMiningAuthHash(block, params, 505400), block.vchMiningSignature, false, nonce));
        BOOST_REQUIRE(Authorized(block, params, 505400));
        BOOST_CHECK_EQUAL(GetBlockWeight(block), fixed_weight);
        if (GetSerializeSize(block, PROTOCOL_VERSION) != first_size) {
            different_length = true;
            break;
        }
    }
    BOOST_CHECK(different_length);
}

BOOST_AUTO_TEST_CASE(reject_malformed_record_trailers)
{
    CBlock block;
    CDataStream prefix(SER_NETWORK, PROTOCOL_VERSION);
    prefix << block;
    const std::vector<unsigned char> original(prefix.begin(), prefix.end());
    const std::vector<std::vector<unsigned char>> invalid = {
        {0}, {'P','M','A','2',1,0}, {'P','M','A','1',0},
        {'P','M','A','1',73}, {'P','M','A','1',2,0},
        {'P','M','A','1',8,0x30,6,2,1,1,2,1,1,0},
        {'P','M','A','1',8,0x30,6,2,1,1,2,1,1,'P','M','A','1',8,0x30,6,2,1,1,2,1,1},
        {'P','M','A','1',253,8,0,0x30,6,2,1,1,2,1,1},
    };
    for (const auto& trailer : invalid) {
        CDataStream stream(original, SER_NETWORK, PROTOCOL_VERSION);
        stream.write(reinterpret_cast<const char*>(trailer.data()), trailer.size());
        CBlock decoded;
        BOOST_CHECK_THROW(ReadBlockWithAuth(stream, decoded), std::ios_base::failure);
    }
}

BOOST_AUTO_TEST_CASE(configured_signer_rejects_wrong_keys_without_echo)
{
    const auto params = TestConsensus();
    std::string error;
    ClearMiningAuthKey();
    BOOST_CHECK(!HasMiningAuthKey());
    BOOST_CHECK(!InitMiningAuthKey("not-a-private-key", params, error));
    BOOST_CHECK(error.find("not-a-private-key") == std::string::npos);
    const auto wrong = EncodeSecret(TestKey(2));
    BOOST_CHECK(!InitMiningAuthKey(wrong, params, error));
    BOOST_CHECK(error.find(wrong) == std::string::npos);
    BOOST_REQUIRE(InitMiningAuthKey(EncodeSecret(TestKey()), params, error));
    BOOST_CHECK(HasMiningAuthKey());
    CBlock block;
    BOOST_CHECK(SignBlockMiningAuth(block, params, 505400));
    BOOST_CHECK(Authorized(block, params, 505400));
    ClearMiningAuthKey();
}

BOOST_AUTO_TEST_SUITE_END()
