/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* relief_model.h — Relief, 3D modelling and the depth effect, as a model
 * (zxm.h): one triangle mesh, turned, scaled and seen in perspective on
 * the screen, split into depth layers that shift as the device tilts, its
 * enclosed volume exact, kept as a file in the Hoard. No allocation,
 * integers only; lengths are hundredths of a millimetre (relief_rules.h),
 * y up, z away from the viewer.
 *
 *   RM1 MESHES ARE FILES. A mesh is a text file in the Hoard (one
 *       "relief 1 <vertices> <faces>" line, then a "v <x> <y> <z>" line per
 *       vertex and an "f <a> <b> <c>" line per face, a face naming its
 *       corners by vertex number from 0), written whole with files.write
 *       (begin/add/end when it does not fit one request) and read back with
 *       files.read. A file that does not parse, or breaks RM2, leaves the
 *       open mesh as it was.
 *   RM2 LIMITS. Every coordinate is inside -RELIEF_COORD_MAX ..
 *       RELIEF_COORD_MAX; a face's three corners are distinct vertices
 *       that exist. A mesh is saved, read and measured only inside
 *       RELIEF_VERTS_MAX vertices and RELIEF_FACES_MAX faces, at least 4 of
 *       each (blocks/relief/limits.eve, Eve). A change that would break
 *       them is refused (ZXM_E_ARG) or, past the limits, ZXM_E_TOOBIG;
 *       nothing is changed first.
 *   RM3 THE VIEW. The mesh is turned yaw whole degrees about the vertical
 *       axis (cosine and sine from a table in 4096ths), scaled by scale
 *       percent and seen from focal units in front of the screen plane.
 *       Each vertex's screen point is relief_screen_x and relief_screen_y
 *       (blocks/relief/screen_x and screen_y, C++ as WebAssembly and KRON,
 *       witnessed). A vertex at or behind the viewer is refused: nothing
 *       is drawn rather than a wrong picture.
 *   RM4 THE DEPTH EFFECT. The turned vertices are split into layers (2 ..
 *       RELIEF_LAYERS_MAX) by depth, the nearest layer 0: layer
 *       floor((d - near) * (layers - 1) / (far - near)) of the depths
 *       near .. far (all in layer 0 when they are equal). When the device
 *       tilts so the nearest layer moves shift pixels, layer k moves
 *       relief_depth_offset(shift, k, layers - 1) (blocks/relief/
 *       depth.kron), and the farthest stays still.
 *   RM5 VOLUME. The volume a closed mesh encloses is relief_mesh_volume
 *       (blocks/relief/volume.tb, Trebago), exact. A mesh is closed when
 *       every edge a face runs a to b is run b to a by exactly one other
 *       face and by no second face the same way; an open mesh has no
 *       volume (ZXM_E_STATE).
 *   RM6 RECORD. A saved mesh is listed by relief_mesh_record_ok
 *       (blocks/relief/record.myra, ^ZXRELIEF). */
#ifndef RELIEF_MODEL_H
#define RELIEF_MODEL_H

#include "relief_rules.h"
#include "zxm.h"

#define RELIEF_PATH_MAX   1024u
#define RELIEF_FILE_MAX   (1u << 16) /* a mesh's text */
#define RELIEF_LAYERS_MAX 16u
#define RELIEF_YAW_TURN   360

typedef struct {
    int32_t x, y, z;
} relief_vertex_t;

typedef struct {
    uint16_t a, b, c; /* counter-clockwise seen from outside */
} relief_face_t;

typedef struct {
    zxm_link_t *link;
    char space[ZXM_SPACE_MAX + 1];
    char path[RELIEF_PATH_MAX + 1]; /* "" = not saved yet */
    uint32_t nv, nf;
    relief_vertex_t v[RELIEF_VERTS_MAX];
    relief_face_t f[RELIEF_FACES_MAX];
    int32_t yaw;   /* RM3: degrees, 0 .. 359 */
    int32_t scale; /* percent, 1 .. RELIEF_SCALE_MAX */
    int32_t focal; /* hundredths of a millimetre, 1 .. RELIEF_FOCAL_MAX */
    bool dirty;
} relief_t;

/* An empty mesh, seen straight on at 100 percent from a metre away. */
void relief_init(relief_t *r, zxm_link_t *link);
/* RM2: an empty mesh; the view and the link are kept. */
void relief_clear(relief_t *r);
/* RM2: a vertex; *index (may be NULL) is its number. */
int relief_add_vertex(relief_t *r, int64_t x, int64_t y, int64_t z, uint32_t *index);
int relief_add_face(relief_t *r, int64_t a, int64_t b, int64_t c);
/* A closed box from corner x0, y0, z0 to x1, y1, z1 (each below its pair):
 * 8 vertices and 12 faces, wound outward. */
int relief_add_box(relief_t *r, int64_t x0, int64_t y0, int64_t z0, int64_t x1, int64_t y1,
                   int64_t z1);
/* RM5: every edge paired with one reverse edge. */
bool relief_closed(const relief_t *r);
/* RM3: cos and sin of whole degrees (any, taken mod 360) in 4096ths. */
void relief_turn(int64_t degrees, int64_t *cos, int64_t *sin);
int relief_set_view(relief_t *r, int64_t yaw, int64_t scale, int64_t focal);
/* RM3: every vertex's screen point into sx, sy (cap each). */
int relief_project(const relief_t *r, int64_t *sx, int64_t *sy, uint32_t cap);
/* RM4: every vertex's layer and its offset at a tilt of shift pixels. */
int relief_depth_layers(const relief_t *r, uint32_t layers, int64_t shift, uint8_t *layer,
                        int64_t *offset, uint32_t cap);
/* RM5: the enclosed volume, num / den reduced, in cubic hundredths of a mm. */
int relief_volume(const relief_t *r, int64_t *num, int64_t *den);
/* RM6: whether this mesh's record, as mesh number `mesh`, fits ^ZXRELIEF. */
bool relief_record(const relief_t *r, int64_t mesh, int64_t mtime);

/* RM1. space: a space id from spaces.list, "" for home. */
int relief_save(relief_t *r); /* to r->path; ZXM_E_STATE when there is none */
int relief_save_as(relief_t *r, const char *space, const char *path);
int relief_open(relief_t *r, const char *space, const char *path);

#endif
