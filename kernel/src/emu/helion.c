/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* helion.c — spin-spiral 2D->3D expansion. See helion.h. */
#include "helion.h"

static surplus_real_t h_dot(h_vec3 a, h_vec3 b){
    return SR_ADD(SR_ADD(SR_MUL(a.x,b.x), SR_MUL(a.y,b.y)), SR_MUL(a.z,b.z));
}
static h_vec3 h_cross(h_vec3 a, h_vec3 b){
    h_vec3 r;
    r.x = SR_SUB(SR_MUL(a.y,b.z), SR_MUL(a.z,b.y));
    r.y = SR_SUB(SR_MUL(a.z,b.x), SR_MUL(a.x,b.z));
    r.z = SR_SUB(SR_MUL(a.x,b.y), SR_MUL(a.y,b.x));
    return r;
}
static h_vec3 h_add(h_vec3 a, h_vec3 b){ h_vec3 r={SR_ADD(a.x,b.x),SR_ADD(a.y,b.y),SR_ADD(a.z,b.z)}; return r; }
static h_vec3 h_sub(h_vec3 a, h_vec3 b){ h_vec3 r={SR_SUB(a.x,b.x),SR_SUB(a.y,b.y),SR_SUB(a.z,b.z)}; return r; }
static h_vec3 h_scale(h_vec3 a, surplus_real_t s){ h_vec3 r={SR_MUL(a.x,s),SR_MUL(a.y,s),SR_MUL(a.z,s)}; return r; }
static surplus_real_t h_len(h_vec3 a){ return SR_SQRT(h_dot(a,a)); }
static h_vec3 h_norm(h_vec3 a){ surplus_real_t l=h_len(a); if (l==SR_ZERO) return a; return h_scale(a, SR_DIV(SR_ONE,l)); }

/* Rodrigues rotation of v about unit axis k by angle (cos c, sin s):
 * v cos + (k x v) sin + k (k.v)(1-cos) */
static h_vec3 h_rot(h_vec3 v, h_vec3 k, surplus_real_t c, surplus_real_t s){
    surplus_real_t kv = h_dot(k, v);
    h_vec3 kxv = h_cross(k, v);
    h_vec3 t1 = h_scale(v, c);
    h_vec3 t2 = h_scale(kxv, s);
    h_vec3 t3 = h_scale(k, SR_MUL(kv, SR_SUB(SR_ONE, c)));
    return h_add(h_add(t1, t2), t3);
}

h_vec3 helion_perpendicular(h_vec3 v, h_vec3 e){
    e = h_norm(e);
    surplus_real_t ve = h_dot(v, e);
    return h_norm(h_sub(v, h_scale(e, ve)));    /* remove the component along e */
}

void helion_configure(helion_field_t *f, h_vec3 env_spin, h_vec3 shape_principal,
                      h_vec3 traj_dir, h_vec3 origin, int spin_step_degrees){
    f->env_spin = h_norm(env_spin);
    /* the object's spin axis is derived from its SHAPE, then forced perpendicular
     * to the environment's spin (M5 orthogonality) */
    f->obj_spin = helion_perpendicular(shape_principal, f->env_spin);
    f->traj_dir = traj_dir;
    f->origin   = origin;
    /* cos/sin of the step angle (small hardcoded table — no libm) */
    switch (spin_step_degrees){
        case 15: f->step_cos = SR_FROM_FLOAT(0.9659); f->step_sin = SR_FROM_FLOAT(0.2588); break;
        case 45: f->step_cos = SR_FROM_FLOAT(0.7071); f->step_sin = SR_FROM_FLOAT(0.7071); break;
        case 60: f->step_cos = SR_FROM_FLOAT(0.5000); f->step_sin = SR_FROM_FLOAT(0.8660); break;
        default: f->step_cos = SR_FROM_FLOAT(0.8660); f->step_sin = SR_FROM_FLOAT(0.5000); break; /* 30 */
    }
    f->traj_step = SR_FROM_FLOAT(0.35);
}

/* the object's in-plane basis at step 0: two axes perpendicular to the spin axis */
static void basis0(const helion_field_t *f, h_vec3 *u, h_vec3 *v){
    /* u perpendicular to obj_spin (pick a stable seed), v = spin x u */
    h_vec3 seed = { SR_FROM_FLOAT(0.0), SR_FROM_FLOAT(0.0), SR_ONE };
    if (h_dot(f->obj_spin, seed) > SR_FROM_FLOAT(0.9) || h_dot(f->obj_spin, seed) < SR_FROM_FLOAT(-0.9)){
        seed.x = SR_ONE; seed.z = SR_ZERO;
    }
    *u = h_norm(helion_perpendicular(seed, f->obj_spin));
    *v = h_norm(h_cross(f->obj_spin, *u));
}

