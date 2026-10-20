#!/usr/bin/env python3
# Copyright (c) 2026 The Osmanli Altin Coin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Secure mining activation, RPC pool submissions, relay, and persistence."""

import struct
from io import BytesIO

from test_framework.address import byte_to_base58
from test_framework.blocktools import create_coinbase
from test_framework.messages import (
    CBlock, CBlockHeader, CInv, BlockTransactionsRequest, FromHex, ToHex, msg_headers,
    NODE_NETWORK, NODE_WITNESS, NODE_MINING_AUTH, msg_getdata, msg_getblocktxn, msg_witness_block,
)
from test_framework.mininode import MAGIC_BYTES, P2PDataStore
from test_framework.test_node import ErrorMatch
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error, connect_nodes_bi, disconnect_nodes, wait_until


def check_block_message_roundtrips():
    # A headers vector has no auth trailers, even with multiple adjacent entries.
    headers = [CBlockHeader() for _ in range(3)]
    for nonce, header in enumerate(headers):
        header.nNonce = nonce
    wire = msg_headers(headers).serialize()
    decoded = msg_headers()
    decoded.deserialize(BytesIO(wire))
    assert_equal([header.nNonce for header in decoded.headers], [0, 1, 2])
    assert_equal(decoded.serialize(), wire)
    for signature in (b"", bytes.fromhex("3006020101020101")):
        block = CBlock(headers[-1])
        block.vchMiningSignature = signature
        # Callers may pass full blocks; a headers message must strip their body/auth.
        assert_equal(msg_headers([block]).serialize(), b"\x01" + headers[-1].serialize() + b"\x00")
        wire = block.serialize(with_witness=True)
        decoded_block = CBlock()
        decoded_block.deserialize(BytesIO(wire))
        assert_equal(decoded_block.vchMiningSignature, signature)
        assert_equal(decoded_block.serialize(with_witness=True), wire)


class SecureMiningTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 3
        self.setup_clean_chain = True
        self.extra_args = [["-secureminingheight=5"] for _ in range(self.num_nodes)]

    def setup_network(self):
        self.setup_nodes()
        connect_nodes_bi(self.nodes, 0, 1)

    def setup_nodes(self):
        # Publicly known regtest scalar 1, unrelated to any user's credentials.
        key = byte_to_base58(bytes(31) + b"\x01\x01", 239)
        if self.is_wallet_compiled():
            for args in self.extra_args:
                args.append("-deprecatedrpc=generate")
        self.add_nodes(self.num_nodes, self.extra_args)
        with open(self.nodes[0].datadir + "/osmanli_altin_coin.conf", "a", encoding="utf8") as config:
            config.write("\nminingprivkey=" + key + "\n")
        self.start_nodes()

    def pool_block(self):
        template = self.nodes[0].getblocktemplate({"rules": ["segwit"]})
        block = CBlock()
        block.nVersion = template["version"]
        block.hashPrevBlock = int(template["previousblockhash"], 16)
        block.nTime = template["curtime"]
        block.nBits = int(template["bits"], 16)
        coinbase = create_coinbase(template["height"])
        coinbase.vout[0].nValue = template["coinbasevalue"]
        coinbase.rehash()
        block.vtx = [coinbase]
        block.hashMerkleRoot = block.calc_merkle_root()
        block.solve()
        return block

    def run_test(self):
        check_block_message_roundtrips()
        signer, verifier = self.nodes[:2]
        recipient = byte_to_base58(bytes(20), 111)
        self.log.info("Legacy blocks work without a signing key")
        verifier.generatetoaddress(4, recipient)
        self.sync_all([self.nodes[:2]])
        assert_equal(signer.getmininginfo()["secure_mining"]["activation_height"], 5)
        assert_equal(verifier.getmininginfo()["secure_mining"]["key_loaded"], False)
        assert_raises_rpc_error(-1, "Secure mining requires miningprivkey", verifier.generatetoaddress, 1, recipient)
        assert_raises_rpc_error(-1, "Secure mining requires miningprivkey", verifier.getblocktemplate, {"rules": ["segwit"]})

        self.log.info("Native mining keeps selected coinbase payout and relays to a keyless node")
        activated = signer.generatetoaddress(1, recipient)[0]
        self.sync_all([self.nodes[:2]])
        raw = verifier.getblock(activated, 0)
        block = FromHex(CBlock(), raw)
        assert block.vchMiningSignature
        assert_equal(block.serialize(with_witness=True).hex(), raw)
        payout = verifier.getblock(activated, 2)["tx"][0]["vout"][0]["scriptPubKey"]
        assert recipient in payout.get("addresses", [payout.get("address")])

        self.log.info("Pool-style unsigned submission is signed only by the authorized daemon")
        pool_block = self.pool_block()
        unsigned = ToHex(pool_block)
        assert_equal(verifier.submitblock(unsigned), "mining-auth-key-missing")
        peer = signer.add_p2p_connection(P2PDataStore(), services=NODE_NETWORK | NODE_WITNESS | NODE_MINING_AUTH)
        peer.send_blocks_and_test([pool_block], signer, success=False, force_send=True)
        assert_equal(signer.getblockcount(), 5)
        pool_block.vchMiningSignature = b"\x30\x00"
        assert_raises_rpc_error(-22, "Block decode failed", signer.submitblock, ToHex(pool_block))
        pool_block.vchMiningSignature = b"\x30\x06\x02\x01\x01\x02\x01\x01"
        assert_equal(signer.submitblock(ToHex(pool_block)), "bad-mining-signature")
        pool_block.vchMiningSignature = b""
        assert_equal(signer.submitblock(unsigned), None)
        self.sync_all([self.nodes[:2]])
        signed_pool = FromHex(CBlock(), verifier.getblock(pool_block.hash, 0))
        assert signed_pool.vchMiningSignature
        assert_equal(signed_pool.vtx[0].serialize(), pool_block.vtx[0].serialize())
        assert_equal(signed_pool.hashMerkleRoot, pool_block.hashMerkleRoot)
        assert_equal(verifier.submitblock(ToHex(signed_pool)), "duplicate")
        self.log.info("Compact block requests fall back to full signed blocks")
        request = msg_getdata()
        request.inv = [CInv(4, pool_block.sha256)]
        peer.send_and_ping(request)
        peer.wait_for_block(pool_block.sha256)
        assert peer.last_message["block"].block.vchMiningSignature
        request_transactions = msg_getblocktxn()
        request_transactions.block_txn_request = BlockTransactionsRequest(pool_block.sha256, [0])
        peer.last_message.pop("block", None)
        peer.send_and_ping(request_transactions)
        peer.wait_for_block(pool_block.sha256)
        assert peer.last_message["block"].block.vchMiningSignature
        signer.disconnect_p2ps()

        self.log.info("Restart and both reindex modes preserve authorization trailers")
        tip = signer.getbestblockhash()
        for flag in ([], ["-reindex-chainstate"], ["-reindex"]):
            self.restart_node(1, self.extra_args[1] + flag)
            wait_until(lambda: verifier.getbestblockhash() == tip)
            assert_equal(verifier.getblock(tip, 0), signer.getblock(tip, 0))
        connect_nodes_bi(self.nodes, 0, 1)

        self.log.info("Reorg below activation restores legacy mining and revalidates signed history")
        verifier.invalidateblock(activated)
        assert_equal(verifier.getblockcount(), 4)
        verifier.invalidateblock(verifier.getbestblockhash())
        assert_equal(verifier.getblockcount(), 3)
        assert_equal(verifier.getmininginfo()["secure_mining"]["active"], False)
        verifier.reconsiderblock(activated)
        verifier.reconsiderblock(signer.getblockhash(4))
        self.sync_all([self.nodes[:2]])
        assert_equal(verifier.getbestblockhash(), tip)

        self.log.info("A longer signed fork reorganizes the keyless verifier across activation")
        signer.invalidateblock(activated)
        # Change time so the replacement height-5 header differs from the old branch.
        signer.setmocktime(signer.getblockheader(tip)["time"] + 10)
        replacement = signer.generatetoaddress(3, recipient)
        self.sync_all([self.nodes[:2]])
        assert_equal(verifier.getbestblockhash(), replacement[-1])
        assert_equal(verifier.getblockcount(), 7)
        assert_equal(verifier.getblockhash(5), replacement[0])

        self.log.info("A stripped P2P signature does not poison the valid copy of the same header")
        disconnect_nodes(signer, 1)
        disconnect_nodes(verifier, 0)
        next_hash = signer.generatetoaddress(1, recipient)[0]
        next_block = FromHex(CBlock(), signer.getblock(next_hash, 0))
        signature = next_block.vchMiningSignature
        next_block.vchMiningSignature = b""
        peer = verifier.add_p2p_connection(P2PDataStore(), services=NODE_NETWORK | NODE_WITNESS | NODE_MINING_AUTH)
        peer.send_and_ping(msg_witness_block(next_block))
        assert_equal(verifier.getblockcount(), 7)
        next_block.vchMiningSignature = signature
        peer.send_and_ping(msg_witness_block(next_block))
        wait_until(lambda: verifier.getbestblockhash() == next_hash)
        verifier.disconnect_p2ps()
        connect_nodes_bi(self.nodes, 0, 1)

        if self.is_wallet_compiled():
            self.log.info("Legacy wallet generate signs blocks and retains wallet-selected rewards")
            assert_raises_rpc_error(-1, "Secure mining requires miningprivkey", verifier.rpc.generate, 1)
            next_hash = signer.rpc.generate(1)[0]
            self.sync_all([self.nodes[:2]])
            wallet_block = verifier.getblock(next_hash, 2)
            wallet_payout = wallet_block["tx"][0]["vout"][0]["scriptPubKey"]
            wallet_address = wallet_payout.get("addresses", [wallet_payout.get("address")])[0]
            assert signer.getaddressinfo(wallet_address)["ismine"]
            assert FromHex(CBlock(), verifier.getblock(next_hash, 0)).vchMiningSignature

        self.log.info("A fresh keyless node imports legacy and signed blocks from a bounded block file")
        import_path = self.options.tmpdir + "/signed-chain.dat"
        with open(import_path, "wb") as block_file:
            for height in range(signer.getblockcount() + 1):
                data = bytes.fromhex(signer.getblock(signer.getblockhash(height), 0))
                block_file.write(MAGIC_BYTES["regtest"] + struct.pack("<I", len(data)) + data)
        self.restart_node(2, self.extra_args[2] + ["-loadblock=" + import_path])
        wait_until(lambda: self.nodes[2].getbestblockhash() == next_hash)
        assert_equal(self.nodes[2].getblock(next_hash, 0), signer.getblock(next_hash, 0))

        self.log.info("Malformed configured keys stop startup")
        self.stop_node(1)
        config_path = verifier.datadir + "/osmanli_altin_coin.conf"
        with open(config_path, encoding="utf8") as config:
            original_config = config.read()
        wrong_key = byte_to_base58(bytes(31) + b"\x02\x01", 239)
        for bad_key in ("invalid", wrong_key):
            with open(config_path, "w", encoding="utf8") as config:
                config.write(original_config + "\nminingprivkey=" + bad_key + "\n")
            verifier.assert_start_raises_init_error(
                self.extra_args[1], expected_msg="Invalid miningprivkey",
                match=ErrorMatch.PARTIAL_REGEX)
        with open(config_path, "w", encoding="utf8") as config:
            config.write(original_config)
        self.start_node(1)
        self.log.info("Daemon logs do not disclose configured authorization credentials")
        signing_key = byte_to_base58(bytes(31) + b"\x01\x01", 239)
        for node in self.nodes:
            with open(node.datadir + "/regtest/debug.log", encoding="utf8") as log:
                contents = log.read()
            assert signing_key not in contents
            assert wrong_key not in contents


if __name__ == '__main__':
    SecureMiningTest().main()
