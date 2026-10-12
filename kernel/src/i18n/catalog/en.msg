# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# ZXV UI message catalog: English. This is the SOURCE catalog. Every message
# id used by the app is defined here first; other catalogs translate it.
# Strings are taken from the desktop companion mockups (Main, Feed, Network,
# Wallet). Device-specific words in the mockups ("this Mac", "macOS") are made
# device-neutral here.
#
# Format, one message per line:   ID|category|status|text
#   category  "-" for a plain message, or a CLDR plural category
#             (zero, one, two, few, many, other) for one form of a plural
#             message; a plural message must have an "other" form.
#   status    source   = original English text (this file only)
#             machine  = machine translation, NEEDS NATIVE REVIEW
#             cldr:<path> = copied from Unicode CLDR (authoritative)
#             native:<reviewer>:<YYYY-MM-DD> = reviewed by a native speaker
#   text      UTF-8. {0} {1} ... are arguments, already formatted by the
#             caller (numbers through i18n_fmt_*, names as given).
# Lines starting with # and blank lines are ignored.

# ---- navigation ----
NAV_ASSISTANT|-|source|Assistant
NAV_FEED|-|source|Feed and groups
NAV_CALLS|-|source|Calls and streams
NAV_WALLET|-|source|Wallet
NAV_NETWORK|-|source|Network and farm
NAV_UPDATES|-|source|Updates
APP_SUBTITLE|-|source|Companion
SET_LANGUAGE|-|source|Language
LANG_REVIEW_NOTICE|-|source|This translation was made by machine and has not yet been checked by a native speaker.

# ---- connection status ----
STATUS_CONNECTED|-|source|Connected
STATUS_OFFLINE|-|source|Not connected
STATUS_PQ_SESSION|-|source|Post-quantum session: {0}
STATUS_PEERS|one|source|{0} peer
STATUS_PEERS|other|source|{0} peers
STATUS_MODEL_LOCAL|-|source|model runs locally

# ---- common buttons ----
BTN_SEND|-|source|Send
BTN_CANCEL|-|source|Cancel
BTN_REQUEST|-|source|Request
BTN_REPLY|-|source|Reply
BTN_TIP|-|source|Tip vouchers
BTN_SHOW_THEM|-|source|Show them
BTN_START_CALL|-|source|Start a call
BTN_END_CALL|-|source|End call
BTN_GO_LIVE|-|source|Go live
BTN_ADD_SERVER|-|source|Add a server
BTN_ADD_BUCKET|-|source|Add a bucket CID
BTN_INSTALL|-|source|Install
BTN_DOWNLOAD_STATEMENT|-|source|Download statement ({0})

# ---- assistant ----
ASSIST_RUNS_LOCAL|-|source|Runs on this device · {0}
ASSIST_PLACEHOLDER|-|source|Ask, or tell it what to do
ASSIST_ASK_LABEL|-|source|Ask the assistant

# ---- confirm payment ----
PAY_CONFIRM_TITLE|-|source|Confirm payment
PAY_TO|-|source|To
PAY_AMOUNT|-|source|Amount
PAY_FEE|-|source|Assurance fee (0.08889%)
PAY_RECORD|-|source|Record
PAY_BUTTON|-|source|Pay {0}
PAY_VERIFIED_PEER|-|source|verified peer

# ---- alerts and tasks ----
ALERTS_TITLE|-|source|Alerts
ALERTS_UNREAD|one|source|Alerts, {0} unread
ALERTS_UNREAD|other|source|Alerts, {0} unread
ALERT_TASK_FINISHED|-|source|Task finished
ALERT_UPDATE_AVAILABLE|-|source|Update available
ALERT_UPDATE_DETAIL|-|source|Default bucket: version {0}, signed by your trusted release key.
ALERT_RUNNING|-|source|Running
TASK_INDEXING|-|source|Indexing shared files to IPFS
TASK_PROGRESS|-|source|{0} · {1} of {2} files

