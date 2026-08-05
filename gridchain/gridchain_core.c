/* gridchain_core.c — GridChain 5D Lattice Consensus Implementation
 * Blocks carry 5D metadata: shard, emotion, jurisdiction, cosmic phase, ordinal.
 * Multi-dimensional PBFT on icosahedral peer graph (simplified: threshold-based).
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#include "gridchain_core.h"
#include "axiom_matrix_core.h"
#include <string.h>
#include <math.h>

static void simple_hash(const void *data, size_t len, uint8_t *out) {
    uint64_t h = 1469598103934665603ULL;
    const uint8_t *p = (const uint8_t *)data;
    for (size_t i = 0; i < len; i++) {
        h ^= p[i];
        h *= 1099511628211ULL;
    }
    for (int i = 0; i < GRIDCHAIN_HASH_SIZE; i++) {
        out[i] = (uint8_t)((h >> (i % 8 * 8)) & 0xFF);
        h = h * 1099511628211ULL + i;
    }
}

void gridchain_init(gridchain_state_t *g, axiom_matrix_t *matrix, uint32_t shards) {
    memset(g, 0, sizeof(gridchain_state_t));
    g->matrix = matrix;
    g->shard_count = shards > 0 ? shards : 1;
    g->num_validators = 0;
    g->consensus_threshold = 0.67;
    g->chain_length = 0;
}

uint32_t gridchain_add_tx(gridchain_state_t *g, const gridchain_tx_t *tx) {
    if (g->chain_length == 0) gridchain_commit_block(g);
    gridchain_block_t *block = &g->chain[g->chain_length - 1];
    if (block->num_tx >= GRIDCHAIN_MAX_TX) {
        gridchain_commit_block(g);
        block = &g->chain[g->chain_length - 1];
    }
    uint32_t idx = block->num_tx++;
    block->transactions[idx] = *tx;
    block->emotion_aggregate += tx->emotion_index;
    block->cosmic_phase += tx->cosmic_phase;
    return idx;
}

int gridchain_commit_block(gridchain_state_t *g) {
    if (g->chain_length >= GRIDCHAIN_MAX_BLOCKS) return -1;
    if (g->chain_length > 0) {
        gridchain_block_t *prev = &g->chain[g->chain_length - 1];
        prev->merkle_root = gridchain_merkle_root(prev);
        memset(prev->hash, 0, GRIDCHAIN_HASH_SIZE);
        simple_hash(prev, sizeof(gridchain_block_t), prev->hash);
    }
    gridchain_block_t *block = &g->chain[g->chain_length];
    memset(block, 0, sizeof(gridchain_block_t));
    block->index = g->chain_length;
    block->timestamp = g->chain_length * 10;
    block->num_tx = 0;
    block->shard_id = (uint32_t)(block->index % g->shard_count);
    block->validator_count = 0;
    if (g->chain_length > 0) {
        memcpy(block->prev_hash, g->chain[g->chain_length - 1].hash, GRIDCHAIN_HASH_SIZE);
    }
    block->merkle_root = gridchain_merkle_root(block);
    memset(block->hash, 0, GRIDCHAIN_HASH_SIZE);
    simple_hash(block, sizeof(gridchain_block_t), block->hash);
    g->chain_length++;
    return 0;
}

double complex gridchain_merkle_root(const gridchain_block_t *block) {
    double complex root = 0.0;
    for (uint32_t i = 0; i < block->num_tx; i++) {
        const gridchain_tx_t *tx = &block->transactions[i];
        root += tx->cosmic_phase * (double complex)tx->ordinal;
    }
    return root;
}

bool gridchain_verify_block(const gridchain_block_t *block) {
    gridchain_block_t tmp = *block;
    uint8_t stored[GRIDCHAIN_HASH_SIZE];
    memcpy(stored, tmp.hash, GRIDCHAIN_HASH_SIZE);
    memset(tmp.hash, 0, GRIDCHAIN_HASH_SIZE);
    uint8_t computed[GRIDCHAIN_HASH_SIZE];
    simple_hash(&tmp, sizeof(gridchain_block_t), computed);
    return memcmp(computed, stored, GRIDCHAIN_HASH_SIZE) == 0;
}

bool gridchain_verify_chain(const gridchain_state_t *g) {
    for (uint32_t i = 1; i < g->chain_length; i++) {
        if (memcmp(g->chain[i].prev_hash, g->chain[i-1].hash, GRIDCHAIN_HASH_SIZE) != 0)
            return false;
        if (!gridchain_verify_block(&g->chain[i]))
            return false;
    }
    return true;
}

int gridchain_consensus_vote(gridchain_state_t *g, uint32_t validator_id, uint32_t block_idx) {
    if (block_idx >= g->chain_length) return -1;
    gridchain_block_t *block = &g->chain[block_idx];
    if (block->validator_count >= GRIDCHAIN_MAX_VALIDATORS) return -1;
    for (uint32_t i = 0; i < block->validator_count; i++) {
        if (block->validator_sigs[i] == validator_id) return -2;
    }
    block->validator_sigs[block->validator_count++] = validator_id;
    if (g->num_validators < block->validator_count) g->num_validators = block->validator_count;
    return 0;
}

bool gridchain_consensus_reached(const gridchain_state_t *g, uint32_t block_idx) {
    if (block_idx >= g->chain_length) return false;
    const gridchain_block_t *block = &g->chain[block_idx];
    if (g->num_validators == 0) return false;
    double ratio = (double)block->validator_count / (double)g->num_validators;
    return ratio >= g->consensus_threshold;
}

uint8_t *gridchain_block_hash(gridchain_block_t *block) {
    return block->hash;
}
