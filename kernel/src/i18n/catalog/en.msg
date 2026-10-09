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
PAY_TITHE|-|source|Network tithe (φ%)
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
FARM_EARNED_TODAY|-|source|Earned today, after tithe
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
WAL_TITLE|-|source|Wallet and tithe
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

# ---- tithe ----
TITHE_TITLE|-|source|Network tithe
TITHE_EXPLAIN|-|source|Every node gives φ percent of the compute, bandwidth and money it uses, so the network stays fast for everyone. Anything you give above that earns vouchers.
TITHE_BASE|-|source|Base tithe
TITHE_YOUR_CONTRIB|-|source|Your contribution:
TITHE_ABOVE|-|source|Above the tithe by {0} of usage. That surplus is credited to you as vouchers.
TITHE_EXACT|-|source|You give exactly the base tithe. Move the slider to give more and earn vouchers.

# ---- errors ----
ERR_GENERIC|-|source|Something went wrong. Please try again.
ERR_NETWORK|-|source|Could not reach the network. Your action is saved and will be sent later.
ERR_INVALID_AMOUNT|-|source|Enter a valid amount.
ERR_INSUFFICIENT_FUNDS|-|source|Not enough funds for this payment.
ERR_PAYMENT_DECLINED|-|source|The payment was declined. Nothing was charged.
ERR_PEER_UNVERIFIED|-|source|This peer is not verified. Check who you are paying.
ERR_SIGNATURE|-|source|The signature could not be verified. Do not install this update.
ERR_CALL_FAILED|-|source|The call could not be connected.