# ---- feed and groups ----
FEED_TITLE|-|source|Feed
FEED_TAB_NEW|-|source|New to you
FEED_TAB_FOLLOWING|-|source|Following
FEED_NEW_POST|-|source|New post
FEED_RANKING_NOTE|-|source|Ranked by Interaction Surplus: posts that add something you have not seen rise; repeats sink.
FEED_NEW_SIGNAL|-|source|new signal {0}
FEED_FOLDED|one|source|{0} similar post about the same release was folded into one.
FEED_FOLDED|other|source|{0} similar posts about the same release were folded into one.
FEED_YOUR_GROUPS|-|source|Your groups
FEED_GROUP_MEMBERS|one|source|Group · {0} member
FEED_GROUP_MEMBERS|other|source|Group · {0} members
FEED_YOU_ARE_ADMIN|-|source|you are admin
FEED_SYNDICATES|-|source|Syndicates
FEED_SYNDICATE_ORGS|one|source|{0} organisation
FEED_SYNDICATE_ORGS|other|source|{0} organisations
FEED_TREASURY_MULTISIG|-|source|treasury {0}-of-{1}
FEED_START_GROUP|-|source|Start a group or syndicate

# ---- network, calls and streams ----
NET_TITLE|-|source|Network
NET_MODE_LABEL|-|source|Connection mode
NET_MODE_OFFLINE|-|source|Offline
NET_MODE_LAN|-|source|Local network
NET_MODE_ONLINE|-|source|Online
NET_NOTE_OFFLINE|-|source|Everything runs on this machine. Actions queue and sync later.
NET_NOTE_LAN|-|source|Nearby devices only. No internet needed.
NET_NOTE_ONLINE|-|source|Local network plus the wider mesh.
CALL_LIVE_STREAM|-|source|Live stream: {0}
CALL_VIEWERS|-|source|Viewers
CALL_YOUR_RATE|-|source|Your rate
CALL_RESERVE|-|source|You re-serve
CALL_SOURCES_NOTE|one|source|Pieces come from {0} viewer at once. More viewers, more sources.
CALL_SOURCES_NOTE|other|source|Pieces come from {0} viewers at once. More viewers, more sources.
CALL_INCOMING|-|source|Incoming call from {0}
CALL_MUTE|-|source|Mute
FARM_TITLE|-|source|Your farm
FARM_THIS_DEVICE|-|source|This device
FARM_IDLE_SHARE|-|source|{0} of idle compute
FARM_EARNED_TODAY|-|source|Earned today, after the assurance fee
FARM_VERIFIED|-|source|Work verified by peers

# ---- updates ----
UPD_BUCKETS|-|source|Update buckets
UPD_TRUSTED_SIGNED|-|source|trusted · signed
UPD_NOT_TRUSTED|-|source|not trusted yet
UPD_RATINGS|one|source|{0} of 5 from {1} rating
UPD_RATINGS|other|source|{0} of 5 from {1} ratings
UPD_READY|-|source|{0} ready to install
UPD_RATINGS_NO_TRUST|-|source|ratings never grant install trust

# ---- wallet ----
WAL_TITLE|-|source|Wallet and fees
WAL_ROLE|-|source|Account role
WAL_ROLE_INDIVIDUAL|-|source|Individual
WAL_ROLE_SELFBANK|-|source|Self-banking operator
WAL_ROLE_INSTITUTION|-|source|Institution
WAL_VFV_NAME|-|source|Vino Floating Vouchers · store credit
WAL_PLATFORM_RAILS|-|source|platform rails
WAL_VIA_GATEWAY|-|source|via linked gateway
WAL_OWN_WALLET|-|source|your own wallet
WAL_RECENT|-|source|Recent activity
WAL_COL_WHO|-|source|Who
WAL_COL_CAPITAL|-|source|Capital
WAL_FARM_EARNINGS|-|source|Farm earnings
CAP_KNOWLEDGE|-|source|Knowledge
CAP_BUILT|-|source|Built
CAP_FINANCIAL|-|source|Financial

