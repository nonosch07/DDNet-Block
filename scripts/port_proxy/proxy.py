# pip install pydivert
import pydivert

TARGET_IP = "127.0.0.1"
RANGE_START = 20000
RANGE_END = 20100
GAME_PORT = 8303

filter_cond = (
    f"(udp.DstPort >= {RANGE_START} and udp.DstPort <= {RANGE_END}) or "
    f"(udp.SrcPort == {GAME_PORT} and ip.SrcAddr == {TARGET_IP})"
)

state_map = {}

print(f"[*] Starting redirector on {TARGET_IP}...")
print(f"[*] Monitoring UDP {RANGE_START}-{RANGE_END} -> {GAME_PORT}")

try:
    with pydivert.WinDivert(filter_cond) as w:
        for packet in w:
            if RANGE_START <= packet.dst_port <= RANGE_END:
                state_map[(packet.src_addr, packet.src_port)] = packet.dst_port
                
                packet.dst_port = GAME_PORT
                packet.dst_addr = TARGET_IP
                w.send(packet)
                
            elif packet.src_port == GAME_PORT:
                lookup_key = (packet.dst_addr, packet.dst_port)
                
                if lookup_key in state_map:
                    packet.src_port = state_map[lookup_key]
                    packet.src_addr = TARGET_IP 
                    w.send(packet)
                else:
                    w.send(packet)

except PermissionError:
    print("[!] ERROR: You must run this script as Administrator!")
except Exception as e:
    print(f"[!] ERROR: {e}")