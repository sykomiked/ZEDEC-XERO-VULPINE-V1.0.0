# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
# frozen_string_literal: true

module Zxv
  # Hash functions (pure; no capability needed). Inputs are Strings of
  # bytes; outputs are binary Strings.
  module Hashes
    module_function

    # SHA3-256 (FIPS 202). @return [String] 32 bytes
    def sha3_256(data) = digest(:zxv_sha3_256, data)

    # Ethereum Keccak-256 (pre-FIPS padding). @return [String] 32 bytes
    def keccak256(data) = digest(:zxv_keccak256, data)

    # SHAKE256 with +length+ output bytes.
    def shake256(data, length)
      b = Call.bytes!(data, "data")
      n = Call.int!(length, "length", min: 1, max: 1 << 24)
      out = FFI::MemoryPointer.new(:uint8, n)
      Call.check(Native.lib.zxv_shake256(Call.mem(b, allow_null: true), b.bytesize, out, n))
      out.read_bytes(n)
    end

    def digest(fn, data)
      b = Call.bytes!(data, "data")
      out = FFI::MemoryPointer.new(:uint8, 32)
      Call.check(Native.lib.public_send(fn, Call.mem(b, allow_null: true), b.bytesize, out))
      out.read_bytes(32)
    end
    private_class_method :digest
  end

  # Secret key material held OUTSIDE the Ruby heap (malloc'd memory), wiped
  # with zxv_secure_zero on #close and, as a backstop, by a finalizer.
  #
  # The bytes never become a Ruby String except through
  # #export_bytes_unsafe, which defeats the wiping (the copy lives in the
  # GC heap until collected and may be moved, swapped or dumped).
  class SecretKey
    # @api private
    def initialize(size)
      @size = size
      @mem = FFI::MemoryPointer.new(:uint8, size)
      @mem.autorelease = false
      ObjectSpace.define_finalizer(self, self.class.send(:wiper, @mem, size))
    end

    # Copy bytes from a Ruby String into a new SecretKey. The source String
    # stays in the Ruby heap: wipe or drop it yourself (and see "Honest limits").
    def self.from_bytes_unsafe(bytes)
      b = Call.bytes!(bytes, "bytes")
      k = new(b.bytesize)
      k.pointer.put_bytes(0, b)
      k
    end

    # @return [Integer]
    def bytesize = @size

    # Wipe and free the key. Idempotent.
    def close
      return if @mem.nil?

      ObjectSpace.undefine_finalizer(self)
      self.class.send(:wiper, @mem, @size).call
      @mem = nil
    end

    # @return [Boolean]
    def closed? = @mem.nil?

    # A heap copy of the key bytes. UNSAFE: the copy cannot be wiped.
    # @return [String]
    def export_bytes_unsafe = pointer.read_bytes(@size)

    def inspect = "#<Zxv::SecretKey #{@size} bytes#{' closed' if closed?}>"
    alias to_s inspect

    # @api private
    def pointer
      raise ZxvUsageError.new(Status::E_STATE, "SecretKey is closed") if @mem.nil?

      @mem
    end

    def self.wiper(mem, size)
      proc do
        Native.lib.zxv_secure_zero(mem, size)
        mem.free
      end
    end
    private_class_method :wiper
  end

  # A public key (String) and its SecretKey.
  KeyPair = Struct.new(:public_key, :secret_key, keyword_init: true) do
    # Wipe the secret key.
    def close = secret_key.close
  end

  # Base for the signature/KEM classes: they hold a capability.
  class CryptoBase
    # @return [Capability]
    attr_reader :capability

    # @param capability [Capability] needs :crypto_sign (signatures) or :crypto_kem (KEM)
    def initialize(capability)
      @capability = capability
    end

    private

    def check(rc) = Call.check(rc, capability.context)

    def secret!(key, size)
      unless key.is_a?(SecretKey) && key.bytesize == size
        raise ZxvUsageError.new(Status::E_ARG, "expected a #{size}-byte Zxv::SecretKey")
      end

      key.pointer
    end

    def verify_rc(rc)
      return true if rc == Status::OK
      return false if rc == Status::E_VERIFY

      check(rc)
    end
  end

  # RFC 8032 Ed25519 signatures.
  class Ed25519 < CryptoBase
    PUBLIC_KEY_BYTES = 32
    SECRET_KEY_BYTES = 32
    SIGNATURE_BYTES = 64

    # A fresh key pair (OS randomness).
    # @return [KeyPair]
    def generate
      pk = FFI::MemoryPointer.new(:uint8, 32)
      sk = SecretKey.new(32)
      check(Native.lib.zxv_ed25519_keygen(capability.ptr, pk, sk.pointer))
      KeyPair.new(public_key: pk.read_bytes(32), secret_key: sk)
    rescue Exception # rubocop:disable Lint/RescueException
      sk&.close
      raise
    end

    # The public key of a secret key.
    def public_key(secret_key)
      pk = FFI::MemoryPointer.new(:uint8, 32)
      check(Native.lib.zxv_ed25519_public(capability.ptr, secret!(secret_key, 32), pk))
      pk.read_bytes(32)
    end

    # @return [String] 64-byte signature
    def sign(secret_key, message)
      m = Call.bytes!(message, "message")
      sig = FFI::MemoryPointer.new(:uint8, 64)
      check(Native.lib.zxv_ed25519_sign(capability.ptr, secret!(secret_key, 32), Call.mem(m, allow_null: true), m.bytesize, sig))
      sig.read_bytes(64)
    end

    # @return [Boolean] whether the signature is valid
    def verify(public_key, message, signature)
      m = Call.bytes!(message, "message")
      verify_rc(Native.lib.zxv_ed25519_verify(capability.ptr, Call.mem(Call.bytes!(public_key, "public_key", size: 32)),
                                              Call.mem(m, allow_null: true), m.bytesize,
                                              Call.mem(Call.bytes!(signature, "signature", size: 64))))
    end

    # Like #verify, but raises VerificationFailedError when invalid.
    def verify!(public_key, message, signature)
      verify(public_key, message, signature) or
        raise VerificationFailedError.new(Status::E_VERIFY, "Ed25519 signature did not verify")
    end
  end

  # FIPS 204 ML-DSA-87 post-quantum signatures (not FIPS 140-3 validated).
  class MlDsa87 < CryptoBase
    PUBLIC_KEY_BYTES = 2592
    SECRET_KEY_BYTES = 4896
    SIGNATURE_BYTES = 4627
    CONTEXT_MAX = 255

    # A key pair; deterministic from a 32-byte +seed+ when given.
    # @return [KeyPair]
    def generate(seed: nil)
      pk = FFI::MemoryPointer.new(:uint8, PUBLIC_KEY_BYTES)
      sk = SecretKey.new(SECRET_KEY_BYTES)
      rc = if seed
             Native.lib.zxv_mldsa87_keygen_seed(capability.ptr, Call.mem(Call.bytes!(seed, "seed", size: 32)), pk, sk.pointer)
           else
             Native.lib.zxv_mldsa87_keygen(capability.ptr, pk, sk.pointer)
           end
      check(rc)
      KeyPair.new(public_key: pk.read_bytes(PUBLIC_KEY_BYTES), secret_key: sk)
    rescue Exception # rubocop:disable Lint/RescueException
      sk&.close
      raise
    end

    # Sign; hedged (fresh randomness) unless +deterministic+.
    # @param context [String] FIPS 204 context string, <= 255 bytes
    # @return [String] 4627-byte signature
    def sign(secret_key, message, context: "", deterministic: false)
      m = Call.bytes!(message, "message")
      c = Call.bytes!(context, "context")
      sig = FFI::MemoryPointer.new(:uint8, SIGNATURE_BYTES)
      fn = deterministic ? :zxv_mldsa87_sign_deterministic : :zxv_mldsa87_sign
      check(Native.lib.public_send(fn, capability.ptr, secret!(secret_key, SECRET_KEY_BYTES), Call.mem(m, allow_null: true),
                                   m.bytesize, Call.mem(c, allow_null: true), c.bytesize, sig))
      sig.read_bytes(SIGNATURE_BYTES)
    end

    # @return [Boolean]
    def verify(public_key, message, signature, context: "")
      m = Call.bytes!(message, "message")
      c = Call.bytes!(context, "context")
      verify_rc(Native.lib.zxv_mldsa87_verify(capability.ptr, Call.mem(Call.bytes!(public_key, "public_key", size: PUBLIC_KEY_BYTES)),
                                              Call.mem(m, allow_null: true), m.bytesize, Call.mem(c, allow_null: true), c.bytesize,
                                              Call.mem(Call.bytes!(signature, "signature", size: SIGNATURE_BYTES))))
    end

    # Like #verify, but raises VerificationFailedError when invalid.
    def verify!(public_key, message, signature, context: "")
      verify(public_key, message, signature, context: context) or
        raise VerificationFailedError.new(Status::E_VERIFY, "ML-DSA-87 signature did not verify")
    end
  end

  # FIPS 203 ML-KEM-1024 key encapsulation (not FIPS 140-3 validated).
  class MlKem1024 < CryptoBase
    ENCAPSULATION_KEY_BYTES = 1568
    DECAPSULATION_KEY_BYTES = 3168
    CIPHERTEXT_BYTES = 1568
    SHARED_SECRET_BYTES = 32

    # A ciphertext and the shared secret it carries.
    Encapsulation = Struct.new(:ciphertext, :shared_secret, keyword_init: true)

    # A key pair (public_key = encapsulation key, secret_key = decapsulation
    # key); deterministic from 32-byte +d+ and +z+ when both are given (KATs).
    # @return [KeyPair]
    def generate(d: nil, z: nil)
      ek = FFI::MemoryPointer.new(:uint8, ENCAPSULATION_KEY_BYTES)
      dk = SecretKey.new(DECAPSULATION_KEY_BYTES)
      rc = if d || z
             Native.lib.zxv_mlkem1024_keygen_derand(capability.ptr, Call.mem(Call.bytes!(d, "d", size: 32)),
                                                    Call.mem(Call.bytes!(z, "z", size: 32)), ek, dk.pointer)
           else
             Native.lib.zxv_mlkem1024_keygen(capability.ptr, ek, dk.pointer)
           end
      check(rc)
      KeyPair.new(public_key: ek.read_bytes(ENCAPSULATION_KEY_BYTES), secret_key: dk)
    rescue Exception # rubocop:disable Lint/RescueException
      dk&.close
      raise
    end

    # Encapsulate to an encapsulation key; deterministic from a 32-byte +m+.
    # @return [Encapsulation]
    def encapsulate(encapsulation_key, m: nil)
      ek = Call.mem(Call.bytes!(encapsulation_key, "encapsulation_key", size: ENCAPSULATION_KEY_BYTES))
      ct = FFI::MemoryPointer.new(:uint8, CIPHERTEXT_BYTES)
      ss = SecretKey.new(SHARED_SECRET_BYTES)
      rc = if m
             Native.lib.zxv_mlkem1024_encaps_derand(capability.ptr, ek, Call.mem(Call.bytes!(m, "m", size: 32)), ct, ss.pointer)
           else
             Native.lib.zxv_mlkem1024_encaps(capability.ptr, ek, ct, ss.pointer)
           end
      check(rc)
      Encapsulation.new(ciphertext: ct.read_bytes(CIPHERTEXT_BYTES), shared_secret: ss)
    rescue Exception # rubocop:disable Lint/RescueException
      ss&.close
      raise
    end

    # Recover the shared secret.
    # @return [SecretKey]
    def decapsulate(decapsulation_key, ciphertext)
      ss = SecretKey.new(SHARED_SECRET_BYTES)
      check(Native.lib.zxv_mlkem1024_decaps(capability.ptr, secret!(decapsulation_key, DECAPSULATION_KEY_BYTES),
                                            Call.mem(Call.bytes!(ciphertext, "ciphertext", size: CIPHERTEXT_BYTES)), ss.pointer))
      ss
    rescue Exception # rubocop:disable Lint/RescueException
      ss&.close
      raise
    end
  end
end
