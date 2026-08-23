#!/usr/bin/env python3
"""
端到端验证：规则引擎在网关内真实触发。
场景：
  1. 模拟设备 A(风扇 0x00F00002)、设备 B(传感器 0x00F00005) 连接 server
  2. 设备 B 上报 temp=35 -> 应触发"高温开风扇"，服务端主动向 A 下发 CMD_SET_FAN=0x0708(1800rpm)
  3. 设备 B 上报 temp=20 -> 应触发"低温关风扇"，服务端下发 CMD_SET_FAN=0x0000(关)
  4. 设备 B 上报 power=30 -> 触发"功耗高告警"（仅日志，不实际下发）

协议帧（大端）: magic(AA55) ver(1) len(2) type(1) dev_id(4) seq(2) payload(...) crc16(2)
CRC16-CCITT(0x1021, init 0xFFFF)，覆盖帧头+payload。
用法:
  python3 test/e2e_rule_engine.py [server_port]     默认 8888
"""
import socket, struct, sys, time

HOST = "127.0.0.1"
PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8888

CMD_REGISTER     = 0x01
CMD_REGISTER_ACK = 0x02
CMD_HEARTBEAT    = 0x03
CMD_SET_FAN      = 0x05
CMD_DATA_REPORT  = 0x07
CMD_SET_ACK      = 0x08

FAN_DEV    = 0x00F00002
SENSOR_DEV = 0x00F00005


def crc16(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


def pack(typ: int, dev: int, seq: int, payload: bytes = b"") -> bytes:
    head = struct.pack(">HBHBIH", 0xAA55, 1, 12 + len(payload), typ, dev, seq)
    body = head + payload
    return body + struct.pack(">H", crc16(body))


def recv_frame(sock, expect_cmd, timeout=3.0):
    """读取一帧并校验，返回 (dev, seq, payload)"""
    sock.settimeout(timeout)
    buf = b""
    while True:
        if len(buf) >= 6:
            magic, ver, flen, typ = struct.unpack(">HBHB", buf[:6])
            if magic == 0xAA55:
                total = flen + 2
                while len(buf) < total:
                    buf += sock.recv(total - len(buf))
                body = buf[:flen]
                if crc16(body) != struct.unpack(">H", buf[flen:flen + 2])[0]:
                    print(f"  [E] CRC 校验失败 cmd=0x{typ:02X}")
                    return None
                dev, seq = struct.unpack(">IH", body[6:12])
                pl = body[12:]
                print(f"  [R] 设备收到 cmd=0x{typ:02X} dev=0x{dev:08X} payload={pl.hex()}")
                if typ == expect_cmd:
                    return dev, seq, pl
                buf = buf[total:]
                continue
        chunk = sock.recv(1024)
        if not chunk:
            print("  [E] 连接已关闭")
            return None
        buf += chunk


def main():
    fan = socket.create_connection((HOST, PORT), timeout=5)
    sen = socket.create_connection((HOST, PORT), timeout=5)

    # 注册风扇设备（等待服务端下发命令）
    fan.sendall(pack(CMD_REGISTER, FAN_DEV, 1))
    recv_frame(fan, CMD_REGISTER_ACK)

    # 注册传感器设备
    sen.sendall(pack(CMD_REGISTER, SENSOR_DEV, 1))
    recv_frame(sen, CMD_REGISTER_ACK)

    time.sleep(0.5)
    print("\n=== 场景1: 温度35°C > 30 -> 高温开风扇(1800rpm) ===")
    sen.sendall(pack(CMD_DATA_REPORT, SENSOR_DEV, 2, bytes([1, 35])))
    dev, seq, pl = recv_frame(fan, CMD_SET_FAN)
    assert pl == bytes([0x07, 0x08]), f"期望转速 0x0708，实际 {pl.hex()}"
    print("  [OK] 收到 CMD_SET_FAN=0x0708(1800rpm)")

    time.sleep(0.5)
    print("\n=== 场景2: 温度20°C < 24 -> 低温关风扇(0rpm) ===")
    sen.sendall(pack(CMD_DATA_REPORT, SENSOR_DEV, 3, bytes([1, 20])))
    dev, seq, pl = recv_frame(fan, CMD_SET_FAN)
    assert pl == bytes([0x00, 0x00]), f"期望转速 0x0000，实际 {pl.hex()}"
    print("  [OK] 收到 CMD_SET_FAN=0x0000(关)")

    time.sleep(0.5)
    print("\n=== 场景3: 功耗30W > 25 -> 功耗高告警(仅日志，不下发) ===")
    sen.sendall(pack(CMD_DATA_REPORT, SENSOR_DEV, 4, bytes([3, 30])))
    time.sleep(0.8)   # 等待；告警规则 action_cmd=0，fan 不应收到任何帧
    fan.settimeout(0.2)
    try:
        data = fan.recv(1024)
        raise AssertionError(f"预期无下发，但风扇设备收到数据: {data.hex()}")
    except socket.timeout:
        print("  [OK] 风扇设备未收到任何命令（告警仅记录日志）")

    # 互发 HEARTBEAT 确认连接仍正常
    sen.sendall(pack(CMD_HEARTBEAT, SENSOR_DEV, 5))
    fan.sendall(pack(CMD_HEARTBEAT, FAN_DEV, 2))
    print("\n[PASS] 端到端规则引擎验证全部通过")
    fan.close()
    sen.close()


if __name__ == "__main__":
    main()