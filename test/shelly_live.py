"""Live acceptance checks. Default checks never actuate a relay.

pip install websocket-client
python test/shelly_live.py 192.168.2.220
python test/shelly_live.py 192.168.2.220 --exercise-relay 0
"""
import argparse
import json
import time
import urllib.request
import websocket


def check(host, exercise=None):
    base = f"http://{host}"

    def http(path, data=None):
        request = urllib.request.Request(
            base + path, data=data,
            headers={"Content-Type": "application/json"} if data else {},
        )
        with urllib.request.urlopen(request, timeout=10) as response:
            return json.load(response)

    info = http('/shelly')
    assert info['gen'] == 2 and info['id'].startswith('shelly-waveshare-')
    assert info['id'].endswith(info['mac'].lower())
    assert info['auth_en'] is False
    initial = http('/api/state')
    assert initial['count'] == 8
    ws = websocket.create_connection(f"ws://{host}/rpc", timeout=10)
    next_id = 0
    events = []

    def rpc(method, params=None, expect_error=False):
        nonlocal next_id
        next_id += 1
        frame = {'id': next_id, 'src': 'acceptance-test', 'method': method}
        if params is not None:
            frame['params'] = params
        ws.send(json.dumps(frame))
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            reply = json.loads(ws.recv())
            if 'method' in reply:
                events.append(reply)
                continue
            assert reply['id'] == next_id
            assert reply['src'] == info['id']
            assert reply['dst'] == 'acceptance-test'
            assert ('error' in reply) == expect_error, reply
            return reply.get('error') if expect_error else reply['result']
        raise TimeoutError(method)

    try:
        assert rpc('Shelly.GetDeviceInfo') == info
        cfg = rpc('Shelly.GetConfig')
        status = rpc('Shelly.GetStatus')
        assert 'sys' in status and 'sys' in cfg
        components = []
        while True:
            page = rpc('Shelly.GetComponents', {'offset': len(components), 'include': ['config']})
            assert page['offset'] == len(components) and page['total'] == 8
            assert page['components']
            components.extend(page['components'])
            if len(components) == page['total']:
                break
        assert [c['key'] for c in components] == [f'switch:{i}' for i in range(8)]
        assert rpc('Shelly.GetComponents', {'dynamic_only': True})['components'] == []
        assert rpc('Shelly.GetComponents', {'offset': 8})['components'] == []
        for i in range(8):
            assert rpc('Switch.GetStatus', {'id': i})['output'] == status[f'switch:{i}']['output']
            assert rpc('Switch.GetConfig', {'id': i})['initial_state'] == 'off'
        for bad in [-1, 8, '0', True, None, 0.5]:
            assert rpc('Switch.Set', {'id': bad, 'on': True}, True)['code'] == -32602
        for bad in ['true', 1, None]:
            assert rpc('Switch.Set', {'id': 0, 'on': bad}, True)['code'] == -32602
        assert rpc('Switch.Set', {'id': 0, 'on': True, 'toggle_after': 1}, True)['code'] == -32602
        assert rpc('Does.NotExist', expect_error=True)['code'] == -32601
        assert rpc('Shelly.GetComponents', {'offset': -1}, True)['code'] == -32602
        assert http('/api/state')['mask'] == initial['mask'], 'Read-only checks changed relay state'
        # Exercise the HTTP POST dispatcher and verify the UI still responds
        # while a WebSocket is held open on the same port.
        post = http('/rpc', json.dumps({'id': 50, 'method': 'Shelly.GetDeviceInfo'}).encode())
        assert post['id'] == 50 and post['result'] == info
        with urllib.request.urlopen(base + '/', timeout=10) as response:
            assert b'ESP32-S3' in response.read()
        if exercise is not None:
            old = rpc('Switch.GetStatus', {'id': exercise})['output']
            try:
                reply = rpc('Switch.Set', {'id': exercise, 'on': not old})
                assert reply['was_on'] == old
                deadline = time.monotonic() + 10
                while time.monotonic() < deadline:
                    if any(e.get('params', {}).get(f'switch:{exercise}', {}).get('output') == (not old) for e in events):
                        break
                    events.append(json.loads(ws.recv()))
                else:
                    raise AssertionError('No state notification')
                assert bool(http('/api/state')['mask'] & (1 << exercise)) == (not old)
                rpc('Switch.Toggle', {'id': exercise})
                assert rpc('Switch.GetStatus', {'id': exercise})['output'] == old
            finally:
                rpc('Switch.Set', {'id': exercise, 'on': old})
        print('PASS: eight channels, pagination, HTTP/WebSocket sharing, validation, UI coexistence'
              + (', relay switching and notifications' if exercise is not None else ' (no relay actuation)'))
    finally:
        ws.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('host', help='Board IP or hostname')
    parser.add_argument('--exercise-relay', type=int, choices=range(8),
                        help='Explicitly switch this zero-based relay, then restore it')
    args = parser.parse_args()
    check(args.host, args.exercise_relay)
