/* mesh_net.c — P2P Mesh Network Layer implementation
 *
 * See mesh_net.h for design rationale. Follows the same conventions
 * as porter_house.c / mesh_token.c: SR_* fixed-point, M5 coordinates,
 * Porter House admission for peer joining, 168-bit peer IDs.
 *
 * Author: Michael Laurence Curzi (c)
 * 36N9 Genetics, LLC — All Rights Reserved
 * License: SEL-3.3 (kernel component)
 */
#include "mesh_net.h"
#include <string.h>

static bool peer_equal(const word168_t *a, const word168_t *b) {
    if (!a || !b) return false;
    for (int i = 0; i < WORD168_OCTETS; i++) {
        if (a->bytes[i] != b->bytes[i]) return false;
    }
    return true;
}

void mn_init(mesh_net_t *mn, uint32_t device_id, const char *name,
              porter_house_t *porter) {
    if (!mn) return;
    memset(mn, 0, sizeof(*mn));
    mn->device_id = device_id;

    uint32_t i;
    for (i = 0; i + 1 < MN_MAX_NAME_LEN && name && name[i]; i++) {
        mn->name[i] = name[i];
    }
    mn->name[i] = '\0';

    mn->porter = porter;
    mn->num_networks = 0;
    mn->num_routes = 0;
    mn->next_net_id = 1;
    mn->next_route_id = 1;

    mn->m5.omega = device_id;
    mn->m5.chi = device_id;
    mn->m5.phi = SR_ZERO;

    mn_update_coverage(mn);
}

int32_t mn_create_network(mesh_net_t *mn, const char *name,
                           mn_net_access_t access,
                           const word168_t *creator_id,
                           uint32_t creator_trust) {
    if (!mn || !name || !creator_id) return -1;

    uint32_t slot = MN_MAX_NETWORKS;
    for (uint32_t i = 0; i < MN_MAX_NETWORKS; i++) {
        if (!mn->networks[i].active) {
            slot = i;
            break;
        }
    }
    if (slot >= MN_MAX_NETWORKS) return -1;

    /* Porter House admission for network creation on mesh port */
    if (mn->porter) {
        if (!porter_house_admit(mn->porter, MN_MESH_PORT, creator_id, creator_trust)) {
            return -2;
        }
    }

    mn_network_t *net = &mn->networks[slot];
    memset(net, 0, sizeof(*net));
    net->id = mn->next_net_id++;
    net->active = true;
    net->state = MN_NET_ACTIVE;
    net->access = access;
    net->creator_id = *creator_id;
    net->creator_trust = creator_trust;

    uint32_t j;
    for (j = 0; j + 1 < MN_MAX_NAME_LEN && name[j]; j++) net->name[j] = name[j];
    net->name[j] = '\0';

    /* Creator is first peer */
    net->peers[0].peer_id = *creator_id;
    net->peers[0].trust_weight = creator_trust;
    net->peers[0].active = true;
    net->peers[0].is_gateway = false;
    net->num_peers = 1;

    mn->num_networks++;
    mn->total_peers_connected++;
    mn_update_coverage(mn);
    return (int32_t)net->id;
}

int32_t mn_join_network(mesh_net_t *mn, uint32_t network_id,
                         const word168_t *peer_id, uint32_t trust_weight,
                         uint64_t bandwidth) {
    if (!mn || !peer_id) return -1;
    mn_network_t *net = mn_get_network(mn, network_id);
    if (!net) return -1;
    if (net->state != MN_NET_ACTIVE) return -4;

    /* Check access control */
    if (net->access == MN_NET_ALLOWLIST || net->access == MN_NET_PRIVATE) {
        /* Only creator can invite for now (simplified) */
        if (!peer_equal(peer_id, &net->creator_id)) {
            /* Check if already a peer (re-joining is ok) */
            bool already = false;
            for (uint32_t i = 0; i < MN_MAX_PEERS_PER_NET; i++) {
                if (net->peers[i].active && peer_equal(&net->peers[i].peer_id, peer_id)) {
                    already = true;
                    break;
                }
            }
            if (!already) return -4;
        }
    }

    /* Check if already a peer */
    for (uint32_t i = 0; i < MN_MAX_PEERS_PER_NET; i++) {
        if (net->peers[i].active && peer_equal(&net->peers[i].peer_id, peer_id)) {
            /* Update bandwidth */
            net->peers[i].bandwidth_avail = bandwidth;
            net->peers[i].trust_weight = trust_weight;
            return 0;
        }
    }

    /* Find free slot */
    uint32_t slot = MN_MAX_PEERS_PER_NET;
    for (uint32_t i = 0; i < MN_MAX_PEERS_PER_NET; i++) {
        if (!net->peers[i].active) {
            slot = i;
            break;
        }
    }
    if (slot >= MN_MAX_PEERS_PER_NET) return -2;

    /* Porter House admission for trusted/private networks */
    if (mn->porter && net->access == MN_NET_TRUSTED) {
        if (!porter_house_admit(mn->porter, MN_MESH_PORT, peer_id, trust_weight)) {
            return -3;
        }
    }

    net->peers[slot].peer_id = *peer_id;
    net->peers[slot].trust_weight = trust_weight;
    net->peers[slot].bandwidth_avail = bandwidth;
    net->peers[slot].active = true;
    net->num_peers++;

    mn->total_peers_connected++;
    mn_update_coverage(mn);
    return 0;
}

