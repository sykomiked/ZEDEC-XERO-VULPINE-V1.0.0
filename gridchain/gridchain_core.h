/* gridchain_core.h — GridChain 5D Lattice Consensus
 * 5-dimensional blockchain: classical shard + quantum cluster + emotion index
 * + jurisdiction + cosmic phase. Post-quantum crypto, multi-dimensional PBFT.
 * Per Cosmic AI Master Build Plan Ch. 3/12.
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#ifndef GRIDCHAIN_CORE_H
#define GRIDCHAIN_CORE_H

#include "m5_types.h"

#define GRIDCHAIN_MAX_BLOCKS 256
#define GRIDCHAIN_MAX_TX 64
#define GRIDCHAIN_HASH_SIZE 32
#define GRIDCHAIN_MAX_VALIDATORS 256

typedef struct gridchain_tx {
    ordinal_t ordinal;
    rational_t amount;
    trit_t attestation;
    phase_t phase;
    collapse_t choice;
    double emotion_index;
    uint32_t jurisdiction;
    double complex cosmic_phase;
    uint8_t sender[168];
    uint8_t receiver[168];
    uint8_t signature[168];
} gridchain_tx_t;

typedef struct gridchain_block {
    uint64_t index;
    uint64_t timestamp;
    uint8_t prev_hash[GRIDCHAIN_HASH_SIZE];
    uint8_t hash[GRIDCHAIN_HASH_SIZE];
    gridchain_tx_t transactions[GRIDCHAIN_MAX_TX];
    uint32_t num_tx;
    double complex merkle_root;
    uint32_t validator_count;
    uint32_t validator_sigs[GRIDCHAIN_MAX_VALIDATORS];
    double emotion_aggregate;
    double complex cosmic_phase;
    uint32_t shard_id;
} gridchain_block_t;

typedef struct gridchain_state {
    gridchain_block_t chain[GRIDCHAIN_MAX_BLOCKS];
    uint32_t chain_length;
    uint32_t shard_count;
    uint32_t num_validators;
    axiom_matrix_t *matrix;
    double consensus_threshold;
} gridchain_state_t;

void gridchain_init(gridchain_state_t *g, axiom_matrix_t *matrix, uint32_t shards);
uint32_t gridchain_add_tx(gridchain_state_t *g, const gridchain_tx_t *tx);
int gridchain_commit_block(gridchain_state_t *g);
bool gridchain_verify_block(const gridchain_block_t *block);
bool gridchain_verify_chain(const gridchain_state_t *g);
double complex gridchain_merkle_root(const gridchain_block_t *block);
int gridchain_consensus_vote(gridchain_state_t *g, uint32_t validator_id, uint32_t block_idx);
bool gridchain_consensus_reached(const gridchain_state_t *g, uint32_t block_idx);
uint8_t *gridchain_block_hash(gridchain_block_t *block);

#endif
