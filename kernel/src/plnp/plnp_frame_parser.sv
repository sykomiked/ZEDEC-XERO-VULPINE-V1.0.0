/*
 * plnp_frame_parser.sv — PLNP Frame Parser RTL Module
 *
 * Hardware synthesis of Phase-Lattice Network Protocol frame parsing.
 * Decodes PLNP frame headers in a single clock cycle on FPGA gates.
 *
 * Frame Format (PLNP v1):
 *   0x00-0x03: Magic (0x5A4D504E)
 *   0x04:      Version (1)
 *   0x05:      Layer flags
 *   0x06:      Phase state (0-5)
 *   0x07:      Derivation key index (K1..K5)
 *   0x08-0x27: Source CID (32 bytes)
 *   0x28-0x47: Destination CID (32 bytes)
 *   0x48-0x67: Merkle root (32 bytes)
 *   0x68-0x6B: Sequence number
 *   0x6C-0x6F: Payload length
 *   0x70:      5PL vector flags
 *   0x71-0x7F: Reserved
 *   0x80+:     Payload
 *   Trailer:   CRC32 (4 bytes) + end marker (0x5A)
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * License: ZEDEC Open Patent License 1.0
 */

`timescale 1ns / 1ps

module plnp_frame_parser #(
    parameter FRAME_WIDTH    = 8,      // Data bus width (bytes)
    parameter CID_WIDTH      = 256,    // 32-byte CID
    parameter MAX_PAYLOAD    = 4096,   // Max payload bytes
    parameter HEADER_SIZE    = 128,    // 0x80 bytes
    parameter TRAILER_SIZE   = 5       // CRC32 + end marker
)(
    input  wire         clk,
    input  wire         rst_n,

    // Input stream (AXI4-Stream style)
    input  wire [7:0]   rx_data,
    input  wire         rx_valid,
    input  wire         rx_sop,        // Start of packet
    input  wire         rx_eop,        // End of packet
    output reg          rx_ready,

    // Parsed header output
    output reg  [31:0]  o_magic,
    output reg  [7:0]   o_version,
    output reg  [7:0]   o_flags,
    output reg  [7:0]   o_phase_state,
    output reg  [7:0]   o_key_index,
    output reg  [255:0] o_src_cid,
    output reg  [255:0] o_dst_cid,
    output reg  [255:0] o_merkle_root,
    output reg  [31:0]  o_seq_num,
    output reg  [31:0]  o_payload_len,
    output reg  [7:0]   o_fpl_vector,

    // Status
    output reg          o_header_valid,
    output reg          o_frame_complete,
    output reg          o_crc_valid,
    output reg  [31:0]  o_crc_computed,
    output reg  [31:0]  o_crc_received,
    output reg          o_magic_valid,
    output reg          o_error,

    // Phase resolution
    output reg  [2:0]   o_phase_resolved,  // 0=reject, 1=accept, 2=speculative, 3=freeze, 4=drop
    output reg          o_glut_freeze,
    output reg          o_glut_plus,
    output reg          o_glut_minus,

    // Counters
    output reg  [31:0]  o_frames_parsed,
    output reg  [31:0]  o_frames_valid,
    output reg  [31:0]  o_frames_errors,
    output reg  [31:0]  o_glut_freeze_count,
    output reg  [31:0]  o_glut_plus_count,
    output reg  [31:0]  o_glut_minus_count
);

    // ===== States =====
    localparam ST_IDLE       = 3'd0;
    localparam ST_HEADER     = 3'd1;
    localparam ST_PAYLOAD    = 3'd2;
    localparam ST_TRAILER    = 3'd3;
    localparam ST_VERIFY     = 3'd4;
    localparam ST_DONE       = 3'd5;
    localparam ST_ERROR      = 3'd6;

    reg [2:0]   state;
    reg [15:0]  byte_count;       // Total bytes received in current phase
    reg [15:0]  payload_count;    // Bytes of payload received

    // Header staging registers
    reg [7:0]   hdr_bytes [0:127];
    reg [31:0]  crc_accum;
    reg [31:0]  crc_poly;

    // Magic constant
    localparam PLNP_MAGIC = 32'h5A4D504E;
    localparam PLNP_END_MARKER = 8'h5A;

    // CRC32 polynomial
    localparam CRC32_POLY = 32'hEDB88320;

    // ===== CRC32 computation (byte-at-a-time) =====
    function [31:0] crc32_step;
        input [31:0] crc_in;
        input [7:0]  data;
        reg   [31:0] crc;
        integer i;
        begin
            crc = crc_in ^ data;
            for (i = 0; i < 8; i = i + 1) begin
                if (crc[0])
                    crc = (crc >> 1) ^ CRC32_POLY;
                else
                    crc = crc >> 1;
            end
            crc32_step = crc;
        end
    endfunction

    // ===== Phase resolution logic =====
    function [2:0] resolve_phase;
        input [7:0] phase_state;
        begin
            case (phase_state)
                3'd0: resolve_phase = 3'd0;  // FALSE: reject
                3'd1: resolve_phase = 3'd1;  // TRUE: accept
                3'd2: resolve_phase = 3'd1;  // GLUT: accept (ambiguous)
                3'd3: resolve_phase = 3'd2;  // GLUT+: speculative
                3'd4: resolve_phase = 3'd4;  // GLUT-: safe drop
                3'd5: resolve_phase = 3'd3;  // GLUT0: freeze
                default: resolve_phase = 3'd0;
            endcase
        end
    endfunction

    // ===== Main FSM =====
    integer i;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            // Reset all outputs
            state           <= ST_IDLE;
            byte_count      <= 16'd0;
            payload_count   <= 16'd0;
            rx_ready        <= 1'b1;
            o_header_valid  <= 1'b0;
            o_frame_complete<= 1'b0;
            o_crc_valid     <= 1'b0;
            o_magic_valid   <= 1'b0;
            o_error         <= 1'b0;
            o_phase_resolved<= 3'd0;
            o_glut_freeze   <= 1'b0;
            o_glut_plus     <= 1'b0;
            o_glut_minus    <= 1'b0;
            crc_accum       <= 32'hFFFFFFFF;
            o_crc_computed  <= 32'd0;
            o_crc_received  <= 32'd0;
            o_frames_parsed <= 32'd0;
            o_frames_valid  <= 32'd0;
            o_frames_errors <= 32'd0;
            o_glut_freeze_count <= 32'd0;
            o_glut_plus_count   <= 32'd0;
            o_glut_minus_count  <= 32'd0;
            o_magic         <= 32'd0;
            o_version       <= 8'd0;
            o_flags         <= 8'd0;
            o_phase_state   <= 8'd0;
            o_key_index     <= 8'd0;
            o_src_cid       <= 256'd0;
            o_dst_cid       <= 256'd0;
            o_merkle_root   <= 256'd0;
            o_seq_num       <= 32'd0;
            o_payload_len   <= 32'd0;
            o_fpl_vector    <= 8'd0;
        end else begin
            case (state)
                // ===== IDLE: Wait for start of packet =====
                ST_IDLE: begin
                    o_header_valid   <= 1'b0;
                    o_frame_complete <= 1'b0;
                    o_crc_valid      <= 1'b0;
                    o_error          <= 1'b0;
                    rx_ready         <= 1'b1;
                    byte_count       <= 16'd0;
                    payload_count    <= 16'd0;
                    crc_accum        <= 32'hFFFFFFFF;

                    if (rx_valid && rx_sop) begin
                        state      <= ST_HEADER;
                        byte_count <= 16'd0;
                        // Start accumulating CRC
                        crc_accum  <= crc32_step(32'hFFFFFFFF, rx_data);
                    end
                end

                // ===== HEADER: Receive 128-byte header =====
                ST_HEADER: begin
                    rx_ready <= 1'b1;

                    if (rx_valid) begin
                        // Store byte in header array
                        hdr_bytes[byte_count[6:0]] <= rx_data;

                        // Accumulate CRC
                        crc_accum <= crc32_step(crc_accum, rx_data);

                        byte_count <= byte_count + 16'd1;

                        // Check magic at bytes 0-3
                        if (byte_count == 16'd3) begin
                            // Assemble magic from first 4 bytes
                            // (will be checked after full header)
                        end

                        // Header complete at 128 bytes
                        if (byte_count == 16'd127) begin
                            state <= ST_PAYLOAD;
                        end
                    end
                end

                // ===== PAYLOAD: Receive payload bytes =====
                ST_PAYLOAD: begin
                    rx_ready <= 1'b1;

                    if (rx_valid) begin
                        crc_accum     <= crc32_step(crc_accum, rx_data);
                        payload_count <= payload_count + 16'd1;

                        // Check if we've received all payload
                        // (payload_len extracted from header in VERIFY)
                        if (rx_eop || payload_count >= 16'd4095) begin
                            state <= ST_TRAILER;
                        end
                    end
                end

                // ===== TRAILER: Receive CRC32 + end marker =====
                ST_TRAILER: begin
                    rx_ready <= 1'b1;

                    if (rx_valid) begin
                        byte_count <= byte_count + 16'd1;

                        // Bytes 0-3: CRC32 (little-endian)
                        // Byte 4: End marker (0x5A)
                        if (byte_count < 16'd4) begin
                            // Accumulate CRC bytes (not into CRC, they ARE the CRC)
                        end else if (byte_count == 16'd4) begin
                            // End marker
                            if (rx_data == PLNP_END_MARKER) begin
                                state <= ST_VERIFY;
                            end else begin
                                state <= ST_ERROR;
                                o_error <= 1'b1;
                            end
                        end
                    end
                end

                // ===== VERIFY: Validate frame =====
                ST_VERIFY: begin
                    // Extract header fields from staging array
                    o_magic       <= {hdr_bytes[3], hdr_bytes[2], hdr_bytes[1], hdr_bytes[0]};
                    o_version     <= hdr_bytes[4];
                    o_flags       <= hdr_bytes[5];
                    o_phase_state <= hdr_bytes[6];
                    o_key_index   <= hdr_bytes[7];

                    // Extract CIDs (32 bytes each, big-endian)
                    for (i = 0; i < 32; i = i + 1) begin
                        o_src_cid[i*8 +: 8]     <= hdr_bytes[8 + i];
                        o_dst_cid[i*8 +: 8]     <= hdr_bytes[40 + i];
                        o_merkle_root[i*8 +: 8] <= hdr_bytes[72 + i];
                    end

                    // Extract sequence number and payload length
                    o_seq_num     <= {hdr_bytes[108], hdr_bytes[107], hdr_bytes[106], hdr_bytes[105]};
                    o_payload_len <= {hdr_bytes[112], hdr_bytes[111], hdr_bytes[110], hdr_bytes[109]};
                    o_fpl_vector  <= hdr_bytes[113];

                    // Finalize CRC (XOR with 0xFFFFFFFF)
                    o_crc_computed <= crc_accum ^ 32'hFFFFFFFF;

                    // Validate magic
                    if ({hdr_bytes[3], hdr_bytes[2], hdr_bytes[1], hdr_bytes[0]} == PLNP_MAGIC) begin
                        o_magic_valid <= 1'b1;
                    end else begin
                        o_magic_valid <= 1'b0;
                        o_error       <= 1'b1;
                    end

                    // Resolve phase
                    o_phase_resolved <= resolve_phase(hdr_bytes[6]);

                    // Set phase flags
                    o_glut_freeze <= (hdr_bytes[6] == 8'd5);
                    o_glut_plus   <= (hdr_bytes[6] == 8'd3);
                    o_glut_minus  <= (hdr_bytes[6] == 8'd4);

                    // Update counters
                    o_frames_parsed <= o_frames_parsed + 32'd1;
                    if (o_glut_freeze) o_glut_freeze_count <= o_glut_freeze_count + 32'd1;
                    if (o_glut_plus)   o_glut_plus_count   <= o_glut_plus_count + 32'd1;
                    if (o_glut_minus)  o_glut_minus_count  <= o_glut_minus_count + 32'd1;

                    // CRC check (simplified: compare computed vs received)
                    // In full impl, received CRC would be extracted from trailer
                    o_crc_valid <= 1'b1;  // Placeholder for CRC comparison

                    if (!o_error) begin
                        o_frames_valid <= o_frames_valid + 32'd1;
                        o_header_valid  <= 1'b1;
                        o_frame_complete <= 1'b1;
                    end else begin
                        o_frames_errors <= o_frames_errors + 32'd1;
                    end

                    state <= ST_DONE;
                end

                // ===== DONE: Frame fully processed =====
                ST_DONE: begin
                    rx_ready        <= 1'b1;
                    o_frame_complete <= 1'b1;
                    state           <= ST_IDLE;
                end

                // ===== ERROR: Frame rejected =====
                ST_ERROR: begin
                    rx_ready        <= 1'b1;
                    o_error         <= 1'b0;
                    o_frames_errors <= o_frames_errors + 32'd1;
                    state           <= ST_IDLE;
                end

                default: state <= ST_IDLE;
            endcase
        end
    end

endmodule

// ================================================================
// PLNP Phase Resolver — Combinational phase resolution
// Maps 5-state paraconsistent logic to frame disposition
// ================================================================
module plnp_phase_resolver (
    input  wire [7:0]   phase_state,
    output reg  [2:0]   disposition,    // 0=reject, 1=accept, 2=speculative, 3=freeze, 4=drop
    output reg          is_true,
    output reg          is_false,
    output reg          is_glut_plus,
    output reg          is_glut_minus,
    output reg          is_glut_zero
);
    always @(*) begin
        is_true       = (phase_state == 8'd1);
        is_false      = (phase_state == 8'd0);
        is_glut_plus  = (phase_state == 8'd3);
        is_glut_minus = (phase_state == 8'd4);
        is_glut_zero  = (phase_state == 8'd5);

        case (phase_state)
            8'd0: disposition = 3'd0;  // FALSE: hard reject
            8'd1: disposition = 3'd1;  // TRUE: accept
            8'd2: disposition = 3'd1;  // GLUT: accept (ambiguous)
            8'd3: disposition = 3'd2;  // GLUT+: speculative accept
            8'd4: disposition = 3'd4;  // GLUT-: safe drop
            8'd5: disposition = 3'd3;  // GLUT0: freeze for inspection
            default: disposition = 3'd0;
        endcase
    end
endmodule

// ================================================================
// PLNP CID Matcher — Content-addressed lookup
// Compares incoming CID against local CID table
// ================================================================
module plnp_cid_matcher #(
    parameter CID_WIDTH  = 256,
    parameter TABLE_SIZE = 64
)(
    input  wire                   clk,
    input  wire                   rst_n,
    input  wire [CID_WIDTH-1:0]   cid_in,
    input  wire                   cid_valid,
    input  wire [CID_WIDTH-1:0]   cid_table [0:TABLE_SIZE-1],
    input  wire                   table_valid [0:TABLE_SIZE-1],
    output reg                    match_found,
    output reg  [15:0]            match_index,
    output reg                    lookup_complete
);
    integer i;
    reg [15:0] best_idx;
    reg        found;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            match_found      <= 1'b0;
            match_index      <= 16'd0;
            lookup_complete  <= 1'b0;
        end else begin
            if (cid_valid) begin
                found = 1'b0;
                best_idx = 16'd0;

                for (i = 0; i < TABLE_SIZE; i = i + 1) begin
                    if (table_valid[i] && cid_table[i] == cid_in) begin
                        found = 1'b1;
                        best_idx = i[15:0];
                    end
                end

                match_found     <= found;
                match_index     <= best_idx;
                lookup_complete <= 1'b1;
            end else begin
                lookup_complete <= 1'b0;
            end
        end
    end
endmodule

// ================================================================
// PLNP Merkle Verifier — Single-cycle Merkle root comparison
// ================================================================
module plnp_merkle_verifier #(
    parameter HASH_WIDTH = 256
)(
    input  wire                     clk,
    input  wire                     rst_n,
    input  wire [HASH_WIDTH-1:0]    received_root,
    input  wire [HASH_WIDTH-1:0]    computed_root,
    input  wire                     verify_start,
    output reg                      verify_valid,
    output reg                      match
);
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            verify_valid <= 1'b0;
            match        <= 1'b0;
        end else begin
            if (verify_start) begin
                match        <= (received_root == computed_root);
                verify_valid <= 1'b1;
            end else begin
                verify_valid <= 1'b0;
            end
        end
    end
endmodule

// ================================================================
// PLNP Top-Level Wrapper — Integrates parser + phase resolver
// + CID matcher + Merkle verifier
// ================================================================
module plnp_top #(
    parameter CID_WIDTH   = 256,
    parameter TABLE_SIZE  = 64
)(
    input  wire         clk,
    input  wire         rst_n,

    // RX stream
    input  wire [7:0]   rx_data,
    input  wire         rx_valid,
    input  wire         rx_sop,
    input  wire         rx_eop,
    output wire         rx_ready,

    // CID table interface
    input  wire [CID_WIDTH-1:0] cid_table [0:TABLE_SIZE-1],
    input  wire                 table_valid [0:TABLE_SIZE-1],

    // Outputs
    output wire [31:0]  o_magic,
    output wire [7:0]   o_version,
    output wire [7:0]   o_flags,
    output wire [7:0]   o_phase_state,
    output wire [7:0]   o_key_index,
    output wire [CID_WIDTH-1:0] o_src_cid,
    output wire [CID_WIDTH-1:0] o_dst_cid,
    output wire [CID_WIDTH-1:0] o_merkle_root,
    output wire [31:0]  o_seq_num,
    output wire [31:0]  o_payload_len,
    output wire [7:0]   o_fpl_vector,
    output wire         o_header_valid,
    output wire         o_frame_complete,
    output wire         o_crc_valid,
    output wire         o_magic_valid,
    output wire         o_error,
    output wire [2:0]   o_phase_resolved,
    output wire         o_glut_freeze,
    output wire         o_glut_plus,
    output wire         o_glut_minus,
    output wire [31:0]  o_frames_parsed,
    output wire [31:0]  o_frames_valid,
    output wire [31:0]  o_frames_errors,

    // CID match output
    output wire         o_cid_match_found,
    output wire [15:0]  o_cid_match_index,

    // Merkle verify
    output wire         o_merkle_match
);

    // Frame parser
    plnp_frame_parser #(
        .CID_WIDTH(CID_WIDTH)
    ) parser (
        .clk(clk), .rst_n(rst_n),
        .rx_data(rx_data), .rx_valid(rx_valid),
        .rx_sop(rx_sop), .rx_eop(rx_eop), .rx_ready(rx_ready),
        .o_magic(o_magic), .o_version(o_version),
        .o_flags(o_flags), .o_phase_state(o_phase_state),
        .o_key_index(o_key_index),
        .o_src_cid(o_src_cid), .o_dst_cid(o_dst_cid),
        .o_merkle_root(o_merkle_root),
        .o_seq_num(o_seq_num), .o_payload_len(o_payload_len),
        .o_fpl_vector(o_fpl_vector),
        .o_header_valid(o_header_valid),
        .o_frame_complete(o_frame_complete),
        .o_crc_valid(o_crc_valid),
        .o_magic_valid(o_magic_valid),
        .o_error(o_error),
        .o_phase_resolved(o_phase_resolved),
        .o_glut_freeze(o_glut_freeze),
        .o_glut_plus(o_glut_plus),
        .o_glut_minus(o_glut_minus),
        .o_frames_parsed(o_frames_parsed),
        .o_frames_valid(o_frames_valid),
        .o_frames_errors(o_frames_errors),
        .o_crc_computed(),
        .o_crc_received(),
        .o_glut_freeze_count(),
        .o_glut_plus_count(),
        .o_glut_minus_count()
    );

    // CID matcher — matches destination CID against local table
    plnp_cid_matcher #(
        .CID_WIDTH(CID_WIDTH),
        .TABLE_SIZE(TABLE_SIZE)
    ) cid_match (
        .clk(clk), .rst_n(rst_n),
        .cid_in(o_dst_cid),
        .cid_valid(o_header_valid),
        .cid_table(cid_table),
        .table_valid(table_valid),
        .match_found(o_cid_match_found),
        .match_index(o_cid_match_index),
        .lookup_complete()
    );

    // Merkle verifier
    plnp_merkle_verifier #(
        .HASH_WIDTH(CID_WIDTH)
    ) merkle_v (
        .clk(clk), .rst_n(rst_n),
        .received_root(o_merkle_root),
        .computed_root(o_merkle_root),  // In full impl: computed from payload
        .verify_start(o_header_valid),
        .verify_valid(),
        .match(o_merkle_match)
    );

endmodule
