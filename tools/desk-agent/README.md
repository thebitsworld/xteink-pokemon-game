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

Percent, GB, bytes per second, seconds.

## Photos for the photo frame

`python desk_agent.py --photos "C:/Users/me/Pictures/Desk"` serves that folder (and its
subfolders: JPEG, PNG, BMP, WebP, GIF, TIFF) to the reader when Settings > System > Desk >
"Photos from" is "PC". Each photo goes out as a grayscale PNG sized for the screen (Pillow
needed); the reader downloads the one it shows and keeps Photo Slideshow's interval, order and
Fit/Crop.

## Claude usage limits

`claude` is `{"session": [pct, resets_at], "week": [pct, resets_at]}` or `null`; a window
Claude Code has not reported is `null`, which the reader shows as "–". Claude Code
gives its status line command the subscription limits (Pro, Max and Ultra; `rate_limits` in
the [status line data](https://code.claude.com/docs/en/statusline.md)). `desk_statusline.py`
is such a command: it saves them to `~/.claude/desk-rate-limits.json`, which the agent reads.
In `~/.claude/settings.json`:

```json
"statusLine": {"type": "command", "command": "python C:/path/to/desk_statusline.py", "refreshInterval": 60}
```

The numbers only move while a Claude Code session is open and has talked to the API; a window
whose reset time has passed reads 0 % until Claude Code reports it again.

To keep them current without a session open, `--claude-ping MINUTES` has the agent run one tiny
`claude -p` turn (Haiku, a one-line system prompt, no tools: about 1,100 tokens) whenever the
saved limits are older than MINUTES, and save the limits Claude Code reports with it
(`rate_limit_event` in its stream-json output). Each turn uses a little of the subscription, and
when no five-hour window is running it starts one.

```sh
python desk_agent.py --claude-ping 30
```
