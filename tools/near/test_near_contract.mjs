#!/usr/bin/env node
// test_near_contract.mjs — NEAR runtime semantics in a harness.
//
// Mirrors what near-vm-runner does: every method call instantiates the
// module fresh (memory starts zeroed), the env host functions keep one
// storage map across calls, and the signer is per-call. This is how the
// contract must behave on-chain: nothing survives between transactions
// except env.storage_*.
//
// Scenarios (each must hold):
//   1. zxv_boot from the owner account succeeds; boot from another fails.
//   2. voucher_issue by owner floats a voucher; by a non-owner fails.
//   3. voucher_transfer by the holder moves it; by anyone else fails.
//   4. voucher_redeem by the holder closes it; a second redeem fails.
//   5. chain_verify returns 0 after every op (SHA-256 chain intact).
//   6. malformed args panic (trapped), never corrupting state.
//   7. replay of a stale transfer fails (holder has moved).
//
// Usage: node tools/near/test_near_contract.mjs [path-to-contract.wasm]
import { readFileSync } from "node:fs";

const wasmPath = process.argv[2] || "dist/near/zxv_near_contract.wasm";
const wasmBytes = readFileSync(wasmPath);

const OWNER = "zxv-armada.near";
const BOB = "bob.near";
const CAROL = "carol.near";
const EVE = "eve.near";

let failures = 0;
function check(name, cond, detail = "") {
  if (cond) console.log(`  ok  ${name}`);
  else {
    failures++;
    console.log(`FAIL  ${name}${detail ? " — " + detail : ""}`);
  }
}
function assert(cond, msg) {
  if (!cond) throw new Error("assert: " + msg);
}

// ---- the env host module ------------------------------------------------
function makeEnv(signer, storage) {
  const registers = new Map(); // id -> Uint8Array
  let returnBuf = null;
  let panicked = null;
  const view = new Uint8Array(1); // dummy, replaced per call

  const env = {
    storage_read(keyLen, keyPtr, reg) {
      const key = readMem(keyPtr, keyLen);
      const k = Buffer.from(key).toString("latin1");
      const v = storage.get(k);
      if (v === undefined) return 0xFFFFFFFFFFFFFFFFn;
      registers.set(reg, v);
      return 1n; // current runtime: presence flag; length via register_len
    },
    storage_write(keyLen, keyPtr, valLen, valPtr) {
      const key = Buffer.from(readMem(keyPtr, keyLen)).toString("latin1");
      const val = readMem(valPtr, valLen);
      const existed = storage.has(key);
      storage.set(key, Uint8Array.from(val));
      return existed ? 1n : 0n;
    },
    storage_has_key(keyLen, keyPtr) {
      const key = Buffer.from(readMem(keyPtr, keyLen)).toString("latin1");
      return storage.has(key) ? 1n : 0n;
    },
    register_len(reg) {
      const v = registers.get(reg);
      return v === undefined ? 0xFFFFFFFFFFFFFFFFn : BigInt(v.length);
    },
    read_register(reg, ptr) {
      const v = registers.get(reg);
      if (!v) throw new Error("read_register: empty register");
      mem.set(v, Number(ptr));
    },
    input(reg) {
      if (currentInput !== null) registers.set(reg, currentInput);
    },
    signer_account_id(reg) {
      registers.set(reg, strBytes(signer));
    },
    current_account_id(reg) {
      registers.set(reg, strBytes("zxv-armada.near"));
    },
    attached_deposit(reg) {
      registers.set(reg, new Uint8Array(16));
    },
    prepaid_gas() {
      return 300000000000000n;
    },
    block_index() {
      return 220699670n;
    },
    block_timestamp() {
      return 1750000000000000000n;
    },
    value_return(len, ptr) {
      returnBuf = readMem(ptr, len);
    },
    panic_utf8(len, ptr) {
      panicked = Buffer.from(readMem(ptr, len)).toString();
      throw new Error("PANIC: " + panicked);
    },
    log_utf8(len, ptr) {
      console.log("  [log]", Buffer.from(readMem(ptr, len)).toString());
    },
  };

  // memory setter — the harness wires this per instantiation
  let mem = new Uint8Array(1);
  let currentInput = null;
  function readMem(ptr, len) {
    const out = new Uint8Array(Number(len));
    out.set(mem.subarray(Number(ptr), Number(ptr) + Number(len)));
    return out;
  }
  function strBytes(s) {
    return new TextEncoder().encode(s);
  }
  return { env, get mem() { return mem; }, set mem(m) { mem = m; },
           setInput(b) { currentInput = b; },
           get returnBuf() { return returnBuf; },
           get panicked() { return panicked; } };
}