h_vec3 helion_lift(const helion_field_t *f, surplus_real_t a, surplus_real_t b, int i){
    h_vec3 u, v; basis0(f, &u, &v);
    /* spin the plane basis i steps about the (perpendicular) spin axis */
    for (int k = 0; k < i; k++){
        u = h_rot(u, f->obj_spin, f->step_cos, f->step_sin);
        v = h_rot(v, f->obj_spin, f->step_cos, f->step_sin);
    }
    /* advance along the event-space trajectory by i steps, then place the 2D
     * point in the spun plane — a helix, not a fixed-plane extrusion */
    h_vec3 along = h_scale(f->traj_dir, SR_MUL(f->traj_step, SR_FROM_INT(i)));
    h_vec3 p = h_add(f->origin, along);
    p = h_add(p, h_scale(u, a));
    p = h_add(p, h_scale(v, b));
    return p;
}

int helion_selfcheck(uint32_t *perp_permille_out){
    helion_field_t f;
    h_vec3 env   = { SR_ZERO, SR_ZERO, SR_ONE };                 /* environment spins about Z */
    h_vec3 shape = { SR_ONE, SR_FROM_FLOAT(0.2), SR_FROM_FLOAT(0.6) }; /* the object's shape axis (tilted) */
    h_vec3 traj  = { SR_ONE, SR_ZERO, SR_ZERO };                 /* trajectory along X       */
    h_vec3 org   = { SR_ZERO, SR_ZERO, SR_ZERO };
    helion_configure(&f, env, shape, traj, org, 30);

    /* (1) object spin perpendicular to environment spin */
    surplus_real_t perp = h_dot(f.obj_spin, f.env_spin);
    surplus_real_t aperp = perp < SR_ZERO ? SR_SUB(SR_ZERO, perp) : perp;
    if (perp_permille_out)
        *perp_permille_out = (uint32_t)((double)aperp / (double)SR_ONE * 1000.0);
    int perpendicular = (aperp < SR_FROM_FLOAT(0.01));

    /* sample the lift of a fixed 2D reference point (1,0) over N steps */
    enum { N = 13 };
    h_vec3 pts[N];
    for (int i = 0; i < N; i++) pts[i] = helion_lift(&f, SR_ONE, SR_ZERO, i);

    /* (3) genuine 3D lift: all three axes gain extent */
    surplus_real_t mnx=pts[0].x,mxx=pts[0].x,mny=pts[0].y,mxy=pts[0].y,mnz=pts[0].z,mxz=pts[0].z;
    for (int i=1;i<N;i++){
        if(pts[i].x<mnx)mnx=pts[i].x; if(pts[i].x>mxx)mxx=pts[i].x;
        if(pts[i].y<mny)mny=pts[i].y; if(pts[i].y>mxy)mxy=pts[i].y;
        if(pts[i].z<mnz)mnz=pts[i].z; if(pts[i].z>mxz)mxz=pts[i].z;
    }
    surplus_real_t ex=SR_SUB(mxx,mnx), ey=SR_SUB(mxy,mny), ez=SR_SUB(mxz,mnz);
    surplus_real_t eps = SR_FROM_FLOAT(0.2);
    int three_d = (ex>eps) && (ey>eps) && (ez>eps);   /* the 2D entered the 3rd dimension */

    /* (2) spiral with consistent chirality + (4) not extrusion: the direction from
     * one slice's reference to the next, projected off the trajectory, rotates the
     * same way every step (cross products aligned) and never stays parallel. */
    int spiral = 1, not_extrusion = 0;
    h_vec3 prevd = {SR_ZERO,SR_ZERO,SR_ZERO}; surplus_real_t chir_ref = SR_ZERO;
    for (int i=1;i<N;i++){
        h_vec3 d = h_sub(pts[i], pts[i-1]);
        d = helion_perpendicular(d, f.traj_dir);      /* the swirl component, off-axis */
        if (i>1){
            h_vec3 cr = h_cross(prevd, d);
            surplus_real_t chir = h_dot(cr, f.obj_spin);   /* which way it curls */
            if (i==2) chir_ref = chir;
            if ((chir>SR_ZERO) != (chir_ref>SR_ZERO)) spiral = 0;   /* chirality flipped */
            surplus_real_t pd = h_dot(h_norm(prevd), h_norm(d));
            if (pd < SR_FROM_FLOAT(0.999)) not_extrusion = 1;       /* directions rotated */
        }
        prevd = d;
    }

    return perpendicular && three_d && spiral && not_extrusion;
}
