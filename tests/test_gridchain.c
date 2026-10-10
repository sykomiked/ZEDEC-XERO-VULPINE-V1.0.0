/* test_gridchain.c — GridChain 5D Consensus Tests
 * Tests block creation, transaction addition, chain verification, consensus voting.
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "gridchain_core.h"
#include "axiom_matrix_core.h"

int main(void) {
    printf("=== GridChain 5D Consensus Tests ===\n");

    axiom_matrix_t matrix;
    matrix.size = 256;
    zxv_cq16_t entries[256];
    memset(entries, 0, sizeof(entries));
    matrix.entries = entries;

    static gridchain_state_t gc;
    gridchain_init(&gc, &matrix, 4);
    assert(gc.shard_count == 4);
    assert(gc.chain_length == 0);

    gridchain_commit_block(&gc);
    assert(gc.chain_length == 1);
    assert(gc.chain[0].index == 0);
    printf("  [PASS] Genesis block created\n");

    gridchain_tx_t tx;
    memset(&tx, 0, sizeof(tx));
    tx.ordinal = 1;
    tx.amount = (rational_t){100, 1};
    tx.attestation = TRIT_TRUE;
    tx.emotion_index = 0.5;
    tx.jurisdiction = 1;
    tx.cosmic_phase = 1.0 + 0.5 * I;

    uint32_t tx_idx = gridchain_add_tx(&gc, &tx);
    assert(tx_idx == 0);
    assert(gc.chain[0].num_tx == 1);
    assert(gc.chain[0].emotion_aggregate == 0.5);
    printf("  [PASS] Transaction added with 5D metadata\n");

    gridchain_commit_block(&gc);
    assert(gc.chain_length == 2);
    assert(memcmp(gc.chain[1].prev_hash, gc.chain[0].hash, GRIDCHAIN_HASH_SIZE) == 0);
    printf("  [PASS] Block chaining (prev_hash linkage)\n");

    assert(gridchain_verify_chain(&gc));
    printf("  [PASS] Chain verification\n");

    gridchain_consensus_vote(&gc, 0, 0);
    gridchain_consensus_vote(&gc, 1, 0);
    gridchain_consensus_vote(&gc, 2, 0);
    gc.num_validators = 4;
    assert(gridchain_consensus_reached(&gc, 0));
    printf("  [PASS] Consensus threshold (3/4 >= 67%%)\n");

    gridchain_consensus_vote(&gc, 0, 1);
    assert(!gridchain_consensus_reached(&gc, 1));
    printf("  [PASS] Insufficient signatures rejected\n");

    printf("=== All gridchain tests passed ===\n\n");
    return 0;
}
