import os
import random
import socket
import struct
import json
import base64
import time
import sys
import keyboard
from enum import IntEnum

try:
    from . import items # helper formatted from item_data_ids.h
except ImportError:
    import items #for testing outside of package

HOST = "127.0.0.1"
PORT = 5555
TEST = False
heartbeat_running = True

default_settings = {
    "idx": 0,
    "item_list": [] # list of {"idx": <int>, "item_id": <int>}
}

def pause_heartbeat():
    global heartbeat_running
    heartbeat_running = False
    print("Heartbeat paused.")
#keyboard.add_hotkey('esc', pause_heartbeat)

def resume_heartbeat():
    global heartbeat_running
    heartbeat_running = True
    print("Heartbeat resumed.")
#keyboard.add_hotkey('tab', resume_heartbeat)

emptyPackets = {
    'prevPacket': {
    },
    'current_packet': {
        "CMD_ID": "0000",
        "CMD_Len": "0000",
    },
    'response': {},
    'idx': 0,

    'busy': False,
    'item_list': [] # tuples of [idx, item_id]
}

current_packet = {
    "CMD_ID": "0000",
    "CMD_Len": "0000",
}

def recv_exact(sock, size: int) -> bytes:
    data = b""
    while len(data) < size:
        try:
            chunk = sock.recv(size - len(data))
            if not chunk:
                raise RuntimeError("Socket closed while receiving data")
            data += chunk
        except socket.timeout:
            pause_heartbeat()
            raise RuntimeError("Socket timeout while receiving data")
    return data

class Cmd(IntEnum):
    CMD_ITEM = 1
    CMD_IDX = 2
    CMD_rIDX = 3
    CMD_rBUSY = 4

class busyEnum(IntEnum):
    NOT_BUSY = 0
    SCENE_BUSY = 1
    BUSY = 2

def toDebug(value: int) -> str:
    if value == 0:
        return "FAILURE / RESPONSE TOO BIG [FALLBACK]"
    elif value == 1:
        return "SUCCESS"
    elif value == 8:
        return f"DUPLICATE/OUT-OF-ORDER"
    elif value == 9:
        return f"BUSY"
    else:
        return f"UNKNOWN({value})"

def test(cmd: str, var1: int = 0, var2: int = 0):
    with open("testpackets.json", "r") as f:
        data = json.load(f)

    if cmd == "item":
        item_id = var1
        idx = var2
        payload = struct.pack(">IH", idx & 0xFFFFFFFF, item_id & 0xFFFF)
        #idxcheck = (read_test_packet("idx"))
        state = write_to_test_packet(data, Cmd.CMD_ITEM, payload)
    elif cmd == "idx":
        idx = var1
        payload = struct.pack(">I", idx & 0xFFFFFFFF)
        state = write_to_test_packet(data, Cmd.CMD_IDX, payload)
    elif cmd == "ridx":
        resp = read_test_packet(cmd)
        state = write_to_test_packet(data, Cmd.CMD_rIDX, b"")
        print(resp)
        return
    elif cmd == "rbusy":
        resp = read_test_packet(cmd)
        state = write_to_test_packet(data, Cmd.CMD_rBUSY, b"")
        print(resp)
        return

    resp = read_test_packet(cmd)
    print(resp)
    print(state)

def write_to_test_packet(data, cmd_id: int, payload: bytes = b"") -> bytes:
    # Simulate a server response for testing purposes
    packets = {
        'prevPacket': {},
        'current_packet': {},
        'response': {},
        'idx': 0,

        'busy': False,
        'item_list': [] # tuples of [idx, item_id]
        }

    with open("testpackets.json", "w") as f:
        preserve_packets_on_assertion = data
        packet_idx = data.get('idx', 0)
        busy_state = data.get('busy', False)
        item_list = data.get('item_list', [])
        if cmd_id == Cmd.CMD_rIDX:
            packet_idx = data.get('idx', 0)
            packets['idx'] = packet_idx

            packets['busy'] = busy_state
            packets['item_list'] = item_list
        elif cmd_id == Cmd.CMD_rBUSY:
            busy_state = data.get('busy', False)
            packets['busy'] = busy_state

            packets['idx'] = packet_idx
            packets['item_list'] = item_list
        elif cmd_id == Cmd.CMD_IDX:
            packet_idx = int.from_bytes(payload[:4], byteorder="big")
            packets['idx'] = packet_idx
        elif cmd_id == Cmd.CMD_ITEM:
            idx = data.get('idx', 0)
            packet_idx = int.from_bytes(payload[:4], byteorder="big")
            item_id = int.from_bytes(payload[4:6], byteorder="big")
            print(f"STORED SERVER IDX:{idx}")
            print(f"STORED PACKET IDX:{packet_idx}")
            if packet_idx != idx + 1:
                json.dump(preserve_packets_on_assertion, f, indent=4) #assertion without overwriting
                assert(packet_idx == idx + 1), f"IDX mismatch: expected {idx + 1}, got {packet_idx}"
                return 1
            if data.get('busy', False):
                json.dump(preserve_packets_on_assertion, f, indent=4) #assertion without overwriting
                return 0

            packets['idx'] = idx + 1  # increment idx for each item command
            item_list = data.get('item_list', [])
            item_list.append((packet_idx, item_id))
            packets['item_list'] = item_list

        packets['prevPacket'] = data['current_packet']        
        payload_len = len(payload)
        current_packet["CMD_ID"] = f"{cmd_id:04X}"
        current_packet["CMD_Len"] = f"{4 + payload_len:04X}"

        if cmd_id != Cmd.CMD_rIDX and cmd_id != Cmd.CMD_rBUSY:
            current_packet["PAYLOAD"] = payload[:payload_len].hex()
            current_packet["WHOLE_PACKET"] = f"{cmd_id:04X}{4 + payload_len:04X}{payload.hex()}"

        packets['current_packet'] = current_packet
        json.dump(packets, f, indent=4)
        return 1

