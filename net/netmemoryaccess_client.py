import os
import ctypes
import struct
import time
import sys
import asyncio
import threading
import queue
from tkinter import ttk
from tkinter import *
from tkinter import font
from tkinter.ttk import *
from enum import IntEnum, StrEnum

#region: const
HOST = "127.0.0.1"
PORT = 5555
TEST = False
heartbeat_running = True

SW_HIDE = 0
SW_SHOW = 5

geometry = "800x600"

global loop
loop = True

global HELP
HELP = False

#Overview of command categories and their respective commands for GUI dropdown (WIP)
"""
CATEGORIES:
- reader: Commands that read data from the Wii (e.g., current item index, busy state)
- base: Core commands that interact with game mechanics (e.g., giving items, setting indices, map interactions)
- effect: Commands that trigger visual or audio effects in the game (e.g., testing an effect command)
    ...
COMMANDS:
- reader:
    - CMD_rIDX: Reads the current item index from the game.
    - CMD_rBUSY: Reads the current busy state of the game.
- base:
    - CMD_ITEM: Command to give an item to the player (expects an integer itemID)
    - CMD_IDX: Command to set the current item index (expects an integer idx)
"""
CATEGORIES = {
    "reader": 0x00,
    "base": 0x01,
    }
COMMANDS = {
    "reader": {
        "CMD_rIDX": 0x00,
        "CMD_rBUSY": 0x01,
    },
    "base": {
        "CMD_ITEM": 0x00,
        "CMD_IDX": 0x01,
    },
}

class CommandDisplay(StrEnum):
    """User-friendly command descriptions for GUI display (WIP)"""
    #Read Commands
    CMD_rIDX = "Read IDX ()"
    CMD_rBUSY = "Read Busy State ()"
    
    #Base Commands
    CMD_ITEM = "Item Command (Int: itemID)"
    CMD_IDX = "Set IDX (Int: idx)"

def toggleHelp():
    global HELP
    HELP = not HELP

