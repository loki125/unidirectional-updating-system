import socket
import errno
import logging
import json
from typing import Dict, Tuple


class FluteBroadcast:
    def __init__(self, socket_path :str):
        self.socket_path = socket_path
        self.client = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)

        try:
            self.client.connect(socket_path)
        except socket.error as e:
            # Specific handling for common connection errors
            if e.errno == errno.ENOENT:
                logging.error(f"Socket file not found at {socket_path}. broadcaster might not be running")
            elif e.errno == errno.ECONNREFUSED:
                logging.error("Connection refused. The server might be down or not listening.")
            else:
                logging.error(f"Caught a general socket error: {e}")


    def add_os_distributions(self, *names: str):
        for dist in names:
            data_to_send = {
                "action": "add_os_distribution",
                "params": {"dist": dist}
            }
            try:
                json_payload = json.dumps(data_to_send)
                self.client.sendall(json_payload.encode('utf-8'))
                logging.info("Sent OS distribution: %s", dist)
            except (TypeError, json.JSONDecodeError) as e:
                logging.error("Failed to encode JSON for distribution '%s': %s", dist, e)
            except OSError as e:
                logging.error("Failed to send distribution '%s' to client: %s", dist, e)

    def add_destinations(self, *remote_dest: Tuple[str, int]):
        for dist in remote_dest:
            if len(dist) != 2:
                logging.warning("Destination %s should be (IP, port). Skipping.", dist)
                continue

            data_to_send = {
                "action": "add_destination",
                "params": {"remote_ip": dist[0], "remote_port": dist[1]}
            }
            try:
                json_payload = json.dumps(data_to_send)
                self.client.sendall(json_payload.encode('utf-8'))
                logging.info("Sent destination: %s:%s", dist[0], dist[1])
            except (TypeError, json.JSONDecodeError) as e:
                logging.error("Failed to encode JSON for destination '%s': %s", dist, e)
            except OSError as e:
                logging.error("Failed to send destination '%s' to client: %s", dist, e)

    def send_object(self, metadata: Dict, file_path: str):
        data_to_send = {
            "action": "send_object",
            "params": {"metadata": metadata, "file_path": file_path}
        }
        try:
            json_payload = json.dumps(data_to_send)
            self.client.sendall(json_payload.encode('utf-8'))
            logging.info("Sent object: %s", file_path)
        except (TypeError, json.JSONDecodeError) as e:
            logging.error("Failed to encode JSON for object '%s': %s", file_path, e)
        except OSError as e:
            logging.error("Failed to send object '%s' to client: %s", file_path, e)