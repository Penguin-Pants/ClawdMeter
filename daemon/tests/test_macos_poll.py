#!/usr/bin/env python3
"""Unit tests for the macOS daemon's poll_api HTTP-status handling.

When the 5h window is exhausted the API answers 429 but still sends the
anthropic-ratelimit-* headers; the daemon must forward them so the firmware
shows the "Limit reached" screen instead of "No data".

Run: python -m pytest daemon/tests/test_macos_poll.py -x -q
"""
import asyncio
import time
from unittest.mock import AsyncMock, MagicMock, patch

from daemon.claude_usage_daemon import poll_api


def _make_mock_response(status_code=200, headers=None):
    """Build a mock httpx.Response-like object with case-insensitive headers."""
    resp = MagicMock()
    resp.status_code = status_code
    resp.text = "mocked"
    header_data = {k.lower(): v for k, v in (headers or {}).items()}
    resp.headers = MagicMock()
    resp.headers.get = lambda name, default=None: header_data.get(name.lower(), default)
    return resp


def _poll_with(resp):
    async def fake_post(*args, **kwargs):
        return resp

    client = AsyncMock()
    client.__aenter__ = AsyncMock(return_value=client)
    client.__aexit__ = AsyncMock(return_value=False)
    client.post = fake_post
    with patch("httpx.AsyncClient", return_value=client):
        return asyncio.run(poll_api("fake-token"))


def test_429_with_ratelimit_headers_returns_limit_payload():
    now = time.time()
    resp = _make_mock_response(
        status_code=429,
        headers={
            "anthropic-ratelimit-unified-5h-utilization": "1.0",
            "anthropic-ratelimit-unified-5h-reset": str(now + 252 * 60),
            "anthropic-ratelimit-unified-7d-utilization": "0.61",
            "anthropic-ratelimit-unified-7d-reset": str(now + 5200 * 60),
            "anthropic-ratelimit-unified-5h-status": "rejected",
        },
    )
    payload = _poll_with(resp)
    assert payload is not None
    assert payload["s"] == 100
    assert abs(payload["sr"] - 252) <= 1
    assert payload["w"] == 61
    assert payload["st"] == "rejected"
    assert payload["ok"] is True


def test_429_without_ratelimit_headers_returns_none():
    assert _poll_with(_make_mock_response(status_code=429, headers={})) is None


def test_5xx_returns_none():
    assert _poll_with(_make_mock_response(status_code=503, headers={})) is None


def test_200_still_works():
    now = time.time()
    resp = _make_mock_response(
        status_code=200,
        headers={
            "anthropic-ratelimit-unified-5h-utilization": "0.42",
            "anthropic-ratelimit-unified-5h-reset": str(now + 3600),
            "anthropic-ratelimit-unified-5h-status": "allowed",
        },
    )
    payload = _poll_with(resp)
    assert payload is not None
    assert payload["s"] == 42
    assert payload["st"] == "allowed"
