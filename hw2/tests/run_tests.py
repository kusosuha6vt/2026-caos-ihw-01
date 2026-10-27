#!/usr/bin/env python3
"""Behavioral, concurrency, signal, I/O and independent-checker regression suite."""
import argparse
from collections import defaultdict
import json
import fcntl
import os
from pathlib import Path
import selectors
import resource
import signal
import subprocess
import tempfile
import time

from check_trace import check_trace

ROOT = Path(__file__).resolve().parent.parent
BASE = ['--arrival-min-ms', '0', '--arrival-max-ms', '0',
        '--view-min-ms', '0', '--view-max-ms', '0']
TOTAL = 0


def run(binary, args=(), expected=0, env=None, prepare_child=None, cwd=None):
    global TOTAL
    process = subprocess.run([str(binary), *args], text=True, encoding='utf-8',
                             capture_output=True, timeout=30, env=env, preexec_fn=prepare_child,
                             cwd=cwd)
    assert process.returncode == expected, (binary, args, process.returncode, process.stderr)
    assert not any(marker in process.stderr for marker in (
        'ERROR: AddressSanitizer', 'WARNING: ThreadSanitizer',
        'LeakSanitizer:', 'runtime error:')), process.stderr
    TOTAL += 1
    return process


def valid(binary, args=(), expected=0, env=None):
    process = run(binary, args, expected, env)
    result = check_trace(process.stdout)
    assert not process.stderr, process.stderr
    return result, process.stdout


def signature(result):
    routes, delays = defaultdict(list), {}
    for event in result[2]:
        if event['event'] == 'arrive':
            delays[event['visitor']] = event['delay_ms']
        if event['event'] == 'view_begin':
            routes[event['visitor']].append((event['painting'], event['delay_ms']))
    return dict(routes), delays


def signal_case(binary, target, signum, args, save=None):
    global TOTAL
    process = subprocess.Popen([str(binary), *args], stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE, bufsize=0)
    prefix = bytearray()
    pending = b''
    selector = selectors.DefaultSelector()
    selector.register(process.stdout, selectors.EVENT_READ)
    deadline = time.monotonic() + 10
    try:
        while time.monotonic() < deadline:
            if not selector.select(0.1):
                continue
            chunk = os.read(process.stdout.fileno(), 65536)
            assert chunk, 'process ended before signal target'
            prefix.extend(chunk)
            pending += chunk
            lines = pending.split(b'\n')
            pending = lines.pop()
            found = False
            for line in lines:
                record = json.loads(line)
                if record.get('event') == target or (target == 'config' and record['type'] == 'config'):
                    found = True
                    break
            if found:
                process.send_signal(signum)
                break
        else:
            raise AssertionError(f'signal target not seen: {target}')
        output, errors = process.communicate(timeout=10)
        assert process.returncode == 128 + signum, (process.returncode, errors)
        assert not errors, errors
        trace = (bytes(prefix) + output).decode('utf-8')
        result = check_trace(trace)
        if save is not None:
            Path(save).write_text(trace)
        assert result[1]['reason'] == 'signal'
        TOTAL += 1
        return result
    finally:
        selector.close()
        if process.poll() is None:
            process.kill()
            process.communicate()


def checker_mutations(trace):
    """Ensure the checker rejects damaged behavior, not merely valid JSON."""
    global TOTAL
    original = [json.loads(line) for line in trace.splitlines()]
    mutations = []
    damaged = json.loads(json.dumps(original))
    next(r for r in damaged if r.get('event') == 'view_end')['painting'] = 999
    mutations.append(damaged)
    damaged = json.loads(json.dumps(original))
    next(r for r in damaged if r.get('event') == 'grant')['gallery'] = 999
    mutations.append(damaged)
    damaged = json.loads(json.dumps(original))
    damaged[-1]['views'] += 1
    mutations.append(damaged)
    damaged = [r for r in original if r.get('event') != 'view_end']
    mutations.append(damaged)
    for damaged in mutations:
        try:
            check_trace('\n'.join(json.dumps(r) for r in damaged))
        except (AssertionError, KeyError):
            TOTAL += 1
        else:
            raise AssertionError('checker accepted a corrupted trace')


