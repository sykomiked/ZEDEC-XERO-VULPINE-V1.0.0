#!/bin/bash
# tol_vovina_pipeline.sh — Automated development pipeline for TOL VOVINA Kernel
# Builds, boots, tests, and reports. Designed to run locally via Docker.
#
# Usage:
#   ./tol_vovina_pipeline.sh           # full pipeline
#   ./tol_vovina_pipeline.sh --build   # build only
#   ./tol_vovina_pipeline.sh --boot    # build + boot test
#   ./tol_vovina_pipeline.sh --test     # build + host-side unit tests
#   ./tol_vovina_pipeline.sh --report   # generate status report
#
# Author: H.M. Michael-Laurence: Curzi (c)
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
KERNEL_DIR="$SCRIPT_DIR/kernel"
IMAGE_NAME="tol-vovina-builder"
OUTPUT_DIR="$SCRIPT_DIR/build_output"
LOG_DIR="$SCRIPT_DIR/build_logs"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)

mkdir -p "$OUTPUT_DIR" "$LOG_DIR"

BUILD_LOG="$LOG_DIR/build_${TIMESTAMP}.log"
BOOT_LOG="$LOG_DIR/boot_${TIMESTAMP}.log"
TEST_LOG="$LOG_DIR/test_${TIMESTAMP}.log"
REPORT_FILE="$OUTPUT_DIR/pipeline_report_${TIMESTAMP}.md"

MODE="${1:---full}"

log() { echo "[$(date '+%H:%M:%S')] $1" | tee -a "$BUILD_LOG"; }

# ---- Phase 1: Docker image ----
ensure_docker() {
    log "Checking Docker..."
    if ! docker info >/dev/null 2>&1; then
        log "Starting Docker daemon..."
        open -a Docker 2>/dev/null || true
        for i in $(seq 1 30); do
            docker info >/dev/null 2>&1 && break
            sleep 2
        done
    fi
    docker info >/dev/null 2>&1 || { log "ERROR: Docker not available"; exit 1; }
    log "Docker ready."
}

ensure_image() {
    log "Building Docker image (if needed)..."
    docker build -t "$IMAGE_NAME" "$SCRIPT_DIR" >"$BUILD_LOG" 2>&1 || {
        log "ERROR: Docker image build failed"
        tail -50 "$BUILD_LOG"
        exit 1
    }
    log "Docker image ready."
}

# ---- Phase 2: Kernel build ----
build_kernel() {
    log "Building TOL VOVINA Kernel..."
    docker run --rm \
        -v "$KERNEL_DIR:/zedec-build/kernel" \
        -v "$SCRIPT_DIR/init:/zedec-build/init" \
        -v "$SCRIPT_DIR/gui:/zedec-build/gui" \
        -v "$SCRIPT_DIR/tests:/zedec-build/tests" \
        "$IMAGE_NAME" \
        bash /zedec-build/build_iso.sh 2>&1 | tee -a "$BUILD_LOG"

    if [ -f "$KERNEL_DIR/tol_vovina.iso" ]; then
        local iso_size=$(wc -c < "$KERNEL_DIR/tol_vovina.iso")
        local bin_size=$(wc -c < "$KERNEL_DIR/tol_vovina.bin")
        log "BUILD SUCCESS: tol_vovina.iso=${iso_size} bytes, tol_vovina.bin=${bin_size} bytes"
        cp "$KERNEL_DIR/tol_vovina.iso" "$OUTPUT_DIR/" 2>/dev/null || true
        cp "$KERNEL_DIR/tol_vovina.bin" "$OUTPUT_DIR/" 2>/dev/null || true
    else
        log "ERROR: Build completed but ISO not found"
        exit 1
    fi
}

