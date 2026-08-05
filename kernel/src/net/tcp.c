/* tcp.c — TCP endpoint. See tcp.h. */
#include "tcp.h"

static void cpy(uint8_t *d, const uint8_t *s, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) d[i] = s[i];
}
static void zero(uint8_t *d, uint32_t n) { for (uint32_t i = 0; i < n; i++) d[i] = 0; }

static uint16_t g16(const uint8_t *p) { return (uint16_t)(((uint16_t)p[0] << 8) | p[1]); }
static uint32_t g32(const uint8_t *p) {
    return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];
}
static void p16(uint8_t *p, uint16_t v) { p[0]=(uint8_t)(v>>8); p[1]=(uint8_t)v; }
static void p32(uint8_t *p, uint32_t v) {
    p[0]=(uint8_t)(v>>24); p[1]=(uint8_t)(v>>16); p[2]=(uint8_t)(v>>8); p[3]=(uint8_t)v;
}

uint16_t tcp_checksum(const uint8_t src_ip[4], const uint8_t dst_ip[4],
                      const uint8_t *seg, uint32_t len) {
    uint32_t sum = 0;
    sum += ((uint32_t)src_ip[0] << 8) | src_ip[1];
    sum += ((uint32_t)src_ip[2] << 8) | src_ip[3];
    sum += ((uint32_t)dst_ip[0] << 8) | dst_ip[1];
    sum += ((uint32_t)dst_ip[2] << 8) | dst_ip[3];
    sum += 6u;                       /* protocol */
    sum += len & 0xFFFFu;
    for (uint32_t i = 0; i < len; i += 2) {
        uint16_t w = (uint16_t)seg[i] << 8;
        if (i + 1 < len) w |= seg[i + 1];
        sum += w;
    }
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)(~sum & 0xFFFF);
}

void tcp_init(tcp_conn_t *c, const uint8_t local_ip[4], uint16_t local_port,
              const uint8_t remote_ip[4], uint16_t remote_port, uint32_t iss) {
    if (!c) return;
    zero((uint8_t *)c, sizeof(*c));
    c->state = TCPS_CLOSED;
    if (local_ip)  cpy(c->local_ip,  local_ip,  4);
    if (remote_ip) cpy(c->remote_ip, remote_ip, 4);
    c->local_port  = local_port;
    c->remote_port = remote_port;
    c->snd_una = iss;
    c->snd_nxt = iss;
    c->snd_wnd = TCP_RCV_BUF;
    c->mss     = TCP_DEFAULT_MSS;
}

void tcp_listen(tcp_conn_t *c) { if (c) c->state = TCPS_LISTEN; }

/* Emit a segment: header, optional MSS option, optional payload, checksum. */
static uint32_t emit(tcp_conn_t *c, uint8_t *out, uint32_t cap, uint8_t flags,
                     const uint8_t *payload, uint32_t plen, bool with_mss) {
    uint32_t hdr = TCP_HDR_MIN + (with_mss ? 4u : 0u);
    if (!out || cap < hdr + plen) return 0;

    zero(out, hdr);
    p16(out + 0, c->local_port);
    p16(out + 2, c->remote_port);
    p32(out + 4, c->snd_nxt);
    p32(out + 8, (flags & TCPF_ACK) ? c->rcv_nxt : 0u);
    out[12] = (uint8_t)((hdr / 4u) << 4);
    out[13] = flags;
    /* Advertise what is actually free, not a constant: claiming space we do
     * not have invites the peer to send data we must then drop. */
    p16(out + 14, (uint16_t)(TCP_RCV_BUF - c->rx_len));
    p16(out + 16, 0);                    /* checksum, filled below */
    p16(out + 18, 0);                    /* urgent pointer */

    if (with_mss) {
        out[20] = 2; out[21] = 4;
        p16(out + 22, TCP_MAX_SEG);
    }
    if (payload && plen) cpy(out + hdr, payload, plen);

    p16(out + 16, tcp_checksum(c->local_ip, c->remote_ip, out, hdr + plen));
    return hdr + plen;
}

uint32_t tcp_connect(tcp_conn_t *c, uint8_t *out, uint32_t cap) {
    if (!c || c->state != TCPS_CLOSED) return 0;
    uint32_t n = emit(c, out, cap, TCPF_SYN, 0, 0, true);
    if (!n) return 0;
    c->state = TCPS_SYN_SENT;
    c->retx_seq   = c->snd_nxt;
    c->retx_len   = 0;          /* SYN carries no data but still needs retry */
    c->retx_ticks = 0;
    c->retx_count = 0;
    c->snd_nxt++;               /* SYN consumes one sequence number */
    return n;
}

