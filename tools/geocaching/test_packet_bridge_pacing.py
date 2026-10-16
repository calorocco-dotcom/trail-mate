import asyncio
import time
import unittest
from websockets.asyncio.client import connect
from websockets.asyncio.server import serve
from packet_bridge import PacketBridge


class BridgePacingTests(unittest.IsolatedAsyncioTestCase):
    async def test_burst_is_paced_and_exact_packets_survive_on_one_connection(self):
        async def echo(reader, writer):
            try:
                while data := await reader.read(4096):
                    writer.write(data)
                    await writer.drain()
            finally:
                writer.close()
                await writer.wait_closed()
        tcp = await asyncio.start_server(echo,'127.0.0.1',0)
        async with tcp:
            bridge = PacketBridge('127.0.0.1',tcp.sockets[0].getsockname()[1],packet_rate=32,burst_packets=2)
            async with serve(bridge.connection,'127.0.0.1',0,origins=['http://localhost:8080'],max_size=500,max_queue=16,compression=None) as websocket:
                url = 'ws://127.0.0.1:'+str(websocket.sockets[0].getsockname()[1])
                async with connect(url,origin='http://localhost:8080') as client:
                    packets = [bytes([i])*500 for i in range(10)]
                    started = time.monotonic()
                    for packet in packets:
                        await client.send(packet)
                    for packet in packets:
                        self.assertEqual(await asyncio.wait_for(client.recv(),3),packet)
                    self.assertGreaterEqual(time.monotonic()-started,.2)
                    self.assertEqual(bridge.clients,1)
                    await client.send(bytes(20))
                    self.assertEqual(await asyncio.wait_for(client.recv(),3),bytes(20))

    def test_rate_and_burst_admission_are_bounded(self):
        for value in [0,4097,True,.5,'512']:
            with self.assertRaises(ValueError):
                PacketBridge('localhost',1,packet_rate=value)
            with self.assertRaises(ValueError):
                PacketBridge('localhost',1,burst_packets=value)
