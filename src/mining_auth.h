// Copyright (c) 2026 The Osmanli Altin Coin developers
// Distributed under the MIT software license, see the accompanying file COPYING.
#ifndef BITCOIN_MINING_AUTH_H
#define BITCOIN_MINING_AUTH_H

#include <consensus/params.h>
#include <primitives/block.h>
#include <string>

class CKey;
class CValidationState;

bool IsSecureMiningActive(int height, const Consensus::Params& params);
uint256 GetMiningAuthHash(const CBlockHeader& block, const Consensus::Params& params, int height);
bool CheckBlockMiningAuth(const CBlock& block, CValidationState& state, const Consensus::Params& params, int height);
bool SignBlockMiningAuth(CBlock& block, const Consensus::Params& params, int height, const CKey& key);
bool SignBlockMiningAuth(CBlock& block, const Consensus::Params& params, int height);
bool InitMiningAuthKey(const std::string& secret, const Consensus::Params& params, std::string& error);
bool HasMiningAuthKey();
void ClearMiningAuthKey();

#endif