/* Walk the options of a received segment. Only MSS is acted on; everything
 * else is skipped. The option list is attacker-controlled, so every length is
 * checked against the header length before it is followed. */
static void parse_options(tcp_conn_t *c, const uint8_t *seg, uint32_t hdr_len) {
    uint32_t at = TCP_HDR_MIN;
    while (at < hdr_len) {
        uint8_t kind = seg[at];
        if (kind == 0) break;                 /* end of option list */
        if (kind == 1) { at++; continue; }     /* no-op padding */
        if (at + 1u >= hdr_len) break;         /* truncated: no length byte */
        uint8_t olen = seg[at + 1];
        if (olen < 2u || at + olen > hdr_len) break;   /* lying length */
        if (kind == 2 && olen == 4) {
            uint16_t m = g16(seg + at + 2);
            /* A zero or absurd MSS would make us emit useless segments. */
            if (m >= 88u && m <= TCP_MAX_SEG) c->mss = m;
        }
        at += olen;
    }
}

/* Drop the bytes the peer has now acknowledged out of the retransmit buffer. */
static void ack_data(tcp_conn_t *c, uint32_t ack) {
    if (!tcp_seq_lt(c->snd_una, ack)) return;          /* nothing new */
    if (tcp_seq_lt(c->snd_nxt, ack)) return;           /* acks what we never sent */
    c->snd_una = ack;
    if (c->retx_len) {
        if (tcp_seq_leq(c->retx_seq + c->retx_len, ack)) {
            c->retx_len = 0;                          /* fully acknowledged */
        } else if (tcp_seq_lt(c->retx_seq, ack)) {    /* partially */
            uint32_t off = ack - c->retx_seq;
            for (uint32_t i = 0; i + off < c->retx_len; i++) c->retx[i] = c->retx[i + off];
            c->retx_len -= off;
            c->retx_seq  = ack;
        }
    }
    c->retx_ticks = 0;
    c->retx_count = 0;
    if (c->fin_sent && tcp_seq_leq(c->snd_nxt, ack)) c->fin_acked = true;
}