def read_test_packet(cmd: str) -> bytes:
    # Read the simulated server response from the test packet file
    with open("testpackets.json", "r") as f:
        data = json.load(f)

        if cmd == "ridx":
            cmd_id = Cmd.CMD_rIDX
            idx = bytes.fromhex(data.get("current_packet", {}).get("PAYLOAD", "00000000")[:8])  # First 4 bytes of payload for idx
            cmd_idx = int.from_bytes(idx, byteorder="big")
            return f"\nCommand ID: {Cmd(cmd_id)}\nidx: {cmd_idx}"
        
        if cmd == "rbusy":
            cmd_id = Cmd.CMD_rBUSY
            busy = data.get('busy', False)
            return f"\nCommand ID: {Cmd(cmd_id)}\nbusy: {busy}"

        command_id = bytes.fromhex(data.get("current_packet", {}).get("CMD_ID", "0000"))
        command_len = bytes.fromhex(data.get("current_packet", {}).get("CMD_Len", "0000"))
        idx = bytes.fromhex(data.get("current_packet", {}).get("PAYLOAD", "00000000")[:8])  # First 4 bytes of payload for idx

        cmd_id_int = int.from_bytes(command_id, byteorder="big")
        cmd_len_int = int.from_bytes(command_len, byteorder="big")
        cmd_idx = int.from_bytes(idx, byteorder="big")

        if cmd == "item":
            assert cmd_id_int == Cmd.CMD_ITEM, "CMD_ID does not match CMD_ITEM"
            item_id = bytes.fromhex(data.get("current_packet", {}).get("PAYLOAD", "00000000")[8:12])  # Next 2 bytes for itemId
            item_id_int = int.from_bytes(item_id, byteorder="big")
            return f"\nCommand ID: {Cmd(cmd_id_int)}\nCommand Length: {cmd_len_int}\nidx: {cmd_idx}\nitem_id: {item_id_int}"
        elif cmd == "idx":
            assert cmd_id_int == Cmd.CMD_IDX, "CMD_ID does not match CMD_IDX"
            return f"\nCommand ID: {Cmd(cmd_id_int)}\nCommand Length: {cmd_len_int}\nidx: {cmd_idx}"
        else:
            return "Unknown command"

def heartbeat(item_list = None):
    if item_list:
        for item_tuple in item_list:
            item_id = item_tuple[1]
            idx = item_tuple[0]
            resp = call_item_command(item_id, idx)
            print(resp)
        return
    while heartbeat_running:
        try:
            print("\n======================================== Sending heartbeat ========================================\n    (Ctrl+C to stop) (esc to pause) (tab to resume)\n")
            server_idx_resp = ridxcmd()
            server_idx = struct.unpack(">I", server_idx_resp)[0]
            server_busy_resp = rbusycmd()
            server_busy = struct.unpack(">B", server_busy_resp)[0]
            server_busy = busyEnum(server_busy)
            if server_busy == busyEnum.NOT_BUSY:
                #time.sleep(1)  #delay before sending item
                resp = call_item_command(random.randint(0, len(items.ITEM_ID_TO_NAME) - 1), server_idx + 1)
                print(resp)
            print(f"\nHeartbeat Running\n    Server idx: {server_idx}\n    Server busy: {server_busy.name}")
        except Exception as e:
            print("Heartbeat error:", e)
        time.sleep(1)  # heartbeat interval

def getP(cmd_id: int) -> str:
    if cmd_id == Cmd.CMD_ITEM:
        return "Item-Send Command"
    elif cmd_id == Cmd.CMD_IDX:
        return "Idx-Set Command"
    elif cmd_id == Cmd.CMD_rIDX:
        return "Idx Check"
    elif cmd_id == Cmd.CMD_rBUSY:
        return "Business Check"
    else:
        return "UNKNOWN_CMD"

