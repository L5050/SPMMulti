import os
import ctypes
from re import L
import struct
import time
import sys
import asyncio
import threading
import queue
from tkinter import * #type: ignore
from tkinter.ttk import * #type: ignore
from enum import IntEnum

# my beloved
import traceback

#region breakdown
# =============================================================================
# SPMAP NETWORK PIPELINE
#
#   Tkinter / Main Thread
#          |
#          | creates packets / reads log_queue
#          v
#      cmd_queue  -------------------------------+
#          |                                     |
#          |                                     |
#          v                                     |
#   +------------------- ASYNC WORKER THREAD -------------------+
#   |                                                           |
#   |  HeartbeatController                                      |
#   |    -> owns worker thread + asyncio event loop             |
#   |    -> starts/stops SPMAPClient.run()                      |
#   |                                                           |
#   |  SPMAPClient TaskGroup                                    |
#   |                                                           |
#   |    connection_loop                                        |
#   |      -> opens/closes TCP connection                       |
#   |      -> provides self.reader / self.writer                |
#   |                                                           |
#   |    writer_loop                                            |
#   |      cmd_queue -> writer.write() -> Wii                   |
#   |                                                           |
#   |    reader_loop                                            |
#   |      Wii -> [u16 length][payload] -> log / handler        |
#   |                                                           |
#   |    heartbeat_loop                                         |
#   |      -> tracks async lifetime / shutdown state            |
#   |                                                           |
#   +-----------------------------------------------------------+
#                          |
#                          v
#                      log_queue
#                          |
#                          v
#                     Tkinter GUI
#
#   shutdown Event -> shared stop signal for all async loops
# =============================================================================

#region const
""" 
    Localhost HOST and PORT to sync with game
    Const values for hiding CMD window upon opening TK window
    Geometry for what size to open window at     
"""
HOST = "127.0.0.1"
PORT = 5555

SW_HIDE = 0
SW_SHOW = 5

geometry = "800x600"