#region: barebones tkinter GUI for item sending and state display (WIP)
def start_gui():
    #GUI Helpers

    #custom callback to catch exceptions in the Tkinter main loop and exit gracefully
    def report_callback_exception(exc, val, tb):
        root._fatal_error = (exc, val, tb)
        if hasattr(root, "poll_id"):
            root.after_cancel(root.poll_id)
        root.destroy()  # Exit the main loop

    def append_log(msg):
        log_box.config(state='normal')
        log_box.insert('end', msg + "\n")
        log_box.see('end')
        log_box.config(state='disabled')

    def clear_log():
        log_box.config(state='normal')
        log_box.delete('1.0', 'end')
        log_box.config(state='disabled')

    def poll_logs():
        try:
            while True:
                line = client.log_queue.get_nowait()
                append_log(line)
        except queue.Empty:
            pass

        root.poll_id = root.after(100, poll_logs)  # run again in 100 ms

    def setup_packet():
        global HELP
        category = cat.get()
        command_display = cmd.get()
        command = CommandDisplay(command_display).name
        try:
            if HELP:
                header = struct.pack(">BBH", 0xFF, 0xFF, 6) #special header to signal help request
                payload = struct.pack(">BB", CATEGORIES[category], COMMANDS[category][command])
                packet = header + payload
                return packet

            #reader commands have no payload, so we can skip the packet setup and just return the cmdID for the state machine to handle
            catID = CATEGORIES[category]
            if catID == 0x00:
                header = struct.pack(">BBH", catID, COMMANDS[category][command], 4) #reader commands have a fixed length of 4 (header only, no payload)
                return header
            
            #for non-reader commands, we need to include the payload from the command box in the GUI
                #the payload will vary later on as we add more commands, but for now we can just take the raw string input and encode it as bytes to send to the state machine
            cmdID = COMMANDS[category][command]
            header = struct.pack(">BBH", catID, cmdID, 0) #length will be calculated in the client loop to account for variable payloads, so just set to 0 here

            #WIP method to send custom payloads with varying types
            raw = client.cmd_var.get()

            payload = bytearray()
            tokens = "idfs" #integer, string, float, double

            i = 0
            n = len(raw)
            while i < n:
                t = raw[i]
                i += 1

                if t == 'i': #integer

                    start = i
                    while i < n and raw[i] not in tokens and not raw[i].isspace():
                        i += 1
                    value = int(raw[start:i].strip())
                    payload += b'i'
                    payload += struct.pack(">i", value)

                elif t == 'f': #float

                    start = i
                    while i < n and raw[i] not in tokens and not raw[i].isspace():
                        i += 1
                    value = float(raw[start:i].strip())
                    payload += b'f'
                    payload += struct.pack(">f", value)

                elif t == 'd': #double

                    start = i
                    while i < n and raw[i] not in tokens and not raw[i].isspace():
                        i += 1
                    value = float(raw[start:i].strip())
                    payload += b'd'
                    payload += struct.pack(">d", value)

                elif t == 's':

                    if i >= n or raw[i] != '"':
                        client.log('String type must be quoted (e.g., s"Hello world")')
                        return None

                    i += 1
                    start = i

                    while i < n and raw[i] != '"':
                        i += 1

                    if i >= n:
                        client.log("Unterminated string")
                        return None

                    value = raw[start:i]
                    i += 1

                    encoded = value.encode()

                    payload += b's'
                    payload += struct.pack(">H", len(encoded))
                    payload += encoded

                elif t == ' ': #skip whitespace
                    continue

                else:
                    raise ValueError(f"Unknown token {t} in payload")

            length = len(header) + len(payload)
            packet = struct.pack(">BBH", catID, cmdID, length) + payload
            return packet
        
        except KeyError as e:
            missing = e.args[0]
            if missing == 'Select Category':
                client.log("No category selected.")
            elif missing == 'Select Command':
                client.log("No command selected.")
            else:
                client.log(f"Invalid category or command: {category}, {command}")

    def resize_cmd_box(category, values):
        max_pixels = 0

        for c in [category]:
            width = GUIFont.measure(str(c))
            if width > max_pixels:
                max_pixels = width

        cat_char_width = GUIFont.measure("0")
        category_width_chars = int(max_pixels / cat_char_width) + 2
        cat.config(width=category_width_chars)

        for v in values:
            width = GUIFont.measure(str(v))
            if width > max_pixels:
                max_pixels = width

        # Convert pixels → character width
        cmd_char_width = GUIFont.measure("0")
        cmd_width_chars = int(max_pixels / cmd_char_width) + 2
        cmd.config(width=cmd_width_chars)

    def commandHandler(passThru=False):
        global HELP
        if cat.get() == "Select Category" or cmd.get() == "Select Command":
            client.log("Please select a valid category and command before sending.")
            return
        
        if passThru:
            packet = setup_packet()
            if packet:
                id = struct.unpack(">BB", packet[:2])
                length = struct.unpack(">H", packet[2:4])[0]
                payload = packet[4:]
                if HELP:
                    client.log(f"Planned packet: {packet.hex()}\nHELP MODE: Requesting args for Category: {payload[0]:02X}, Command: {payload[1]:02X}")
                    return
                
                client.log(f"Planned packet: {packet.hex()}\nCategory: {id[0]:02X}, Command: {id[1]:02X}, Length: {length}, Payload: {payload.hex()}")
            else:
                return
            return
        
        if client.running:
            packet = setup_packet()
            if packet:
                client.cmd_queue.put(packet)
                client.log(f"Queueing command: {packet.hex()}")
                append_log(f"Queued command: {cmd.get()}")
            else:
                return
        else:
            append_log(f"Client is not alive, cannot queue command: {cmd.get()}")

    def preview_params(text: str) -> str:
        tokens = "idfsbp"
        i = 0
        n = len(text)

        parts = []
        while i < n:
            t = text[i]
            i += 1

            if t.isspace():
                continue

            if t in ("i", "f", "d"):
                if t == "i":
                    type_name = "Int"
                elif t == "f":
                    type_name = "Float"
                else:
                    type_name = "Double"
                start = i
                while i < n and (text[i] not in tokens):
                    i += 1
                num_str = text[start:i].strip()
                if not num_str:
                    parts.append(f"{type_name}(?)")
                    continue

                try:
                    if t == "i":
                        parts.append(f"Int({int(num_str)})")
                    elif t == "f":
                        parts.append(f"Float({float(num_str)})")
                    else:
                        parts.append(f"Double({float(num_str)})")
                except ValueError:
                    parts.append(f"{type_name}(!)")

            if t == "s":
                type_name = "Str"
            
                # require quoted string
                if i >= n or text[i] != '"':
                    parts.append(f'{type_name}(?)')
                    continue
                i += 1
                start = i
                while i < n and text[i] != '"':
                    i += 1
                if i >= n:
                    parts.append(f'{type_name}(unterminated)')
                    break
                s = text[start:i]

                if t == "s":
                    parts.append(f'Str({s})')
                    
                else:
                    parts.append(f"?{t}")

        if not parts:
            return "Params: (none)"
        return "Params: " + ", ".join(parts)

    root = Tk()
    root.geometry(geometry)
    client = SPMAPClient() # client instance to sync GUI and heartbeat state
    heartbeat_controller = HeartbeatController(client)
    root.title("SPMAP Client GUI")
    root._fatal_error = None
    try:
        GUIFont = font.Font(family="PaperMarioFont", size=12)
    except:
        print("Font failed to initialize.. Falling back")
        GUIFont = font.Font(family="Calibri", size=12)

    root.report_callback_exception = report_callback_exception

    # widgets and layout
    style = Style()
    print(style.theme_names())
    style.theme_use('default')

    #Configure is for 'permanent settings'
    style.configure("BW.TButton",
                    font=GUIFont)
    
    #Map is for 'variable button-state settings'; i.e. hover / click
    style.map("BW.TButton",
              foreground=[('pressed', 'red'), ('active', 'blue')], #pressed = clicked; active = hover
              background=[('pressed', 'yellow'), ('active', 'green')],
              )
    
    buttons = Frame(root, width=1000, height=400)

    cmd_var = StringVar()

    command_box = Entry(root, width=30, font=GUIFont, textvariable=cmd_var)
    command_label = Label(root, text="Params: (none)", anchor='w', font=GUIFont, justify="left")
    xscroll = ttk.Scrollbar(root, orient="horizontal", command=command_box.xview)

    log_box = Text(root, height=10, state='disabled')
    help_button = ttk.Checkbutton(root, text="Help (Request the game to send the arg(s) for the selected command)", command=toggleHelp)

    cat = ttk.Combobox(buttons, values=list(CATEGORIES.keys()), state="readonly", font=GUIFont)
    cat.set("Select Category")

    cmd = ttk.Combobox(buttons, values=list(), state="readonly", font=GUIFont)
    cmd.set("Select Command")

    restartApp = Button(buttons, text="Restart (Test)", command=force_crash)
    quitApp = Button(buttons, text="Quit (test)", command=quit)
    hbStart = Button(buttons, text="Start Heartbeat Thread", command=heartbeat_controller.start)
    hbStop = Button(buttons, text="Stop Heartbeat Thread", command=lambda: heartbeat_controller.stop(client))
    testCom = Button(buttons, text="Test Command (mapInteract)", command=lambda: commandHandler())
    clear = Button(buttons, text="Clear Log", command=clear_log)
    testPacket = Button(buttons, text="See planned packet (Test)", command=lambda: commandHandler(passThru=True), style="BW.TButton")

    buttons.place(relx=0.0, rely=0.0, anchor='nw')
    restartApp.place(relx=0.0, rely=0.0, anchor='w', x=5, y=30)
    quitApp.place(relx=0.0, rely=0.0, anchor='w', x=5, y=60)
    hbStart.place(relx=0.0, rely=0.0, anchor='w', x=5, y=90)
    hbStop.place(relx=0.0, rely=0.0, anchor='w', x=5, y=120)
    testCom.place(relx=0.0, rely=0.0, anchor='w', x=5, y=150)
    testPacket.place(relx=0.0, rely=0.0, anchor='w', x=5, y=180)

    cat.place(relx=0.0, rely=0.0, anchor='w', x=5, y=210)
    cmd.place(relx=0.0, rely=0.0, anchor='w', x=5, y=240)

    clear.place(anchor='se', x=20, y=10)
    command_box.place(relx=0.0, rely=0.0, anchor='w', x=5, y=270)
    xscroll.place(relx=0.0, rely=0.0, anchor='w', x=5, y=270 + 22, width=240)  # tweak y/width as needed
    command_label.place(relx=0.0, rely=0.0, anchor='w', x=5, y=350)
    command_box.configure(xscrollcommand=xscroll.set)

    def on_cmd_change(*_):
        text = cmd_var.get()
        command_label.config(text=preview_params(text))

    cmd_var.trace_add("write", on_cmd_change)

    client.cmd_var = cmd_var


    def update_commands(event=None):
        category = cat.get()

        if category in COMMANDS:
            cmds = COMMANDS[category]
            cat_display_value = category.capitalize()
            cmd_display_values = []

            for cmd_name in cmds.keys():
                try:
                    cmd_display_values.append(CommandDisplay[cmd_name])
                except KeyError:
                    # Fallback if display enum missing
                    cmd_display_values.append(cmd_name)

            cmd['values'] = cmd_display_values
            cmd.set("Select Command")

            resize_cmd_box(cat_display_value, cmd_display_values)

    def update_layout(event=None):
        """Dynamically update the layout of the log box and heart pillar based on the current window size."""
        window_height = root.winfo_height()
        window_width = root.winfo_width()

        help_button.place_configure(relx=1.0, rely=0.0, x=-10, y=10, anchor='ne')
        log_box.place_configure(relx=0, rely=1.0, x=10,y=-10, height=window_height//3, width=window_width*(4/5), anchor='sw')

    root.bind('<Configure>', update_layout)
    cat.bind('<<ComboboxSelected>>', update_commands)

    root.after(100, poll_logs)
    return root

def force_crash():
    raise RuntimeError("Intentional Tkinter crash for testing")

def hide_console():
    hwnd = ctypes.windll.kernel32.GetConsoleWindow()
    if hwnd:
        ctypes.windll.user32.ShowWindow(hwnd, SW_HIDE)

def show_console():
    hwnd = ctypes.windll.kernel32.GetConsoleWindow()
    if hwnd:
        ctypes.windll.user32.ShowWindow(hwnd, SW_SHOW)
        ctypes.windll.user32.SetForegroundWindow(hwnd)

#region: heartbeat
class HeartbeatController:
    def __init__(self, client):
        self.client = client
        self.thread = None
        self.loop = None

    def _thread_target(self):
        loop = asyncio.new_event_loop()
        asyncio.set_event_loop(loop)
        self.loop = loop
        loop.run_until_complete(self.client.run())

    def start(self):
        if self.thread and self.thread.is_alive():
            self.client.log("Heartbeat thread already running.")
            return
        
        self.thread = threading.Thread(target=self._thread_target, daemon=True)
        self.thread.start()
        self.client.log("Heartbeat thread started.")

    def stop(self, client):
        if not self.thread or not self.thread.is_alive():
            self.client.log("Heartbeat thread is not running.")
            return
    
        self.client.log("Stopping heartbeat...")
        client.heartbeat_enabled.set()  # Ensure heartbeat loop isn't waiting when we signal shutdown
        self.client.running = False

        self.loop.call_soon_threadsafe(self.client.shutdown.set)  # Signal the client to shut down
        threading.Thread(
            target=self.__join_thread,
            daemon=True
        ).start()

        while not self.client.cmd_queue.empty():
            self.client.cmd_queue.get_nowait()

    def __join_thread(self):
        self.thread.join()

        self.thread = None
        self.loop = None
        self.client.log("Heartbeat thread stopped.")

class SPMAPClient:
    def __init__(self):
        self.cmd_queue = queue.Queue()
        self.log_queue = queue.Queue()
        self.log("SPMAPClient initialized.")

        self.send_queue = None
        self.shutdown = None
        self.heartbeat_enabled = None
        self.beat = 0
        self.running = False

        self.reader = None
        self.writer = None

        self.command_box = None

    async def init_async(self):
        self.heartbeat_controller = HeartbeatController(self)
        self.send_queue = asyncio.Queue()
        self.shutdown = asyncio.Event()
        self.heartbeat_enabled = asyncio.Event()
        self.heartbeat_enabled.set()

        self.pending = {}
        self.reader = None
        self.writer = None
        self.beat = 0

    #Loops
    async def heartbeat_loop(self):
        while not self.shutdown.is_set():

            # If heartbeat is disabled, wait until enabled OR shutdown
            while not self.heartbeat_enabled.is_set() and not self.shutdown.is_set():
                enable_task = asyncio.create_task(self.heartbeat_enabled.wait())
                shutdown_task = asyncio.create_task(self.shutdown.wait())
                done, pending = await asyncio.wait(
                    {enable_task, shutdown_task},
                    return_when=asyncio.FIRST_COMPLETED
                )
                for t in pending:
                    t.cancel()

            if self.shutdown.is_set():
                break

            self.beat += 1

            # Sleep 1s, but still respond quickly to shutdown
            try:
                await asyncio.wait_for(self.shutdown.wait(), timeout=1.0)
            except asyncio.TimeoutError:
                pass
    async def writer_loop(self):
        while not self.shutdown.is_set():
            if self.writer is None:
                await asyncio.sleep(0.1)
                continue

            try:
                try:
                    packet = await asyncio.wait_for(
                        self.send_queue.get(),
                        timeout=0.25
                    )
                except asyncio.TimeoutError:
                    continue
                self.writer.write(packet)

                await self.writer.drain()
                self.log(f"Sent command {packet.hex()}")

            except Exception as e:
                self.log(f"Writer error: {e}")

                await self.heartbeat_controller.stop(self)
                await self.reset_connection_state()

    async def reader_loop(self):
        while not self.shutdown.is_set():

            if self.reader is None:
                await asyncio.sleep(0.1)
                continue

            try:
                header = await asyncio.wait_for(self.reader.readexactly(4), timeout=2.0)
                cmdID, length = struct.unpack(">HH", header)

                payload_size = length - 4
                payload = await asyncio.wait_for(self.reader.readexactly(payload_size), timeout=2.0)

                self.log(
                f"Received packet cmd={cmdID:04X} len={length}\n"
                )

                future = self.pending.pop(cmdID, None)

                if future:
                    """This is where client-initiated packet responses are handled"""
                    future.set_result(payload)

                    if cmdID == 0xFFFF: #help response
                        self.log(f"Help message (arg(s))\n{payload.decode(errors='ignore')}")
                    
                    elif cmdID == 0x0000: #rIdx response
                        idx = struct.unpack(">I", payload)[0]
                        self.log(f"Current item index: {idx}")
                    elif cmdID == 0x0001: #rBusy response
                        busy_state = struct.unpack(">I", payload)[0]
                        self.log(f"Current busy state: {busy_state} ({toDebug(busy_state)})")

                    else:
                        self.log(f"\n{payload.decode()}\n")

                else:
                    """This is where non-client-initiated packets would be handled"""
                    packetString = payload.decode(errors='ignore') if payload else ''
                    self.log(
                        f"Unexpected packet {cmdID:04X}\n{packetString}"
                    )
            except asyncio.TimeoutError:
                continue

            except asyncio.IncompleteReadError:
                self.log("Connection closed by Wii.")
                await self.heartbeat_controller.stop(self)
                await self.reset_connection_state()

            except Exception as e:
                self.log(f"Reader error: {e}")

                await self.heartbeat_controller.stop(self)
                await self.reset_connection_state()

    async def state_machine(self, packet=None):
        while not self.shutdown.is_set():
            try:
                packet = self.cmd_queue.get_nowait()
            except queue.Empty:
                await asyncio.sleep(0.1)  # Avoid busy waiting
                continue

            try:
                self.log(f"Processing command from GUI: {packet.hex()}")
                cmdID = struct.unpack(">B", packet[:1])[0]
                expect_response = True

                await self.send_command(packet, expect_response=expect_response)
            except Exception as e:
                self.log(f"Error processing command: {e}")

    async def connection_loop(self):
        self.running = True
        while not self.shutdown.is_set():
            try:
                self.log("Connecting to Wii..")

                self.reader, self.writer = await asyncio.open_connection(
                    HOST,
                    PORT
                )

                self.log("Connected to Wii")

                while not self.shutdown.is_set():
                    await asyncio.sleep(1)

            except Exception as e:

                self.log(f"Connection lost: {e}")

                await asyncio.sleep(2)

            finally:
                if self.writer:
                    self.log("Closing connection")

                    self.writer.close()
                    await self.writer.wait_closed()

                await self.reset_connection_state()
                self.running = False

    #Commands
    async def send_command(self, packet, expect_response=False, timeout=5.0):
        cmdID = struct.unpack(">H", packet[:2])[0]
        future = None
        if expect_response:
            future = asyncio.get_running_loop().create_future()
            self.pending[cmdID] = future

        await self.send_queue.put((packet))

        if not expect_response:
            return None

        try:
            return await asyncio.wait_for(future, timeout=timeout)
        except asyncio.TimeoutError:
            self.pending.pop(cmdID, None)
            self.log(f"Timeout waiting for response to {cmdID:04X}")
            return None
    
    def handle_response(self, packet):
        header = struct.unpack(">BB", packet[:2])
        cat = header[0]
        cmd = header[1]

        if cat == 0x00: #reader category
            if cmd == 0x00: #rIdx response
                self.log(f"Sent rIdx command... ")
            elif cmd == 0x01: #rBusy response
                self.log(f"Sent rBusy command...")

        else:
            self.log(f"Received response for unknown cmd {cmd}: {packet.hex()}")

    def log(self, msg):
        timestamp = time.strftime("%Y-%m-%d %H:%M:%S", time.localtime())
        self.log_queue.put(f"[{timestamp}] {msg}")

    async def run(self):
        await self.init_async()
        try:
            async with asyncio.TaskGroup() as tg:
                tg.create_task(self.heartbeat_loop())
                tg.create_task(self.writer_loop())
                tg.create_task(self.reader_loop())
                tg.create_task(self.state_machine())
                tg.create_task(self.connection_loop())
        except* asyncio.CancelledError:
            print("Tasks cancelled, shutting down.")
            pass
        except* Exception as e:
            print(f"Unexpected error in client run loop: {e!r}")
            self.shutdown.set()  # Ensure shutdown on unexpected errors

    async def reset_connection_state(self):
        self.log("Resetting connection state")

        # Drop reader/writer
        self.reader = None
        self.writer = None

        # Clear send queue
        while not self.send_queue.empty():
            try:
                self.send_queue.get_nowait()
            except asyncio.QueueEmpty:
                break

        # Cancel pending futures
        for cmd, future in self.pending.items():
            if not future.done():
                future.cancel()

        self.pending.clear()

class Reader(IntEnum):
    CMD_rIDX = 0
    CMD_rBUSY = 1

class busyEnum(IntEnum):
    NOT_BUSY = 0
    SCENE_BUSY = 1
    BUSY = 2

def toDebug(value: int) -> str:
    if value == 0:
        return "Not busy"
    else:
        return "Busy"
    
def restart_program():
    print("Restarting script..\n")
    time.sleep
    os.execv(sys.executable, [sys.executable] + sys.argv)

def main():
    while loop:

        timer = 3
        index = 0
        root = start_gui() #heartbeat logic will have to be integrated into the GUI for full functionality

        try:
            root.mainloop()

            if getattr(root, "_fatal_error", None):
                exc, val, tb = root._fatal_error
                raise val.with_traceback(tb)

            print("GUI closed normally")
            break

        except Exception:

            show_console()

            import traceback
            print("GUI error:")
            traceback.print_exc()

            for i in range(index, timer):
                print(f"Restarting in {timer-i}...")
                time.sleep(1)
                index += 1

            restart_program()

def load_font(path):
    FR_PRIVATE = 0x10
    FR_NOT_ENUM = 0x20

    ctypes.windll.gdi32.AddFontResourceExW(
        path,
        FR_PRIVATE,
        0
    )

if __name__ == "__main__":
    os.system('cls' if os.name == 'nt' else 'clear')
    #hide_console() #NOTE: console hide command
    loop = True

    try:
        import ctypes
        from ctypes import wintypes
        load_font("./fonts/PaperMarioFont.ttf")
    except Exception as e:
        print(f"Failed to load font..")

    main()