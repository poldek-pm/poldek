#!/usr/bin/env python3
"""
HTTP/HTTPS Test Server for vfile tests
Supports: file serving, redirects, basic auth, status codes
"""

import argparse
import base64
import http.server
import os
import socketserver
import ssl
import sys
import threading
import tempfile
import time
from http import HTTPStatus


class TestHTTPRequestHandler(http.server.BaseHTTPRequestHandler):
    """Custom handler for test endpoints"""
    
    # Test credentials for basic auth
    AUTH_USER = "test"
    AUTH_PASS = "pass"
    
    def log_message(self, format, *args):
        """Suppress default logging unless verbose"""
        if self.server.verbose:
            super().log_message(format, *args)
    
    def do_GET(self):
        """Handle GET requests"""
        path = self.path
        
        # Control endpoints
        if path == "/stop":
            self.send_response(HTTPStatus.OK)
            self.end_headers()
            self.server.running = False
            return
        
        if path == "/ping":
            body = b"pong"
            self.send_response(HTTPStatus.OK)
            self.send_header("Content-Type", "text/plain")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        
        # File serving: /file/<filename>
        if path.startswith("/file/"):
            self._serve_file(path[6:])
            return
        
        # Redirect chain: /redirect/<count>/<target>
        if path.startswith("/redirect/"):
            self._handle_redirect(path[10:])
            return
        
        # Basic auth: /auth/basic
        if path == "/auth/basic":
            self._handle_auth()
            return
        
        # Status code: /status/<code>
        if path.startswith("/status/"):
            try:
                code = int(path[8:])
                self.send_response(code)
                self.end_headers()
            except ValueError:
                self.send_error(HTTPStatus.BAD_REQUEST)
            return
        
        # HTTPS downgrade test: /force-http
        if path == "/force-http":
            self.send_response(HTTPStatus.OK)
            self.send_header("Content-Type", "text/plain")
            self.send_header("Content-Length", "16")
            self.end_headers()
            self.wfile.write(b"served-over-http")
            return

        # HTTPS->HTTP downgrade redirect test: /https-downgrade
        if path == "/https-downgrade":
            # Redirect HTTP client to HTTP (simulate downgrade attack)
            self.send_response(HTTPStatus.FOUND)  # 302
            self.send_header("Location", "http://127.0.0.1/force-http")
            self.end_headers()
            return

        # HTTP->HTTPS upgrade redirect test (should be allowed)
        if path == "/http-upgrade":
            # Redirect HTTP to HTTPS (upgrade - should be allowed)
            https_port = self.server.https_port
            if https_port == 443:
                self.send_response(HTTPStatus.FOUND)
                self.send_header("Location", "https://127.0.0.1/file/small.txt")
                self.end_headers()
            else:
                # Non-standard HTTPS port, include it in URL
                self.send_response(HTTPStatus.FOUND)
                self.send_header("Location", f"https://127.0.0.1:{https_port}/file/small.txt")
                self.end_headers()
            return

        # HTTPS->HTTPS redirect test (same protocol - should work)
        if path == "/https-redirect":
            # Redirect to another HTTPS path (same protocol, should succeed)
            # Always include port to handle non-standard ports correctly
            host = f'127.0.0.1:{self.server.server_port}'
            self.send_response(HTTPStatus.FOUND)  # 302
            self.send_header("Location", f"https://{host}/file/small.txt")
            self.end_headers()
            return
        
        # Default 404
        self.send_error(HTTPStatus.NOT_FOUND)
    
    def do_POST(self):
        """Handle POST requests"""
        if self.path == "/echo":
            content_length = int(self.headers.get('Content-Length', 0))
            body = self.rfile.read(content_length)
            
            self.send_response(HTTPStatus.OK)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        
        self.send_error(HTTPStatus.NOT_FOUND)
    
    def _serve_file(self, filename):
        """Serve file from data directory"""
        # Security: prevent path traversal
        filename = os.path.basename(filename)
        filepath = os.path.join(self.server.data_dir, filename)
        
        if not os.path.exists(filepath) or not os.path.isfile(filepath):
            self.send_error(HTTPStatus.NOT_FOUND)
            return
        
        try:
            with open(filepath, 'rb') as f:
                content = f.read()
            
            self.send_response(HTTPStatus.OK)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Content-Length", str(len(content)))
            self.send_header("Connection", "close")
            self.end_headers()
            self.wfile.write(content)
        except Exception as e:
            self.send_error(HTTPStatus.INTERNAL_SERVER_ERROR, str(e))
    
    def _handle_redirect(self, redirect_spec):
        """Handle redirect chains like /redirect/3/file/small.txt"""
        try:
            parts = redirect_spec.split("/", 1)
            count = int(parts[0])
            
            if len(parts) > 1:
                final_target = "/" + parts[1]
            else:
                final_target = "/file/small.txt"
            
            if count > 1:
                # Redirect to same endpoint with decremented count (relative)
                next_url = f"/redirect/{count - 1}{final_target}"
                self.send_response(HTTPStatus.FOUND)  # 302
                self.send_header("Location", next_url)
                self.end_headers()
            else:
                # Final redirect (relative)
                self.send_response(HTTPStatus.FOUND)
                self.send_header("Location", final_target)
                self.end_headers()
        except (ValueError, IndexError):
            self.send_error(HTTPStatus.BAD_REQUEST)
    
    def _handle_auth(self):
        """Handle basic authentication"""
        auth_header = self.headers.get('Authorization', '')
        
        if not auth_header.startswith('Basic '):
            self._request_auth()
            return
        
        try:
            encoded = auth_header[6:]
            decoded = base64.b64decode(encoded).decode('utf-8')
            username, password = decoded.split(':', 1)
            
            if username == self.AUTH_USER and password == self.AUTH_PASS:
                body = b"authenticated"
                self.send_response(HTTPStatus.OK)
                self.send_header("Content-Type", "text/plain")
                self.send_header("Content-Length", str(len(body)))
                self.send_header("Connection", "close")
                self.end_headers()
                self.wfile.write(body)
            else:
                self._request_auth()
        except Exception:
            self._request_auth()
    
    def _request_auth(self):
        """Send 401 authentication required"""
        self.send_response(HTTPStatus.UNAUTHORIZED)
        self.send_header("WWW-Authenticate", 'Basic realm="test"')
        self.end_headers()


