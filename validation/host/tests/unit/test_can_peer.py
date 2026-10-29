"""`hal_st_validation.can_peer`: the `--can-peer` specs, the bit rates each peer runs at, the port-bridge wire format,
and the python-can peer on a virtual bus and the port-bridge peer on a local socket."""

import socket
import threading

import pytest

from hal_st_validation.can_peer import (
    BusFrame,
    CanPeerError,
    PortBridgeCanPeer,
    PythonCanPeer,
    decode_frame,
    encode_frame,
    open_peer,
)


def test_open_peer_specs():
    slcan = open_peer("slcan:socket://host.docker.internal:5002")
    assert isinstance(slcan, PythonCanPeer)
    assert (slcan.interface, slcan.channel) == ("slcan", "socket://host.docker.internal:5002")
    assert slcan.adjustable and slcan.listen_only_capable
    bridge = open_peer("port-bridge:host.docker.internal:5001", 500000)
    assert isinstance(bridge, PortBridgeCanPeer)
    assert (bridge.host, bridge.port, bridge.fixed_bitrate) == ("host.docker.internal", 5001, 500000)
    for spec in ("slcan", ":COM7", "port-bridge:host", "port-bridge:host:x"):
        with pytest.raises(CanPeerError):
            open_peer(spec)
    with pytest.raises(CanPeerError):
        open_peer("port-bridge:localhost:5001")
    with pytest.raises(CanPeerError):
        open_peer("socketcan:can0")


def test_supported_bit_rates():
    assert open_peer("slcan:COM7").supports(1000000)
    assert open_peer("slcan:COM7").supports(750000)
    assert not open_peer("slcan:COM7").supports(123456)
    assert not open_peer("gs_usb:0").supports(500000, listen_only=True)
    socketcan = open_peer("socketcan:can0", 250000)
    assert socketcan.supports(250000) and not socketcan.supports(500000) and not socketcan.adjustable


def test_wire_format_matches_port_bridge():
    raw = encode_frame(0x1ABCDEF0, b"\x01\x02\x03", ext=True)
    assert len(raw) == 16
    assert raw[:4] == (0x1ABCDEF0 | 0x80000000).to_bytes(4, "little")
    assert raw[4] == 3 and raw[8:] == b"\x01\x02\x03" + bytes(5)
    assert decode_frame(raw) == BusFrame(0x1ABCDEF0, True, b"\x01\x02\x03")
    assert decode_frame(encode_frame(0x7FF, b"", ext=False)) == BusFrame(0x7FF, False, b"")
    error = (0x20000000 | 0x4).to_bytes(4, "little") + bytes(12)
    assert decode_frame(error) is None
    remote = (0x40000000 | 0x123).to_bytes(4, "little") + bytes(12)
    assert decode_frame(remote) is None


def test_python_can_peer_on_a_virtual_bus():
    can = pytest.importorskip("can")
    peer = open_peer("virtual:unit-peer")
    other = can.Bus(interface="virtual", channel="unit-peer")
    try:
        peer.configure(500000)
        peer.send(0x123, b"\xaa", ext=False)
        message = other.recv(0.5)
        assert (message.arbitration_id, bytes(message.data)) == (0x123, b"\xaa")
        other.send(can.Message(arbitration_id=0x10, is_extended_id=False, data=b"\x01"))
        other.send(can.Message(arbitration_id=0x1ABCDEF0, is_extended_id=True, data=b"\x02"))
        frame = peer.receive(0.5, predicate=lambda f: f.ext)
        assert frame == BusFrame(0x1ABCDEF0, True, b"\x02")
        assert peer.receive(0.05) is None
    finally:
        other.shutdown()
        peer.close()


def test_port_bridge_peer_round_trip():
    server = socket.create_server(("127.0.0.1", 0))
    port = server.getsockname()[1]
    received = []

    def serve():
        connection, _ = server.accept()
        with connection:
            frame = b""
            while len(frame) < 16:
                chunk = connection.recv(16 - len(frame))
                if not chunk:
                    break
                frame += chunk
            received.append(frame)
            connection.sendall(encode_frame(0x55, b"\x09", ext=False)[:7])
            connection.sendall(encode_frame(0x55, b"\x09", ext=False)[7:] + encode_frame(0x66, b"", ext=True))

    thread = threading.Thread(target=serve, daemon=True)
    thread.start()
    peer = PortBridgeCanPeer("127.0.0.1", port, 500000)
    try:
        with pytest.raises(CanPeerError):
            peer.configure(250000)
        peer.configure(500000)
        peer.send(0x1F, b"\x01\x02", ext=False)
        assert peer.receive(1.0) == BusFrame(0x55, False, b"\x09")
        assert peer.receive(1.0) == BusFrame(0x66, True, b"")
    finally:
        peer.close()
        thread.join(1.0)
        server.close()
    assert decode_frame(received[0]) == BusFrame(0x1F, False, b"\x01\x02")
