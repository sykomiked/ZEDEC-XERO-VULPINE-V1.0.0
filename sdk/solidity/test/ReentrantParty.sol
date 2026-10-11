// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
pragma solidity ^0.8.24;

interface IZxvStateChannel {
    function deposit(bytes32 channelId) external payable;
    function closeWithDeposits(bytes32 channelId) external;
    function withdraw() external;
}

/// Test-only: a channel party that tries to re-enter withdraw() from its
/// receive hook. The guard must make the whole withdrawal revert.
contract ReentrantParty {
    IZxvStateChannel public immutable target;
    uint256 public reentries;

    constructor(IZxvStateChannel t) {
        target = t;
    }

    function fund(bytes32 id) external payable {
        target.deposit{value: msg.value}(id);
    }

    function close(bytes32 id) external {
        target.closeWithDeposits(id);
    }

    function attack() external {
        target.withdraw();
    }

    receive() external payable {
        reentries++;
        target.withdraw();
    }
}