#region tkinter GUI
def start_gui():
    """
        barebones async-threaded GUI with send/recv loop handler 
    """

    """ Custom callback to catch exceptions in the Tkinter main loop and exit gracefully """
    def report_callback_exception(exc, val, tb):
        root._fatal_error = (exc, val, tb) #type: ignore
        if hasattr(root, "poll_id"):
            root.after_cancel(root.poll_id) #type: ignore
        root.destroy()  # Exit the main loop


    """ Enables log box, adds line + \n, ends, and disables """
    def append_log(msg):
        log_box.config(state='normal')
        log_box.insert('end', msg + "\n")
        log_box.see('end')
        log_box.config(state='disabled')


    """ Clears log box """
    def clear_log():
        log_box.config(state='normal')
        log_box.delete('1.0', 'end')
        log_box.config(state='disabled')


    """ Non-blocking log polling helper; 
    dumps queue to the log in order, to allow multiple 
    to be made simultaneously without losing logs 
    """
    def poll_logs():
        try:
            while True:
                line = client.log_queue.get_nowait()
                append_log(line)
        except queue.Empty:
            pass

        #run again in 100 ms
        root.poll_id = root.after(100, poll_logs)  #type: ignore 


    """ Universal packet setup to ensure that struct creation is handled in one place """
    def setup_packet():
        try:
            #5 byte magic + payload (index, id)
            packet = struct.pack(">5s", "SPMAP".encode())
            """ 
                The payloads being sent to the game are the 5 digit Magic Value
                to verify packets, a half for the item index, and a byte for the item id 
            """
            payload = struct.pack(f">HB", 1500, 13)

            return packet + payload

        except Exception as e:
            print(e)

    """ Simple wrapper to ensure connection and create/queue packet; packet preview on attempt """
    def commandHandler():
        packet = None

        if client.running:
            if packet == None:
                packet = setup_packet()

                append_log(f"Packet: {packet}")

            if packet:
                client.cmd_queue.put(packet)
                client.log(f"Queueing command: {packet.hex()}")
                append_log(f"Queued command: \"AP\"")
            else:
                return
        else:
            packet = setup_packet()
            append_log(f"Packet: {packet}")
            
            append_log(f"Client is not alive, cannot queue command: \"AP\"")



    """ Main() """
    root = Tk()
    root.geometry(geometry)
    client = SPMAPClient() # client instance to sync GUI and heartbeat state
    heartbeat_controller = HeartbeatController(client)
    root.title("SPMAP Client GUI")

    root.report_callback_exception = report_callback_exception

    """ Window-Operator Instancing """
    #essentials
    restartApp = Button(text="Restart (Test)", command=force_crash)
    quitApp = Button(text="Quit (test)", command=quit)
    hbStart = Button(text="Start Heartbeat Thread", command=heartbeat_controller.start)
    hbStop = Button(text="Stop Heartbeat Thread", command=lambda: heartbeat_controller.stop(client))

    #user-side
    log_box = Text(root, height=10, state='disabled')
    testCom = Button(text="Send Command", command=lambda: commandHandler())
    clear = Button(text="Clear Log", command=clear_log)

    """ Operator Placement """
    #essentials
    restartApp.place(relx=0.0, rely=0.0, anchor='w', x=5, y=30)
    quitApp.place(relx=0.0, rely=0.0, anchor='w', x=5, y=60)
    hbStart.place(relx=0.0, rely=0.0, anchor='w', x=5, y=90)
    hbStop.place(relx=0.0, rely=0.0, anchor='w', x=5, y=120)

    #user-side
    testCom.place(relx=0.0, rely=0.0, anchor='w', x=5, y=150)
    clear.place(relx=0.0, rely=1.0, anchor='sw', x=10, y=-10)


    """ Dynamically update the layout of the log box based on the current window size """
    def update_layout(event=None):
        window_height = root.winfo_height()
        window_width = root.winfo_width()

        log_box.place_configure(relx=0, rely=1.0, x=10,y=-20, height=window_height//3, width=window_width*(4/5), anchor='sw')

    # <Configure> means any window movement / resizing; keeping size and log-box consistent ratio-wise
    root.bind('<Configure>', update_layout)

    # Poll logs to write every ~100 ms
    root.after(100, poll_logs)
    return root

""" Used for iterative restart-testing; allows script reload with 1 click """
def force_crash():
    raise RuntimeError("Intentional Tkinter crash for testing")


""" hide/show console | used to hide cmd window when opening with cmd (win64) """
def hide_console():
    hwnd = ctypes.windll.kernel32.GetConsoleWindow()
    if hwnd:
        ctypes.windll.user32.ShowWindow(hwnd, SW_HIDE)

def show_console():
    hwnd = ctypes.windll.kernel32.GetConsoleWindow()
    if hwnd:
        ctypes.windll.user32.ShowWindow(hwnd, SW_SHOW)
        ctypes.windll.user32.SetForegroundWindow(hwnd)

#region heartbeat
""" Controls the worker thread / asyncio event loop that runs SPMAPClient """
class HeartbeatController:
    def __init__(self, client):
        self.client = client
        self.thread = None
        self.loop = None

    """ Worker-thread entry point; creates/stores its asyncio loop and runs the client """
    def _thread_target(self):
        loop = asyncio.new_event_loop()
        asyncio.set_event_loop(loop)
        self.loop = loop
        loop.run_until_complete(self.client.run())

    """ Asyncio main thread startup / stop """
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

        self.loop.call_soon_threadsafe(self.client.shutdown.set)  #type: ignore | Signal the client to shut down
        threading.Thread(
            target=self.__join_thread,
            daemon=True
        ).start()

        while not self.client.cmd_queue.empty():
            self.client.cmd_queue.get_nowait()

    """ 
        separated tiny thread uses this to join post-shutdown;
        keeps the TK window running smooth even if shutdown delayed
    """
    def __join_thread(self):
        self.thread.join() #type: ignore

        self.thread = None
        self.loop = None
        self.client.log("Heartbeat thread stopped.")

"""
    Top-level network client

    Created / owned by Tkinter main thread
    Executed inside HeartbeatController's asyncio worker thread

    Owns shared network state and spawns the async TaskGroup loops
"""
#region SPMAP
class SPMAPClient:
    def __init__(self):
        self.cmd_queue = queue.Queue()
        self.log_queue = queue.Queue()
        self.log("SPMAPClient initialized.")

        self.shutdown = None
        self.heartbeat_enabled = None
        self.beat = 0
        self.running = False

        self.reader = None
        self.writer = None

        self.command_box = None

    """ Create management queues and events """
    async def init_async(self):
        self.shutdown = asyncio.Event()
        self.heartbeat_enabled = asyncio.Event()
        self.heartbeat_enabled.set()

        self.reader = None
        self.writer = None
        self.beat = 0

    #region hb loop
    """ Overhead loop, handles global shutdown call """
    async def heartbeat_loop(self):
        if not self.shutdown or not self.heartbeat_enabled:
            raise Exception("[SPMAPClient] [heartbeat_loop] self.shutdown or self.heartbeat_enabled missing? FATAL")

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

    """
        Packet writing loop

        If conditions are met, pushes packet to Wii with self.writer.write(packet)
        |
        |   self.writer = open asyncio connection()
    """
    #region write loop
    async def writer_loop(self):
        if not self.shutdown:
            raise Exception("[SPMAPClient] [writer_loop] self.shutdown missing? FATAL")

        # break on shutdown
        while not self.shutdown.is_set():

            # continue on empty
            if self.writer is None:
                await asyncio.sleep(0.1)
                continue

            # overhead error catch
            try:

                # packet-write error catch
                try:
                    packet = self.cmd_queue.get_nowait()

                # pass on empty
                except queue.Empty:
                    await asyncio.sleep(0.05)
                    continue

                self.writer.write(packet)
                await self.writer.drain()

            except Exception as e:
                self.log(f"Writer error: {e}")
                self.shutdown.set() #type: ignore

    """
        Packet receiving loop

        Reads a 2-byte payload length, then reads exactly that many payload bytes.
        Currently forwards decoded payloads to the GUI log.
    """
    #region read loop
    async def reader_loop(self):
        if not self.shutdown:
            raise Exception("[SPMAPClient] [reader_loop] self.shutdown missing? FATAL")

        # break on shutdown
        while not self.shutdown.is_set():

            # continue on empty
            if self.reader is None:
                await asyncio.sleep(0.1)
                continue

            # read packet
            try:
                payloadLen = int.from_bytes(await asyncio.wait_for(self.reader.readexactly(2), timeout=2.0))

                payload = await asyncio.wait_for(self.reader.readexactly(payloadLen), timeout=2.0)

                self.log(f"[Packet] {payload.decode()}")
                    

            except asyncio.TimeoutError:
                continue

            except asyncio.IncompleteReadError:
                self.log("Connection closed by Wii.")
                self.shutdown.set() #type: ignore

            except Exception as e:
                self.log(f"Reader error: {e}")

                self.shutdown.set() #type: ignore
 
    """
        TCP connection lifecycle

        Opens reader/writer streams, keeps the connection alive until shutdown,
        then closes the stream and clears connection state.
    """
    #region conn loop
    async def connection_loop(self):
        if not self.shutdown:
            raise Exception("[SPMAPClient] [connection_loop]: self.shutdown missing | FATAL")
        
        self.running = True
        # Break if shutdown
        while not self.shutdown.is_set():

            #try connection
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

            # cleanup on close
            finally:
                if self.writer:
                    self.log("Closing connection")

                    self.writer.close()
                    await self.writer.wait_closed()

                await self.reset_connection_state()
                self.running = False


    #region log
    def log(self, msg):
        timestamp = time.strftime("%Y-%m-%d %H:%M:%S", time.localtime())
        self.log_queue.put(f"{msg}")

    async def run(self):
        await self.init_async()
        try:
            async with asyncio.TaskGroup() as tg:
                tg.create_task(self.heartbeat_loop(), name="heartbeat_loop")
                tg.create_task(self.writer_loop(), name="writer_loop")
                tg.create_task(self.reader_loop(), name="reader_loop")
                tg.create_task(self.connection_loop(), name="connection_loop")

        except* asyncio.CancelledError:
            print("Tasks cancelled, shutting down.")

        except* Exception as eg:
            print("Unexpected error in client run loop:")
            traceback.print_exception(eg)
            self.shutdown.set()  #type: ignore | Ensure shutdown on unexpected errors

    async def reset_connection_state(self):
        self.log("Resetting connection state")

        # Drop reader/writer
        self.reader = None
        self.writer = None

#region client-end


""" Simple restart func to effectively reload script | Iterative testing my beloved """    
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
                exc, val, tb = root._fatal_error #type: ignore
                raise val.with_traceback(tb)

            print("GUI closed normally")
            break

        except Exception:

            show_console()

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
    hide_console() #NOTE: console hide command
    loop = True

    main()