uint32_t tcp_input(tcp_conn_t *c, const uint8_t *seg, uint32_t len,
                   uint8_t *out, uint32_t cap) {
    if (!c || !seg || len < TCP_HDR_MIN) return 0;

    uint32_t hdr_len = (uint32_t)(seg[12] >> 4) * 4u;
    /* The data offset is where the previous implementation went wrong: it
     * assumed 20 and handed the peer's options to the application as data. */
    if (hdr_len < TCP_HDR_MIN || hdr_len > len) return 0;

    uint16_t sport = g16(seg + 0);
    uint16_t dport = g16(seg + 2);
    uint32_t seq   = g32(seg + 4);
    uint32_t ack   = g32(seg + 8);
    uint8_t  flags = seg[13];
    uint16_t wnd   = g16(seg + 14);

    /* The segment must belong to THIS connection. Without this check any
     * segment reaching the interface would drive the state machine. */
    if (dport != c->local_port) return 0;
    if (c->state != TCPS_LISTEN && c->remote_port && sport != c->remote_port) return 0;

    const uint8_t *payload = seg + hdr_len;
    uint32_t plen = len - hdr_len;

    if (flags & TCPF_RST) {
        /* Only accept a reset that falls in our window — otherwise anyone who
         * can guess the four-tuple can tear the connection down. */
        if (c->state == TCPS_SYN_SENT) {
            if ((flags & TCPF_ACK) && ack == c->snd_nxt) {
                c->state = TCPS_CLOSED; c->reset = true;
            }
        } else if (seq == c->rcv_nxt) {
            c->state = TCPS_CLOSED; c->reset = true;
        }
        return 0;
    }

    switch (c->state) {

    case TCPS_LISTEN:
        if (flags & TCPF_SYN) {
            /* The remote ADDRESS comes from the IP header, which this module
             * never sees — the caller sets c->remote_ip before feeding a SYN
             * to a listening connection. The port is right here in the segment. */
            c->remote_port = sport;
            c->rcv_nxt = seq + 1u;
            c->snd_wnd = wnd;
            parse_options(c, seg, hdr_len);
            c->state = TCPS_SYN_RCVD;
            uint32_t n = emit(c, out, cap, TCPF_SYN | TCPF_ACK, 0, 0, true);
            c->retx_seq = c->snd_nxt; c->retx_len = 0;
            c->retx_ticks = 0; c->retx_count = 0;
            c->snd_nxt++;
            return n;
        }
        return 0;

    case TCPS_SYN_SENT:
        if ((flags & TCPF_SYN) && (flags & TCPF_ACK)) {
            if (ack != c->snd_nxt) return 0;         /* not our SYN */
            c->rcv_nxt = seq + 1u;
            c->snd_una = ack;
            c->snd_wnd = wnd;
            parse_options(c, seg, hdr_len);
            c->state = TCPS_ESTABLISHED;
            c->retx_len = 0; c->retx_count = 0; c->retx_ticks = 0;
            return emit(c, out, cap, TCPF_ACK, 0, 0, false);
        }
        if (flags & TCPF_SYN) {                      /* simultaneous open */
            c->rcv_nxt = seq + 1u;
            c->snd_wnd = wnd;
            parse_options(c, seg, hdr_len);
            c->state = TCPS_SYN_RCVD;
            return emit(c, out, cap, TCPF_SYN | TCPF_ACK, 0, 0, true);
        }
        return 0;

    case TCPS_SYN_RCVD:
        if (flags & TCPF_ACK) {
            if (ack != c->snd_nxt) return 0;
            c->snd_una = ack;
            c->snd_wnd = wnd;
            c->state = TCPS_ESTABLISHED;
            c->retx_len = 0; c->retx_count = 0; c->retx_ticks = 0;
        }
        break;

    default:
        break;
    }

    if (c->state == TCPS_CLOSED || c->state == TCPS_LISTEN ||
        c->state == TCPS_SYN_SENT || c->state == TCPS_SYN_RCVD)
        return 0;

    if (flags & TCPF_ACK) { ack_data(c, ack); c->snd_wnd = wnd; }

    /* ---- receive data, in order only ---- */
    bool need_ack = false;
    if (plen) {
        if (seq == c->rcv_nxt) {
            uint32_t room = TCP_RCV_BUF - c->rx_len;
            uint32_t take = plen < room ? plen : room;
            cpy(c->rx + c->rx_len, payload, take);
            c->rx_len += take;
            c->rcv_nxt += take;
            need_ack = true;
        } else {
            /* Out of order or already seen. Re-ACK what we do have so the peer
             * retransmits from the right place rather than stalling. */
            need_ack = true;
        }
    }

    /* ---- FIN handling ---- */
    if (flags & TCPF_FIN) {
        /* A FIN only counts once we have consumed everything before it. */
        uint32_t fin_seq = seq + plen;
        if (fin_seq == c->rcv_nxt) {
            c->rcv_nxt++;
            c->peer_fin = true;
            need_ack = true;
            switch (c->state) {
            case TCPS_ESTABLISHED: c->state = TCPS_CLOSE_WAIT; break;
            case TCPS_FIN_WAIT_1:
                c->state = c->fin_acked ? TCPS_TIME_WAIT : TCPS_CLOSING;
                if (c->state == TCPS_TIME_WAIT) c->wait_ticks = TCP_TIME_WAIT_TICKS;
                break;
            case TCPS_FIN_WAIT_2:
                c->state = TCPS_TIME_WAIT;
                c->wait_ticks = TCP_TIME_WAIT_TICKS;
                break;
            default: break;
            }
        }
    }

    /* ---- our own FIN being acknowledged ---- */
    if ((flags & TCPF_ACK) && c->fin_acked) {
        if (c->state == TCPS_FIN_WAIT_1) c->state = TCPS_FIN_WAIT_2;
        else if (c->state == TCPS_CLOSING) {
            c->state = TCPS_TIME_WAIT; c->wait_ticks = TCP_TIME_WAIT_TICKS;
        } else if (c->state == TCPS_LAST_ACK) c->state = TCPS_CLOSED;
    }

    if (need_ack) return emit(c, out, cap, TCPF_ACK, 0, 0, false);
    return 0;
}

