#!/bin/sh
# mk_vps_disc.sh <core-disc.img> <swarm-disc.img> <out.img>
#
# Graft the swarm disc's single partition onto the end of the core disc so a
# VPS boots one drive holding both: the kernel scans every disk's GPT for
# partitions of the ZXV swarm type (HW_GPT_ZXV_SWARM) named ZXV_SWARM_*.
# The partition's type GUID, name and byte content are copied verbatim from
# the swarm disc, so whatever the golden-disc build sealed is what mounts.
#
# Tools: sgdisk, dd. Exit 0 = out.img written and its GPT verifies.

set -eu

CORE=$1 SWARM=$2 OUT=$3

for t in sgdisk dd; do
    command -v "$t" >/dev/null 2>&1 || { echo "mk_vps_disc: needs $t" >&2; exit 1; }
done
[ -f "$CORE" ] && [ -f "$SWARM" ] || { echo "mk_vps_disc: missing input image" >&2; exit 1; }

# The swarm disc must be exactly one partition; its type GUID and name move
# over unchanged.
nparts=$(sgdisk -p "$SWARM" | grep -cE '^[[:space:]]*[0-9]+[[:space:]]')
[ "$nparts" = 1 ] || { echo "mk_vps_disc: $SWARM has $nparts partitions, expected 1" >&2; exit 1; }
sline=$(sgdisk -p "$SWARM" | grep -E '^[[:space:]]*1[[:space:]]' | head -1)
sstart=$(echo "$sline" | awk '{print $2}')
send=$(echo "$sline" | awk '{print $3}')
sguid=$(sgdisk -i 1 "$SWARM" | awk '/Partition GUID code/{print $4}')
sname=$(sgdisk -i 1 "$SWARM" | awk -F"'" '/Partition name/{print $2}')
ssz=$((send - sstart + 1))
[ -n "$sguid" ] && [ -n "$sname" ] || { echo "mk_vps_disc: cannot read swarm partition" >&2; exit 1; }

cp "$CORE" "$OUT"
cend=$(sgdisk -p "$OUT" | awk '/^Disk .* sectors/{print $3}')
# 2048-aligned start past the old end; room for the secondary GPT (33
# sectors) after the partition.
newstart=$(( (cend + 2048 + 2047) / 2048 * 2048 ))
newend=$((newstart + ssz - 1))
total=$((newend + 33 + 1))
truncate -s $((total * 512)) "$OUT"
# sgdisk refuses partitions past the header's last-usable LBA, so the
# backup structures must move to the new end before the partition is added;
# the write then leaves the backup array offset behind again, so relocate
# once more at the end.
sgdisk --move-second-header "$OUT" >/dev/null
sgdisk --new=0:"$newstart":"$newend" --typecode=0:"$sguid" --change-name=0:"$sname" "$OUT" >/dev/null

# The FAT filesystem bytes, verbatim, sector for sector.
dd if="$SWARM" of="$OUT" bs=1M iflag=skip_bytes oflag=seek_bytes \
   skip=$((sstart * 512)) seek=$((newstart * 512)) conv=notrunc status=none

# sgdisk -v reports problems on stdout but still exits 0; the text is the
# check.
sgdisk --move-second-header "$OUT" >/dev/null
sgdisk -v "$OUT" 2>&1 | grep -q "No problems found" || {
    echo "mk_vps_disc: $OUT GPT verification failed" >&2
    sgdisk -v "$OUT" >&2
    exit 1
}
echo "mk_vps_disc: $OUT  core+swarm partition '$sname' sectors $newstart..$newend"
