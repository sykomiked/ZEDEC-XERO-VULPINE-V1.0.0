#!/usr/bin/env python3
"""transactional_disk.py — Transactional disk installation and rollback

Installs verified ZXV payloads to a target mount/directory with a rollback
journal.  All writes are staged and validated before the journal is committed;
any failure restores the original files from the backup copies recorded in the
journal.

Author: H.M. Michael-Laurence: Curzi (c)
License: Apache-2.0
"""

import hashlib
import json
import os
import shutil
import tempfile
from datetime import datetime, timezone
from pathlib import Path


ROLLBACK_DIR = ".zxv-rollback"
JOURNAL_NAME = "rollback_journal.json"


def _sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest().lower()


def _verify_digest(path: Path, expected: str) -> bool:
    if not isinstance(expected, str) or not expected.startswith("sha256:"):
        return False
    return _sha256_file(path) == expected.split(":", 1)[1].lower()


def _target_path_for_payload(payload: dict, target_root: Path) -> Path:
    """Map a payload to its on-disk target path."""
    src_name = Path(payload["path"]).name
    payload_type = payload.get("type", "")
    firmware = payload.get("firmware", "")

    if payload_type == "stage0_bootstrap" and firmware == "uefi":
        return target_root / "EFI" / "BOOT" / src_name
    if payload_type == "stage0_bootstrap" and firmware == "devicetree":
        return target_root / "boot" / src_name
    if payload.get("id", "").startswith("kernel.cell."):
        # Default kernel image location for UEFI-less direct boot images
        return target_root / "cells" / src_name

    # Fallback: keep same relative tree under target root
    return target_root / payload["path"]


class TransactionalDiskInstall:
    """File-level transactional installer for ZXV payloads.

    The installer works on any directory or mounted block device.  It creates a
    rollback journal in ``<target>/.zxv-rollback/`` and, on failure, uses that
    journal to restore the previous state.
    """

    def __init__(self, target: Path):
        self.target = Path(target).resolve()
        self.rollback_dir = self.target / ROLLBACK_DIR
        self.journal_path = self.rollback_dir / JOURNAL_NAME
        self.journal = {
            "created": datetime.now(timezone.utc).isoformat(),
            "target": str(self.target),
            "backups": [],
            "installed": [],
            "committed": False,
        }

    def _ensure_rollback_dir(self):
        self.rollback_dir.mkdir(parents=True, exist_ok=True)

    def _backup_original(self, dst: Path) -> dict:
        """If dst exists, copy it to the rollback dir and return a backup record."""
        if not dst.exists():
            return {"original": None, "backup": None}

        rel = dst.relative_to(self.target)
        backup_name = str(rel).replace(os.sep, "_")
        backup_path = self.rollback_dir / backup_name
        counter = 0
        while backup_path.exists():
            counter += 1
            backup_path = self.rollback_dir / f"{backup_name}.{counter}"

        shutil.copy2(dst, backup_path)
        return {"original": str(dst), "backup": str(backup_path)}

    def _install_payload(self, bundle_dir: Path, payload: dict) -> dict:
        src = bundle_dir / payload["path"]
        dst = _target_path_for_payload(payload, self.target)

        if not src.exists():
            raise FileNotFoundError(f"payload source missing: {src}")

        expected = payload.get("digest", "")
        if not _verify_digest(src, expected):
            raise ValueError(f"digest mismatch for {payload['id']}")

        backup = self._backup_original(dst)
        if backup["original"]:
            self.journal["backups"].append(backup)

        dst.parent.mkdir(parents=True, exist_ok=True)
        # Stage to a temp file then rename for near-atomicity.
        fd, tmp_path = tempfile.mkstemp(dir=str(dst.parent), prefix=".zxv-")
        try:
            os.close(fd)
            shutil.copy2(src, tmp_path)
            os.replace(tmp_path, str(dst))
        except Exception:
            try:
                os.unlink(tmp_path)
            except OSError:
                pass
            raise

        if not _verify_digest(dst, expected):
            raise ValueError(f"post-install digest mismatch for {payload['id']}")

        record = {
            "id": payload.get("id"),
            "source": str(src),
            "target": str(dst),
            "digest": expected,
        }
        self.journal["installed"].append(record)
        return record

    def install(self, bundle_dir: Path, payloads: list) -> list:
        """Install payloads transactionally.  Returns installed records or raises."""
        self._ensure_rollback_dir()

        installed = []
        try:
            for payload in payloads:
                if payload.get("status") != "available":
                    continue
                installed.append(self._install_payload(bundle_dir, payload))
        except Exception:
            self.rollback()
            raise

        self._write_journal()
        self._commit()
        return installed

    def _write_journal(self):
        with open(self.journal_path, "w") as f:
            json.dump(self.journal, f, indent=2)

    def _commit(self):
        """Commit the transaction: validate digests and mark the journal committed.

        Backup copies remain in place so rollback() can still restore originals
        until finalize() is explicitly called after the new install is verified
        stable.
        """
        for record in self.journal["installed"]:
            dst = Path(record["target"])
            if not _verify_digest(dst, record["digest"]):
                raise ValueError(f"commit validation failed for {record['id']}")

        self.journal["committed"] = True
        self._write_journal()

    def rollback(self):
        """Restore original files from the rollback journal."""
        if not self.rollback_dir.exists():
            return

        if self.journal_path.exists():
            with open(self.journal_path) as f:
                try:
                    self.journal = json.load(f)
                except json.JSONDecodeError:
                    pass

        # Remove newly installed files first
        for record in self.journal.get("installed", []):
            dst = Path(record["target"])
            if dst.exists():
                try:
                    dst.unlink()
                except OSError:
                    pass

        # Restore originals
        for backup in self.journal.get("backups", []):
            original = Path(backup["original"])
            bp = Path(backup["backup"])
            if bp.exists():
                original.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(bp, original)

    def finalize(self):
        """Finalize the transaction after a successful and stable install.

        Backup copies and the rollback journal are removed only here, once the
        previous system is no longer needed.  Raises RuntimeError if the journal
        is not committed.
        """
        if not self.journal_path.exists():
            return

        with open(self.journal_path) as f:
            try:
                journal = json.load(f)
            except json.JSONDecodeError:
                journal = {}

        if journal.get("committed") is not True:
            raise RuntimeError("cannot finalize an uncommitted transaction")

        for backup in journal.get("backups", []):
            bp = Path(backup["backup"])
            if bp and bp.exists():
                bp.unlink()

        self.journal_path.unlink()
        if self.rollback_dir.exists() and not any(self.rollback_dir.iterdir()):
            self.rollback_dir.rmdir()
