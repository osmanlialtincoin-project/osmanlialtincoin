// Copyright (c) 2026 The Osmanli Altin Coin developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#include <mining_auth.h>

#include <consensus/validation.h>
#include <hash.h>
#include <key.h>
#include <key_io.h>
#include <script/interpreter.h>

namespace {
// Set once before RPC/network startup, cleared after their shutdown.
CKey mining_key;

bool MatchesAuthority(const CKey& key, const Consensus::Params& params)
{
    return key.IsValid() && key.IsCompressed() && key.GetPubKey() == CPubKey(params.miningAuthPubKey);
}
}

bool IsSecureMiningActive(int height, const Consensus::Params& params)
{
    return height >= params.nSecureMiningHeight;
}

uint256 GetMiningAuthHash(const CBlockHeader& block, const Consensus::Params& params, int height)
{
    CHashWriter writer(SER_GETHASH, 0);
    writer << std::string("OsmanliAltinCoin/MiningAuthorization/v1") << params.hashGenesisBlock << int32_t(height) << block.GetHash();
    return writer.GetHash();
}

bool CheckBlockMiningAuth(const CBlock& block, CValidationState& state, const Consensus::Params& params, int height)
{
    if (!IsSecureMiningActive(height, params) && block.vchMiningSignature.empty()) return true;
    bool valid = IsSecureMiningActive(height, params);
    const auto& signature = block.vchMiningSignature;
    valid = valid && signature.size() >= 8 && signature.size() <= MAX_MINING_SIGNATURE_SIZE;
    if (valid) {
        // Script encoding validation includes a trailing sighash byte; the
        // mining signature itself has none. Preserve historic key verification.
        auto encoded = signature;
        encoded.push_back(SIGHASH_ALL);
        const CPubKey pubkey(params.miningAuthPubKey);
        valid = pubkey.IsCompressed() && pubkey.IsFullyValid() &&
            CheckSignatureEncoding(encoded, SCRIPT_VERIFY_DERSIG | SCRIPT_VERIFY_LOW_S, nullptr) &&
            pubkey.Verify(GetMiningAuthHash(block, params, height), signature);
    }
    if (valid) return true;
    // The header does not commit to these bytes. A corrupt copy must not
    // permanently invalidate a later correctly signed copy of the same header.
    return state.DoS(0, false, REJECT_INVALID, "bad-mining-signature", true);
}

bool SignBlockMiningAuth(CBlock& block, const Consensus::Params& params, int height, const CKey& key)
{
    if (!IsSecureMiningActive(height, params)) return block.vchMiningSignature.empty();
    if (!MatchesAuthority(key, params)) return false;
    block.fChecked = false;
    return key.Sign(GetMiningAuthHash(block, params, height), block.vchMiningSignature);
}

bool SignBlockMiningAuth(CBlock& block, const Consensus::Params& params, int height)
{
    return SignBlockMiningAuth(block, params, height, mining_key);
}

bool InitMiningAuthKey(const std::string& secret, const Consensus::Params& params, std::string& error)
{
    ClearMiningAuthKey();
    if (secret.empty()) return true;
    const CKey key = DecodeSecret(secret);
    if (!MatchesAuthority(key, params)) {
        error = "Invalid miningprivkey: expected this network's compressed WIF matching the mining authorization public key";
        return false;
    }
    mining_key = key;
    return true;
}

bool HasMiningAuthKey() { return mining_key.IsValid(); }
void ClearMiningAuthKey() { mining_key = CKey(); }
