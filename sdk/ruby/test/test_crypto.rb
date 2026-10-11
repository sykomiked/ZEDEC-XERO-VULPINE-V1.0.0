# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
# frozen_string_literal: true

require_relative "test_helper"

class TestCrypto < Minitest::Test
  include ZxvTest

  def test_hashes_match_vectors
    vectors["hash"].each do |v|
      d = v["utf8"].b
      assert_equal v["keccak256"], unhex(Zxv::Hashes.keccak256(d))
      assert_equal v["sha3_256"], unhex(Zxv::Hashes.sha3_256(d))
      assert_equal v["shake256_64"], unhex(Zxv::Hashes.shake256(d, 64))
    end
  end

  def test_ed25519_matches_vectors
    v = vectors["ed25519"]
    with_context do |_ctx, cap|
      ed = Zxv::Ed25519.new(cap)
      sk = Zxv::SecretKey.from_bytes_unsafe(hex(v["sk"]))
      assert_equal v["pk"], unhex(ed.public_key(sk))
      sig = ed.sign(sk, v["msg_utf8"])
      assert_equal v["sig"], unhex(sig)
      assert ed.verify(hex(v["pk"]), v["msg_utf8"], sig)
      bad = sig.dup
      bad.setbyte(0, bad.getbyte(0) ^ 1)
      assert_equal false, ed.verify(hex(v["pk"]), v["msg_utf8"], bad)
      assert_raises(Zxv::VerificationFailedError) { ed.verify!(hex(v["pk"]), v["msg_utf8"], bad) }
      assert_raises(Zxv::CapabilityDeniedError) { Zxv::Ed25519.new(cap.attenuate(:crypto_kem)).sign(sk, "x") }
      kp = ed.generate
      assert ed.verify(kp.public_key, "hi", ed.sign(kp.secret_key, "hi"))
      kp.close
      sk.close
      assert sk.closed?
      assert_raises(Zxv::ZxvUsageError) { ed.sign(sk, "x") }
    end
  end

  def test_mldsa87_matches_vectors
    v = vectors["mldsa87"]
    with_context do |_ctx, cap|
      dsa = Zxv::MlDsa87.new(cap)
      kp = dsa.generate(seed: hex(v["seed"]))
      assert_equal v["pk_sha3_256"], unhex(Zxv::Hashes.sha3_256(kp.public_key))
      assert_equal v["sk_sha3_256"], unhex(Zxv::Hashes.sha3_256(kp.secret_key.export_bytes_unsafe))
      sig = dsa.sign(kp.secret_key, v["msg_utf8"], context: v["ctx_utf8"], deterministic: true)
      assert_equal v["sig_deterministic_sha3_256"], unhex(Zxv::Hashes.sha3_256(sig))
      assert dsa.verify(kp.public_key, v["msg_utf8"], sig, context: v["ctx_utf8"])
      assert_equal false, dsa.verify(kp.public_key, v["msg_utf8"], sig, context: "zxw")
      hedged = dsa.sign(kp.secret_key, "agent message", context: "zxv")
      assert dsa.verify(kp.public_key, "agent message", hedged, context: "zxv")
      kp.close
    end
  end

  def test_mlkem1024_matches_vectors
    v = vectors["mlkem1024"]
    with_context do |_ctx, cap|
      kem = Zxv::MlKem1024.new(cap.attenuate(:crypto_kem))
      kp = kem.generate(d: hex(v["d"]), z: hex(v["z"]))
      assert_equal v["ek_sha3_256"], unhex(Zxv::Hashes.sha3_256(kp.public_key))
      enc = kem.encapsulate(kp.public_key, m: hex(v["m"]))
      assert_equal v["ct_sha3_256"], unhex(Zxv::Hashes.sha3_256(enc.ciphertext))
      assert_equal v["ss"], unhex(enc.shared_secret.export_bytes_unsafe)
      ss = kem.decapsulate(kp.secret_key, enc.ciphertext)
      assert_equal v["ss"], unhex(ss.export_bytes_unsafe)
      assert_match(/32 bytes/, ss.inspect)
      refute_includes ss.inspect, v["ss"]
      bad = kp.public_key.dup
      bad[0, 32] = "\xFF".b * 32
      assert_raises(Zxv::ZxvUsageError) { kem.encapsulate(bad) }
      [kp, ss, enc.shared_secret].each(&:close)
    end
  end
end
