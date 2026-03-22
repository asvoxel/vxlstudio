"""VxlStudio VLM utilities -- Python-side HTTP transport for VLMAssistant."""

from __future__ import annotations

import json
from typing import Optional


def setup_vlm(
    provider: str = "openai",
    api_key: str = "",
    model: str = "gpt-4o",
    base_url: str = "",
    timeout_seconds: int = 30,
    max_tokens: int = 1024,
) -> None:
    """Configure the global VLM assistant with Python ``requests`` as HTTP transport.

    Parameters
    ----------
    provider : str
        One of ``"openai"``, ``"anthropic"``, ``"ollama"``, ``"custom"``.
    api_key : str
        API key (not required for ``ollama``).
    model : str
        Model identifier (e.g. ``"gpt-4o"``, ``"claude-sonnet-4-20250514"``).
    base_url : str
        Override API endpoint. For ollama: ``"http://localhost:11434"``.
    timeout_seconds : int
        HTTP request timeout.
    max_tokens : int
        Maximum tokens in response.
    """
    import _vxl_core as _core  # noqa: WPS433

    config = _core.VLMConfig()
    config.provider = provider
    config.api_key = api_key
    config.model = model
    config.base_url = base_url
    config.timeout_seconds = timeout_seconds
    config.max_tokens = max_tokens

    assistant = _core.vlm_assistant()
    assistant.configure(config)

    def _http_caller(url: str, headers_json: str, body: str) -> str:
        try:
            import requests  # noqa: WPS433
        except ImportError as exc:
            raise RuntimeError(
                "The 'requests' package is required for VLM HTTP transport. "
                "Install it with: pip install requests"
            ) from exc

        headers = json.loads(headers_json)
        resp = requests.post(
            url, headers=headers, data=body, timeout=timeout_seconds
        )
        return resp.text

    assistant.set_http_caller(_http_caller)


def vlm_query(
    prompt: str,
    image: Optional[object] = None,
    provider: Optional[str] = None,
    api_key: Optional[str] = None,
    model: Optional[str] = None,
) -> str:
    """Quick one-shot VLM query.

    If the global assistant is not yet configured, this will configure it
    using the provided parameters (or defaults).

    Parameters
    ----------
    prompt : str
        The question or instruction.
    image : Image, optional
        An optional vxl.Image to include.
    provider, api_key, model : str, optional
        Override config for this call (configures globally on first use).

    Returns
    -------
    str
        The model's text response.
    """
    import _vxl_core as _core  # noqa: WPS433

    assistant = _core.vlm_assistant()
    if not assistant.is_configured():
        setup_vlm(
            provider=provider or "openai",
            api_key=api_key or "",
            model=model or "gpt-4o",
        )

    response = assistant.query(prompt, image)
    return response.text