class TestHTTPServer(socketserver.ThreadingMixIn, http.server.HTTPServer):
    """Threaded HTTP server for testing"""
    allow_reuse_address = True
    daemon_threads = True
    
    def __init__(self, server_address, RequestHandlerClass, data_dir, verbose=False, https_port=443):
        super().__init__(server_address, RequestHandlerClass)
        self.data_dir = data_dir
        self.verbose = verbose
        self.running = True
        self.server_port = server_address[1]
        self.https_port = https_port


class TestHTTPSServer(TestHTTPServer):
    """HTTPS test server with self-signed certificate"""
    
    def __init__(self, server_address, RequestHandlerClass, data_dir, 
                 cert_file, key_file, verbose=False):
        super().__init__(server_address, RequestHandlerClass, data_dir, verbose)
        
        # Create SSL context
        self.ssl_context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        self.ssl_context.load_cert_chain(cert_file, key_file)
        
        # Wrap socket
        self.socket = self.ssl_context.wrap_socket(self.socket, server_side=True)


def generate_self_signed_cert(cert_path, key_path):
    """Generate self-signed certificate for testing"""
    try:
        from cryptography import x509
        from cryptography.x509.oid import NameOID
        from cryptography.hazmat.primitives import hashes, serialization
        from cryptography.hazmat.primitives.asymmetric import rsa
        import datetime
        
        # Generate private key
        key = rsa.generate_private_key(
            public_exponent=65537,
            key_size=2048,
        )
        
        # Generate certificate
        subject = issuer = x509.Name([
            x509.NameAttribute(NameOID.COUNTRY_NAME, u"US"),
            x509.NameAttribute(NameOID.STATE_OR_PROVINCE_NAME, u"Test"),
            x509.NameAttribute(NameOID.LOCALITY_NAME, u"Test"),
            x509.NameAttribute(NameOID.ORGANIZATION_NAME, u"Poldek Test"),
            x509.NameAttribute(NameOID.COMMON_NAME, u"localhost"),
        ])
        
        cert = x509.CertificateBuilder().subject_name(
            subject
        ).issuer_name(
            issuer
        ).public_key(
            key.public_key()
        ).serial_number(
            x509.random_serial_number()
        ).not_valid_before(
            datetime.datetime.now(datetime.timezone.utc)
        ).not_valid_after(
            datetime.datetime.now(datetime.timezone.utc) + datetime.timedelta(days=1)
        ).add_extension(
            x509.SubjectAlternativeName([x509.DNSName(u"localhost")]),
            critical=False,
        ).sign(key, hashes.SHA256())
        
        # Write to files
        with open(key_path, "wb") as f:
            f.write(key.private_bytes(
                encoding=serialization.Encoding.PEM,
                format=serialization.PrivateFormat.TraditionalOpenSSL,
                encryption_algorithm=serialization.NoEncryption()
            ))
        
        with open(cert_path, "wb") as f:
            f.write(cert.public_bytes(serialization.Encoding.PEM))
        
        return True
    except ImportError:
        return False


