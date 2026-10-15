<!--
Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
SPDX-License-Identifier: Apache-2.0
-->

> Read first: since these notes, the waves follow the matrix order
> (`MATRIX_ORDER.tsv`), so these apps now sit in other waves; and an app's
> tests go in its own two files (`APP_TESTS.md`), not in `test_zxapp.c` or
> `test_apps.c`. Where a line below says "add to test_wave0", "test_apps.sh
> does not compile" or "test_apps.c does not include", write
> `kernel/src/apps/tests/<id>_zxapp.inc` and `<id>_apps.inc` instead. The rest
> stands.

# Wave 1 fix notes (reviewer, base 2216922d)

None of the 12 branches can be folded in. All 12 add zero test lines, so "test_zxapp ok" only meant the old tests still passed. Rebase each branch on 2216922d, where tools/apps/app_gate.py now runs inside test_zxapp.sh, then fix until `bash kernel/src/apps/test_zxapp.sh` passes. Below is what the new gate reports for each branch now:

## server/wave1-004-keystone
- kernel/src/apps/keystone.zxapp: logic file apps/blocks/keystone/settings.kron does not exist
- kernel/src/apps/keystone.zxapp: blocks/keystone/keystone.cpp is used by no logic line
- kernel/src/apps/keystone.zxapp: blocks/keystone/keystone.eve is used by no logic line
- kernel/src/apps/keystone.zxapp: blocks/keystone/keystone.kron is used by no logic line
- kernel/src/apps/keystone.zxapp: test_zxapp.c never loads it (add "apps/keystone.zxapp" to test_wave0)
- kernel/src/apps/keystone.zxapp: test_apps.sh does not compile keystone_model.c
- kernel/src/apps/keystone.zxapp: test_apps.c does not include keystone_model.h, so the model is untested
- kernel/src/apps/keystone.zxapp: keystone_model.c calls sys.budget, which zxvd does not have (api/zxvd.openrpc.json); use only existing methods
- kernel/src/apps/keystone.zxapp: keystone_model.c calls sys.hoard, which zxvd does not have (api/zxvd.openrpc.json); use only existing methods
- kernel/src/apps/keystone.zxapp: keystone_model.c calls sys.model, which zxvd does not have (api/zxvd.openrpc.json); use only existing methods
- kernel/src/apps/keystone.zxapp: keystone_model.c calls sys.network, which zxvd does not have (api/zxvd.openrpc.json); use only existing methods
- kernel/src/apps/keystone.zxapp: keystone_model.c calls sys.settings, which zxvd does not have (api/zxvd.openrpc.json); use only existing methods
- kernel/src/apps/keystone.zxapp: keystone_model.c calls sys.sharing, which zxvd does not have (api/zxvd.openrpc.json); use only existing methods

## server/wave1-006-tideturn
- kernel/src/apps/tideturn.zxapp: test_zxapp.c never loads it (add "apps/tideturn.zxapp" to test_wave0)
- kernel/src/apps/tideturn.zxapp: test_apps.sh does not compile tideturn_model.c
- kernel/src/apps/tideturn.zxapp: test_apps.c does not include tideturn_model.h, so the model is untested
- kernel/src/apps/tideturn.zxapp: tideturn_model.c calls tideturn.update_check, which zxvd does not have (api/zxvd.openrpc.json); use only existing methods
- kernel/src/apps/tideturn.zxapp: tideturn_model.c calls tideturn.update_record, which zxvd does not have (api/zxvd.openrpc.json); use only existing methods
- kernel/src/apps/tideturn.zxapp: tideturn_model.c calls tideturn.update_valid, which zxvd does not have (api/zxvd.openrpc.json); use only existing methods

## server/wave1-007-satchel
- kernel/src/apps/satchel.zxapp: logic file apps/blocks/satchel/file_share.wasm does not exist
- kernel/src/apps/satchel.zxapp: logic file apps/blocks/satchel/file_share.kron does not exist
- kernel/src/apps/satchel.zxapp: logic file apps/blocks/satchel/package_record.myra does not exist
- kernel/src/apps/satchel.zxapp: blocks/satchel/satchel.eve is used by no logic line
- kernel/src/apps/satchel.zxapp: blocks/satchel/satchel.kron is used by no logic line
- kernel/src/apps/satchel.zxapp: blocks/satchel/satchel.myra is used by no logic line
- kernel/src/apps/satchel.zxapp: blocks/satchel/satchel.rs is used by no logic line
- kernel/src/apps/satchel.zxapp: blocks/satchel/satchel.tb is used by no logic line
- kernel/src/apps/satchel.zxapp: test_zxapp.c never loads it (add "apps/satchel.zxapp" to test_wave0)
- kernel/src/apps/satchel.zxapp: test_apps.sh does not compile satchel_model.c
- kernel/src/apps/satchel.zxapp: test_apps.c does not include satchel_model.h, so the model is untested
- kernel/src/apps/satchel.zxapp: satchel_model.c calls sys.package, which zxvd does not have (api/zxvd.openrpc.json); use only existing methods

