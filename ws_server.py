# requirements pip3 install websockets


import asyncio
import websockets

# This function runs whenever a client connects
async def handler(websocket):
    print("Client connected")

    try:
        async for message in websocket:
            print(f"Received: {message}")

            # Echo back to the ESP32
            await websocket.send(f"ACK: {message}")

    except websockets.exceptions.ConnectionClosed:
        print("Client disconnected")

# Start server
async def main():
    async with websockets.serve(handler, "0.0.0.0", 8080):
        print("WebSocket server running on port 8080")
        await asyncio.Future()  # run forever

asyncio.run(main())