int32_t mn_leave_network(mesh_net_t *mn, uint32_t network_id,
                          const word168_t *peer_id) {
    if (!mn || !peer_id) return -1;
    mn_network_t *net = mn_get_network(mn, network_id);
    if (!net) return -1;

    for (uint32_t i = 0; i < MN_MAX_PEERS_PER_NET; i++) {
        if (net->peers[i].active && peer_equal(&net->peers[i].peer_id, peer_id)) {
            net->peers[i].active = false;
            if (net->num_peers > 0) net->num_peers--;
            mn_update_coverage(mn);
            return 0;
        }
    }
    return -1;
}

int32_t mn_close_network(mesh_net_t *mn, uint32_t network_id) {
    if (!mn) return -1;
    mn_network_t *net = mn_get_network(mn, network_id);
    if (!net) return -1;
    net->state = MN_NET_CLOSED;
    mn_update_coverage(mn);
    return 0;
}

int32_t mn_create_route(mesh_net_t *mn, uint32_t network_id,
                         mn_route_type_t type,
                         const word168_t *source,
                         const word168_t *destination,
                         uint64_t price_per_unit, uint64_t capacity,
                         uint64_t current_cycle) {
    if (!mn || !source || !destination) return -1;
    mn_network_t *net = mn_get_network(mn, network_id);
    if (!net) return -2;

    uint32_t slot = MN_MAX_ROUTES;
    for (uint32_t i = 0; i < MN_MAX_ROUTES; i++) {
        if (!mn->routes[i].active) {
            slot = i;
            break;
        }
    }
    if (slot >= MN_MAX_ROUTES) return -1;

    mn_route_t *r = &mn->routes[slot];
    memset(r, 0, sizeof(*r));
    r->id = mn->next_route_id++;
    r->network_id = network_id;
    r->type = type;
    r->state = MN_ROUTE_ACTIVE;
    r->source = *source;
    r->destination = *destination;
    r->price_per_unit = price_per_unit;
    r->capacity = capacity;
    r->created_cycle = current_cycle;
    r->last_active_cycle = current_cycle;
    r->num_hops = 0;
    r->active = true;

    mn->num_routes++;
    mn->total_routes_created++;
    net->route_count++;
    mn_update_coverage(mn);
    return (int32_t)r->id;
}

int32_t mn_add_hop(mesh_net_t *mn, uint32_t route_id,
                    const word168_t *hop) {
    if (!mn || !hop) return -1;
    mn_route_t *r = mn_get_route(mn, route_id);
    if (!r || r->state != MN_ROUTE_ACTIVE) return -1;
    if (r->num_hops >= MN_MAX_HOPS) return -1;

    r->hops[r->num_hops] = *hop;
    r->num_hops++;
    return 0;
}

int32_t mn_send_data(mesh_net_t *mn, uint32_t route_id,
                      uint64_t data_size, uint64_t current_cycle) {
    if (!mn) return -1;
    mn_route_t *r = mn_get_route(mn, route_id);
    if (!r || r->state != MN_ROUTE_ACTIVE) return -1;

    r->data_transferred += data_size;
    r->last_active_cycle = current_cycle;
    r->revenue += (data_size * r->price_per_unit);

    mn_network_t *net = mn_get_network(mn, r->network_id);
    if (net) {
        net->total_data_routed += data_size;
    }

    mn->total_data_routed += data_size;
    mn->total_revenue += (data_size * r->price_per_unit);

    mn_update_coverage(mn);
    return 0;
}