## server/wave1-008-gatehouse
- kernel/src/apps/gatehouse.zxapp: logic file apps/blocks/gatehouse/is_granted.eve does not exist
- kernel/src/apps/gatehouse.zxapp: logic file apps/blocks/gatehouse/is_granted.kron does not exist
- kernel/src/apps/gatehouse.zxapp: logic file apps/blocks/gatehouse/is_granted.wasm does not exist
- kernel/src/apps/gatehouse.zxapp: logic file apps/blocks/gatehouse/count.kron does not exist
- kernel/src/apps/gatehouse.zxapp: logic file apps/blocks/gatehouse/count.trebago does not exist
- kernel/src/apps/gatehouse.zxapp: blocks/gatehouse/gatehouse.cpp is used by no logic line
- kernel/src/apps/gatehouse.zxapp: blocks/gatehouse/gatehouse.eve is used by no logic line
- kernel/src/apps/gatehouse.zxapp: blocks/gatehouse/gatehouse.kron is used by no logic line
- kernel/src/apps/gatehouse.zxapp: blocks/gatehouse/gatehouse.tb is used by no logic line
- kernel/src/apps/gatehouse.zxapp: test_zxapp.c never loads it (add "apps/gatehouse.zxapp" to test_wave0)
- kernel/src/apps/gatehouse.zxapp: test_apps.sh does not compile gatehouse_model.c
- kernel/src/apps/gatehouse.zxapp: test_apps.c does not include gatehouse_model.h, so the model is untested
- kernel/src/apps/gatehouse.zxapp: gatehouse_model.c calls sys.capabilities, which zxvd does not have (api/zxvd.openrpc.json); use only existing methods

## server/wave1-009-lockbox
- kernel/src/apps/lockbox.zxapp: logic file apps/blocks/lockbox/key_expiry_days_left.wasm does not exist
- kernel/src/apps/lockbox.zxapp: test_zxapp.c never loads it (add "apps/lockbox.zxapp" to test_wave0)
- kernel/src/apps/lockbox.zxapp: test_apps.sh does not compile lockbox_model.c
- kernel/src/apps/lockbox.zxapp: test_apps.c does not include lockbox_model.h, so the model is untested

## server/wave1-012-ferry
- kernel/src/apps/ferry.zxapp: blocks/ferry/ferry.kron is used by no logic line
- kernel/src/apps/ferry.zxapp: blocks/ferry/ferry.sutra is used by no logic line
- kernel/src/apps/ferry.zxapp: blocks/ferry/ferry.tb is used by no logic line
- kernel/src/apps/ferry.zxapp: test_zxapp.c never loads it (add "apps/ferry.zxapp" to test_wave0)
- kernel/src/apps/ferry.zxapp: test_apps.sh does not compile ferry_model.c
- kernel/src/apps/ferry.zxapp: test_apps.c does not include ferry_model.h, so the model is untested
- kernel/src/apps/ferry.zxapp: ferry_model.c calls use sutra 1, which zxvd does not have (api/zxvd.openrpc.json); use only existing methods
- kernel/src/apps/ferry.zxapp: ferry_model.c calls use used_share 1, which zxvd does not have (api/zxvd.openrpc.json); use only existing methods