def main():
    parser = argparse.ArgumentParser(description='HTTP/HTTPS Test Server')
    parser.add_argument('--port', type=int, default=0, 
                        help='Port (0 for auto)')
    parser.add_argument('--data-dir', default='./data',
                        help='Directory containing test files')
    parser.add_argument('--https', action='store_true',
                        help='Use HTTPS')
    parser.add_argument('--cert', help='Certificate file (or auto-generate)')
    parser.add_argument('--key', help='Private key file')
    parser.add_argument('--verbose', '-v', action='store_true',
                        help='Verbose logging')
    parser.add_argument('--write-port', help='Write port number to this file')
    parser.add_argument('--https-port', type=int, default=443,
                        help='HTTPS port for HTTP->HTTPS upgrade redirects')
    
    args = parser.parse_args()
    
    # Ensure data directory exists
    if not os.path.isdir(args.data_dir):
        print(f"Error: Data directory not found: {args.data_dir}", file=sys.stderr)
        sys.exit(1)
    
    cert_file = None
    key_file = None
    
    # Setup HTTPS if requested
    if args.https:
        if args.cert and args.key:
            cert_file = args.cert
            key_file = args.key
        else:
            # Auto-generate self-signed cert
            cert_dir = tempfile.mkdtemp()
            cert_file = os.path.join(cert_dir, "test.crt")
            key_file = os.path.join(cert_dir, "test.key")
            
            if not generate_self_signed_cert(cert_file, key_file):
                print("Error: cryptography package required for auto-generated certs", 
                      file=sys.stderr)
                print("Install: pip3 install cryptography", file=sys.stderr)
                sys.exit(1)
            
            if args.verbose:
                print(f"Auto-generated cert: {cert_file}")
    
    # Create server
    server_address = ('127.0.0.1', args.port)
    
    if args.https:
        httpd = TestHTTPSServer(server_address, TestHTTPRequestHandler,
                                args.data_dir, cert_file, key_file, args.verbose)
        proto = "HTTPS"
    else:
        httpd = TestHTTPServer(server_address, TestHTTPRequestHandler,
                               args.data_dir, args.verbose, args.https_port)
        proto = "HTTP"
    
    actual_port = httpd.server_port
    
    if args.write_port:
        with open(args.write_port, 'w') as f:
            f.write(str(actual_port))
    
    print(f"{proto} test server running on 127.0.0.1:{actual_port}")
    if args.verbose or os.environ.get('VERBOSE'):
        print(f"Data directory: {args.data_dir}")
        print(f"Endpoints:")
        print(f"  GET /file/<name>     - Serve file from data dir")
        print(f"  GET /redirect/<n>/<target> - Redirect chain")
        print(f"  GET /auth/basic      - Basic auth (user:test pass:pass)")
        print(f"  GET /status/<code>   - Return status code")
        print(f"  POST /echo           - Echo request body")
        print(f"  GET /ping            - Health check")
        print(f"  GET /stop            - Stop server")
    sys.stdout.flush()
    
    # Run server in thread so we can monitor running flag
    server_thread = threading.Thread(target=httpd.serve_forever)
    server_thread.daemon = True
    server_thread.start()
    
    # Wait for shutdown signal
    try:
        while httpd.running:
            time.sleep(0.1)
    except KeyboardInterrupt:
        pass
    finally:
        httpd.shutdown()
        print("\nServer stopped")


if __name__ == '__main__':
    main()
