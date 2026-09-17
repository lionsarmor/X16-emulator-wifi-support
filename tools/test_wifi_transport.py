#!/usr/bin/env python3
"""Exercise the emulator's actual modem engine against local TCP/TLS servers."""
import ctypes
import os
from pathlib import Path
import shlex
import socket
import ssl
import subprocess
import tempfile
import threading
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]


class TransportTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        folder = cls.folder = Path(cls.temp.name)
        cls.previous_ca = os.environ.get('SSL_CERT_FILE')
        cls.cert = folder / 'cert.pem'
        cls.key = folder / 'key.pem'
        subprocess.run(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes',
                        '-keyout', str(cls.key), '-out', str(cls.cert), '-days', '1',
                        '-subj', '/CN=localhost', '-addext', 'subjectAltName=DNS:localhost'],
                       check=True, capture_output=True)
        os.environ['SSL_CERT_FILE'] = str(cls.cert)
        harness = folder / 'transport.c'
        harness.write_text('''#include "esp32wifi.c"
uint8_t MHZ = 8;
bool has_wifi_card = true;
uint16_t wifi_card_addr = 0x9fe0;
void test_reset(void) { wifi_card_shutdown(); wifi_card_init(); }
void test_send(const char *s) { while (*s) zm_feed_out_byte((uint8_t)*s++); }
void test_poll(void) { g_cycle_total += 8000; zm_poll(); }
int test_read(uint8_t *out, int limit) {
    int n = 0; while (n < limit && ring_pop(&dev_rx[0], out+n)) n++; return n;
}
int test_overrun(void) { return uregs[0].lsr_oe; }
''')
        flags = shlex.split(subprocess.check_output(['pkg-config', '--cflags', '--libs', 'sdl2', 'openssl'], text=True))
        library = folder / 'transport.so'
        subprocess.run(['cc', '-shared', '-fPIC', '-std=c11', '-Wall', '-Werror',
                        '-DWIFI_HAVE_OPENSSL=1', '-I'+str(ROOT/'x16-emulator/src'),
                        '-I'+str(ROOT/'x16-emulator/src/extern/include'), str(harness),
                        '-o', str(library)] + flags, check=True)
        cls.lib = ctypes.CDLL(str(library))
        cls.lib.test_send.argtypes = [ctypes.c_char_p]
        cls.lib.test_read.argtypes = [ctypes.c_void_p, ctypes.c_int]

    @classmethod
    def tearDownClass(cls):
        cls.lib.wifi_card_shutdown()
        if cls.previous_ca is None:
            os.environ.pop('SSL_CERT_FILE', None)
        else:
            os.environ['SSL_CERT_FILE'] = cls.previous_ca
        cls.temp.cleanup()

    def setUp(self):
        self.lib.test_reset()
        self.lib.test_send(b'\x03ATE0Q0V1\r\n')
        self.assertIn(b'OK', self.read())

    def read(self, limit=8192):
        buffer = ctypes.create_string_buffer(limit)
        count = self.lib.test_read(buffer, limit)
        return buffer.raw[:count]

    def until(self, suffix, timeout=6, limit=8192):
        data = bytearray()
        deadline = time.monotonic() + timeout
        while not data.endswith(suffix) and time.monotonic() < deadline:
            self.lib.test_poll()
            data.extend(self.read(limit))
            time.sleep(.001)
        self.assertTrue(data.endswith(suffix), repr(data[-150:]))
        return bytes(data)

    def server(self, secure, body):
        listener = socket.socket(socket.AF_INET6)
        listener.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_V6ONLY, 0)
        listener.bind(('::', 0))
        listener.listen(1)
        listener.settimeout(6)
        self.addCleanup(listener.close)
        received = bytearray()

        def serve():
            try:
                connection, _ = listener.accept()
                connection.settimeout(6)
                if secure:
                    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
                    context.load_cert_chain(self.cert, self.key)
                    connection = context.wrap_socket(connection, server_side=True)
                with connection:
                    while not received.endswith(b'\r\n\r\n'):
                        part = connection.recv(4096)
                        if not part:
                            return
                        received.extend(part)
                    connection.sendall(body)
                    if secure:
                        # Send close_notify; no need to wait for client shutdown.
                        try:
                            connection.unwrap()
                        except (OSError, ssl.SSLError):
                            pass
            except (OSError, ssl.SSLError):
                pass

        thread = threading.Thread(target=serve, daemon=True)
        thread.start()
        self.addCleanup(lambda: thread.join(timeout=7))
        return listener.getsockname()[1], received

    def test_tls_and_large_binary_with_slow_reader(self):
        body = bytes(range(256))*240
        wire = b'HTTP/1.0 200 OK\r\nContent-Length: 61440\r\n\r\n'+body
        port, received = self.server(True, wire)
        self.lib.test_send(f'ATDS"localhost:{port}"\r\n'.encode())
        self.assertIn(b'CONNECT', self.until(b'CONNECT\r\n'))
        request = b'GET /radar HTTP/1.0\r\nHost: localhost\r\n\r\n'
        self.lib.test_send(request)
        # Fill the staging ring without consuming it; TCP must apply backpressure.
        for _ in range(100):
            self.lib.test_poll()
            time.sleep(.001)
        actual = self.until(b'\r\nNO CARRIER\r\n', timeout=10, limit=128)
        self.assertEqual(actual, wire+b'\r\nNO CARRIER\r\n')
        self.assertEqual(received, request)
        self.assertEqual(self.lib.test_overrun(), 0)

    def test_plain_tcp_still_works(self):
        port, received = self.server(False, b'plain reply')
        self.lib.test_send(f'ATD"localhost:{port}"\r\n'.encode())
        self.until(b'CONNECT\r\n')
        self.lib.test_send(b'hello\r\n\r\n')
        self.assertEqual(self.until(b'\r\nNO CARRIER\r\n'), b'plain reply\r\nNO CARRIER\r\n')
        self.assertEqual(received, b'hello\r\n\r\n')

    def test_hangup_and_reset_preserve_wifi_join(self):
        self.lib.test_send(b'ATW"X16-EMULATOR-NET,"\r\n')
        self.assertIn(b'OK', self.read())
        for command in (b'ATH0', b'ATZ'):
            self.lib.test_send(command+b'\r\nATI2\r\n')
            self.assertIn(b'192.168.4.42', self.read())

    def test_untrusted_certificate_fails_without_connect(self):
        old = os.environ['SSL_CERT_FILE']
        os.environ['SSL_CERT_FILE'] = '/etc/ssl/certs/ca-certificates.crt'
        self.addCleanup(os.environ.__setitem__, 'SSL_CERT_FILE', old)
        self.lib.test_reset()
        self.lib.test_send(b'ATE0\r\n')
        self.read()
        port, _ = self.server(True, b'never sent')
        self.lib.test_send(f'ATDS"localhost:{port}"\r\n'.encode())
        reply = self.until(b'NO CARRIER\r\n')
        self.assertNotIn(b'CONNECT', reply)


if __name__ == '__main__':
    unittest.main()