## server/wave1-013-flint
- kernel/src/apps/flint.zxapp: logic file apps/blocks/flint/scrollback_lines.eve does not exist
- kernel/src/apps/flint.zxapp: logic file apps/blocks/flint/window_fit.wasm does not exist
- kernel/src/apps/flint.zxapp: logic file apps/blocks/flint/command_history.myra does not exist
- kernel/src/apps/flint.zxapp: blocks/flint/flint.cpp is used by no logic line
- kernel/src/apps/flint.zxapp: blocks/flint/flint.eve is used by no logic line
- kernel/src/apps/flint.zxapp: blocks/flint/flint.kron is used by no logic line
- kernel/src/apps/flint.zxapp: blocks/flint/flint.myra is used by no logic line
- kernel/src/apps/flint.zxapp: test_zxapp.c never loads it (add "apps/flint.zxapp" to test_wave0)
- kernel/src/apps/flint.zxapp: test_apps.sh does not compile flint_model.c
- kernel/src/apps/flint.zxapp: test_apps.c does not include flint_model.h, so the model is untested
- kernel/src/apps/flint.zxapp: flint_model.c calls term.history, which zxvd does not have (api/zxvd.openrpc.json); use only existing methods
- kernel/src/apps/flint.zxapp: flint_model.c calls term.scrollback, which zxvd does not have (api/zxvd.openrpc.json); use only existing methods
- kernel/src/apps/flint.zxapp: flint_model.c calls term.window, which zxvd does not have (api/zxvd.openrpc.json); use only existing methods

## server/wave1-018-witness
- kernel/src/apps/witness.zxapp: logic file apps/blocks/witness/chronicle.wasm does not exist
- kernel/src/apps/witness.zxapp: logic file apps/blocks/witness/provenance.wasm does not exist
- kernel/src/apps/witness.zxapp: logic file apps/blocks/witness/peer_audit.wasm does not exist
- kernel/src/apps/witness.zxapp: blocks/witness/chronicle.myra is used by no logic line
- kernel/src/apps/witness.zxapp: blocks/witness/chronicle.sutra is used by no logic line
- kernel/src/apps/witness.zxapp: blocks/witness/chronicle.tb is used by no logic line
- kernel/src/apps/witness.zxapp: blocks/witness/peer_audit.myra is used by no logic line
- kernel/src/apps/witness.zxapp: blocks/witness/peer_audit.sutra is used by no logic line
- kernel/src/apps/witness.zxapp: blocks/witness/peer_audit.tb is used by no logic line
- kernel/src/apps/witness.zxapp: blocks/witness/provenance.myra is used by no logic line
- kernel/src/apps/witness.zxapp: blocks/witness/provenance.sutra is used by no logic line
- kernel/src/apps/witness.zxapp: blocks/witness/provenance.tb is used by no logic line
- kernel/src/apps/witness.zxapp: test_zxapp.c never loads it (add "apps/witness.zxapp" to test_wave0)
- kernel/src/apps/witness.zxapp: test_apps.sh does not compile witness_model.c
- kernel/src/apps/witness.zxapp: test_apps.c does not include witness_model.h, so the model is untested
- kernel/src/apps/witness.zxapp: witness_model.c calls sys.journal, which zxvd does not have (api/zxvd.openrpc.json); use only existing methods

## server/wave1-019-cellar
- kernel/src/apps/cellar.zxapp: logic file apps/blocks/used_share/used_x100.wasm does not exist
- kernel/src/apps/cellar.zxapp: logic file apps/blocks/used_share/used_x100.kron does not exist
- kernel/src/apps/cellar.zxapp: logic file apps/blocks/cellar/trash_size.tb does not exist
- kernel/src/apps/cellar.zxapp: logic file apps/blocks/cellar/safe_to_repair.eve does not exist
- kernel/src/apps/cellar.zxapp: blocks/cellar/cellar.cpp is used by no logic line
- kernel/src/apps/cellar.zxapp: blocks/cellar/cellar.eve is used by no logic line
- kernel/src/apps/cellar.zxapp: blocks/cellar/cellar.kron is used by no logic line
- kernel/src/apps/cellar.zxapp: blocks/cellar/cellar.tb is used by no logic line
- kernel/src/apps/cellar.zxapp: test_zxapp.c never loads it (add "apps/cellar.zxapp" to test_wave0)
- kernel/src/apps/cellar.zxapp: test_apps.sh does not compile cellar_model.c
- kernel/src/apps/cellar.zxapp: test_apps.c does not include cellar_model.h, so the model is untested

## server/wave1-021-brazier
- kernel/src/apps/brazier.zxapp: blocks/brazier/battery_level.eve is used by no logic line
- kernel/src/apps/brazier.zxapp: blocks/brazier/current_temp.sutra is used by no logic line
- kernel/src/apps/brazier.zxapp: blocks/brazier/minutes_left.eve is used by no logic line
- kernel/src/apps/brazier.zxapp: blocks/brazier/power_draw.eve is used by no logic line
- kernel/src/apps/brazier.zxapp: blocks/brazier/temp_crit.sutra is used by no logic line
- kernel/src/apps/brazier.zxapp: blocks/brazier/temp_warn.sutra is used by no logic line
- kernel/src/apps/brazier.zxapp: test_zxapp.c never loads it (add "apps/brazier.zxapp" to test_wave0)
- kernel/src/apps/brazier.zxapp: no model line