# ---- Phase 3: Boot test ----
boot_test() {
    log "Running QEMU boot test (10s timeout)..."
    if ! command -v qemu-system-i386 >/dev/null 2>&1; then
        log "WARNING: qemu-system-i386 not available locally, trying Docker..."
        docker run --rm \
            -v "$KERNEL_DIR:/zedec-build/kernel" \
            "$IMAGE_NAME" \
            timeout 10 qemu-system-i386 -cdrom /zedec-build/kernel/tol_vovina.iso \
            -m 256M -boot d -nographic -no-reboot 2>&1 | tee "$BOOT_LOG"
    else
        timeout 10 qemu-system-i386 -cdrom "$KERNEL_DIR/tol_vovina.iso" \
            -m 256M -boot d -nographic -no-reboot 2>&1 | tee "$BOOT_LOG" || true
    fi

    if grep -q "All systems online" "$BOOT_LOG" 2>/dev/null; then
        log "BOOT TEST: PASS"
    else
        log "BOOT TEST: FAIL (banner not found)"
    fi
}

# ---- Phase 4: Host-side unit tests ----
run_tests() {
    log "Running host-side unit tests..."
    docker run --rm \
        -v "$KERNEL_DIR:/zedec-build/kernel" \
        -v "$SCRIPT_DIR/init:/zedec-build/init" \
        -v "$SCRIPT_DIR/gui:/zedec-build/gui" \
        -v "$SCRIPT_DIR/tests:/zedec-build/tests" \
        "$IMAGE_NAME" \
        bash -c 'cd /zedec-build/kernel && make test' 2>&1 | tee "$TEST_LOG"

    local pass_count=$(grep -c '=== .* ===' "$TEST_LOG" 2>/dev/null || echo 0)
    local fail_count=$(grep -c 'FAIL' "$TEST_LOG" 2>/dev/null || echo 0)
    log "TESTS: ${pass_count} test groups run, ${fail_count} failures"
}

# ---- Phase 5: Report ----
generate_report() {
    log "Generating report..."
    cat > "$REPORT_FILE" << EOF
# TOL VOVINA Kernel — Pipeline Report
**Generated:** $(date)
**Commit:** $(git -C "$SCRIPT_DIR" rev-parse --short HEAD 2>/dev/null || echo 'N/A')

## Build Status
- **ISO:** $(ls -la "$KERNEL_DIR/tol_vovina.iso" 2>/dev/null | awk '{print $5}' || echo 'MISSING') bytes
- **BIN:** $(ls -la "$KERNEL_DIR/tol_vovina.bin" 2>/dev/null | awk '{print $5}' || echo 'MISSING') bytes
- **Build log:** \`build_logs/build_${TIMESTAMP}.log\`

## Boot Test
$(grep -q 'All systems online' "$BOOT_LOG" 2>/dev/null && echo '- **Status:** PASS ✅' || echo '- **Status:** NOT RUN / FAIL ❌')
$(grep -q 'Tantra' "$BOOT_LOG" 2>/dev/null && echo '- Tantra engine: initialized' || echo '- Tantra engine: not found')
$(grep -q 'Naga Raja' "$BOOT_LOG" 2>/dev/null && echo '- Naga Raja: initialized' || echo '- Naga Raja: not found')

## Artifacts
$(ls -la "$OUTPUT_DIR/" 2>/dev/null | grep -v '^total' | grep -v '^d' | sed 's/^/  /')

## Logs
- Build: \`build_logs/build_${TIMESTAMP}.log\`
- Boot:  \`build_logs/boot_${TIMESTAMP}.log\`
- Tests: \`build_logs/test_${TIMESTAMP}.log\`
EOF
    log "Report written to $REPORT_FILE"
    cat "$REPORT_FILE"
}

# ---- Main dispatch ----
case "$MODE" in
    --build) ensure_docker; ensure_image; build_kernel ;;
    --boot)  ensure_docker; ensure_image; build_kernel; boot_test ;;
    --test)  ensure_docker; ensure_image; build_kernel; run_tests ;;
    --report) generate_report ;;
    --full|*)
        ensure_docker
        ensure_image
        build_kernel
        boot_test
        run_tests
        generate_report
        ;;
esac

log "Pipeline complete."