def send_packet(cmd_id: int, payload: bytes = b"") -> bytes:
    packet_len = 4 + len(payload)
    header = struct.pack(">HH", cmd_id, packet_len)
    packet = header + payload

    #print(f"=================================================\nPacket: {packet.hex(" ")}  Packet Check: {getP(cmd_id)}     \n=================================================")
    if cmd_id == Cmd.CMD_ITEM:
        item_id = int.from_bytes(payload[4:6], byteorder="big")
        print(f"Item ID: {item_id} ({items.item_id_to_name(item_id)})")

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.settimeout(0.5)
        s.connect((HOST, PORT))
        s.sendall(packet)

        if cmd_id == Cmd.CMD_rIDX:
            return recv_exact(s, 4)   # read raw u32
        if cmd_id == Cmd.CMD_ITEM:
            return recv_exact(s, 2)   # read raw u16 (itemState)
        if cmd_id == Cmd.CMD_rBUSY:
            return recv_exact(s, 1)   # read raw bool (1 byte)

        # 1) read string length
        raw_len = recv_exact(s, 4)
        msg_len = struct.unpack(">I", raw_len)[0]

        # 2) read string bytes
        if msg_len == 0:
            return b""

        return recv_exact(s, msg_len)
    
def item(item_id: int, idx: int = 0) -> bytes:
    # payload for CMD_ITEM: u16 idx, u16 itemId
    payload = struct.pack(">IH", idx & 0xFFFFFFFF, item_id & 0xFFFF)
    print(payload.hex(" "))
    return send_packet(Cmd.CMD_ITEM, payload)

def idxcmd(idx: int) -> bytes:
    payload = struct.pack(">I", idx & 0xFFFFFFFF)
    print(payload.hex(" "))
    return send_packet(Cmd.CMD_IDX, payload)

def ridxcmd() -> bytes:
    return send_packet(Cmd.CMD_rIDX, b"")

def rbusycmd() -> bytes:
    return send_packet(Cmd.CMD_rBUSY, b"")

def call_item_command(item_id: int, idx: int = 1):
    if TEST:
        test("item", item_id, idx)
    else:
        resp = item(item_id, idx)
        print("response:", toDebug(struct.unpack(">H", resp)[0]))
        return resp

if __name__ == "__main__":
    if TEST:
        print("\n     ===============\n     TEST mode is ON\n     ===============")

    if len(sys.argv) < 2:
        if TEST:
            with open("testpackets.json", "w") as f:
                json.dump(emptyPackets, f, indent=4)
            print("Wrote testpackets.json")
        else:
            print("Usage: <item|idx> [args...] -- TEST mode is off")
        sys.exit(1)

    cmd = sys.argv[1].lower()

    if cmd == "item":
        if len(sys.argv) < 3:
            print("Usage: item <itemid> [idx]")
            sys.exit(1)

        item_id = int(sys.argv[2], 10)
        idx = int(sys.argv[3], 10) if len(sys.argv) >= 4 else 1

        call_item_command(item_id, idx) #needs to be separate for queue logic

    elif cmd == "idx":
        if len(sys.argv) < 3:
            print("Usage: idx <idx>")
            sys.exit(1)
        idx = int(sys.argv[2], 10)

        if TEST:
            test(cmd, idx)
        else:
            resp = idxcmd(idx)
            print("response:", resp.decode("utf-8", errors="replace"))

    elif cmd == "ridx":
        if TEST:
            test(cmd)
        else:
            resp = ridxcmd()
            idx = struct.unpack(">I", resp)[0]
            print("Server idx:", idx)

    elif cmd == "rbusy":
        if TEST:
            test(cmd)
        else:
            resp = rbusycmd()
            busy = struct.unpack(">B", resp)[0]
            busy = busyEnum(busy)
            print("Server busy-state:", busy.name)


    #NOTE: STILL VERY-MUCH WIP
    elif cmd == "heartbeat":
        directory = os.path.dirname(os.path.abspath(__file__))
        itemlist_name = "item-list.json"
        try:
            with open("client-memory.txt", "r") as f:
                item_path = f.read().strip()
                print(f"Loaded item list path from client-memory.txt: {item_path}")
        except FileNotFoundError:
            print("client-memory.txt not found. How would you like to proceed?\n[NOTE] to set it manually, create a `client-memory.txt` file in the script directory, leading to your item list to create/load\n[1] Create default path (Script Directory)\n[2] Custom Directory\n[3] Exit")
            response = input()
            if response == "1":
                item_path = os.path.dirname(os.path.abspath(__file__))
                with open("client-memory.txt", "w") as f:
                    f.write(f"{os.path.join(item_path, itemlist_name)}")
                print(f"Logged {itemlist_name} at path: {item_path}")
            elif response == "2":
                response = input("Enter directory path: (Folder for default name, file for custom name/load existing)")
                
                if response.strip().upper().startswith("./"):
                    response = os.path.join(directory, response[2:])

                if not response.strip().lower().endswith(".json"):
                    print("Assuming default name 'item-list.json'")
                    response = os.path.join(response, itemlist_name)
                
                with open("client-memory.txt", "w") as f:
                    f.write(f"{response}")
                
            elif response == "3":
                print("Exiting.")
                sys.exit(0)

        """heartbeat_running = True
        resp = idxcmd(0)
        print("response:", resp.decode("utf-8", errors="replace"))
        try:
            print("Starting heartbeat. Press Ctrl+C to stop.")
            heartbeat()
        except KeyboardInterrupt:
            print("Heartbeat stopped by user.")"""

    else:
        print("Unknown command")
        sys.exit(1)