def suite(binary, skip_file_limit=False):
    print(f'Testing {binary}', flush=True)
    cases = [
        ['--visitors', '0'],
        ['--visitors', '1', '--paintings', '1', '--capacity', '1', '--painting-capacity', '1'],
        ['--visitors', '20', '--paintings', '4', '--capacity', '1', '--strategy', 'sequential'],
        ['--visitors', '40', '--paintings', '1', '--capacity', '8', '--painting-capacity', '1'],
        ['--visitors', '20', '--paintings', '7', '--capacity', '100', '--painting-capacity', '100'],
        ['--visitors', '100', '--paintings', '5', '--capacity', '10'],
        ['--visitors', '300', '--paintings', '5', '--capacity', '20'],
    ]
    for seed in [0, 42, 18446744073709551615]:
        for case in cases:
            valid(binary, [*BASE, *case, '--seed', str(seed)])
    # Keep resource contention visible when sanitizers slow thread creation.
    (_, summary, events), trace = valid(binary, [
        '--config', str(ROOT / 'tests/cases/contention.conf'),
        '--view-min-ms', '100', '--view-max-ms', '100'])
    assert summary['completed'] == 8
    assert any(e['event'] == 'wait_painting' for e in events)
    assert any(e['event'] == 'wait_entrance' for e in events)
    checker_mutations(trace)
    args = ['--config', str(ROOT / 'tests/cases/demo.conf'), '--visitors', '12']
    first, _ = valid(binary, args)
    second, _ = valid(binary, args)
    assert first[0]['visitors'] == 12 and signature(first) == signature(second)
    valid(binary, [*BASE, '--visitors=1', '--display-delay-ms=1'])
    help_process = run(binary, ['--help'])
    assert 'Usage:' in help_process.stdout
    for invalid in [
        ['--capacity', '0'], ['--paintings', '-1'], ['--visitors', '4097'],
        ['--paintings', '1025'], ['--capacity', '999999999999999999999999'],
        ['--seed', '18446744073709551616'], ['--seed', '+1'],
        ['--view-min-ms', '4', '--view-max-ms', '3'], ['--strategy', 'missing'],
        ['--unknown', '1'], ['--capacity'], ['positional'], ['--visitors', '2x'],
        ['--config', '/no/such/file'],
        ['--config', str(ROOT / 'tests/cases/demo.conf'), '--config', 'x'],
    ]:
        process = run(binary, invalid, 2)
        assert process.stderr and not process.stdout
    slow = ['--visitors', '100', '--capacity', '2', '--paintings', '3',
            '--arrival-min-ms', '0', '--arrival-max-ms', '0', '--view-min-ms', '10', '--view-max-ms', '10']
    for target in ['arrive', 'wait_entrance', 'view_begin', 'entrance_closed']:
        for signum in [signal.SIGINT, signal.SIGTERM]:
            signal_case(binary, target, signum, slow)
    started = time.monotonic()
    signal_case(binary, 'config', signal.SIGINT,
                ['--arrival-min-ms', '60000', '--arrival-max-ms', '60000'])
    assert time.monotonic() - started < 5, 'arrival delay did not stop promptly'
    with tempfile.TemporaryDirectory() as temporary:
        path = Path(temporary)
        log = path / 'run.jsonl'
        process = run(binary, [*BASE, '--visitors', '5', '--log', str(log)])
        assert log.read_text() == process.stdout
        check_trace(log.read_text())
        process = run(binary, [*BASE, '--visitors', '0', '--log', str(path / 'missing/log')], 1)
        assert process.stderr and not process.stdout
        for config_text in ['unknown=3\n', 'capacity=0\n', 'no-equals\n',
                            'a' * 4096, 'capacity=2\x00\n']:
            config = path / 'invalid.conf'
            config.write_text(config_text)
            process = run(binary, ['--config', str(config)], 2)
            assert process.stderr
        config = path / 'valid.conf'
        config.write_text('# comment\n visitors = 0\nlog = ' + str(log))
        valid(binary, ['--config', str(config)])
        config.write_text('visitors=0' + ' ' * (4095 - len('visitors=0')) + '\n')
        valid(binary, ['--config', str(config)])
        if Path('/dev/full').exists():
            process = run(binary, [*BASE, '--visitors', '2', '--log', '/dev/full'], 1)
            assert 'Cannot open log' in process.stderr and not process.stdout
            with open('/dev/full', 'w') as full:
                process = subprocess.run([str(binary), *BASE, '--visitors', '2', '--log', str(log)],
                                         stdout=full, stderr=subprocess.PIPE, text=True, timeout=10)
            assert process.returncode == 1 and 'Cannot write stdout' in process.stderr
            assert check_trace(log.read_text())[1]['reason'] == 'error'
        def restrict_log_size():
            signal.signal(signal.SIGXFSZ, signal.SIG_IGN)
            resource.setrlimit(resource.RLIMIT_FSIZE, (512, 512))

        if not skip_file_limit:
            failed_write = run(binary, [*BASE, '--visitors', '2', '--log', str(log)],
                               expected=1, prepare_child=restrict_log_size)
            assert 'Cannot write log' in failed_write.stderr
            assert check_trace(failed_write.stdout)[1]['reason'] == 'error'
        process = subprocess.Popen([str(binary), *BASE, '--visitors', '2', '--log', str(log)],
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        process.stdout.close()
        process.stdout = None
        try:
            _, errors = process.communicate(timeout=10)
            assert process.returncode == 1 and b'Cannot write stdout' in errors
        finally:
            if process.poll() is None:
                process.kill()
                process.communicate()
        assert check_trace(log.read_text())[1]['reason'] == 'error'
    paced, _ = valid(binary, [*BASE, '--visitors', '1', '--paintings', '3', '--display-delay-ms', '2'])
    assert paced[1]['time_ms'] >= 6, 'display delay was skipped'
    # Scheduling stress: repeat genuine contention, not only random permutations.
    for seed in range(20):
        valid(binary, [*BASE, '--visitors', '50', '--paintings', '3',
                       '--capacity', '6', '--painting-capacity', '1', '--seed', str(seed)])
    return signature(first)



def file_security_suite(binary):
    """Проверки сохранности файлов и отсутствия блокировки на специальных файлах."""
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        victim = root / 'victim'
        sentinel = b'not a gallery log\n'
        victim.write_bytes(sentinel)
        link = root / 'symlink.log'
        link.symlink_to(victim)
        result = run(binary, [*BASE, '--visitors', '0', '--log', str(link)], 1)
        assert result.stderr and not result.stdout and victim.read_bytes() == sentinel
        hardlink = root / 'hardlink.log'
        os.link(victim, hardlink)
        result = run(binary, [*BASE, '--visitors', '0', '--log', str(hardlink)], 1)
        assert result.stderr and victim.read_bytes() == sentinel
        hardlink.unlink()
        config = root / 'input.conf'
        contents = b'visitors=0\n'
        config.write_bytes(contents)
        result = run(binary, ['--config', str(config), '--log', str(config)], 1)
        assert result.stderr and not result.stdout and config.read_bytes() == contents
        alias = root / 'config-alias'
        os.link(config, alias)
        result = run(binary, ['--config', str(config), '--log', str(alias)], 1)
        assert result.stderr and config.read_bytes() == contents
        alias.unlink()
        # Открытый stdout указывает на тот же узел: журнал не должен усечь файл.
        with victim.open('r+b') as output:
            process = subprocess.run([str(binary), '--visitors', '0', '--log', str(victim)],
                                     stdout=output, stderr=subprocess.PIPE, timeout=5)
        assert process.returncode == 1 and victim.read_bytes() == sentinel
        for option in ['--config', '--log']:
            fifo = root / (option[2:] + '.fifo')
            os.mkfifo(fifo)
            started = time.monotonic()
            result = run(binary, ['--visitors', '0', option, str(fifo)],
                         2 if option == '--config' else 1)
            assert time.monotonic() - started < 5 and result.stderr and not result.stdout
        result = run(binary, ['--config', str(root), '--visitors', '0'], 2)
        assert result.stderr and not result.stdout
        for contents in ['', ' \t\n\r\n']:
            config.write_text(contents)
            valid(binary, ['--config', str(config), '--visitors', '0'])
        config.write_text('visitors=0\n' + '\n' * (65536 - len('visitors=0\n')))
        valid(binary, ['--config', str(config)])
        with config.open('a') as output:
            output.write('\n')
        result = run(binary, ['--config', str(config)], 2)
        assert result.stderr and not result.stdout
        private_log = root / 'private.log'
        valid(binary, [*BASE, '--visitors', '0', '--log', str(private_log)])
        assert (private_log.stat().st_mode & 0o777) & ~0o600 == 0


def parsing_suite(binary, skip_file_limit):
    """Library integration: argument boundaries, INI grammar and final write failure."""
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        for arguments in [['--log', '--help'], ['--log=--help']]:
            result = run(binary, ['--visitors', '0', *arguments], cwd=root)
            assert check_trace(result.stdout)[1]['reason'] == 'completed'
            assert (root / '--help').read_text() == result.stdout
        assert 'Usage:' in run(binary, ['-h']).stdout
        run(binary, ['--visitors', '--help'], 2)
        run(binary, ['--', 'unexpected'], 2)
        valid(binary, [*BASE, '--vis=0'])
        config = root / 'input.conf'
        config.write_text('; comment\nvisitors: 0 # comment\n')
        valid(binary, ['--config', str(config)])
        for text in ['[section]\n', '[]\n', '\ufeffvisitors=0\n',
                     'visitors=0\n  continued\n', 'visitors=0#comment\n']:
            config.write_text(text)
            run(binary, ['--config', str(config)], 2)
        if not skip_file_limit:
            _, trace = valid(binary, ['--visitors', '0', '--paintings', '1'])
            limit = sum(len(line) for line in trace.encode().splitlines(keepends=True)[:-1]) + 24

            def restrict_final_record():
                signal.signal(signal.SIGXFSZ, signal.SIG_IGN)
                resource.setrlimit(resource.RLIMIT_FSIZE, (limit, limit))

            log = root / 'limited.log'
            result = run(binary, ['--visitors', '0', '--paintings', '1', '--log', str(log)],
                         1, prepare_child=restrict_final_record)
            assert 'Cannot write log' in result.stderr
            assert check_trace(result.stdout)[1]['reason'] == 'completed'
            assert b'"type":"summary"' in log.read_bytes().splitlines()[-1]


def stalled_output_suite(binary):
    """An unread pipe must neither hang completion nor block signal handling forever."""
    global TOTAL
    with tempfile.TemporaryDirectory() as directory:
        for signum in [None, signal.SIGTERM]:
            read_fd, write_fd = os.pipe()
            if hasattr(fcntl, 'F_SETPIPE_SZ'):
                fcntl.fcntl(write_fd, fcntl.F_SETPIPE_SZ, 4096)
            flags = fcntl.fcntl(write_fd, fcntl.F_GETFL)
            fcntl.fcntl(write_fd, fcntl.F_SETFL, flags | os.O_NONBLOCK)
            try:
                while True:
                    os.write(write_fd, b'.' * 4096)
            except BlockingIOError:
                pass
            fcntl.fcntl(write_fd, fcntl.F_SETFL, flags)
            flags = fcntl.fcntl(write_fd, fcntl.F_GETFL)
            log = Path(directory) / f'trace-{signum}.jsonl'
            process = subprocess.Popen([str(binary), *BASE, '--visitors', '100',
                                        '--capacity', '1', '--paintings', '1',
                                        '--log', str(log)], stdout=write_fd, stderr=subprocess.PIPE)
            try:
                if signum is not None:
                    deadline = time.monotonic() + 5
                    while time.monotonic() < deadline:
                        if log.exists():
                            process.send_signal(signum)
                            break
                        time.sleep(0.001)
                    else:
                        raise AssertionError('log was not opened before signal')
                _, errors = process.communicate(timeout=5)
                assert process.returncode == 1 and b'Cannot write stdout' in errors
                restored = fcntl.fcntl(write_fd, fcntl.F_GETFL)
                assert restored == flags, ('descriptor flags changed', flags, restored, signum)
                summary = check_trace(log.read_text())[1]
                assert summary['reason'] == 'error'
                if signum is not None:
                    assert summary['signal'] == signum
                TOTAL += 1
            finally:
                if process.poll() is None:
                    process.kill()
                    process.communicate()
                os.close(write_fd)
                os.close(read_fd)


def fault_suite(binary):
    for count in [0, 3, 12]:
        env = dict(os.environ, GALLERY_FAIL_AFTER=str(count))
        process = run(binary, [*BASE, '--visitors', '20'], expected=1, env=env)
        result = check_trace(process.stdout)
        assert result[1]['reason'] == 'error'
        assert result[1]['created'] == count
        assert 'Cannot create visitor' in process.stderr


    # Отказ уведомления, первой/средней картины и первого/среднего посетителя.
    for count in [0, 1, 3, 6, 9]:
        env = dict(os.environ, GALLERY_FAIL_PERMIT_AFTER=str(count))
        process = run(binary, [*BASE, '--visitors', '20'], expected=1, env=env)
        assert not process.stdout
        assert 'Cannot initialize' in process.stderr


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binaries', nargs='+', type=Path)
    parser.add_argument('--fault', action='append', type=Path, default=[])
    parser.add_argument('--skip-file-limit', action='store_true',
                        help='Не ограничивать размер файла: несовместимо с этой средой TSan.')
    args = parser.parse_args()
    signatures = [suite(binary.resolve(), args.skip_file_limit) for binary in args.binaries]
    for binary in args.binaries:
        file_security_suite(binary.resolve())
        parsing_suite(binary.resolve(), args.skip_file_limit)
        stalled_output_suite(binary.resolve())
    assert all(item == signatures[0] for item in signatures), 'backend changed seeded choices'
    for binary in args.fault:
        fault_suite(binary.resolve())
    print(f'PASS: {TOTAL} subprocess/checker checks', flush=True)


if __name__ == '__main__':
    main()
