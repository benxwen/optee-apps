# socket_echo_server.py
import socket
import sys

# Create a UDP/IP socket
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
data_size = 1 * 1024

# Bind the socket to the port
server_address = ('192.168.1.34', 9998)
print('starting up UDP server on {} port {}'.format(*server_address))
sock.bind(server_address)

while True:
    # Wait for a connection
    print('waiting for a UDP connection')
    data, client_address = sock.recvfrom(data_size + 1)
    print('UDP connection from', client_address)
    print('received {!r}'.format(data))
