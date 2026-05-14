"""GitHub-releases auto-updater for Qnob Companion.

Checks the latest release tag against the running version at startup and
signals the UI to show a banner when a newer version is available.
Download is intentionally browser-based (no in-process patching) to stay
simple and to avoid admin-rights complications with MSIX side-loading.
"""

from __future__ import annotations

import asyncio
import json
import logging
import urllib.request
from dataclasses import dataclass

from qnob_companion import __version__

log = logging.getLogger(__name__)

_RELEASES_URL = (
    "https://api.github.com/repos/alperbasarn/homio-round-hmi/releases/latest"
)
_USER_AGENT = "qnob-companion-updater"
_TIMEOUT_S = 10


@dataclass(frozen=True)
class UpdateInfo:
    """Describes an available update."""

    version: str        # "0.2.0"
    release_url: str    # GitHub HTML page for the release
    changelog: str      # release body (markdown)


class Updater:
    """One-shot update checker; safe to call from the asyncio main loop."""

    async def check(self) -> UpdateInfo | None:
        """Return an :class:`UpdateInfo` if a newer release exists, else ``None``.

        Never raises — any network or parse error returns ``None`` silently so
        startup is not affected by a connectivity issue.
        """
        try:
            release = await asyncio.to_thread(self._fetch_latest_release)
            tag = release.get("tag_name", "").lstrip("v")
            if not tag:
                return None
            if _is_newer(tag, __version__):
                log.info(
                    "Update available: %s (running %s)", tag, __version__
                )
                return UpdateInfo(
                    version=tag,
                    release_url=release.get("html_url", _RELEASES_URL),
                    changelog=release.get("body", ""),
                )
            log.debug("Up to date (%s)", __version__)
        except Exception as exc:
            log.debug("Update check failed (ignored): %s", exc)
        return None

    # ----- internals -----

    @staticmethod
    def _fetch_latest_release() -> dict:
        req = urllib.request.Request(
            _RELEASES_URL,
            headers={
                "User-Agent": _USER_AGENT,
                "Accept": "application/vnd.github+json",
                "X-GitHub-Api-Version": "2022-11-28",
            },
        )
        with urllib.request.urlopen(req, timeout=_TIMEOUT_S) as resp:
            return json.loads(resp.read().decode("utf-8"))  # type: ignore[return-value]


# ---------------------------------------------------------------------------
# Pure helpers (exposed for unit tests)
# ---------------------------------------------------------------------------

def _parse_version(v: str) -> tuple[int, ...]:
    """Parse a semver string into a comparable tuple. Ignores pre-release suffixes."""
    numeric = v.split("-")[0]       # strip pre-release label
    parts = numeric.lstrip("v").split(".")[:3]
    return tuple(int(p) for p in parts if p.isdigit())


def _is_newer(remote: str, local: str) -> bool:
    """Return True if *remote* version is strictly greater than *local*."""
    try:
        return _parse_version(remote) > _parse_version(local)
    except (ValueError, TypeError):
        return False
