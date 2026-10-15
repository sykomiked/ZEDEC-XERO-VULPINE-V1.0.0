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
# the staple apps (Burrow, Quillon, Glint, Spindle) and the Playground
UI_TAB_SWARM|-|source|Swarm
UI_TAB_PLAY|-|source|Playground
UI_DOCK_APPS|-|source|Apps
UI_DOCK_TOOLBOX|-|source|Toolbox
UI_APP_MURMURATION|-|source|Murmuration
UI_APP_BURROW|-|source|Burrow
UI_APP_QUILLON|-|source|Quillon
UI_APP_GLINT|-|source|Glint
UI_APP_SPINDLE|-|source|Spindle
UI_FILES_NEW_FOLDER|-|source|New folder
UI_FILES_NEW_TEXT|-|source|New text file
UI_IMPORT|-|source|Import…
UI_FILES_TRASH|-|source|Trash
UI_FILES_EMPTY_TRASH|-|source|Empty Trash
UI_FILES_COL_NAME|-|source|Name
UI_FILES_COL_SIZE|-|source|Size
UI_FILES_COL_MODIFIED|-|source|Modified
UI_TEXT_UNTITLED|-|source|Untitled
UI_TEXT_NEW|-|source|New
UI_TEXT_OPEN|-|source|Open…
UI_TEXT_SAVE|-|source|Save
UI_TEXT_SAVE_AS|-|source|Save as…
UI_TEXT_PREVIEW|-|source|Preview
UI_TEXT_PROSE|-|source|Prose
UI_TEXT_PLACEHOLDER|-|source|Start typing. Save with ⌘S or Ctrl+S.
UI_CONSOLE_TITLE|-|source|Spindle: call zxvd
UI_RUN|-|source|Run
UI_CONSOLE_CLEAR|-|source|Clear
UI_CONSOLE_PARAMS|-|source|Parameters as a JSON object, for example {"path":""}
UI_CONSOLE_NOTE|-|source|Every call goes to this machine's zxvd, the same methods the SDKs and ZXV Studio use (api/zxvd.openrpc.json).
UI_CLOSE|-|source|Close
UI_CANCEL|-|source|Cancel
UI_OK|-|source|OK
UI_PLAY_TEMPLATES|-|source|Templates
UI_LOADING|-|source|Loading…
UI_PLAY_TEMPLATES_NOTE|-|source|Each one runs in ZXV's own languages, on scratch state: your books are never touched. From a terminal:
UI_PLAY_NEW_CMD|-|source|zxv new ID
UI_PLAY_PICK|-|source|Pick a template
UI_PLAY_RESET_HINT|-|source|Undo your edits to this template
UI_PLAY_RESET|-|source|Reset
UI_PLAY_REQ_PLACEHOLDER|-|source|Describe it in plain words; the local AI drafts the file
UI_PLAY_DRAFT_BTN|-|source|Draft with AI
UI_PLAY_USE|-|source|Use this draft
UI_PLAY_DISCARD|-|source|Discard
UI_PLAY_RESULT|-|source|Result
UI_PLAY_PRESS_RUN|-|source|Press Run to check and run the template.
UI_PLAY_ANSWERS|-|source|Four answers, never forced into yes or no:
UI_V_TRUE|-|source|TRUE
UI_PLAY_TRUE_MEANS|-|source|passed
UI_V_FALSE|-|source|FALSE
UI_PLAY_FALSE_MEANS|-|source|refused, with the line
UI_V_BOTH|-|source|BOTH
UI_PLAY_BOTH_MEANS|-|source|contradictory evidence, held for a person
UI_V_NEITHER|-|source|NEITHER
UI_PLAY_NEITHER_MEANS|-|source|not enough to decide yet.
UI_HOST_REMOTE|-|source|the remote instance
UI_HOST_LOCAL|-|source|this computer
UI_PLAY_ABSENT|-|source|The Playground is not part of this build.
UI_PLAY_RUNNING|-|source|Running…
UI_PLAY_RUN_FAILED|-|source|Could not run: {0}
UI_PLAY_PASSED|-|source|Passed
UI_PLAY_REFUSED|-|source|Refused
UI_PLAY_HELD_REVIEW|-|source|Held for review
UI_PLAY_UNDECIDED|-|source|Undecided
UI_PLAY_APPROVAL|-|source|A person must review this before anything happens. The Playground never actuates or settles for real.
UI_PLAY_LEDGER|-|source|Ledger: {0}
UI_PLAY_CHIP_GATE|-|source|gate
UI_PLAY_CHIP_ADVISORY|-|source|advisory
UI_PLAY_CHIP_NOT_RUN|-|source|not run
UI_PLAY_HELD|-|source|HELD · {0}
UI_PLAY_PENDING|-|source|pending · {0}
UI_PLAY_AT_LINE|-|source|at line {0}, column {1}
UI_PLAY_DRAFTING|-|source|The local swarm is drafting…
UI_PLAY_DRAFT_READY|-|source|AI draft for {0}. Untrusted until it passes the checks: review it, use it, then press Run.
UI_PLAY_DRAFT_STANDIN|-|source|No language model is installed yet, so a stand-in answered. Install a model pack to draft with AI.
UI_PLAY_DRAFT_FAILED|-|source|Could not draft: {0}
UI_RPC_NO_CONNECTION|-|source|no connection to the swarm
UI_RPC_NO_ZXVD|-|source|this engine has no zxvd, so the apps are not available
UI_RPC_REFUSED|-|source|the engine refused ({0})
UI_SIZE_B|-|source|{0} B
UI_SIZE_KB|-|source|{0} KB
UI_SIZE_MB|-|source|{0} MB
UI_SIZE_GB|-|source|{0} GB
UI_IMPORTING|-|source|Importing {0}: {1}%
UI_NAME_ERROR|-|source|{0}: {1}
UI_IMPORTED_ONE|-|source|Imported {0} file.
UI_IMPORTED_N|-|source|Imported {0} files.
UI_FILES_ROOT|-|source|ZXV
UI_FILES_BACK|-|source|Back to files
UI_FILES_STAT_ONE|-|source|{0} item · stored in {1} on {2}
UI_FILES_STAT_N|-|source|{0} items · stored in {1} on {2}
UI_FILES_TRASH_EMPTY|-|source|The Trash is empty.
UI_FILES_EMPTY|-|source|This folder is empty. Make a text file, a folder, or import files.
UI_FILES_PUT_BACK|-|source|Put back
UI_FILES_DELETE_NOW|-|source|Delete now
UI_FILES_OPEN|-|source|Open
UI_FILES_RENAME|-|source|Rename
UI_FILES_MOVE_TRASH|-|source|Move to Trash
UI_FILES_PUT_BACK_HINT|-|source|Where it goes, as a path in the ZXV folder.
UI_FILES_DELETE_Q|-|source|Delete for good?
UI_FILES_DELETE_TEXT|-|source|“{0}” will be removed from this computer. This cannot be undone.
UI_FILES_DELETE|-|source|Delete
UI_FILES_EMPTY_Q|-|source|Empty the Trash?
UI_FILES_EMPTY_TEXT|-|source|Everything in the Trash is removed from this computer. This cannot be undone.
UI_FILES_REMOVED_ONE|-|source|Removed {0} item.
UI_FILES_REMOVED_N|-|source|Removed {0} items.
UI_FILES_NEW_FOLDER_NAME|-|source|New Folder
UI_CREATE|-|source|Create
UI_TEXT_UNTITLED_FILE|-|source|Untitled.txt
UI_TEXT_EDITED|-|source|● edited
UI_TEXT_WORDS_ONE|-|source|{0} word
UI_TEXT_WORDS_N|-|source|{0} words
UI_TEXT_CHARS_ONE|-|source|{0} character
UI_TEXT_CHARS_N|-|source|{0} characters
UI_TEXT_LINES_ONE|-|source|{0} line
UI_TEXT_LINES_N|-|source|{0} lines
UI_TEXT_ZXV_PATH|-|source|ZXV/{0}
UI_TEXT_DISCARD_Q|-|source|Discard your changes?
UI_TEXT_DISCARD_TEXT|-|source|“{0}” has changes that are not saved.
UI_TEXT_OPENING|-|source|Opening…
UI_TEXT_SAVE_AS_TITLE|-|source|Save as
UI_TEXT_SAVE_HINT|-|source|A path in the ZXV folder, for example Notes/idea.md.
UI_TEXT_SAVING|-|source|Saving…
UI_TEXT_SAVED|-|source|Saved ZXV/{0}
UI_TEXT_CODE|-|source|Code
UI_PHOTOS_ONE|-|source|{0} photo or media file
UI_PHOTOS_N|-|source|{0} photos and media files
UI_PHOTOS_NONE|-|source|No photos or media here. Import some, or drop them on Burrow.
UI_CONSOLE_BAD_JSON|-|source|parameters are not valid JSON
UI_CONSOLE_STOP_Q|-|source|Stop the swarm?
UI_CONSOLE_STOP_TEXT|-|source|zxvd.shutdown stops the engine; this window will disconnect.
UI_CONSOLE_STOP|-|source|Stop
# the people apps (zxvd_people.h) and simple mode
UI_SIMPLE_MODE|-|source|Simple mode
UI_SIMPLE_HINT|-|source|Simple mode shows fewer details. It changes no check, key or limit.
UI_DOCK_PEOPLE|-|source|People
UI_APP_ASK_PAY|-|source|Ask and Pay
UI_APP_FEED|-|source|Feed
UI_APP_CALLS|-|source|Calls
UI_APP_TRANSLATE|-|source|Translate
UI_APP_DEVICES|-|source|Devices
UI_PP_ASK|-|source|Ask
UI_PP_ASK_PLACEHOLDER|-|source|Ask the companion anything
UI_PP_PAY|-|source|Pay
UI_PP_FROM|-|source|From
UI_PP_TO|-|source|To
UI_PP_AMOUNT_PLACEHOLDER|-|source|Amount, e.g. 12.50
UI_PP_REF_PLACEHOLDER|-|source|Reference (optional; each payment needs its own)
UI_PP_PAY_NOTE|-|source|Pay moves money between accounts in this computer's books. You confirm every payment. Accounts are opened from the command line (ledger.open_account).
UI_PP_NO_ACCOUNTS|-|source|No accounts yet.
UI_PP_PICK_TWO|-|source|Pick two accounts.
UI_PP_PICK_DIFFERENT|-|source|Pick two different accounts.
UI_PP_SAME_CURRENCY|-|source|Both accounts must hold the same currency.
UI_PP_AMOUNT_HINT|-|source|Enter an amount like 12.50.
UI_PP_PAY_Q|-|source|Pay?
UI_PP_PAY_TEXT|-|source|Pay {0} {1} from {2} to {3}?
UI_PP_PAID|-|source|Paid.
UI_PP_PAID_ENTRY|-|source|Paid. Entry {0}
UI_PP_POST|-|source|Post
UI_PP_POST_PLACEHOLDER|-|source|Write a post
UI_PP_REPLY_PLACEHOLDER|-|source|Write a reply
UI_PP_GROUP|-|source|Group
UI_PP_PUBLIC|-|source|Public
UI_PP_GROUPS|-|source|Groups
UI_PP_GROUP_NAME|-|source|New group name
UI_PP_CREATE|-|source|Create
UI_PP_POSTED|-|source|Posted.
UI_PP_FEED_INFO|-|source|Me {0} · {1} records held · {2} candidates · diversity {3}/65536
UI_PP_SOMEONE|-|source|Someone
UI_PP_REPLY_TAG|-|source|reply
UI_PP_A_GROUP|-|source|a group
UI_PP_SCORE|-|source|score {0}
UI_PP_REPLY|-|source|Reply
UI_PP_REPLY_N|-|source|Reply ({0})
UI_PP_FEED_EMPTY|-|source|Nothing here yet.
UI_PP_MEMBERS|-|source|{0} members
UI_PP_INVITE_ONLY|-|source|invite only
UI_PP_JOIN|-|source|Join
UI_PP_LEAVE|-|source|Leave
UI_PP_NO_GROUPS|-|source|No groups yet.
UI_PP_PLACE_CALL|-|source|Place a call
UI_PP_CALL_PLACEHOLDER|-|source|Member number, or loopback
UI_PP_CALL|-|source|Call
UI_PP_CALL_NOTE|-|source|Signalling only: this build carries no sound or video yet. "loopback" rings a test line inside this computer.
UI_PP_LINES|-|source|Lines
UI_PP_MY_NUMBER|-|source|My member number
UI_PP_LOOPBACK_NUMBER|-|source|Loopback number
UI_PP_MEDIA|-|source|Media
UI_PP_OUTBOX|-|source|Waiting to send
UI_PP_LOOPBACK_LINE|-|source|loopback line
UI_PP_LINE|-|source|line {0}
UI_PP_OUTGOING|-|source|outgoing
UI_PP_INCOMING|-|source|incoming
UI_PP_CALL_ID|-|source|call {0}
UI_PP_MISSED|-|source|{0} missed
UI_PP_MEMBER_STATE|-|source|Member {0} · {1}
UI_PP_ANSWER|-|source|Answer
UI_PP_HANG_UP|-|source|Hang up
UI_PP_NO_CALLS|-|source|No calls.
UI_PP_XLATE_PLACEHOLDER|-|source|Text to translate
UI_PP_INTO|-|source|Into
UI_PP_RESULT|-|source|Result
UI_PP_NOTHING_YET|-|source|Nothing yet.
UI_PP_LANG_UNKNOWN|-|source|Language not recognised.
UI_PP_LOOKS_LIKE|-|source|Looks like {0}
UI_PP_LOOKS_LIKE_PCT|-|source|Looks like {0} ({1}%)
UI_PP_MACHINE_NOTE|-|source|Machine translation: it may be wrong.
UI_PP_NOT_TRANSLATED|-|source|Already in that language: not translated.
UI_PP_THIS_DEVICE_TITLE|-|source|This device
UI_PP_ADD_DEVICE|-|source|Add a device
UI_PP_INVITE|-|source|Invite a device
UI_PP_JOIN_PLACEHOLDER|-|source|Code from your other device (XXXXX-XXXXX)
UI_PP_PAIR_NOTE|-|source|Pairing between two machines also needs a carrier (the phone link); this build has none wired to the window.
UI_PP_NAME|-|source|Name
UI_PP_ROLE|-|source|Role
UI_PP_IN_MESH|-|source|In a mesh
UI_PP_YES|-|source|yes
UI_PP_NO|-|source|no
UI_PP_DEVICE_ID|-|source|Device id
UI_PP_LEVEL|-|source|Security level
UI_PP_ROSTER|-|source|Roster version
UI_PP_FRAMES|-|source|Frames waiting
UI_PP_SAVE_ERROR|-|source|Save error
UI_PP_SAS_CHECK|-|source|Check that your other device shows the same number:
UI_PP_MATCH|-|source|They match
UI_PP_DIFFER|-|source|They differ
UI_PP_DEVICE_ADDED|-|source|Device added.
UI_PP_PAIRING_STOPPED|-|source|Pairing stopped.
UI_PP_ADMIN|-|source|admin
UI_PP_THIS_DEVICE|-|source|this device
UI_PP_REMOVE|-|source|Remove
UI_PP_REMOVE_Q|-|source|Remove {0} from your devices?
UI_PP_REMOVE_TEXT|-|source|It loses access at once.
UI_PP_ONLY_THIS|-|source|Only this device.
UI_PP_INVITE_CODE|-|source|On the other device, enter this code (valid {0} minutes):
UI_PP_QR_PAYLOAD|-|source|QR payload
UI_PP_JOINING|-|source|Joining…
UI_SCREEN_LEFT|-|source|Minutes left today for this app: {0}
UI_SCREEN_BLOCKED|-|source|Shepherd: {0}
