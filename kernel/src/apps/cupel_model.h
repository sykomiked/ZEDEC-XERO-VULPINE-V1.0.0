/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* cupel_model.h — Cupel, the simulation bench, as a model (zxm.h): one
 * model of atoms in molecules joined by springs, stepped by the leapfrog
 * method on the integer engine, measured as it goes, compared with
 * another, and its runs kept in the Hoard. No drawing, no allocation,
 * integers only; the rules it runs are cupel_rules.h (CR1-CR10), in
 * reduced units (a unit mass, positions in 1/scale of a length unit).
 *
 * zxvd methods used: files.read, files.write and sys.info.
 *
 *   CM1 A MODEL is a text file in the Hoard: "cupel 1 <model> <scale>
 *       <dt> <declared> <bound> <seconds> <atoms> <pairs>", then "n
 *       <name>", an "a <molecule> <element> <x> <y> <z> <vx> <vy> <vz>"
 *       line per atom and a "p <p> <q> <k> <rx> <ry> <rz>" line per spring
 *       (rest offsets from p to q). Saved whole (files.write, in pieces
 *       when it does not fit one request) only on the owner's word, every
 *       atom checked against the tree (CR6) first; read back with
 *       files.read, where a file that does not parse leaves the open model
 *       as it was.
 *   CM2 LIMITS. At most CUPEL_ATOMS atoms and CUPEL_PAIRS springs (Relief's
 *       mesh limits: an atom a vertex, a spring a face drawn); every
 *       coordinate and velocity inside CR2/CR3's domain; a spring joins
 *       two different atoms with k 1 .. CUPEL_K_MAX. The bounds (scale,
 *       declared steps, drift bound) must pass sim_precision_ok at the
 *       start (CR4), dt 1 .. CUPEL_DT_MAX and the owner's seconds a run
 *       retort.run_limits_ok's 1 .. 3600. A change that breaks them is
 *       refused and changes nothing.
 *   CM3 A LATTICE. cupel_lattice builds a cubic lattice of side^3 atoms,
 *       atom n at the site lattice_site_index numbers n (CR5), a spring to
 *       each next neighbour along x, y and z at the spacing; its face's
 *       area is gnomon.triangle_area2 of its half triangle.
 *   CM4 THE STEP. The springs' forces (CR8, floored), summed per atom and
 *       axis as pair_force_net sums them (CR1), then the kick and the drift
 *       (CR2, CR3) for every atom and axis. Everything is checked and
 *       computed before anything changes: a force or a new value outside
 *       the domain halts the run (CUPEL_HALT_RANGE) with the model as it
 *       was. After the step the bounds are checked again: past the
 *       declared steps the step is refused (CUPEL_HALT_STEPS) and a drift
 *       past the bound halts the run (CUPEL_HALT_DRIFT) with that step
 *       kept, to be seen.
 *   CM5 ENERGY. Twice an atom's kinetic energy is |v|^2 and twice a
 *       spring's is floor(k |stretch|^2 / scale), each squared length by
 *       gnomon.side_len_sq; the energy of molecules lo .. hi is half of
 *       furrow.range_total over them, floored.
 *   CM6 A RUN. cupel_run takes the steps asked (at most the declared
 *       ones), its limits checked first (retort.run_limits_ok: the note's
 *       bytes, steps times atoms and springs, the owner's seconds), timed
 *       by sys.info's uptime and scored (CR10). The machine's facts
 *       (cupel_machine, sys.info) are kept with it.
 *   CM7 RECORDS. Cupel.runs in the Hoard holds every recorded run, one "r"
 *       line each; a run is recorded only on the owner's word, when it
 *       fits Cupel's record (CR7) and Retort's (a scratch run, kind
 *       RETORT_KIND_PLAY, its energy use not measured: uj 0). Each model's
 *       score over its recorded runs is retort.model_score_total.
 *   CM8 COMPARE. Two models' energies after the same steps within a
 *       tolerance: cupel_compare (CR9) gives TRUE, or BOTH with both
 *       values shown; never one picked over the other. */
#ifndef CUPEL_MODEL_H
#define CUPEL_MODEL_H

#include "cupel_rules.h"
#include "zxm.h"

#define CUPEL_RUNS_FILE   "Cupel.runs"
#define CUPEL_PATH_MAX    1024u
#define CUPEL_FILE_MAX    (1u << 17) /* a model's text */
#define CUPEL_NAME_MAX    47u
#define CUPEL_NOTE_MAX    255u
#define CUPEL_RUNS        64u
#define CUPEL_ARCH_MAX    31u
#define CUPEL_MODEL_MAX   99 /* CR6: 2 digits */
#define CUPEL_MOLECULE    9999
#define CUPEL_ELEMENT_MAX 118
#define CUPEL_SIDE_MAX    8 /* 8^3 = 512 atoms */
#define CUPEL_RETORT_KIND 1 /* retort_model.h RETORT_KIND_PLAY: a run on scratch state */

enum { CUPEL_GOING = 0, CUPEL_HALT_STEPS = 1, CUPEL_HALT_DRIFT = 2, CUPEL_HALT_RANGE = 3 };

typedef struct {
    int64_t molecule, element;
    int64_t x[3], v[3]; /* v at the half step before x's */
} cupel_atom_t;

typedef struct {
    uint32_t p, q;
    int64_t k, rest[3];
} cupel_pair_t;

typedef struct {
    int64_t run, model, steps, energy, drift, score, ms, work;
    bool halted;
    char note[CUPEL_NOTE_MAX + 1];
} cupel_run_t;

typedef struct {
    bool known;
    char arch[CUPEL_ARCH_MAX + 1];
    uint64_t cpus, mem_total, mem_avail, uptime_s;
} cupel_machine_t;

typedef struct {
    zxm_link_t *link;
    char space[ZXM_SPACE_MAX + 1];
    char path[CUPEL_PATH_MAX + 1]; /* "" = not saved yet */
    int64_t model;
    char name[CUPEL_NAME_MAX + 1];
    int64_t scale, dt, declared, bound, seconds; /* CM2 */
    cupel_atom_t atom[CUPEL_ATOMS];
    uint32_t natom;
    cupel_pair_t pair[CUPEL_PAIRS];
    uint32_t npair;
    int64_t side, spacing; /* CM3: 0 when not a lattice */
    int64_t taken, e0, energy;
    int32_t halt; /* CUPEL_GOING or CUPEL_HALT_* */
    cupel_run_t run[CUPEL_RUNS];
    uint32_t nrun;
    bool runs_loaded;
    cupel_machine_t machine;
    bool dirty;
} cupel_t;

/* An empty model 1 with the default bounds: scale 1000, dt 1, 1000 steps
 * declared, a drift bound of 1000 and 60 seconds a run. */
void cupel_init(cupel_t *c, zxm_link_t *link);
/* CM2 */
int cupel_set_model(cupel_t *c, int64_t model, const char *name);
int cupel_set_bounds(cupel_t *c, int64_t scale, int64_t dt, int64_t declared, int64_t bound,
                     int64_t seconds);
/* the new atom's number in *n */
int cupel_add_atom(cupel_t *c, int64_t molecule, int64_t element, const int64_t x[3],
                   const int64_t v[3], uint32_t *n);
int cupel_add_pair(cupel_t *c, uint32_t p, uint32_t q, int64_t k, const int64_t rest[3]);
/* CM3 */
int cupel_lattice(cupel_t *c, int64_t side, int64_t spacing, int64_t element, int64_t k);
int cupel_face_area(const cupel_t *c, int64_t *area);
/* CM5 */
int cupel_energy(const cupel_t *c, int64_t lo, int64_t hi, int64_t *e);
/* CM4: a run's start (the energy now is its e0), then one step at a time */
int cupel_begin(cupel_t *c);
int cupel_step(cupel_t *c);
/* CM6 */
int cupel_machine(cupel_t *c);
int cupel_run(cupel_t *c, int64_t steps, const char *note, cupel_run_t *out);
/* CM7 */
int cupel_load_runs(cupel_t *c);
int cupel_record(cupel_t *c, const cupel_run_t *r, bool owner_confirmed);
int64_t cupel_model_total(const cupel_t *c, int64_t model);
/* CM8 */
lpres_state_t cupel_compare(int64_t a, int64_t b, int64_t tol, int64_t shown[2]);
/* an atom on the screen (relief.vertex_screen_x, _y) and whether the
 * model can be drawn (relief.mesh_limits_ok) */
int cupel_screen(const cupel_t *c, uint32_t atom, int64_t cos, int64_t sin, int64_t scale,
                 int64_t focal, int64_t *sx, int64_t *sy);
bool cupel_drawable(const cupel_t *c);
/* CM1. space: a space id from spaces.list, "" for home. */
int cupel_save(cupel_t *c, const char *space, const char *path, bool owner_confirmed);
int cupel_open(cupel_t *c, const char *space, const char *path);

#endif
