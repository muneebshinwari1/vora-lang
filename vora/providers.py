"""Demo and loopback Ollama providers; no SDK or credentials required.

Ollama request/response schema: https://docs.ollama.com/api/chat
The loopback client does not guarantee that the configured Ollama server itself
uses local inference. Choose a downloaded local model for local-only operation.
"""
from __future__ import annotations

import hashlib
import http.client
import json
import math
import socket
from urllib.error import HTTPError, URLError
from urllib.parse import urlsplit
from urllib.request import HTTPRedirectHandler, ProxyHandler, Request, build_opener

from .runtime import ExecutionError, TransientProviderError


class DemoProvider:
    """Deterministic simulation for testing plumbing, not AI reasoning."""

    def __call__(self, *, role: str, prompt: str) -> str:
        digest = hashlib.sha256((role + "\0" + prompt).encode("utf-8")).hexdigest()[:12]
        excerpt = " ".join(prompt.split())[:240]
        return f"[SIMULATED DEMO; no AI inference; {digest}] {role}: {excerpt}"


class _NoRedirects(HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


class OllamaProvider:
    """One non-streaming call per invocation; runtime owns all retries.

    timeout bounds socket operations, not the entire workflow. Response bodies
    are capped at 4 MiB. Only HTTP on localhost/127.0.0.1 is accepted, with no
    redirects or environment proxies. This provider never executes tool calls.
    """

    def __init__(self, model: str = "llama3.2", *,
                 endpoint: str = "http://127.0.0.1:11434/api/chat",
                 timeout: float = 120.0, max_output_tokens: int = 512):
        if not isinstance(model, str) or not model.strip():
            raise ValueError("Ollama model must be a non-empty string.")
        if isinstance(timeout, bool) or not isinstance(timeout, (int, float)) or not math.isfinite(timeout) or timeout <= 0:
            raise ValueError("Ollama timeout must be a finite positive number.")
        if type(max_output_tokens) is not int or max_output_tokens < 1:
            raise ValueError("max_output_tokens must be a positive integer.")
        try:
            parts = urlsplit(endpoint)
            port = 11434 if parts.port is None else parts.port
        except (TypeError, ValueError):
            raise ValueError("Invalid Ollama endpoint.") from None
        if (parts.scheme != "http" or parts.hostname not in {"localhost", "127.0.0.1"}
                or parts.username is not None or parts.password is not None
                or parts.query or parts.fragment or parts.path not in {"", "/", "/api/chat"}
                or not 1 <= port <= 65535):
            raise ValueError("Ollama endpoint must use loopback HTTP and /api/chat.")
        self.model = model
        self.endpoint = f"http://127.0.0.1:{port}/api/chat"
        self.timeout = float(timeout)
        self.max_output_tokens = max_output_tokens

    def __call__(self, *, role: str, prompt: str) -> str:
        body = json.dumps({"model": self.model, "stream": False,
                           "options": {"num_predict": self.max_output_tokens},
                           "messages": [{"role": "system", "content": role},
                                        {"role": "user", "content": prompt}]}).encode("utf-8")
        request = Request(self.endpoint, data=body, method="POST",
                          headers={"Content-Type": "application/json"})
        # A fresh opener avoids sharing mutable handler state between threads.
        opener = build_opener(ProxyHandler({}), _NoRedirects())
        try:
            with opener.open(request, timeout=self.timeout) as response:
                raw = response.read(4 * 1024 * 1024 + 1)
        except HTTPError as exc:
            code = exc.code
            exc.close()
            if code == 429 or 500 <= code <= 599:
                raise TransientProviderError(f"Ollama returned HTTP {code}.") from None
            raise ExecutionError(f"Ollama returned HTTP {code}.") from None
        except (URLError, socket.timeout, TimeoutError, ConnectionError, http.client.HTTPException):
            raise TransientProviderError("Ollama connection failed or timed out.") from None
        if len(raw) > 4 * 1024 * 1024:
            raise ExecutionError("Ollama response exceeded 4 MiB.")
        try:
            payload = json.loads(raw)
        except (ValueError, UnicodeDecodeError):
            raise ExecutionError("Ollama returned invalid JSON.") from None
        if not isinstance(payload, dict) or not isinstance(payload.get("message"), dict):
            raise ExecutionError("Ollama response is missing a message object.")
        message = payload["message"]
        if message.get("tool_calls"):
            raise ExecutionError("Ollama requested tools; tool execution is not supported.")
        content = message.get("content")
        if not isinstance(content, str) or not content.strip():
            raise ExecutionError("Ollama response is missing non-empty message.content.")
        if payload.get("done") is not True:
            raise ExecutionError("Ollama returned an incomplete response.")
        return content
