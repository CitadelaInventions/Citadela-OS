"""Screen refresh scheduling, independent of serial I/O and Tk."""


class ScreenSync:
    def __init__(self, now, interval=30, stall_timeout=12):
        self.interval = interval
        self.stall_timeout = stall_timeout
        self.enabled = False
        self.pending = False
        self.scene_started = False
        self.frame_open = False
        self.damaged = False
        self.last_progress = now
        self.next_periodic = None
        self.last_input = now
        self.mouse = (0, 0)
        self.buttons = 0
        self.key_down = False

    def requested(self, now):
        self.enabled = True
        self.pending = True
        self.scene_started = False
        self.frame_open = False
        self.damaged = False
        self.last_progress = now

    def disabled(self):
        self.enabled = False
        self.pending = self.frame_open = self.damaged = False
        self.scene_started = False
        self.next_periodic = None

    def received(self, records, now):
        if not self.pending or self.scene_started or any(op == "H" for op, _ in records):
            self.last_progress = now
        for op, args in records:
            if op == "STATE" and not args[0]:
                self.disabled()
            elif op == "H":
                self.enabled = self.pending = self.frame_open = True
                self.scene_started = True
            elif op == "E":
                if self.scene_started:
                    self.pending = self.frame_open = self.damaged = False
                    self.scene_started = False
                    self.next_periodic = now + self.interval
            elif op == "FRAME":
                self.frame_open = self.damaged = False
            elif op != "CHECK" and self.enabled:
                self.frame_open = True

    def input(self, command, now):
        parts = command.split()
        if parts[:2] != ["VDM", "INPUT"]:
            return
        self.last_input = now
        try:
            if len(parts) == 6 and parts[2] == "mouse":
                self.mouse = (int(parts[3]), int(parts[4]))
                self.buttons = int(parts[5])
            elif len(parts) == 4 and parts[2] == "key":
                self.key_down = parts[3].lower() != "rlsd"
        except ValueError:
            pass

    def release_commands(self):
        commands = []
        if self.buttons:
            x, y = self.mouse
            commands.append(f"VDM INPUT mouse {x} {y} 0")
        if self.key_down:
            commands.append("VDM INPUT key rlsd")
        self.buttons = 0
        self.key_down = False
        return commands

    def due(self, now):
        if not self.enabled:
            return None
        if (self.pending or self.frame_open or self.damaged) and now - self.last_progress >= self.stall_timeout:
            return "stalled"
        if (not self.pending and not self.frame_open and not self.damaged and self.next_periodic is not None
                and now >= self.next_periodic and now - self.last_input >= 2
                and not self.buttons and not self.key_down):
            return "periodic"
        return None
