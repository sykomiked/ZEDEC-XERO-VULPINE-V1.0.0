// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

/// The ZXV SDK for Dart and Flutter, over libzxv (ABI 1.5) with dart:ffi.
///
/// ```dart
/// final ctx = ZxvContext.create();
/// final root = ctx.rootCapability(Permission.values.toSet());
/// ctx.enableCurrency(root, 'NGN');
/// final ledger = Ledger(root);
/// ...
/// ctx.close(); // closes root and everything else made from ctx
/// ```
library;

export 'src/aipi.dart'
    show
        Multicodec,
        CidBase,
        Cid,
        Intent,
        VerifyMode,
        Privacy,
        Determinism,
        Route,
        FunctionRecord,
        CallRecord,
        ClaimRecord,
        AipiInfo,
        AipiNode;
export 'src/budgets.dart' show Budgets, Budget, BudgetStatus, Resource, CancelToken, BudgetWaitCancelled;
export 'src/context.dart' show ZxvContext, Capability, CapabilityInfo, Permission, FatalPolicy;
export 'src/coin.dart'
    show
        Coins,
        Octane,
        Tokens,
        FibonacciCode,
        FibonacciEncoded,
        FibonacciDecoded,
        TokenHolding,
        TokenWallet,
        TokenWalletInfo;
export 'src/crypto.dart' show Hashes, SecretKey, Ed25519, Ed25519KeyPair, MlDsa87, MlDsa87KeyPair;
export 'src/errors.dart';
export 'src/fee.dart' show FeeBucket, FeeUnit, FeeRatio, FeePolicy, FeeCharge, FeeSplit, AssuranceFee;
export 'src/freight.dart'
    show
        FreightHeader,
        FreightMember,
        Freight,
        FreightJoinStatus,
        FreightJoin,
        Generator,
        CidResolver,
        OpStreamPeek,
        OpStream,
        OpStreamWriter;
export 'src/hexagram.dart' show Hexagram, HexBase, HexPolarity, HexPhase, HexAction;
export 'src/paradox.dart'
    show
        ParadoxKind,
        ParadoxAction,
        Outcome,
        NodeState,
        ParadoxLog,
        ParadoxFlag,
        Trip,
        Evidence,
        Classification,
        ParadoxLogRecord,
        ParadoxQuarantineRecord,
        RepairProof,
        Draft,
        Paradox,
        RegistryEntry,
        Detection,
        Containment,
        HoldOutcome,
        FrontierOutcome,
        ParadoxBook,
        PolicyValues,
        PolicyBounds,
        ParadoxPolicy,
        HoldInfo,
        HoldBranch,
        ParadoxHold,
        GraphNode,
        GraphInfo,
        ParadoxGraph;
export 'src/ledger.dart' show Ledger, Account, AccountKind, Balance, JournalEntry;
export 'src/quarantine_log.dart' show QuarantineLog, QuarantineRecord;
export 'src/rational.dart' show Rational;
export 'src/treaty.dart'
    show
        TreatyFault,
        TreatyOp,
        PicDivision,
        Pic18,
        TreatyParty,
        TreatyObligation,
        Treaty,
        TreatyPartyResult,
        TreatyLeg,
        TreatyResult;
export 'src/truth.dart' show Truth;
export 'src/vhub.dart' show VHubCurrency, VHubBalance, VHubQuote, VHubSwap, VHub;
export 'src/web4.dart'
    show
        Web4,
        ConsentScope,
        ToolFlag,
        Tool,
        VerifiedCard,
        AgentCard,
        EnvelopeKind,
        AgentKey,
        AgentDirectory,
        ConsentToken,
        ConsentLog,
        Envelope,
        ManifestInfo,
        ToolManifest;
export 'src/zxv.dart' show Zxv, ZxvVersion, Feature, Feature2;