int32_t mn_close_route(mesh_net_t *mn, uint32_t route_id) {
    if (!mn) return -1;
    mn_route_t *r = mn_get_route(mn, route_id);
    if (!r) return -1;
    r->state = MN_ROUTE_CLOSED;
    mn_update_coverage(mn);
    return 0;
}

uint32_t mn_check_expired(mesh_net_t *mn, uint64_t current_cycle) {
    if (!mn) return 0;
    uint32_t expired = 0;

    for (uint32_t i = 0; i < MN_MAX_ROUTES; i++) {
        mn_route_t *r = &mn->routes[i];
        if (!r->active) continue;
        if (r->state != MN_ROUTE_ACTIVE) continue;

        if (current_cycle - r->last_active_cycle >= MN_ROUTE_TIMEOUT) {
            r->state = MN_ROUTE_EXPIRED;
            mn->total_routes_expired++;
            expired++;

            mn_network_t *net = mn_get_network(mn, r->network_id);
            if (net && net->route_count > 0) net->route_count--;
        }
    }

    if (expired > 0) mn_update_coverage(mn);
    return expired;
}

int32_t mn_federate(mesh_net_t *mn, uint32_t net1_id, uint32_t net2_id) {
    if (!mn) return -1;
    mn_network_t *n1 = mn_get_network(mn, net1_id);
    mn_network_t *n2 = mn_get_network(mn, net2_id);
    if (!n1 || !n2 || n1 == n2) return -1;

    n1->state = MN_NET_FEDERATED;
    n2->state = MN_NET_FEDERATED;

    /* Mark gateway peers: creators become gateways */
    for (uint32_t i = 0; i < MN_MAX_PEERS_PER_NET; i++) {
        if (n1->peers[i].active && peer_equal(&n1->peers[i].peer_id, &n1->creator_id)) {
            n1->peers[i].is_gateway = true;
        }
        if (n2->peers[i].active && peer_equal(&n2->peers[i].peer_id, &n2->creator_id)) {
            n2->peers[i].is_gateway = true;
        }
    }

    mn_update_coverage(mn);
    return 0;
}

mn_network_t *mn_get_network(mesh_net_t *mn, uint32_t network_id) {
    if (!mn) return NULL;
    for (uint32_t i = 0; i < MN_MAX_NETWORKS; i++) {
        if (mn->networks[i].active && mn->networks[i].id == network_id) {
            return &mn->networks[i];
        }
    }
    return NULL;
}

mn_route_t *mn_get_route(mesh_net_t *mn, uint32_t route_id) {
    if (!mn) return NULL;
    for (uint32_t i = 0; i < MN_MAX_ROUTES; i++) {
        if (mn->routes[i].active && mn->routes[i].id == route_id) {
            return &mn->routes[i];
        }
    }
    return NULL;
}

surplus_real_t mn_update_coverage(mesh_net_t *mn) {
    if (!mn) return SR_ZERO;

    /* r: active route ratio = active routes / total routes */
    uint32_t active_routes = 0;
    uint32_t total_routes = 0;
    for (uint32_t i = 0; i < MN_MAX_ROUTES; i++) {
        if (!mn->routes[i].active) continue;
        total_routes++;
        if (mn->routes[i].state == MN_ROUTE_ACTIVE) active_routes++;
    }
    mn->m5.r = (total_routes == 0) ? SR_ONE
        : SR_DIV(SR_FROM_INT((int64_t)active_routes), SR_FROM_INT((int64_t)total_routes));

    /* ell: network utilization = active networks / total networks */
    uint32_t active_nets = 0;
    uint32_t total_nets = 0;
    for (uint32_t i = 0; i < MN_MAX_NETWORKS; i++) {
        if (!mn->networks[i].active) continue;
        total_nets++;
        if (mn->networks[i].state == MN_NET_ACTIVE ||
            mn->networks[i].state == MN_NET_FEDERATED) active_nets++;
    }
    mn->m5.ell = (total_nets == 0) ? SR_ZERO
        : SR_DIV(SR_FROM_INT((int64_t)active_nets), SR_FROM_INT((int64_t)total_nets));

    surplus_real_t product = SR_MUL(mn->m5.r, mn->m5.ell);
    surplus_real_t floor = SR_DIV(SR_FROM_INT(COVERAGE_FLOOR_NUM), SR_FROM_INT(COVERAGE_FLOOR_DEN));
    mn->coverage_ratio = SR_DIV(product, floor);

    return mn->coverage_ratio;
}
