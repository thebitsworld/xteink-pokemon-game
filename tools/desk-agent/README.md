# Desk agent

Serves this PC's CPU, RAM, disk and network numbers to an X3 Lite in Desk mode
([plan](../../docs/development/desk-mode-plan.md)). The reader asks for `GET /v1/stats` over
the LAN about once a minute while charging and finds the agent by mDNS (`_xdesk._tcp`).

It is read-only and has no login: anyone on your network can read these numbers (host name,
CPU, RAM, disk sizes, network rate, uptime), nothing else. Run it on a home network only.

## Run

Python 3.9 or newer:

```sh
pip install -r requirements.txt
python desk_agent.py            # port 8765, advertised by mDNS
python desk_agent.py --no-mdns  # if mDNS is blocked; set the address on the reader instead
```

Windows asks once whether Python may accept connections on private networks: allow it, or the
reader cannot reach the agent.

Start with Windows: create a Task Scheduler entry that runs at log-on, for example

```bat
schtasks /Create /SC ONLOGON /TN "Desk agent" /TR "pythonw \"C:\path\to\desk_agent.py\""
```

## Reader side

Without mDNS, put the agent's address in `/.crosspoint/desk.json` on the SD card:

```json
{"city": "Hà Nội", "agent": "192.168.1.20:8765"}
```

## Response

```json
{"v": 1, "host": "DESKTOP", "ts": 1795174800, "cpu": 23, "ram": [19.5, 32.0],
 "disks": [["C:", 412.0, 1000.0]], "net": [2400000, 310000], "uptime": 274000, "claude": null}
```

Percent, GB, bytes per second, seconds. `claude` stays `null` until a stable source for the
Claude usage limits is found.
