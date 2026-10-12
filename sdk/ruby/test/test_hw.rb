# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
# frozen_string_literal: true

require_relative "test_helper"

# ABI 1.4: hardware emulation and hardware updates. Mirrors the key cases
# of sdk/libzxv/test/test_zxv_abi14.c.
class TestHw < Minitest::Test
  include ZxvTest
  Hw = Zxv::Hw

  SHA256_ABC = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
  # RFC 4231 test case 2: key "Jefe".
  HMAC_JEFE = "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843"
  MAC_IN = [4].pack("v") + "Jefe" + "what do ya want for nothing?"

  def with_hw
    Zxv::Context.open do |ctx|
      cap = ctx.root_capability(:all_1_4)
      yield ctx, cap, cap.attenuate(Zxv::Capability::ALL)
    end
  end

  def status_of(klass = Zxv::StatusError, &block)
    assert_raises(klass, &block).status
  end

  def test_version_statuses_and_caps
    major, minor, = Zxv.abi_version
    assert_equal 1, major
    assert_operator minor, :>=, 4
    assert Zxv.feature?(:hw)
    { E_NOPROVIDER: -25, E_DISPUTE: -26, E_QUARANTINED: -27, E_ROLLBACK: -28, E_BLOCKED: -29, E_TARGET: -30,
      E_POLICY: -31 }.each do |name, value|
      assert_equal value, Zxv::Status.const_get(name)
      assert_equal "ZXV_#{name}", Zxv::Status.name_of(value)
    end
    assert_equal Zxv::Status::CLASS_REFUSED, Zxv::Status.class_of(Zxv::Status::E_DISPUTE)
    assert_equal Zxv::Status::CLASS_USAGE, Zxv::Status.class_of(Zxv::Status::E_TARGET)
    assert_equal Zxv::Status::CLASS_REFUSED, Zxv::Status.class_of(Zxv::Status::E_POLICY)
    assert_equal 0x3FF, Zxv::Capability::ALL
    assert_equal 0xFFF, Zxv::Capability::ALL_1_4
    with_hw do |ctx, cap, nohw|
      assert_includes cap.info.names, :hw_matrix
      assert_includes cap.info.names, :hw_reconf
      refute_includes nohw.info.names, :hw_reconf
      assert_equal Zxv::Capability::ALL, ctx.root_capability.info.bits
    end
  end

  def test_profiles
    host = Zxv::HwProfile.probe_host
    r1 = Zxv::HwProfile.reference
    assert_equal 16, r1.count
    assert_operator host.count, :>=, 2
    assert_equal [nil].pack("p").bytesize * 8, host.cap(2, Hw::CAP_WORD_BITS)
    assert_operator host.cap(2, Hw::CAP_COUNT), :>=, 1

    native, emulated = host.match(r1)
    assert_operator native, :>=, 1
    assert_equal 14, native + emulated
    p = host.pair(r1, 1)
    assert_equal [2, :group], [p.need_id, p.kind]
    p = host.pair(r1, 2)
    assert_equal [3, :native, 2], [p.need_id, p.kind, p.have_id]
    assert_equal :emulate, host.pair(r1, 7).kind
    assert_equal Zxv::Status::E_NOT_FOUND, status_of { host.pair(r1, 99) }

    assert_equal Hw::FN_TENSOR, r1.cap(6, Hw::CAP_CONTRACT_ID)
    pres = Zxv::HwProfile.present(host, r1)
    assert_equal Hw::KIND_HARD, pres.cap(3, Hw::CAP_KIND)
    assert_equal Hw::KIND_SOFT, pres.cap(8, Hw::CAP_KIND)
    assert_nil pres.cap(1, Hw::CAP_KIND)
    pres.close
    assert pres.closed?

    bytes = r1.encode
    copy = Zxv::HwProfile.decode(bytes)
    assert_equal r1.digest, copy.digest
    assert_equal 32, r1.digest.bytesize
    assert_equal r1.nodes, copy.nodes
    assert_equal Zxv::Status::E_FORMAT, status_of { Zxv::HwProfile.decode(bytes[0...-1]) }

    mine = Zxv::HwProfile.new
    mine.add_node(1, parent: Hw::ROOT, cls: Hw::CLASS_MACHINE)
        .add_node(2, parent: 1, cls: Hw::CLASS_TERNARY_UNIT)
        .set_cap(2, Hw::CAP_RADIX, 3)
    assert_equal Zxv::Status::E_ARG, status_of { mine.add_node(2, parent: 1, cls: Hw::CLASS_CACHE) }
    assert_equal Zxv::Status::E_NOT_FOUND, status_of { mine.set_cap(9, Hw::CAP_RADIX, 3) }
    mine.add_node(3, parent: 1, cls: Hw::CLASS_PRIVATE | 1)
    p = mine.pair(r1, 7)
    assert_equal [:native, 2], [p.kind, p.have_id]
    d1 = mine.digest
    mine.set_ext(3, "\xA5\x00\x5A".b)
    refute_equal d1, mine.digest
    assert_equal "\xA5\x00\x5A".b, mine.ext(3)
    assert_equal "".b, mine.ext(2)
    assert_equal Zxv::Status::E_RANGE, status_of { mine.set_ext(3, "\0".b * (Hw::EXT_MAX + 1)) }
    assert_raises(Zxv::FloatRejectedError) { mine.set_cap(2, Hw::CAP_RADIX, 3.0) }
    assert_equal({ id: 3, parent: 1, cls: Hw::CLASS_PRIVATE | 1, ncaps: 0 }, mine.node(2).to_h)
  end

  def test_standard_engines_and_discovery
    with_hw do |_ctx, cap, nohw|
      assert_raises(Zxv::CapabilityDeniedError) { Zxv::HwMatrix.new(nohw) }
      mx = Zxv::HwMatrix.new(cap)
      assert_equal SHA256_ABC, unhex(mx.call(Hw::FN_SHA256, "abc"))
      assert_equal HMAC_JEFE, unhex(mx.call(Hw::FN_MAC, MAC_IN))
      assert_equal [6].pack("Q<"), mx.call(Hw::FN_GCD, [12, 18].pack("Q<Q<"))

      engines = mx.providers
      assert_operator engines.size, :>=, 5
      engines.each do |e|
        assert_equal :soft, e.kind
        assert_equal Zxv::Truth::TRUE, e.proof
        assert_equal 1, e.contract
        refute e.flag?(:fault)
      end
      fns = engines.map(&:fn_id)
      %i[sha256 keccak mac gcd logic64].each { |n| assert_includes fns, Hw::STANDARD_ENGINES[n] }
      assert_equal Zxv::Status::E_NOT_FOUND, status_of { mx.provider_info(engines.size) }
      info = mx.function_info(Hw::FN_MAC)
      assert_equal 1, info.contract
      assert_equal Hw::POLICY_KEY_BEARING, info.policy & Hw::POLICY_KEY_BEARING
      assert_operator info.vectors, :>=, 1
      sha = mx.bound(Hw::FN_SHA256)
      assert mx.provider_info(sha).serving?
      assert_operator mx.provider_info(sha).calls, :>=, 1
    end
  end

  def test_mac_refuses_remote_and_placement_policy
    with_hw do |_ctx, cap, _nohw|
      mx = Zxv::HwMatrix.new(cap)
      mac_local = mx.bound(Hw::FN_MAC)
      refute_nil mac_local
      reached = 0
      mx.add_resource(7)
      remote = mx.add_provider(Hw::FN_MAC, resource: 7, kind: :remote, cost: 1) do |_op, _in|
        reached += 1
        Zxv::Status::E_STATE
      end
      mx.attest(remote)
      calls_before = reached
      assert_equal HMAC_JEFE, unhex(mx.call(Hw::FN_MAC, MAC_IN))
      assert_equal calls_before, reached, "key-bearing work never reaches a REMOTE implementation"
      assert_equal mac_local, mx.bound(Hw::FN_MAC)
      assert mx.provider_info(remote).flag?(:policy)
      assert_equal Zxv::Status::E_POLICY, status_of(Zxv::ZxvRefusedError) { mx.set_policy(Hw::FN_MAC) }
      assert_equal Zxv::Status::E_POLICY, status_of(Zxv::ZxvRefusedError) { mx.memo(Hw::FN_MAC, 16) }
      assert_equal Zxv::Status::E_ARG, status_of { mx.set_policy(Hw::FN_MAC, flags: 0x80) }

      g = [12, 18].pack("Q<Q<")
      mx.set_policy(Hw::FN_GCD, power_budget_mw: 500)
      assert_equal Zxv::Status::E_NOPROVIDER, status_of(Zxv::ZxvRefusedError) { mx.call(Hw::FN_GCD, g) }
      assert_nil mx.bound(Hw::FN_GCD)
      gcd = mx.providers.find { |e| e.fn_id == Hw::FN_GCD }.index
      mx.set_traits(gcd, latency_ns: 2000, power_mw: 300, throughput_class: 4)
      assert_equal Zxv::Status::E_RANGE, status_of { mx.set_traits(gcd, latency_ns: 2000, power_mw: 300, throughput_class: 16) }
      assert_equal [6].pack("Q<"), mx.call(Hw::FN_GCD, g)
      i = mx.provider_info(gcd)
      assert_equal [4, Hw::POWER_1W], [i.throughput_class, i.power_class]
      mx.set_policy(Hw::FN_GCD, flags: :local_only, max_latency_ns: 1000)
      assert_equal Zxv::Status::E_NOPROVIDER, status_of { mx.call(Hw::FN_GCD, g) }
      mx.set_policy(Hw::FN_GCD)
      assert_equal [6].pack("Q<"), mx.call(Hw::FN_GCD, g)
    end
  end

  def test_ruby_implementations_proof_failover_shadow_memo_contracts
    with_hw do |_ctx, cap, _nohw|
      mx = Zxv::HwMatrix.new(cap)
      mx.add_function(0x100, "add8").add_function(0x101, "add16")
      assert_equal Zxv::Status::E_DUPLICATE, status_of { mx.add_function(0x100, "again") }
      mx.add_resource(1).add_resource(2)
      input = [0, 1, 9, 200].pack("C*")
      ex8 = [8, 9, 17, 208].pack("C*")
      ex16 = [16, 17, 25, 216].pack("C*")
      mx.add_vector(0x100, input, ex8).add_vector(0x101, input, ex16)

      fx = { bad: 0 }
      sw = { bad: 0 }
      add8 = lambda do |state, data|
        raise Zxv::ZxvSystemError, Zxv::Status::E_IO if state[:bad] == 2

        data.bytes.map { |b| (b + 8 + (state[:bad] == 1 && b == 9 ? 1 : 0)) & 0xFF }.pack("C*")
      end
      pfx = mx.add_provider(0x100, resource: 2, kind: :hard, cost: 1) { |_op, d| add8.call(fx, d) }
      psw = mx.add_provider(0x100, resource: 1, kind: :soft, cost: 10) { |_op, d| add8.call(sw, d) }
      p16 = mx.add_provider(0x101, resource: 1, kind: Hw::KIND_SOFT, cost: 20) do |op, d|
        mx.call(0x100, mx.call(0x100, d, op: op), op: op)
      end
      assert_equal Zxv::Status::E_ARG, status_of { mx.add_provider(0x100, resource: 1, kind: 9, cost: 1) { "" } }
      assert_equal Zxv::Status::E_NOPROVIDER, status_of { mx.call(0x100, input) }
      assert_equal Zxv::Truth::TRUE, mx.attest(pfx)
      assert_equal Zxv::Truth::TRUE, mx.attest(psw)
      assert_equal Zxv::Truth::FALSE, mx.attest(p16), "an undeclared dependency must not prove"
      mx.add_dependency(p16, 0x100)
      assert_equal Zxv::Truth::TRUE, mx.attest(p16)
      assert_equal ex16, mx.call(0x101, input)

      assert_equal pfx, mx.bound(0x100)
      mx.set_resource(2, :faulted)
      assert_equal ex8, mx.call(0x100, input)
      assert_equal psw, mx.bound(0x100)
      mx.set_resource(2, :ok)

      fx[:bad] = 2
      assert_equal Zxv::Status::E_IO, status_of(Zxv::ZxvSystemError) { mx.call(0x100, input) }
      fx[:bad] = 0

      out, checked = mx.shadow(0x100, input)
      assert_equal [ex8, true], [out, checked]
      sw[:bad] = 1
      assert_equal Zxv::Status::E_DISPUTE, status_of(Zxv::ZxvRefusedError) { mx.shadow(0x100, input) }
      assert_equal Zxv::Status::E_NOPROVIDER, status_of { mx.call(0x100, input) }
      assert_equal Zxv::Truth::TRUE, mx.attest(pfx)
      assert_equal Zxv::Truth::FALSE, mx.attest(psw)

      assert_equal Zxv::Status::E_ARG, status_of { mx.memo(0x100, 6) }
      mx.memo(0x100, 16)
      calls0 = mx.provider_info(pfx).calls
      2.times { assert_equal ex8, mx.call(0x100, input) }
      assert_equal calls0 + 1, mx.provider_info(pfx).calls
      assert_equal [1, 1], mx.memo(0x100, 16)

      assert_equal({ contract: 0, policy: 0, vectors: 1 }, mx.function_info(0x100).to_h)
      mx.start_contract(0x100, 2)
      assert_equal Zxv::Status::E_ROLLBACK, status_of(Zxv::ZxvRefusedError) { mx.start_contract(0x100, 2) }
      assert_equal Zxv::Status::E_NOPROVIDER, status_of { mx.call(0x100, input) }
      mx.add_vector(0x100, input, ex8)
      mx.attest(pfx)
      assert_equal [Zxv::Truth::TRUE, 2], [mx.provider_info(pfx).proof, mx.provider_info(pfx).contract]
      assert mx.provider_info(psw).flag?(:unproven)
      assert_raises(Zxv::FloatRejectedError) { mx.call(1.5, input) }
    end
  end

  # A two-slot device. readback asks a separate matrix's SHA-256 engine
  # (a callback calling back into libzxv).
  class SimDevice
    attr_reader :active

    def initialize(oracle)
      @oracle = oracle
      @slots = ["".b, "".b]
      @active = -1
    end

    def load(slot, payload)
      return Zxv::Status::E_RANGE if slot > 1 || payload.bytesize > 256

      @slots[slot] = payload
      nil
    end

    def readback(slot) = @oracle.call(Zxv::Hw::FN_SHA256, @slots[slot])

    def activate(slot)
      @active = slot
      nil
    end

    def run(input)
      return Zxv::Status::E_STATE if @active.negative?

      input.bytes.map { |b| @slots[@active].getbyte(b) }.pack("C*")
    end
  end

  # Three image builds in total (SLH-DSA signing takes seconds each).
  def test_signed_images_regions_and_contract_packages
    with_hw do |ctx, cap, nohw|
      key = Hw.keygen(cap)
      other = Hw.keygen(nohw) # CRYPTO_SIGN is an ABI 1.1 bit
      begin
        assert_equal Hw::PK_BYTES, key.public_key.bytesize
        assert_equal Hw::MLDSA_SK_BYTES, key.mldsa_secret.bytesize
        refute_equal key.public_key, other.public_key

        oracle = Zxv::HwMatrix.new(cap)
        mx = Zxv::HwMatrix.new(cap)
        mx.add_function(0x200, "inc").add_resource(1).add_resource(2)
        vin = (0..255).to_a.pack("C*")
        inc = (0..255).map { |i| (i + 1) & 0xFF }.pack("C*")
        mx.add_vector(0x200, vin, inc)
        sim = SimDevice.new(oracle)
        psoft = mx.add_provider(0x200, resource: 1, kind: :soft, cost: 50) { |_op, d| d.bytes.map { |b| (b + 1) & 0xFF }.pack("C*") }
        preg = mx.add_provider(0x200, resource: 2, kind: :fabric, cost: 1) { |_op, d| sim.run(d) }
        assert_equal Zxv::Truth::TRUE, mx.attest(psoft)

        args = { region_id: 1, target_class: 0xD0, device: sim, matrix: mx, log_cap: 12 }
        assert_raises(Zxv::CapabilityDeniedError) { Zxv::HwRegion.new(nohw, provider: preg, **args) }
        assert_equal Zxv::Status::E_ARG, status_of { Zxv::HwRegion.new(cap, provider: psoft, **args) }
        rg = Zxv::HwRegion.new(cap, provider: preg, **args)
        rg.trust_key(key.public_key)
        events = []
        rg.on_event { |ev, _reason, version| events << [ev, version] }

        img = { kind: :partial, target_class: 0xD0, fn_id: 0x200, version: 1, svn: 1, payload: inc }
        forged = Hw.build_image(cap, **img, key: other)
        good = Hw.build_image(cap, **img, key: key)
        assert_equal Hw::HEADER_BYTES + 256 + Hw::SIG_BYTES, good.bytesize
        assert_equal Zxv::Status::E_VERIFY, status_of(Zxv::VerificationFailedError) { rg.stage(forged) }
        wrong_hw = good.dup
        wrong_hw[12, 4] = [0xD1].pack("V")
        assert_equal Zxv::Status::E_TARGET, status_of(Zxv::ZxvUsageError) { rg.stage(wrong_hw) }

        rg.stage(good)
        assert_equal Zxv::Status::E_STATE, status_of { rg.commit }
        rg.trial(5)
        assert_equal preg, mx.bound(0x200)
        rg.commit
        assert_equal({ state: :active, version: 1, floor: 1, quarantined: 1 }, rg.info.to_h)
        assert_equal [[:rejected, 1], [:staged, 1], [:tested, 1], [:activated, 1]], events
        assert_nil rg.event_error
        assert_equal inc[0, 4], mx.call(0x200, vin[0, 4])

        assert_equal Zxv::Status::E_ROLLBACK, status_of(Zxv::ZxvRefusedError) { rg.stage(good) }
        head, count = rg.log
        assert_equal 5, count
        assert_equal events.size, count
        last = rg.log_entry(count - 1)
        assert_equal [:rejected, 1, head], [last.event, last.id, last.hash]
        act = rg.log_entry(3)
        assert_equal [:activated, 1], [act.event, act.version]
        refute_equal 2, (act.tag >> 2) & 3
        assert_kind_of Zxv::Hexagram, act.hexagram
        assert_equal count, rg.log_entries.size
        assert_equal Zxv::Status::E_NOT_FOUND, status_of { rg.log_entry(count) }

        payload = Hw.vector_list([[0, [5, 250].pack("C*"), [6, 251].pack("C*")]])
        assert_equal 16, payload.bytesize
        pkg = Hw.build_image(cap, kind: :vectors, target_class: 0, fn_id: 0x200, version: 7, svn: 3,
                                  payload: payload, key: key)
        matrix_only = Zxv::HwMatrix.new(cap.attenuate(Zxv::Capability::ALL | Zxv::Capability::BITS[:hw_matrix]))
        assert_raises(Zxv::CapabilityDeniedError) { matrix_only.apply_contract(pkg) }
        assert_equal Zxv::Status::E_VERIFY, status_of { mx.apply_contract(pkg) } # no key trusted yet
        mx.trust_key(key.public_key)
        assert_equal Zxv::Status::E_FORMAT, status_of { mx.apply_contract(good) }
        assert_equal Zxv::Status::E_FORMAT, status_of { rg.stage(pkg) }
        assert_equal 2, mx.apply_contract(pkg)
        info = mx.function_info(0x200)
        assert_equal [7, 1], [info.contract, info.vectors]
        assert_equal Zxv::Status::E_ROLLBACK, status_of { mx.apply_contract(pkg) }
        _head, mcount = mx.log
        assert_operator mcount, :>=, 1
        rec = mx.log_entries.find { |e| e.event == :contract }
        assert_equal [0x200, 7], [rec.id, rec.version]

        # Fill the region's log (12): the next recorded refusal halts the
        # region and poisons the context. Fail closed.
        while rg.log[1] < 12
          assert_raises(Zxv::VerificationFailedError) { rg.stage(forged) }
        end
        assert_raises(Zxv::ZxvFatal) { rg.stage(forged) }
        assert ctx.poisoned?
      ensure
        key.close
        other.close
        assert key.mldsa_secret.closed?
        assert other.slhdsa_secret.closed?
      end
    end
  end
end