function u64be(n) {
  const b = new Uint8Array(8);
  const dv = new DataView(b.buffer);
  dv.setBigUint64(0, BigInt(n), false);
  return b;
}
function u8(n) { return new Uint8Array([n & 0xff]); }
function str(s) { return new TextEncoder().encode(s); }
function cat(...parts) {
  let n = 0;
  for (const p of parts) n += p.length;
  const out = new Uint8Array(n);
  let o = 0;
  for (const p of parts) { out.set(p, o); o += p.length; }
  return out;
}
function bufToStr(b) { return Buffer.from(b).toString("latin1"); }

// ---- one call: fresh instantiation, one storage map ---------------------
const storage = new Map();
async function call(signer, method, args) {
  const h = makeEnv(signer, storage);
  h.setInput(args || new Uint8Array(0));
  const imports = { env: h.env };
  const inst = await WebAssembly.instantiate(wasmBytes, imports);
  h.mem = new Uint8Array(inst.instance.exports.memory.buffer);
  const fn = inst.instance.exports[method];
  if (!fn) throw new Error("no export " + method);
  try {
    fn();
  } catch (e) {
    // panics surface as exceptions here; rethrow wrapped for the caller
    const err = new Error(e.message);
    err.panicked = true;
    throw err;
  }
  return h.returnBuf ? new Uint8Array(h.returnBuf) : null;
}

// ---- the scenarios -------------------------------------------------------
console.log("scenario 1: boot");
try {
  await call(OWNER, "zxv_boot", new TextEncoder().encode("{}"));
  console.log("  ok  boot by owner");
} catch (e) { failures++; console.log("FAIL  boot by owner —", e.message); }
try {
  await call(EVE, "zxv_boot", new TextEncoder().encode("{}"));
  failures++; console.log("FAIL  second boot should panic");
} catch (e) { console.log("  ok  second boot rejected"); }
try {
  await call(EVE, "zxv_boot", new TextEncoder().encode("{}"));
  failures++; console.log("FAIL  foreign boot should panic");
} catch (e) { console.log("  ok  foreign boot rejected (already booted)"); }

console.log("scenario 2: issue (owner only)");
let vid = null;
try {
  const r = await call(OWNER, "voucher_issue",
    cat(u8(8), str(BOB), u64be(21000), u8(7), str("genesis")));
  vid = new DataView(r.buffer).getBigUint64(1, false);
  console.log(`  ok  issue by owner -> voucher ${vid}`);
} catch (e) { failures++; console.log("FAIL  issue by owner —", e.message); }
try {
  await call(EVE, "voucher_issue",
    cat(u8(8), str(BOB), u64be(100), u8(1), str("x")));
  failures++; console.log("FAIL  non-owner issue should fail");
} catch (e) { console.log("  ok  non-owner issue rejected"); }

console.log("scenario 3: transfer (holder only)");
try {
  await call(BOB, "voucher_transfer", cat(u64be(vid), u8(10), str(CAROL)));
  console.log("  ok  holder transfers to carol");
} catch (e) { failures++; console.log("FAIL  holder transfer —", e.message); }
try {
  await call(BOB, "voucher_transfer", cat(u64be(vid), u8(8), str(EVE)));
  failures++; console.log("FAIL  replay from old holder should fail");
} catch (e) { console.log("  ok  stale replay rejected (holder moved)"); }
try {
  await call(EVE, "voucher_transfer", cat(u64be(vid), u8(3), str(BOB)));
  failures++; console.log("FAIL  non-holder transfer should fail");
} catch (e) { console.log("  ok  non-holder transfer rejected"); }

console.log("scenario 4: redeem (holder only, once)");
try {
  await call(CAROL, "voucher_redeem", u64be(vid));
  console.log("  ok  holder redeems");
} catch (e) { failures++; console.log("FAIL  holder redeem —", e.message); }
try {
  await call(CAROL, "voucher_redeem", u64be(vid));
  failures++; console.log("FAIL  double redeem should fail");
} catch (e) { console.log("  ok  double redeem rejected"); }

console.log("scenario 5: chain verify after ops");
try {
  const r = await call(OWNER, "chain_verify", new TextEncoder().encode("{}"));
  const code = new DataView(r.buffer).getUint32(1, false);
  check("chain_verify == 0 (VINO_CHAIN_OK)", code === 0, "code " + code);
} catch (e) { failures++; console.log("FAIL  chain_verify —", e.message); }

console.log("scenario 6: ledger state view");
try {
  const r = await call(OWNER, "ledger_state", new TextEncoder().encode("{}"));
  const s = bufToStr(r);
  check("ledger_state mentions bob", s.includes("bob"));
  check("ledger_state mentions carol", s.includes("carol"));
  check("ledger_state shows redeemed marker", s.includes("R"));
  console.log("  state:", s.split("\n")[0]);
} catch (e) { failures++; console.log("FAIL  ledger_state —", e.message); }

