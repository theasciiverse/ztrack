"""drive.py SESSION SECONDS: play ztrack in a tmux pane, dodging the traffic.

Recorded asciiverse/ztrack.cast: asciinema rec runs ./ztrack inside a 100x30
tmux session while this script reads the pane and sends the keys."""
import random, subprocess, sys, time

S, secs = sys.argv[1], float(sys.argv[2])
def key(*k): subprocess.run(["tmux", "send-keys", "-t", S, *k])
def screen():
    out = subprocess.run(["tmux", "capture-pane", "-p", "-t", S], capture_output=True, text=True).stdout
    return [r.ljust(100) for r in (out.split("\n") + [""] * 30)[:30]]
ROAD = set(" /\\_-'`")   # road edges and lane borders, never part of a car alone

def busy(rows, x0, x1, y0=9, y1=20):
    return sum(1 for y in range(y0, y1) for x in range(x0, x1) if rows[y][x] not in ROAD)

time.sleep(2.5); key("Space"); time.sleep(0.6); key("Up")
lane, t0, cooldown, crashes, slow = 1, time.time(), 0, 0, 0
while time.time() - t0 < secs:
    rows = screen()
    if "CRASH" in "".join(rows[10:13]):
        crashes += 1; lane = 1; time.sleep(2.3); key("Up"); continue
    ahead = sum(1 for y in range(9, 20) for x in range(22 + (20 - y) + 2, 56 - (20 - y) - 2)
                if rows[y][x] not in ROAD)
    if slow and time.time() > slow:
        key("Up"); slow = 0
    if ahead > 4 and cooldown <= 0:
        left_ok = lane > 0 and "#" not in rows[22][10:18] and busy(rows, 0, 24) < 3
        right_ok = lane < 2 and "#" not in rows[22][61:69] and busy(rows, 55, 80) < 3
        opts = [d for d, ok in (("Left", left_ok), ("Right", right_ok)) if ok]
        if opts:
            d = random.choice(opts); key(d); lane += -1 if d == "Left" else 1; cooldown = 3
        elif not slow:
            key("Down"); slow = time.time() + 1.0
    cooldown -= 1
    time.sleep(0.05)
print("crashes", crashes, file=sys.stderr)
