# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
# frozen_string_literal: true
#
# A tour of the Ruby SDK. Run from sdk/ruby:
#   ruby -Ilib examples/tour.rb
$LOAD_PATH.unshift File.expand_path("../lib", __dir__)
require "zxv"

puts "libzxv ABI #{Zxv.abi_version.join('.')}"

# Exact numbers: 0.1 + 0.2 is 3/10, and a float only enters on purpose.
sum = Zxv::Rational.parse("0.1") + Zxv::Rational.parse("0.2")
puts "0.1 + 0.2 = #{sum}; the double 0.1 is really #{Zxv::Rational.from_double_exact(0.1).inspect}"

# Four-valued logic: a contradiction is held, never collapsed.
t = Zxv::Truth.from_evidence(2, 1)
puts "evidence 2 for, 1 against -> #{t}; certainly true? #{t.certainly_true?}"

Zxv::Context.open do |ctx|
  root = ctx.root_capability

  # Ledger, through a capability.
  ctx.enable_currency("NGN", cap: root)
  ledger = Zxv::Ledger.new(root)
  bank = ledger.open_account("bank", "NGN", kind: :issuer)
  ada = ledger.open_account("ada", "NGN")
  ledger.post(bank, ada, 1_500_075, "NGN", "E2E-0001")
  puts "ada holds #{ledger.balance(ada).equity} kobo"

  # A read-only view cannot post.
  reader = Zxv::Ledger.new(root.attenuate(:ledger_read))
  begin
    reader.post(ada, bank, 1, "NGN", "E2E-0002")
  rescue Zxv::CapabilityDeniedError => e
    puts "read-only cap: #{e.status_name}"
  end

  # Budgets with exact rational limits; async waiting for a replenish.
  gpu = Zxv::Budgets.new(root).budget("gpu", resource: :compute_cycles, limit: 10)
  gpu.consume(10)
  later = gpu.consume_async(Zxv::Rational.of(5, 2), timeout: 5)
  gpu.release(3)
  later.value
  puts "gpu used #{gpu.used} of 10"

  # Phase ticks: an S0 event clears when the clock passes its tick.
  clock = Zxv::PhaseClock.new(root)
  event = clock.defer(Zxv::PhaseClock::CYCLE)
  clock.advance(Zxv::PhaseClock::CYCLE)
  puts "event #{event.id} is #{event.state} at tick #{clock.now}"

  # A UBH-168 frame, packed in pure Ruby.
  frame = Zxv::Ubh168Frame.new(tag: Zxv::Ubh168Frame.tag(3, 2, 1), s_plus: [1, 2, 3], s_minus: [4, 5])
  puts "frame #{frame.to_bytes.unpack1('H*')} resolves #{frame.resolve}"

  # Post-quantum signature.
  dsa = Zxv::MlDsa87.new(root)
  keys = dsa.generate
  sig = dsa.sign(keys.secret_key, "agent message", context: "zxv")
  puts "ML-DSA-87 signature (#{sig.bytesize} bytes) verifies: #{dsa.verify(keys.public_key, 'agent message', sig, context: 'zxv')}"
  keys.close

  # The integer engine, if the fixtures are present.
  fixtures = File.expand_path("../../conformance/fixtures", __dir__)
  model_path = File.join(fixtures, "tiny-qwen2.gguf")
  if File.exist?(model_path)
    Zxv::Model.open(root, model_path, File.join(fixtures, "tiny-tokenizer.gguf")) do |model|
      session = model.new_session(n_ctx: 128)
      result = session.generate("Hello", Zxv::GenerateOptions.new(max_tokens: 8))
      puts "generated #{result.tokens.size} tokens (#{result.stop_reason}): #{result.tokens.inspect}"
    end
  end

  # Settle on an EVM chain: the state ZxvStateChannel.sol checks.
  state = Zxv::StateChannel.new(root).ledger_state(
    channel_id: Zxv::Hashes.keccak256("demo channel"), nonce: 1, currency: "NGN",
    accounts: [ada], parties: ["0x#{'11' * 20}"]
  )
  puts "channel state digest #{state.digest_hex}"

  # Fail closed: a halted context raises ZxvFatal, which a bare rescue does not catch.
  ctx.halt("demo")
  begin
    ledger.balance(ada)
  rescue Zxv::ZxvFatal => e
    puts "after halt: #{e.status_name} (never retry)"
  end
end
