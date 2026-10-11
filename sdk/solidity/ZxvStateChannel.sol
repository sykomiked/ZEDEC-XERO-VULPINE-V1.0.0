// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
pragma solidity ^0.8.24;

/// @title ZxvStateChannel
/// @notice Settles ZXV's exact integer ledger balances on an EVM chain.
///
/// The ZXV ledger runs off chain. When the parties want to leave, they sign
/// a final state, and this contract pays it out. The state is exactly what
/// libzxv's `zxv_evm_encode_state` / `zxv_evm_ledger_state` produce:
///
///     encoding = abi.encode(bytes32 channelId, uint256 nonce,
///                           address[] parties, int256[] balances)
///     digest   = keccak256(encoding)
///
/// and every party signs the EIP-191 personal-message hash of `digest`
/// ("\x19Ethereum Signed Message:\n32" ++ digest) with its own Ethereum key.
///
/// How a channel maps ZXV balances
/// -------------------------------
/// * One channel holds ETH only. It has a fixed `unitWei`: one ledger minor
///   unit is `unitWei` wei. ZXV ledger amounts are limited to 2^53 - 1, so
///   a unit of 1 gwei (1e9 wei) lets a channel hold up to ~9 million ETH;
///   `unitWei = 1` makes ledger units plain wei.
/// * `balances[i]` is party i's FINAL HOLDING in ledger units (in ZXV terms,
///   the EQUITY (888) rail of that party's account). The ledger is signed,
///   but a holding paid out on chain cannot be negative, so every balance
///   must be >= 0 and the balances must add up to exactly the deposits:
///   `sum(balances) * unitWei == totalDeposits`. Nothing is minted, nothing
///   is burned, no rounding happens anywhere.
/// * Nonce 0 is reserved for the "deposits" state (each party gets back what
///   it put in). Any signed state with a higher nonce supersedes it.
///
/// Lifecycle
/// ---------
///   open -> deposit* -> submitState / closeWithDeposits (starts the
///   challenge window) -> submitState with a higher nonce (optional, until
///   the deadline) -> settle (after the deadline) -> withdraw (pull payment).
///
/// Security notes: not audited. Payouts use the pull pattern (settle only
/// credits `withdrawable`; `withdraw` sends) behind a reentrancy guard, with
/// checks-effects-interactions. The channel id is derived on chain from the
/// chain id and this contract's address, so a signed state cannot be
/// replayed on another chain or another deployment.
contract ZxvStateChannel {
    /// @notice Channel lifecycle.
    enum Status {
        None, //       never opened
        Open, //       accepting deposits; no state submitted yet
        Closing, //    a state was submitted; challenge window running
        Settled //     paid out (credited to withdrawable); terminal
    }

    /// @notice One channel's on-chain record.
    struct Channel {
        Status status;
        uint64 challengePeriod; // seconds between the first submission and settle
        uint64 deadline; //        settle allowed when block.timestamp >= deadline
        uint256 unitWei; //        wei per ledger minor unit
        uint256 totalDeposits; //  wei held for this channel
        uint256 nonce; //          nonce of the state currently on file
        address[] parties;
        int256[] balances; //      state currently on file (ledger units)
        uint256[] deposits; //     wei deposited per party
    }

    uint256 public constant MAX_PARTIES = 64;
    uint64 public constant MIN_CHALLENGE_PERIOD = 60;
    uint64 public constant MAX_CHALLENGE_PERIOD = 30 days;

    // secp256k1n / 2: signatures with a higher s are malleable (EIP-2).
    uint256 private constant HALF_N =
        0x7fffffffffffffffffffffffffffffff5d576e7357a4501ddfe92f46681b20a0;

    mapping(bytes32 => Channel) private channels;
    /// @notice Wei each address can pull with `withdraw`.
    mapping(address => uint256) public withdrawable;

    uint256 private locked = 1;

    event ChannelOpened(bytes32 indexed channelId, address[] parties, uint256 unitWei, uint64 challengePeriod);
    event Deposited(bytes32 indexed channelId, address indexed party, uint256 amountWei);
    event StateSubmitted(bytes32 indexed channelId, uint256 nonce, bytes32 digest, uint64 deadline);
    event ChannelSettled(bytes32 indexed channelId, uint256 nonce);
    event Withdrawn(address indexed to, uint256 amountWei);

    error BadParties();
    error BadParameter();
    error WrongStatus(Status actual);
    error NotParty();
    error NotUnitMultiple();
    error StaleNonce(uint256 onFile, uint256 offered);
    error WindowClosed();
    error WindowOpen();
    error LengthMismatch();
    error NegativeBalance(uint256 index);
    error SumMismatch(uint256 sumWei, uint256 totalDeposits);
    error BadSignature(uint256 index);
    error NothingToWithdraw();
    error TransferFailed();
    error Reentrancy();

    modifier nonReentrant() {
        if (locked != 1) revert Reentrancy();
        locked = 2;
        _;
        locked = 1;
    }

    // ------------------------------------------------------------------
    // Channel ids

    /// @notice The id `open` will assign. Off-chain code calls this (or
    ///         recomputes it) to know the channelId to put in ZXV states.
    function channelIdFor(
        address opener,
        address[] calldata parties,
        uint256 unitWei,
        uint64 challengePeriod,
        bytes32 salt
    ) public view returns (bytes32) {
        return keccak256(abi.encode(block.chainid, address(this), opener, parties, unitWei, challengePeriod, salt));
    }

    // ------------------------------------------------------------------
    // Open and fund

    /// @notice Open a channel between `parties` (distinct, non-zero, 2..64).
    ///         Anyone may open; the caller's ETH (if any) is not accepted here,
    ///         parties fund with `deposit`.
    /// @return channelId the id the parties must sign states for.
    function open(address[] calldata parties, uint256 unitWei, uint64 challengePeriod, bytes32 salt)
        external
        returns (bytes32 channelId)
    {
        uint256 n = parties.length;
        if (n < 2 || n > MAX_PARTIES) revert BadParties();
        if (unitWei == 0) revert BadParameter();
        if (challengePeriod < MIN_CHALLENGE_PERIOD || challengePeriod > MAX_CHALLENGE_PERIOD) revert BadParameter();
        for (uint256 i = 0; i < n; i++) {
            if (parties[i] == address(0)) revert BadParties();
            for (uint256 j = 0; j < i; j++) {
                if (parties[j] == parties[i]) revert BadParties();
            }
        }
        channelId = channelIdFor(msg.sender, parties, unitWei, challengePeriod, salt);
        Channel storage c = channels[channelId];
        if (c.status != Status.None) revert WrongStatus(c.status);
        c.status = Status.Open;
        c.challengePeriod = challengePeriod;
        c.unitWei = unitWei;
        c.parties = parties;
        c.deposits = new uint256[](n);
        emit ChannelOpened(channelId, parties, unitWei, challengePeriod);
    }

    /// @notice A party adds ETH to an open channel. The amount must be a
    ///         whole number of ledger units (a multiple of `unitWei`).
    ///         Deposits close as soon as any state is submitted.
    function deposit(bytes32 channelId) external payable {
        Channel storage c = channels[channelId];
        if (c.status != Status.Open) revert WrongStatus(c.status);
        if (msg.value == 0 || msg.value % c.unitWei != 0) revert NotUnitMultiple();
        uint256 i = _partyIndex(c, msg.sender);
        c.deposits[i] += msg.value;
        c.totalDeposits += msg.value;
        emit Deposited(channelId, msg.sender, msg.value);
    }

    // ------------------------------------------------------------------
    // Close

    /// @notice Put a state signed by EVERY party on file. The first one
    ///         starts the challenge window; until the deadline, any state
    ///         with a strictly higher nonce replaces it. Anyone may submit
    ///         (the signatures are what count).
    /// @param signatures one 65-byte (r, s, v) signature per party, in the
    ///        order of `parties`, over the EIP-191 hash of the state digest.
    function submitState(bytes32 channelId, uint256 nonce, int256[] calldata balances, bytes[] calldata signatures)
        external
    {
        Channel storage c = channels[channelId];
        if (c.status == Status.Open) {
            // first submission
        } else if (c.status == Status.Closing) {
            if (block.timestamp >= c.deadline) revert WindowClosed();
        } else {
            revert WrongStatus(c.status);
        }
        if (nonce == 0) revert StaleNonce(c.nonce, nonce); // nonce 0 is the deposits state
        if (c.status == Status.Closing && nonce <= c.nonce) revert StaleNonce(c.nonce, nonce);
        uint256 n = c.parties.length;
        if (balances.length != n || signatures.length != n) revert LengthMismatch();
        _checkConservation(c, balances);

        bytes32 digest = stateDigest(channelId, nonce, c.parties, balances);
        bytes32 signed = keccak256(abi.encodePacked("\x19Ethereum Signed Message:\n32", digest));
        for (uint256 i = 0; i < n; i++) {
            if (_recover(signed, signatures[i]) != c.parties[i]) revert BadSignature(i);
        }

        _putOnFile(c, nonce, balances);
        emit StateSubmitted(channelId, nonce, digest, c.deadline);
    }

    /// @notice A party that cannot get a signed state starts the close with
    ///         the deposits state (nonce 0: everyone gets back its deposit).
    ///         The others then have the challenge window to submit a signed
    ///         state with a higher nonce.
    function closeWithDeposits(bytes32 channelId) external {
        Channel storage c = channels[channelId];
        if (c.status != Status.Open) revert WrongStatus(c.status);
        _partyIndex(c, msg.sender);
        uint256 n = c.parties.length;
        int256[] memory b = new int256[](n);
        for (uint256 i = 0; i < n; i++) {
            // deposits are unit multiples and the total fits in uint256 / unit
            b[i] = int256(c.deposits[i] / c.unitWei);
        }
        _putOnFile(c, 0, b);
        emit StateSubmitted(channelId, 0, stateDigest(channelId, 0, c.parties, b), c.deadline);
    }

    /// @notice After the deadline, credit every party its final holding.
    ///         Anyone may call. Nothing is sent here: parties `withdraw`.
    function settle(bytes32 channelId) external {
        Channel storage c = channels[channelId];
        if (c.status != Status.Closing) revert WrongStatus(c.status);
        if (block.timestamp < c.deadline) revert WindowOpen();
        c.status = Status.Settled;
        uint256 n = c.parties.length;
        for (uint256 i = 0; i < n; i++) {
            // >= 0 was enforced when the state was put on file
            withdrawable[c.parties[i]] += uint256(c.balances[i]) * c.unitWei;
        }
        emit ChannelSettled(channelId, c.nonce);
    }

    /// @notice Pull everything credited to the caller.
    function withdraw() external nonReentrant {
        uint256 amount = withdrawable[msg.sender];
        if (amount == 0) revert NothingToWithdraw();
        withdrawable[msg.sender] = 0; // effects before the interaction
        (bool ok,) = payable(msg.sender).call{value: amount}("");
        if (!ok) revert TransferFailed();
        emit Withdrawn(msg.sender, amount);
    }

    // ------------------------------------------------------------------
    // Views

    /// @notice keccak256(abi.encode(channelId, nonce, parties, balances)):
    ///         byte for byte the digest of libzxv's zxv_evm_encode_state.
    function stateDigest(bytes32 channelId, uint256 nonce, address[] memory parties, int256[] memory balances)
        public
        pure
        returns (bytes32)
    {
        return keccak256(abi.encode(channelId, nonce, parties, balances));
    }

    /// @notice The raw state encoding (for cross-checking off-chain code).
    function encodeState(bytes32 channelId, uint256 nonce, address[] calldata parties, int256[] calldata balances)
        external
        pure
        returns (bytes memory)
    {
        return abi.encode(channelId, nonce, parties, balances);
    }

    /// @notice A channel's public record.
    function getChannel(bytes32 channelId)
        external
        view
        returns (
            Status status,
            uint64 challengePeriod,
            uint64 deadline,
            uint256 unitWei,
            uint256 totalDeposits,
            uint256 nonce,
            address[] memory parties,
            int256[] memory balances,
            uint256[] memory deposits
        )
    {
        Channel storage c = channels[channelId];
        return (
            c.status,
            c.challengePeriod,
            c.deadline,
            c.unitWei,
            c.totalDeposits,
            c.nonce,
            c.parties,
            c.balances,
            c.deposits
        );
    }

    // ------------------------------------------------------------------
    // Internals

    function _putOnFile(Channel storage c, uint256 nonce, int256[] memory balances) private {
        if (c.status == Status.Open) {
            c.status = Status.Closing;
            c.deadline = uint64(block.timestamp) + c.challengePeriod;
        }
        c.nonce = nonce;
        c.balances = balances;
    }

    function _checkConservation(Channel storage c, int256[] calldata balances) private view {
        uint256 sum = 0;
        for (uint256 i = 0; i < balances.length; i++) {
            if (balances[i] < 0) revert NegativeBalance(i);
            sum += uint256(balances[i]); // checked: reverts on overflow
        }
        uint256 sumWei = sum * c.unitWei; // checked
        if (sumWei != c.totalDeposits) revert SumMismatch(sumWei, c.totalDeposits);
    }

    function _partyIndex(Channel storage c, address who) private view returns (uint256) {
        uint256 n = c.parties.length;
        for (uint256 i = 0; i < n; i++) {
            if (c.parties[i] == who) return i;
        }
        revert NotParty();
    }

    function _recover(bytes32 hash, bytes calldata sig) private pure returns (address) {
        if (sig.length != 65) return address(0);
        bytes32 r;
        bytes32 s;
        uint8 v;
        assembly {
            r := calldataload(sig.offset)
            s := calldataload(add(sig.offset, 32))
            v := byte(0, calldataload(add(sig.offset, 64)))
        }
        if (v < 27) v += 27;
        if (v != 27 && v != 28) return address(0);
        if (uint256(s) > HALF_N) return address(0);
        return ecrecover(hash, v, r, s);
    }

    /// @dev Plain ETH sent outside `deposit` is refused, so the contract's
    ///      balance is always exactly what channels account for.
    receive() external payable {
        revert BadParameter();
    }
}