# ---- assurance fee (0.08889%, replaced the phi% tithe 2026-10-10) ----
FEE_TITLE|-|source|Network assurance fee
FEE_EXPLAIN|-|source|Every payment carries a 0.08889% assurance fee. Half backs the Vino reserve floor; the rest funds the V-Bill dividend pool, node bounties and regenerative capital. Anything you give above it earns vouchers.
FEE_BASE|-|source|Base fee
FEE_YOUR_CONTRIB|-|source|Your contribution:
FEE_ABOVE|-|source|Above the fee by {0} of usage. That surplus is credited to you as vouchers.
FEE_EXACT|-|source|You give exactly the base fee. Move the slider to give more and earn vouchers.

# ---- errors ----
ERR_GENERIC|-|source|Something went wrong. Please try again.
ERR_NETWORK|-|source|Could not reach the network. Your action is saved and will be sent later.
ERR_INVALID_AMOUNT|-|source|Enter a valid amount.
ERR_INSUFFICIENT_FUNDS|-|source|Not enough funds for this payment.
ERR_PAYMENT_DECLINED|-|source|The payment was declined. Nothing was charged.
ERR_PEER_UNVERIFIED|-|source|This peer is not verified. Check who you are paying.
ERR_SIGNATURE|-|source|The signature could not be verified. Do not install this update.
ERR_CALL_FAILED|-|source|The call could not be connected.

