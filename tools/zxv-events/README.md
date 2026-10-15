# zxv-events

Reads a ZXV serial log and checks the chiglet governor's event log
(`kernel/src/chiglet/chg_abi.h` A8): every `[EVT]` record is decoded and
shown in words, every chain head is recomputed from the record bytes, and the
head signed by the console command `events` is verified with ML-DSA-65.

    cc -std=c11 -O2 -I../../kernel/include -I../../kernel/src/chiglet \
       -I../../kernel/src/lpres -I../../kernel/src/pqsec -I../../kernel/src/robin_debanks \
       -I../../kernel/src/modbind -I../../kernel/src/event_space -I../../kernel/src/surplus \
       -I../../kernel/src/edp_risk zxv_events.c ../../kernel/src/chiglet/chg_abi.c \
       ../../kernel/src/lpres/lpres.c ../../kernel/src/robin_debanks/sha256.c \
       ../../kernel/src/pqsec/pq_mldsa65.c ../../kernel/src/pqsec/mldsa/*.c -o zxv_events
    ./zxv_events serial.log

Exit 0 means every chain is whole and every signature verifies; 1 names the
first line that fails; 2 means the log could not be read or holds no events.

The signing key is made per boot, so a signature shows the log is whole and
in order as that key saw it, not which device wrote it.

`test_zxv_events.sh` (run by `make -C kernel test-zxv-events`) makes a
two-boot log from a real `chg_abi` session and checks that each kind of
tampering fails.