## server/wave1-023-lodestone
- kernel/src/apps/lodestone.zxapp: blocks/lodestone/lodestone.cpp is used by no logic line
- kernel/src/apps/lodestone.zxapp: blocks/lodestone/lodestone.kron is used by no logic line
- kernel/src/apps/lodestone.zxapp: blocks/lodestone/lodestone.myra is used by no logic line
- kernel/src/apps/lodestone.zxapp: blocks/lodestone/lodestone.tb is used by no logic line
- kernel/src/apps/lodestone.zxapp: test_zxapp.c never loads it (add "apps/lodestone.zxapp" to test_wave0)
- kernel/src/apps/lodestone.zxapp: test_apps.sh does not compile lodestone_model.c
- kernel/src/apps/lodestone.zxapp: test_apps.c does not include lodestone_model.h, so the model is untested
- kernel/src/apps/lodestone.zxapp: lodestone_model.c calls net.latency, which zxvd does not have (api/zxvd.openrpc.json); use only existing methods

## server/wave1-024-treadle
- kernel/src/apps/treadle.zxapp: logic file apps/blocks/treadle/next_run.wasm does not exist
- kernel/src/apps/treadle.zxapp: logic file apps/blocks/treadle/next_run.kron does not exist
- kernel/src/apps/treadle.zxapp: logic file apps/blocks/treadle/within_budget.eve does not exist
- kernel/src/apps/treadle.zxapp: logic file apps/blocks/treadle/within_budget.kron does not exist
- kernel/src/apps/treadle.zxapp: logic file apps/blocks/treadle/task_record.myra does not exist
- kernel/src/apps/treadle.zxapp: blocks/treadle/treadle.cpp is used by no logic line
- kernel/src/apps/treadle.zxapp: blocks/treadle/treadle.eve is used by no logic line
- kernel/src/apps/treadle.zxapp: blocks/treadle/treadle.kron is used by no logic line
- kernel/src/apps/treadle.zxapp: blocks/treadle/treadle.myra is used by no logic line
- kernel/src/apps/treadle.zxapp: blocks/treadle/treadle.sutra is used by no logic line
- kernel/src/apps/treadle.zxapp: test_zxapp.c never loads it (add "apps/treadle.zxapp" to test_wave0)
- kernel/src/apps/treadle.zxapp: test_apps.sh does not compile treadle_model.c
- kernel/src/apps/treadle.zxapp: test_apps.c does not include treadle_model.h, so the model is untested
- kernel/src/apps/treadle.zxapp: treadle_model.c calls budget.check, which zxvd does not have (api/zxvd.openrpc.json); use only existing methods
- kernel/src/apps/treadle.zxapp: treadle_model.c calls task.next_run, which zxvd does not have (api/zxvd.openrpc.json); use only existing methods
- kernel/src/apps/treadle.zxapp: treadle_model.c calls task.record, which zxvd does not have (api/zxvd.openrpc.json); use only existing methods

## Also, from reading the code
- keystone: the .cpp includes <zxv.h> and uses atoi/argv. A wasm block exports `int run(...)` over integers only, with no libc (see blocks/sounding/rate.rs and blocks/banker/share.cpp). keystone_model.c calls almanac_bytes_text without including almanac_model.h, so it does not compile. It also re-implements used_x100: use the used_share block (docs/APP_BLOCKS.md).
- keystone: the .eve block is not Eve syntax. Copy the shape of blocks/shepherd/limit.eve.
- ferry: zxm_begin("use used_share 1") is wrong. `use used_share 1` is a manifest line; call `used_share.used_x100` through the composed app.
- tideturn: `tideturn.*` methods do not exist. Use `update.check` (see api/zxvd.openrpc.json).
- lodestone: `net.latency` does not exist. Take latencies from the methods that do exist, or leave the stat out and list it in the report.
- For every app, write the test sections first, then the code: 64-row sweeps against the model's own C function, the edges (0, max, max+1, negative), and one input that makes the languages disagree and must give BOTH.

## Suggested runner change
Have the runner loop: run test_zxapp.sh, feed the failing lines back to the model, and retry, up to N rounds, before handing a branch back. Nothing fails silently now, so the error text is the best spec the model can get.
