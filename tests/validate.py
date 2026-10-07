"""Independent trace reconstruction, not a second implementation of the scheduler."""
import json
import math
from collections import deque
from pathlib import Path


def load(path):
    return [json.loads(line) for line in Path(path).read_text().splitlines()]


class TraceValidator:
    """Replay events, check occupancy snapshots, then reconcile statistics.

    This tracks observed history only: it never computes the next scheduled event.
    """

    def __init__(self, config):
        self.config = config
        self.count = self.config["paintings"]
        self.entrance = deque()
        self.queues = [deque() for _ in range(self.count)]
        self.viewing = [set() for _ in range(self.count)]
        self.actors, self.inside = {}, set()
        self.areas = [0] * self.count
        self.peaks = [0] * self.count
        self.started = [0] * self.count
        self.finished = [0] * self.count
        self.entrance_waits, self.painting_waits = [], []
        self.gallery_area = 0
        self.peak_gallery = 0
        self.pending_entrance = self.pending_painting = 0
        self.now = self.entered = self.completed = self.cancelled = 0
        self.closed = False
        self.painting_stats = {}
        self.summary = None
        self.kinds = set()

    def accept(self, seq, event):
        assert event["seq"] == seq, ("sequence gap", event)
        assert self.summary is None, "event after summary"
        time = event["time_ms"]
        assert time >= self.now, ("clock moved backwards", event)
        dt = time - self.now
        self.gallery_area += len(self.inside) * dt
        self.areas = [area + len(group) * dt for area, group in zip(self.areas, self.viewing)]
        self.now = time
        kind = event["event"]
        self.kinds.add(kind)
        if kind == "config":
            assert seq == 0, "repeated configuration"
            return
        if kind == "painting_stats":
            self.painting_stats[event["painting"] - 1] = event
            return
        if kind == "summary":
            self.summary = event
            return
        self.transition(event)
        self.check_snapshot(event)

    def transition(self, event):
        kind = event["event"]
        visitor = event["visitor"]
        p = event["painting"] - 1
        actor = self.actors.get(visitor)
        if kind == "arrival":
            assert not self.closed and visitor == len(self.actors) + 1
            actor = {"state": "entrance", "arrival": self.now, "seen": set(), "order": []}
            self.actors[visitor] = actor
            self.entrance.append(visitor)
        elif kind == "entrance_wait":
            assert actor["state"] == "entrance" and visitor in self.entrance
        elif kind == "permission":
            assert self.entrance.popleft() == visitor, "entrance FIFO violated"
            assert actor["state"] == "entrance"
            assert len(self.inside) < self.config["capacity"]
            actor["state"] = "permitted"
        elif kind == "enter":
            assert actor["state"] == "permitted"
            self.inside.add(visitor)
            actor["state"] = "ready"
            self.entered += 1
            self.entrance_waits.append(self.now - actor["arrival"])
            self.peak_gallery = max(self.peak_gallery, len(self.inside))
        elif kind == "choose":
            self.choose(actor, visitor, p)
        elif kind == "painting_wait":
            assert actor["state"] == "queued" and visitor in self.queues[p]
        elif kind == "view_start":
            self.start_view(actor, visitor, p, event)
        elif kind == "view_end":
            assert actor["state"] == "viewing" and actor["painting"] == p
            assert self.now == actor["finish"], "incorrect viewing duration"
            assert p not in actor["seen"]
            actor["seen"].add(p)
            actor["state"] = "ready"
            self.viewing[p].remove(visitor)
            self.finished[p] += 1
        elif kind == "exit":
            assert actor["state"] == "ready" and len(actor["seen"]) == self.count
            assert not any(visitor in group for group in self.viewing)
            self.inside.remove(visitor)
            actor["state"] = "done"
            self.completed += 1
        elif kind == "cancel":
            self.cancel(actor, visitor)
        elif kind == "close":
            assert not self.closed
            self.closed = True
        elif kind == "limit":
            pass
        else:
            raise AssertionError(f"unknown event {kind}")

    def choose(self, actor, visitor, p):
        assert actor["state"] == "ready" and visitor in self.inside
        assert 0 <= p < self.count and p not in actor["seen"]
        if self.config["strategy"] == "ordered":
            assert p == min(set(range(self.count)) - actor["seen"])
        elif self.config["strategy"] == "least-crowded":
            available = set(range(self.count)) - actor["seen"]
            expected = min(available, key=lambda i: (len(self.viewing[i]) + len(self.queues[i]), i))
            assert p == expected
        actor.update(state="queued", painting=p, wait_since=self.now)
        actor["order"].append(p)
        # C emits choice BEFORE joining the queue; check that snapshot first.

    def start_view(self, actor, visitor, p, event):
        assert actor["state"] == "queued" and actor["painting"] == p
        assert self.queues[p].popleft() == visitor, "painting FIFO violated"
        assert len(self.viewing[p]) < self.config["painting_capacity"]
        assert not any(visitor in group for group in self.viewing)
        duration = event["duration_ms"]
        assert self.config["view_min"] <= duration <= self.config["view_max"]
        actor.update(state="viewing", finish=self.now + duration)
        self.painting_waits.append(self.now - actor["wait_since"])
        self.viewing[p].add(visitor)
        self.started[p] += 1
        self.peaks[p] = max(self.peaks[p], len(self.viewing[p]))

    def cancel(self, actor, visitor):
        state = actor["state"]
        assert state not in ("done", "cancelled")
        if state == "entrance":
            self.pending_entrance += self.now - actor["arrival"]
            self.entrance.remove(visitor)
        elif state == "queued":
            self.pending_painting += self.now - actor["wait_since"]
            self.queues[actor["painting"]].remove(visitor)
        elif state == "viewing":
            self.viewing[actor["painting"]].remove(visitor)
        self.inside.discard(visitor)
        actor["state"] = "cancelled"
        self.cancelled += 1

    def check_snapshot(self, event):
        visitor = event["visitor"]
        p = event["painting"] - 1
        actor = self.actors.get(visitor)
        kind = event["event"]
        assert len(self.inside) <= self.config["capacity"]
        assert all(len(group) <= self.config["painting_capacity"] for group in self.viewing)
        assert event["gallery"] == len(self.inside), ("gallery snapshot", event)
        assert event["entrance_queue"] == len(self.entrance), ("entrance snapshot", event)
        if p >= 0:
            assert event["viewers"] == len(self.viewing[p]), ("viewers snapshot", event)
            assert event["painting_queue"] == len(self.queues[p]), ("queue snapshot", event)
        if actor:
            assert event["viewed"] == len(actor["seen"])
        if kind == "choose":
            self.queues[p].append(visitor)

    def check_result(self):
        assert self.summary, "missing final summary"
        assert not self.inside and not self.entrance and not any(self.queues) and not any(self.viewing)
        assert self.summary["arrived"] == len(self.actors)
        assert self.summary["entered"] == self.entered
        assert self.summary["completed"] == self.completed
        assert self.summary["cancelled"] == self.cancelled
        assert self.summary["arrived"] == self.completed + self.cancelled
        assert self.summary["views_started"] == sum(self.started)
        assert self.summary["views_finished"] == sum(self.finished)
        assert self.summary["pending_entrance_wait_ms"] == self.pending_entrance
        assert self.summary["pending_painting_wait_ms"] == self.pending_painting
        assert self.summary["peak_gallery"] == self.peak_gallery
        for name, waits in [("entrance", self.entrance_waits), ("painting", self.painting_waits)]:
            mean = sum(waits) / len(waits) if waits else 0
            assert math.isclose(self.summary[f"mean_{name}_wait_ms"], mean, abs_tol=1e-6)
            assert self.summary[f"max_{name}_wait_ms"] == max(waits, default=0)
        gallery_util = self.gallery_area / (self.now * self.config["capacity"]) if self.now else 0
        painting_util = sum(self.areas) / (self.now * self.count * self.config["painting_capacity"]) if self.now else 0
        assert math.isclose(self.summary["gallery_utilization"], gallery_util, abs_tol=1e-9)
        assert math.isclose(self.summary["painting_utilization"], painting_util, abs_tol=1e-9)
        assert len(self.painting_stats) == self.count
        for p, stats in self.painting_stats.items():
            assert stats["started"] == self.started[p] and stats["finished"] == self.finished[p]
            assert stats["peak"] == self.peaks[p]
            util = self.areas[p] / (self.now * self.config["painting_capacity"]) if self.now else 0
            assert math.isclose(stats["utilization"], util, abs_tol=1e-9)
        if self.summary["status"] == "completed":
            assert self.closed and not self.config["unlimited"]
            assert self.completed == self.config["visitors"] and self.cancelled == 0
        if self.config["strategy"] == "cyclic":
            for actor in self.actors.values():
                order = actor["order"]
                assert len(order) == len(set(order))
                if len(order) >= 2:
                    step = (order[1] - order[0]) % self.count
                    assert math.gcd(step, self.count) == 1
                    assert all((b - a) % self.count == step for a, b in zip(order, order[1:]))
        return self.summary, self.kinds


def validate(records):
    assert records and records[0]["event"] == "config"
    validator = TraceValidator(records[0])
    for seq, event in enumerate(records):
        try:
            validator.accept(seq, event)
        except (AssertionError, IndexError, KeyError) as error:
            raise AssertionError(f"invalid event #{seq}: {event}: {error}") from error
    return validator.check_result()