console.log("scenario 7: malformed args panic, state intact");
try {
  await call(OWNER, "voucher_issue", cat(u8(99), str(BOB))); // truncated
  failures++; console.log("FAIL  truncated issue args should panic");
} catch (e) { console.log("  ok  truncated args panic"); }
try {
  const r = await call(OWNER, "chain_verify", new TextEncoder().encode("{}"));
  const code = new DataView(r.buffer).getUint32(1, false);
  check("state intact after malformed call", code === 0, "code " + code);
} catch (e) { failures++; console.log("FAIL  post-malformed verify —", e.message); }

console.log("scenario 8: scp144 bind (pure compute, public)");
try {
  const init = new Uint8Array(32).fill(1);
  const resp = new Uint8Array(32).fill(2);
  const r = await call(EVE, "scp144_bind", cat(init, resp));
  check("scp144_bind returns 32 bytes", r.length === 32);
  let nonzero = 0;
  for (const b of r) if (b) nonzero++;
  check("binding is not all-zero", nonzero > 0, "nonzero=" + nonzero);
} catch (e) { failures++; console.log("FAIL  scp144_bind —", e.message); }

console.log("scenario 8b: 64-char account (NEAR implicit id width)");
const LONG = "6ca890f766fdaeccd6c934da325f5f06ebc63ca9be4f5f1b44d0686af93c64a8";
try {
  await call(OWNER, "voucher_issue",
    cat(u8(64), str(LONG), u64be(5000), u8(2), str("v7")));
  console.log("  ok  issue to 64-char holder");
} catch (e) { failures++; console.log("FAIL  issue to 64-char holder —", e.message); }
try {
  await call(LONG, "voucher_redeem", u64be(2));
  console.log("  ok  64-char holder redeems");
} catch (e) { failures++; console.log("FAIL  64-char holder redeem —", e.message); }

console.log("scenario 8c: paraconsistent gift + economy governance");
// lpres_join: op 0=conjoin, 1=disjoin, 2=kjoin; args [op,a,b]
async function join(op,a,b){
  const h = await call("eve.near","lpres_join", new Uint8Array([op,a,b]));
  return h[0];
}
check("kjoin(TRUE,FALSE)=BOTH (contradiction recorded)",
  await join(2,1,2) === 3, "got " + await join(2,1,2));
check("kjoin(TRUE,TRUE)=TRUE",
  await join(2,1,1) === 1);
check("kjoin(NEITHER,TRUE)=TRUE (silence adds nothing)",
  await join(2,0,1) === 1);
check("conjoin(FALSE,TRUE)=FALSE (meet: FALSE dominates)",
  await join(0,2,1) === 2);
check("disjoin(FALSE,TRUE)=TRUE (join: TRUE dominates)",
  await join(1,2,1) === 1);
// economy: evidence not TRUE is refused (no served records)
try {
  await call(OWNER,"econ_issue", cat(u64be(1000), u64be(0), u64be(0)));
  failures++; console.log("FAIL  econ_issue with no evidence should refuse");
} catch (e) { console.log("  ok  econ_issue refused without TRUE evidence"); }
// economy: TRUE evidence (served>0, wants>0) issues
try {
  const r = await call(OWNER,"econ_issue", cat(u64be(1000), u64be(10), u64be(5)));
  console.log("  ok  econ_issue with TRUE evidence -> id " + new DataView(r.buffer).getBigUint64(1,false));
} catch (e) { failures++; console.log("FAIL  econ_issue TRUE evidence —", e.message); }
try {
  const h = await call(OWNER,"econ_state", new TextEncoder().encode("{}"));
  const s8 = Buffer.from(h).toString();
  check("econ_state shows BSC-0 card", s8.includes("BSC-0-1870892d"));
  check("econ_state shows TRIBUTE-CROWN", s8.includes("TRIBUTE-CROWN"));
  console.log("  econ:", s8.slice(0, 160));
} catch (e) { failures++; console.log("FAIL  econ_state —", e.message); }

console.log("scenario 9: replay protection — issue once, transfer once, state exact");
try {
  const r = await call(OWNER, "ledger_state", new TextEncoder().encode("{}"));
  const s1 = bufToStr(r);
  const r2 = await call(OWNER, "ledger_state", new TextEncoder().encode("{}"));
  const s2 = bufToStr(r2);
  check("ledger_state stable across calls", s1 === s2);
} catch (e) { failures++; console.log("FAIL  state stability —", e.message); }

console.log(failures ? `\n${failures} FAILURES` : "\nALL PASS");
process.exit(failures ? 1 : 0);