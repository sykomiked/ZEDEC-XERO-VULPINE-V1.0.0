// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// The public API, shared by the Node and browser entry points.

export { Zxv, Feature, Feature2, KernelExact, KernelWire, Iso4217 } from './zxv.js';
export type { FeatureName, Feature2Name, Version, Iso4217Currency, LoadOptions } from './zxv.js';
export { ZxvContext, Capability, Cap, CurrencyFlag, IDENTITY_BYTES } from './context.js';
export type { CapabilityInfo, ContextOptions, RootCapabilityOptions, CurrencyInfo } from './context.js';
export { Rational } from './rational.js';
export { Truth, TruthNotCertainError, toBoolean } from './truth.js';
export type { TruthName, TruthPolicy } from './truth.js';
export { Ubh168Frame, WireTruth, UBH168_FRAME_BYTES } from './ubh168.js';
export type { SPlus, SMinus } from './ubh168.js';
export {
  Ledger, AccountKind, AMOUNT_MAX, Iso20022Message, MxKind, MxField, MxAmount, CreditDebit, EntryStatus,
  NettingCycle, Cards, CardNetwork, VssCheck, VssLevel, runVss, vssLevel,
} from './ledger.js';
export type { Balance, JournalEntry, AccountInfo, NetPosition, IssuedCard, Authorization } from './ledger.js';
export { Attestations, Subject } from './attestations.js';
export {
  Hexagram, HexBase, HexPolarity, HexPhase, HexAction, HEX_ACTION_NAMES, QuarantineLog, QUARANTINE_MAX_RECORDS,
} from './hexagram.js';
export type { QuarantineRecord } from './hexagram.js';
export {
  HwProfile, HwMatrix, HwRegion, HwReleaseKey, HwKind, HwClass, HwCapKey, HwIsa, HwPairing, HwResourceState, HwFn, HwPolicy,
  HwProviderFlag, HwPower, HwImageKind, HwRegionState, HwEvent, encodeVectors, HW_RES_HOST, HW_ROOT, HW_EXT_MAX,
  HW_CLASS_PRIVATE, HW_PK_BYTES, HW_MLDSA_SK_BYTES, HW_SLHDSA_SK_BYTES, HW_SIG_BYTES, HW_HEADER_BYTES,
} from './hardware.js';
export type {
  Implementation, HwDevice, ProfileNode, ProfilePairing, FunctionInfo, ProviderInfo, HwLogEntry, PolicyOptions, RegionEvent,
  RegionInfo, RegionOptions,
} from './hardware.js';
export type { AttestationState, AttestOptions } from './attestations.js';
export { Budgets, Budget, BudgetResource } from './budgets.js';
export type { Amount, BudgetStatus, ConsumeAsyncOptions, ExhaustedEvent, BudgetOptions } from './budgets.js';
export { PhaseClock, DeferredEvent, PHASE_CYCLE, Decision, StepFlag, Health, PhaseId, EventState } from './phase.js';
export type { Admission, Advance, AwaitOptions } from './phase.js';
export { Swarm, SwarmInfo } from './swarm.js';
export type { SwarmOptions, Allotment, SwarmGrant, EmotionProfile } from './swarm.js';
export { SecretKey, Hashes, Ed25519, MlDsa87, MlKem1024 } from './crypto.js';
export type { KeyPair, Encapsulation, MlDsaOptions, BytesLike, LibraryOwner } from './crypto.js';
export { Model, Session, KvCache, StopReason } from './engine.js';
export type { ModelInfo, SessionOptions, GenerateOptions, GeneratedToken, GenerateResult, TokenStream } from './engine.js';
export { StateChannel } from './evm.js';
export type { ChannelParty, LedgerParty, ChannelState } from './evm.js';
export {
  Status, StatusClass, ZxvError, ZxvUsageError, FloatRejectedError, ZxvRefusedError, CapabilityDeniedError,
  BudgetExhaustedError, InsufficientFundsError, VerificationFailedError, NotImplementedError, ZxvSystemError,
  ZxvFatalError, DenominationError, TreatyError, CidMismatchError, ConsentDeniedError, ErasureError, PegError,
  LimitExceededError, ParadoxStepLimitError, OwnerApprovalRequiredError, WrongRailError, CoverageError, WrongLaneError,
  RequestExpiredError, PaidChanceRefusedError,
} from './errors.js';
export type { FatalPolicy, ZxvStatusInfo } from './errors.js';
// ---- ABI 1.5 ----
export {
  TREATY_RECORD_BYTES, TREATY_RESULT_BYTES, TREATY_SUTRA_MAX, TREATY_MAX_PARTIES, TREATY_MAX_OWES, TREATY_NAME_MAX, TREATY_ID_MAX, TREATY_PIC18_MAX, TREATY_WEIGHT_MAX, TreatyOp, TreatyFault, Treaty, TreatyResult, Treaties,
} from './treaty.js';
export type {
  TreatyParty, TreatyObligation, TreatyRunSummary, TreatyPartyOutcome, TreatyLeg, PicDivision,
} from './treaty.js';
export {
  CID_MAX_BYTES, CID_TEXT_MAX, CID_DIGEST_MAX, CidCodec, CidHash, CidBase, Cid, Cids,
} from './cid.js';
export {
  AIPI_NAME_MAX, AIPI_IO_MAX, AIPI_CERT_MAX, AIPI_QUORUM_MAX, AIPI_FN_MAX, AIPI_CALL_MAX, AIPI_CLAIM_MAX, AIPI_BLOCK_MAX, AIPI_STORE_MIN, AIPI_STORE_MAX, AIPI_BLOCKS_MAX, AipiIntent, AipiVerify, AipiPrivacy, AipiDeterminism, AipiRoute, AipiRecords, AipiNode,
} from './aipi.js';
export type {
  AipiFunctionSpec, AipiFunctionInfo, AipiCall, AipiClaim, AipiNodeOptions, AipiNodeInfo,
} from './aipi.js';
export {
  COIN_RUNGS, COIN_TOP_FACE, TOKEN_TIERS, TOKEN_SUPER_TIER, TOKEN_CODE_MAX_BITS, TOKEN_CODE_MAX_BYTES, Octane, Coins, Tokens, TokenWallet,
} from './coin.js';
export type {
  TokenCounts, TokenFace, TokenValue, TokenCodeStream, TokenWalletInfo,
} from './coin.js';
export {
  FEE_NUM, FEE_DEN, FeeBucket, FeeUnit, Fees,
} from './fee.js';
export type {
  FeeWithCarry, FeeSplit, FeePolicy,
} from './fee.js';
export {
  VHUB_MAX_CURRENCIES, VHUB_MAX_RULES, VHUB_LIMIT_SENDERS, VHUB_PEG_MSG_BYTES, VHUB_CODE_MAX, VHUB_AMOUNT_MAX, VHUB_VBILL_ISSUER, VHUB_ISSUER_KEY_BYTES, VHUB_PEG_SIG_BYTES, VHub,
} from './vhub.js';
export type {
  VHubPeg, VHubCurrency, VHubCurrencyOptions, VHubBalance, VHubQuote, VHubSwapRequest, VHubSwapResult,
} from './vhub.js';
export {
  FREIGHT_PACKET_BYTES, FREIGHT_ROW_PAYLOAD, FREIGHT_ROWS, FREIGHT_BYTES, FREIGHT_MAX_PAYLOAD, FREIGHT_HEADER_BYTES, FREIGHT_VERSION, FREIGHT_MAX_SET, FREIGHT_SET_MAX_BYTES, FREIGHT_CID_MAX, FREIGHT_GEN_PARAM_MAX, FREIGHT_OPS_MAX, FreightFlag, FreightGen, FreightHeader, FreightCodec, FreightJoin, FreightOpsWriter,
} from './freight.js';
export type {
  FreightMember, FreightOptions, FreightHeaderFields, FreightJoinStatus, FreightResolve, FreightExpandOptions,
} from './freight.js';
export {
  W4_PK_BYTES, W4_SIG_BYTES, W4_ID_BYTES, W4_NAME_MAX, W4_MAX_TOOLS, W4_MSG_MAX_PAYLOAD, W4_CONSENT_BYTES, W4_CARD_MAX, W4_MANIFEST_MAX, W4_ENV_MAX, W4_DIR_MAX, W4_CONSENT_LOG_MAX, W4ToolFlag, W4ConsentScope, W4MessageKind, W4MessageFlag, W4Agent, W4Directory, W4Manifest, W4ConsentLog, Web4,
} from './web4.js';
export type {
  W4CardInfo, W4Tool, W4ManifestInfo, W4ConsentRequest, W4ConsentInfo, W4Envelope, W4SealOptions, W4ReplyOptions, W4AgentOptions,
} from './web4.js';
export {
  ParadoxKind, ParadoxDo, ParadoxOutcome, ParadoxNodeState, ParadoxFlag, ParadoxTrip, ParadoxLog, PDX_REC_BYTES, PDX_QREC_BYTES, PDX_REPAIR_BYTES, PDX_DRAFT_BYTES, PDX_LOG_MAX, PDX_REGISTRY_MAX, PDX_POLICY_MAX, PDX_HOLD_MAX, PDX_GRAPH_MAX, PDX_MAX_DEPS, PDX_UPDATES_MAX, PDX_TRACE_MAX, PDX_SRC_SEED, pdxProposalBytes, RepairProof, Paradox, ParadoxPolicy, ParadoxHold, ParadoxGraph, ParadoxBook,
} from './paradox.js';
export type {
  ParadoxClass, ParadoxClassification, ParadoxClassifyOptions, ParadoxRegistryEntry, ParadoxDestructiveInput, ParadoxDestructiveResult, ParadoxPolicyValues, ParadoxPolicyBounds, ParadoxHoldInfo, ParadoxBranch, ParadoxNode, ParadoxGraphInfo,
} from './paradox.js';
// ---- ABI 1.6 ----
export {
  VINO_REQUEST_BYTES, VINO_REQUEST_ID_BYTES, VINO_MEMO_MAX, VINO_MERCHANT_BYTES, VINO_NONCE_BYTES, VINO_RECEIPT_MSG_BYTES, VINO_WALLET_PK_BYTES, VINO_WALLET_SK_BYTES, VINO_SIG_BYTES, VINO_MAX_ACCOUNTS, VINO_MAX_VOUCHERS, VINO_AMOUNT_MAX, VinoRail, VinoLane, VinoAccountKind, VinoVoucherState, VinoRequestFlag, VinoPlay, SpendMeter, PaymentRequest, Receipt, Vino,
} from './vino.js';
export type {
  VinoAccountInfo, VinoVoucherInfo, SpendMeterResult, PaymentRequestFields, EncodedPaymentRequest, DecodedPaymentRequest, ReceiptFields,
} from './vino.js';
