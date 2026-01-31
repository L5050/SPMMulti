import socket
import struct
import json
import base64
import time
import sys
from enum import IntEnum

import items # helper formatted from item_data_ids.h

HOST = "127.0.0.1"
PORT = 5555

class Cmd(IntEnum):
    CMD_ITEM = 1
    # has potential to setup for more commands later
    # CMD_READ = 2
    # CMD_WRITE = 3
    # CMD_MSGBOX = 4

def send_packet(cmd_id: int, payload: bytes = b"", recv_size: int = 1024) -> bytes:
    #packet is 8 total bytes: header(4) + payload(x)
    # header: u16 cmd_id, u16 (total) packet_len
    # item payload: varies by command
    packet_len = 4 + len(payload)
    if packet_len > 0xFFFF:
        raise ValueError(f"packet too large: {packet_len}")

    header = struct.pack(">HH", cmd_id, packet_len)
    packet = header + payload

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.connect((HOST, PORT))
        s.sendall(packet)
        return s.recv(recv_size)

def item(item_id: int, idx: int = 0) -> bytes:
    # payload for CMD_ITEM: u16 idx, u16 itemId
    payload = struct.pack(">HH", idx & 0xFFFF, item_id & 0xFFFF)
    return send_packet(Cmd.CMD_ITEM, payload)

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: item <itemid> [idx]")
        sys.exit(1)

    cmd = sys.argv[1].lower()

    if cmd == "item":
        if len(sys.argv) < 3:
            print("Usage: item <itemid> [idx]")
            sys.exit(1)

        item_id = int(sys.argv[2], 10)
        idx = int(sys.argv[3], 10) if len(sys.argv) >= 4 else 0

        resp = item(item_id, idx)
        print("response:", resp)

    else:
        print("Unknown command")
        sys.exit(1)