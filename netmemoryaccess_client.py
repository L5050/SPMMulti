import socket
import json
import base64
import time
import sys

def sendData(cmd):
    cmd += '\n' * (4 - (len(cmd) % 4))
    fd = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    fd.connect(("127.0.0.1", 5555))
    fd.sendall(cmd.encode())

    data = fd.recv(1024)

    fd.close()
    return data

def read(addr, size):
    return sendData(f"read {addr:x} {size}")

def item(itemid):
    return sendData(f"item {itemid}")

def write(addr, data: bytes):
    cmd = f"write {addr:x} {base64.b64encode(data).decode()} {len(data)}"
    print(cmd)
    return sendData(cmd)

def msgbox(message: str):
    cmd = f"msgbox {base64.b64encode(message.encode()).decode()} {len(message)}"
    print(cmd)
    return sendData(cmd)


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: read/write/msgbox ...")
        sys.exit(1)

    cmd = sys.argv[1]

    if cmd == "read":
        addr = int(sys.argv[2], 16)
        size = int(sys.argv[3])
        print(read(addr, size))

    elif cmd == "write":
        addr = int(sys.argv[2], 16)
        data = sys.argv[3].encode()
        print(write(addr, data))

    elif cmd == "item":
        itemid = int(sys.argv[2])
        print(item(itemid))

    elif cmd == "msgbox":
        message = sys.argv[2]
        print(msgbox(message))

    else:
        print("Unknown command")