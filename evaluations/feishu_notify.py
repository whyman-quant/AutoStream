"""Small, fail-soft Feishu custom-bot notifier for research rounds.

The webhook is supplied at runtime (``AUTOSTREAM_FEISHU_WEBHOOK``); secrets are
never committed.  Notification failure is reported to the caller and should
not change factor results or promotion state.
"""

from __future__ import annotations

import json
import base64
import hashlib
import hmac
import os
import time
from typing import Mapping, Optional, Sequence
from urllib.request import Request, urlopen


EVIDENCE_STATES = ("supported", "promising", "unsupported", "not_evaluable", "error")


def _round_date_range(research_round: Mapping[str, object]) -> str:
    date_lists = research_round.get("date_lists", {})
    ranges = []
    for split in ("training", "observation"):
        value = date_lists.get(split, {}) if isinstance(date_lists, Mapping) else {}
        start = value.get("date_start") if isinstance(value, Mapping) else None
        end = value.get("date_end") if isinstance(value, Mapping) else None
        if start and end:
            ranges.append("{} {}–{}".format(split, start, end))
    return "; ".join(ranges) or "L2/L3/L4 training and observation only"


def build_round_start(round_id: str, scope: str, tests: Sequence[str], note: str = "",
                      research_round: Optional[Mapping[str, object]] = None) -> dict:
    lines = [
        "AutoStream 研究轮次开始",
        "轮次: {}".format(round_id),
        "范围: {}".format(scope),
    ]
    if research_round:
        lines.extend((
            "数据范围: {}".format(_round_date_range(research_round)),
            "holdout 状态: 2025 holdout: sealed（未读取；仅冻结后 display）",
            "预注册家族:",
        ))
        families = research_round.get("families", {})
        for name in sorted(families):
            family = families[name]
            if not isinstance(family, Mapping):
                continue
            parents = ", ".join(str(value) for value in family.get("parent_candidate_ids", ()))
            lines.extend((
                "- {} 问题: {}".format(name, family.get("research_question", "未提供")),
                "  变量/对照: {}; 父因子: {}".format(
                    family.get("single_variable_control", "未提供"), parents or "未提供"),
                "  成功: {}; 失败: {}; 停止: {}".format(
                    family.get("success_condition", "未提供"),
                    family.get("rejection_condition", "未提供"),
                    family.get("stop_condition", "未提供"),
                ),
            ))
    lines.append("本轮阶段:")
    lines.extend("- {}".format(item) for item in tests)
    if note:
        lines.append("说明: {}".format(note))
    return {"msg_type": "text", "content": {"text": "\n".join(lines)}}


def build_round_end(
    round_id: str,
    report: Mapping[str, object],
    effective: Sequence[Mapping[str, object]] = (),
    plots: Sequence[str] = (),
    note: str = "",
) -> dict:
    # ``effective`` remains accepted only for callers from the pre-evidence API;
    # it is deliberately not rendered or used to infer a research conclusion.
    status_counts = report.get("status_counts", report) if isinstance(report, Mapping) else {}
    counts = ", ".join("{}={}".format(key, int(status_counts.get(key, 0)))
                       for key in EVIDENCE_STATES)
    supported = report.get("supported_cells", ()) if isinstance(report, Mapping) else ()
    next_actions = report.get("next_actions", ()) if isinstance(report, Mapping) else ()
    lines = [
        "AutoStream 研究轮次结束",
        "轮次: {}".format(round_id),
        "单元证据状态: {}".format(counts),
        "supported 范围（因子 × 事件 × 股票池 × 标签；父因子增量）:",
    ]
    if supported:
        lines.extend(
            "- {} × {} × {} × {}{}".format(
                item.get("factor"), item.get("event"), item.get("universe"), item.get("label"),
                "; parent_delta={}".format(item.get("parent_delta"))
                if item.get("parent_delta") is not None else "",
            ) for item in supported
        )
    else:
        lines.append("- 本轮没有 supported 单元")
    lines.append("下一轮动作:")
    lines.extend("- {}".format(action) for action in next_actions)
    if not next_actions:
        lines.append("- 依据各单元证据决定保留、收缩、对照或停止；不作自动晋级")
    lines.append("效果图:")
    lines.extend("- {}".format(path) for path in plots)
    if not plots:
        lines.append("- 本轮未生成效果图")
    lines.append("promotion_allowed=false")
    if note:
        lines.append("说明: {}".format(note))
    return {"msg_type": "text", "content": {"text": "\n".join(lines)}}


class FeishuNotifier:
    def __init__(self, webhook: Optional[str] = None, secret: Optional[str] = None, opener=None, timeout: float = 10.0, clock=None):
        self.webhook = webhook or os.environ.get("AUTOSTREAM_FEISHU_WEBHOOK", "")
        # Custom bots without the signature security switch accept plain JSON.
        # Only sign when the caller explicitly opts in; a copied verification
        # token must never be guessed to be a webhook signing secret.
        self.secret = secret if secret is not None else (
            os.environ.get("AUTOSTREAM_FEISHU_SECRET", "")
            if os.environ.get("AUTOSTREAM_FEISHU_SIGN", "0") == "1" else ""
        )
        self.opener = opener or type("_Opener", (), {"open": staticmethod(urlopen)})()
        self.timeout = timeout
        self.clock = clock or time.time

    @property
    def enabled(self) -> bool:
        return bool(self.webhook)

    def send(self, payload: Mapping[str, object]) -> dict:
        if not self.webhook:
            return {"skipped": True, "reason": "AUTOSTREAM_FEISHU_WEBHOOK is not set"}
        body = dict(payload)
        if self.secret:
            timestamp = str(int(self.clock()))
            sign_raw = (timestamp + "\n" + self.secret).encode("utf-8")
            body["timestamp"] = timestamp
            body["sign"] = base64.b64encode(
                hmac.new(self.secret.encode("utf-8"), sign_raw, hashlib.sha256).digest()
            ).decode("ascii")
        data = json.dumps(body, ensure_ascii=False).encode("utf-8")
        request = Request(
            self.webhook,
            data=data,
            headers={"Content-Type": "application/json; charset=utf-8"},
            method="POST",
        )
        response = self.opener.open(request, timeout=self.timeout)
        raw = response.read().decode("utf-8")
        try:
            result = json.loads(raw)
        except json.JSONDecodeError:
            result = {"raw": raw}
        if isinstance(result, dict) and result.get("code") not in (None, 0):
            raise RuntimeError("Feishu webhook rejected message: {}".format(result))
        return result


def send_round_start(round_id: str, scope: str, tests: Sequence[str], note: str = "",
                     research_round: Optional[Mapping[str, object]] = None) -> dict:
    return FeishuNotifier().send(build_round_start(round_id, scope, tests, note, research_round))


def send_round_end(round_id: str, report: Mapping[str, object], effective: Sequence[Mapping[str, object]] = (),
                   plots: Sequence[str] = (), note: str = "") -> dict:
    return FeishuNotifier().send(build_round_end(round_id, report, effective, plots, note))


__all__ = ["FeishuNotifier", "build_round_start", "build_round_end", "send_round_start", "send_round_end"]
