#!/usr/bin/env python3
"""Reconstruct gallery state from JSON Lines; do not trust producer counters."""
import argparse
from collections import Counter, deque
import json
from pathlib import Path


def check_trace(text):
    """Return (config, summary, events) or raise AssertionError for a bad trace."""
    records = [json.loads(line) for line in text.splitlines() if line.strip()]
    assert records and records[0]['type'] == 'config', 'missing configuration'
    config = records[0]
    assert records[-1]['type'] == 'summary', 'missing final summary'
    assert sum(r['type'] == 'config' for r in records) == 1
    assert sum(r['type'] == 'summary' for r in records) == 1
    summary = records[-1]
    states, selected, viewing, seen = {}, {}, {}, {}
    queue = deque()
    admitted, inside = set(), set()
    occupancies = Counter()
    peaks = Counter()
    counts = Counter()
    maximum = 0
    closed = stopped = warden_done = False
    sequence, previous_time = 0, 0.0
    events = []
    painting_summaries = {}
    for record in records[1:-1]:
        if record['type'] == 'painting_summary':
            assert warden_done
            painting = record['painting']
            assert painting not in painting_summaries
            painting_summaries[painting] = record
            continue
        assert record['type'] == 'event', record
        assert not painting_summaries, 'event after final painting statistics'
        event = record['event']
        visitor, painting = record['visitor'], record['painting']
        sequence += 1
        assert record['seq'] == sequence, 'missing/reordered event'
        assert record['time_ms'] >= previous_time, 'time moved backwards'
        previous_time = record['time_ms']
        assert not warden_done or event == 'stop', 'event after warden completion'
        if visitor:
            assert 1 <= visitor <= config['visitors']
        if painting:
            assert 1 <= painting <= config['paintings']
        if event == 'arrive':
            assert visitor not in states
            assert config['arrival_min_ms'] <= record['delay_ms'] <= config['arrival_max_ms']
            states[visitor] = 'arrived'
            seen[visitor] = set()
        elif event == 'queue':
            assert states[visitor] == 'arrived' and not stopped
            states[visitor] = 'queued'
            queue.append(visitor)
        elif event == 'wait_entrance':
            assert states[visitor] == 'queued'
            assert len(admitted) == config['capacity']
        elif event == 'grant':
            assert not stopped
            assert queue and queue.popleft() == visitor, 'non-FIFO grant'
            assert states[visitor] == 'queued'
            states[visitor] = 'granted'
            admitted.add(visitor)
            maximum = max(maximum, len(admitted))
        elif event == 'enter':
            assert states[visitor] == 'granted' and visitor in admitted
            states[visitor] = 'inside'
            inside.add(visitor)
        elif event == 'choose':
            assert visitor in inside and visitor not in selected and visitor not in viewing
            assert painting not in seen[visitor], 'painting counted twice'
            if config['strategy'] == 'sequential':
                assert painting == len(seen[visitor]) + 1
            selected[visitor] = painting
        elif event == 'wait_painting':
            assert selected[visitor] == painting and visitor not in viewing
        elif event == 'view_begin':
            assert visitor in inside and visitor not in viewing
            assert selected[visitor] == painting
            assert config['view_min_ms'] <= record['delay_ms'] <= config['view_max_ms']
            viewing[visitor] = painting
            occupancies[painting] += 1
            peaks[painting] = max(peaks[painting], occupancies[painting])
        elif event == 'view_end':
            assert viewing.pop(visitor) == painting
            assert selected.pop(visitor) == painting
            assert painting not in seen[visitor]
            seen[visitor].add(painting)
            occupancies[painting] -= 1
        elif event == 'exit':
            assert visitor in inside and visitor not in viewing and visitor not in selected
            assert len(seen[visitor]) == config['paintings'], 'incomplete route'
            inside.remove(visitor)
            admitted.remove(visitor)
            states[visitor] = 'finished'
        elif event == 'cancel':
            assert stopped and states[visitor] in ('queued', 'arrived')
            if states[visitor] == 'queued':
                assert queue and queue.popleft() == visitor, 'non-FIFO cancellation'
            states[visitor] = 'cancelled'
        elif event == 'stop':
            assert not stopped
            stopped = True
            assert record['delay_ms'] in (2, 15)
        elif event == 'entrance_closed':
            assert not closed
            closed = True
        elif event == 'warden_done':
            assert closed and not queue and not admitted and not inside and not viewing
            assert not warden_done
            warden_done = True
        elif event == 'error':
            pass
        else:
            raise AssertionError(f'unknown event {event}')
        assert 0 <= len(admitted) <= config['capacity'], 'gallery limit'
        assert all(0 <= value <= config['painting_capacity'] for value in occupancies.values())
        assert record['gallery'] == len(admitted), 'wrong gallery snapshot'
        assert record['queue'] == len(queue), 'wrong queue snapshot'
        assert record['viewers'] == (occupancies[painting] if painting else 0)
        counts[event] += 1
        events.append(record)
    assert closed and warden_done and not queue and not admitted and not inside and not viewing
    assert all(value in ('finished', 'cancelled') for value in states.values())
    assert len(painting_summaries) == config['paintings']
    for painting in range(1, config['paintings'] + 1):
        assert painting_summaries[painting]['maximum'] == peaks[painting]
        assert painting_summaries[painting]['viewers'] == 0
    for key, event in [('arrived', 'arrive'), ('admitted', 'grant'), ('completed', 'exit'),
                       ('cancelled', 'cancel'), ('views', 'view_end')]:
        assert summary[key] == counts[event], f'wrong summary {key}'
    assert summary['created'] == counts['arrive']
    assert summary['requested'] == config['visitors']
    assert summary['arrived'] == summary['completed'] + summary['cancelled']
    assert summary['admitted'] == summary['completed']
    assert summary['views'] == summary['completed'] * config['paintings']
    assert summary['maximum'] == maximum
    assert summary['gallery'] == summary['queue'] == 0
    assert summary['time_ms'] >= previous_time
    if summary['reason'] == 'completed':
        assert not stopped and not counts['error']
        assert summary['created'] == config['visitors']
        assert summary['cancelled'] == summary['signal'] == 0
    elif summary['reason'] == 'signal':
        assert stopped and summary['signal'] in (2, 15)
    else:
        assert summary['reason'] == 'error'
    return config, summary, events


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace', type=Path)
    args = parser.parse_args()
    config, summary, events = check_trace(args.trace.read_text())
    print(f"OK: {config['backend']}, {len(events)} events, "
          f"{summary['completed']} completed, {summary['views']} views, {summary['reason']}")


if __name__ == '__main__':
    main()