# ---- the desktop window (kernel/arch/hosted/zxv_ui.html) ----
# Every word that window shows comes from this catalog: gen_ui.py compiles
# kernel/arch/hosted/gen_ui_strings.c against the i18n module, which writes
# a table of the ids the page uses for every locale, and embeds it in the
# page. Besides the UI_ ids below the window uses BTN_SEND, NET_TITLE,
# NET_MODE_ONLINE, NAV_UPDATES and LANG_REVIEW_NOTICE. The UI_ ids are
# English only until someone translates them: other languages show the
# English text, marked lang="en", and the window says so (UI_LANG_FALLBACK).
UI_WINDOW_TITLE|-|source|ZXV Swarm
UI_HEADER_TITLE|-|source|ZXV SWARM
UI_CONNECTING|-|source|connecting…
UI_QUIT|-|source|Quit
UI_QUIT_HINT|-|source|Stop the ZXV swarm
UI_PREVIEW_NOTE|-|source|Preview build: the swarm engine (Fibonacci budget, market, emotions, witness, ledger, DNA, harmonic cycles) is running for real. The agents are stand-ins until the AI model packs are installed, so they don't write real answers yet.
UI_LANG_FALLBACK|-|source|Parts of this window are not translated into {0} yet and are shown in English.
UI_SWARM_TITLE|-|source|Swarm, this cycle
UI_COL_LEVEL|-|source|Lvl
UI_COL_AGENT|-|source|Agent
UI_COL_FEELING|-|source|Feeling
UI_COL_TOKENS|-|source|Tokens
UI_COL_SPLIT|-|source|Split
UI_COL_MONEY|-|source|Money
UI_COL_SOCIAL|-|source|Social
UI_COL_GEN|-|source|Gen
UI_COL_WITNESS|-|source|Witness
UI_LEGEND_FLOOR|-|source|Fibonacci floor
UI_LEGEND_MARKET|-|source|market
UI_LEGEND_EMOTIONAL|-|source|emotional (imaginary)
UI_VERDICT_FALSE|-|source|✗ false
UI_VERDICT_TRUE|-|source|✓ true
UI_VERDICT_CONTESTED|-|source|⊥̸ contested
UI_WITNESS_BY|-|source|by {0}
UI_TALK_TITLE|-|source|Talk to the companion
UI_ASK_PLACEHOLDER|-|source|Ask, or write Hackronomicon shorthand like fan_out(question) -> verify(answers)
UI_WHO_YOU|-|source|You
UI_WHO_COMPANION|-|source|Companion
UI_GREETING|-|source|Hello. I am the companion at the top of the swarm. The engine is live; my specialists are stand-ins until their models are installed.
UI_NO_CONNECTION|-|source|(no connection to the swarm)
UI_STOPPED|-|source|The ZXV swarm has stopped. You can close this window.
UI_INSTANCE_REMOTE|-|source|remote instance
UI_INSTANCE_LOCAL|-|source|this device
UI_INSTANCE_LINE|-|source|{0} · {1}
UI_DISCONNECTED|-|source|disconnected
UI_BUDGET_TITLE|-|source|Machine budget
UI_KV_MODE|-|source|Mode
UI_BUDGET_REMOTE|-|source|remote (up to 99%)
UI_BUDGET_LOCAL|-|source|local guest (≤ 13/21 of free)
UI_KV_CORES|-|source|Cores
UI_CORES_VALUE|-|source|{0} total, {1} granted
UI_KV_MEMORY|-|source|Memory
UI_MEMORY_VALUE|-|source|{0} MB total, {1} MB granted
UI_KV_LEVELS|-|source|Levels
UI_KV_AGENTS|-|source|Agents
UI_KV_TOKENS_CYCLE|-|source|Tokens / cycle
UI_KV_COMPANION_LEFT|-|source|Companion left
UI_COMPANION_EXHAUSTED|-|source|budget exhausted for this cycle
UI_COMPANION_REMAINING|-|source|{0} of {1} tokens
UI_HARMONIC_TITLE|-|source|Harmonic cycles
UI_KV_TICK|-|source|Tick
UI_KV_FUNDAMENTAL|-|source|Fundamental
UI_FUNDAMENTAL_VALUE|-|source|{0} ({1} ticks each)
UI_KV_MARKET_CYCLE|-|source|Market cycle
UI_LEDGER_TITLE|-|source|Ledger and market
UI_KV_SETTLED|-|source|Settled cycles
UI_KV_HELD|-|source|Held cycles
UI_KV_MONEY_SUPPLY|-|source|Money supply
UI_KV_POT|-|source|Pot
UI_KV_IMAG_POOL|-|source|Imaginary pool
UI_KV_MOOD|-|source|Mood
UI_KV_SAVED|-|source|Tokens saved by sharing
UI_NET_OFF|-|source|Off
UI_NET_LAN|-|source|LAN
UI_NET_NOTE|-|source|Off: no socket, nothing leaves this machine. LAN: private addresses only.
UI_KV_SOCKET|-|source|Socket
UI_SOCKET_UDP|-|source|UDP {0}:{1}
UI_SOCKET_NONE|-|source|none
UI_KV_NODE|-|source|Node
UI_KV_PEERS|-|source|Peers
UI_KV_DATAGRAMS|-|source|Datagrams
UI_DATAGRAMS_VALUE|-|source|{0} in, {1} out
UI_KV_ERROR|-|source|Error
UI_UPD_CHECK|-|source|Check for updates
UI_UPD_NOTE|-|source|Checks only when you ask, or daily while Online. Unsigned listings are never installable.
UI_KV_STATUS|-|source|Status
UI_KV_CHECKS|-|source|Checks
UI_CHECKS_VALUE|-|source|{0} ({1} requests)
UI_KV_GATEWAY|-|source|Gateway
UI_KV_DETAIL|-|source|Detail
UI_NOTES_TITLE|-|source|Notifications
UI_NOTES_READ|-|source|Mark all read
UI_NOTES_NONE|-|source|None yet.
UI_NOTE_COUNT|-|source|{0} ({1})
UI_NOTE_BODY|-|source|{0} · {1}