uint32_t tcp_send(tcp_conn_t *c, const uint8_t *data, uint32_t len,
                  uint8_t *out, uint32_t cap, uint32_t *consumed) {
    if (consumed) *consumed = 0;
    if (!c || !data || !len) return 0;
    if (c->state != TCPS_ESTABLISHED && c->state != TCPS_CLOSE_WAIT) return 0;
    if (c->retx_len) return 0;               /* wait for the last chunk to be acked */

    uint32_t limit = c->mss;
    if (limit > TCP_MAX_SEG) limit = TCP_MAX_SEG;
    if (limit > TCP_SND_BUF) limit = TCP_SND_BUF;
    if (c->snd_wnd && limit > c->snd_wnd) limit = c->snd_wnd;
    if (!limit) return 0;                    /* the peer's window is shut */
    uint32_t take = len < limit ? len : limit;

    uint32_t n = emit(c, out, cap, TCPF_ACK | TCPF_PSH, data, take, false);
    if (!n) return 0;

    cpy(c->retx, data, take);
    c->retx_len   = take;
    c->retx_seq   = c->snd_nxt;
    c->retx_ticks = 0;
    c->retx_count = 0;
    c->snd_nxt   += take;
    if (consumed) *consumed = take;
    return n;
}

uint32_t tcp_close(tcp_conn_t *c, uint8_t *out, uint32_t cap) {
    if (!c) return 0;
    if (c->state != TCPS_ESTABLISHED && c->state != TCPS_CLOSE_WAIT &&
        c->state != TCPS_SYN_RCVD) return 0;
    uint32_t n = emit(c, out, cap, TCPF_FIN | TCPF_ACK, 0, 0, false);
    if (!n) return 0;
    c->state = (c->state == TCPS_CLOSE_WAIT) ? TCPS_LAST_ACK : TCPS_FIN_WAIT_1;
    c->fin_sent = true;
    c->snd_nxt++;                            /* FIN consumes a sequence number */
    c->retx_ticks = 0;
    c->retx_count = 0;
    return n;
}

uint32_t tcp_reset(tcp_conn_t *c, uint8_t *out, uint32_t cap) {
    if (!c) return 0;
    uint32_t n = emit(c, out, cap, TCPF_RST | TCPF_ACK, 0, 0, false);
    c->state = TCPS_CLOSED;
    c->reset = true;
    return n;
}

uint32_t tcp_tick(tcp_conn_t *c, uint8_t *out, uint32_t cap) {
    if (!c) return 0;

    if (c->state == TCPS_TIME_WAIT) {
        if (c->wait_ticks) c->wait_ticks--;
        if (!c->wait_ticks) c->state = TCPS_CLOSED;
        return 0;
    }

    bool awaiting = (c->state == TCPS_SYN_SENT) || (c->state == TCPS_SYN_RCVD) ||
                    (c->retx_len > 0) ||
                    (c->fin_sent && !c->fin_acked);
    if (!awaiting) return 0;

    c->retx_ticks++;
    /* Exponential backoff: 1, 2, 4, 8 ... ticks between attempts. */
    uint32_t due = 1u << (c->retx_count < 5u ? c->retx_count : 5u);
    if (c->retx_ticks < due) return 0;

    c->retx_ticks = 0;
    if (++c->retx_count > TCP_MAX_RETX) {
        /* Say the connection is dead rather than retrying forever. */
        c->state = TCPS_CLOSED;
        c->reset = true;
        return 0;
    }

    uint32_t saved = c->snd_nxt;
    uint32_t n = 0;
    if (c->state == TCPS_SYN_SENT) {
        c->snd_nxt = c->snd_una;
        n = emit(c, out, cap, TCPF_SYN, 0, 0, true);
    } else if (c->state == TCPS_SYN_RCVD) {
        c->snd_nxt = c->snd_una;
        n = emit(c, out, cap, TCPF_SYN | TCPF_ACK, 0, 0, true);
    } else if (c->retx_len) {
        c->snd_nxt = c->retx_seq;
        n = emit(c, out, cap, TCPF_ACK | TCPF_PSH, c->retx, c->retx_len, false);
    } else {                                  /* an unacknowledged FIN */
        c->snd_nxt = c->snd_una;
        n = emit(c, out, cap, TCPF_FIN | TCPF_ACK, 0, 0, false);
    }
    c->snd_nxt = saved;
    return n;
}

uint32_t tcp_read(tcp_conn_t *c, uint8_t *dst, uint32_t cap) {
    if (!c || !dst || !cap || !c->rx_len) return 0;
    uint32_t n = c->rx_len < cap ? c->rx_len : cap;
    cpy(dst, c->rx, n);
    if (n < c->rx_len) {
        for (uint32_t i = 0; i + n < c->rx_len; i++) c->rx[i] = c->rx[i + n];
        c->rx_len -= n;
    } else {
        c->rx_len = 0;
    }
    return n;
}
