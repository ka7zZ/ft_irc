import socket

server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)

server.bind(("127.0.0.1", 9999))
server.listen(2)

print("Waiting for HexChat...")
client, address = server.accept()

print("HexChat connected!")

message = ":server 464 * :Password incorrect\r\n"
client.sendall(message.encode())

print("Sent:", repr(message))

client.close()
server.close()
