"""CPU-only ASGI tests: cancel this request's child, retain the service."""
import asyncio
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
try:
    import fastapi
except ImportError:
    fastapi = None

if fastapi is not None:
    spec = importlib.util.spec_from_file_location("cancellation_server", ROOT / "scripts/ptv3_server.py")
    server = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(server)


@unittest.skipIf(fastapi is None, "FastAPI and AnyIO are required for ASGI integration tests")
class InferenceCancellationTest(unittest.IsolatedAsyncioTestCase):
    async def request(self, path, body, received, sent):
        await received.put({"type": "http.request", "body": json.dumps(body).encode(), "more_body": False})
        scope = {"type": "http", "asgi": {"version": "3.0"}, "http_version": "1.1",
                 "method": "POST", "scheme": "http", "path": path, "raw_path": path.encode(),
                 "query_string": b"", "headers": [(b"content-type", b"application/json")],
                 "client": ("127.0.0.1", 12345), "server": ("127.0.0.1", 8008)}

        async def send(message):
            sent.append(message)

        await server.app(scope, received.get, send)

    async def test_disconnect_stops_child_and_removes_input_for_both_routes(self):
        popen = subprocess.Popen
        unrelated = popen([sys.executable, "-c", "import time; time.sleep(30)"])
        try:
            for route in ("/classify", "/regress"):
                with self.subTest(route=route), tempfile.TemporaryDirectory() as folder:
                    (Path(folder) / "best_model.pth").touch()
                    started = threading.Event()
                    children, inputs = [], []

                    def spawn(cmd, **kwargs):
                        inputs.append(Path(cmd[cmd.index("--input") + 1]))
                        child = popen([sys.executable, "-c", "import time; time.sleep(30)"], **kwargs)
                        children.append(child)
                        started.set()
                        return child

                    received, sent = asyncio.Queue(), []
                    body = dict(points_text="0 0 0\n", primitive_type="FlatRoofHouse",
                                bbox_x=1, bbox_y=1, bbox_z=1, scale=1)
                    with patch.object(server.subprocess, "Popen", side_effect=spawn), \
                         patch.object(server, "regression_log_dir", return_value=Path(folder)):
                        task = asyncio.create_task(self.request(route, body, received, sent))
                        try:
                            self.assertTrue(await asyncio.to_thread(started.wait, 3))
                            self.assertTrue(inputs[0].exists())
                            await received.put({"type": "http.disconnect"})
                            await asyncio.wait_for(task, 3)
                        finally:
                            for child in children:
                                if child.poll() is None:
                                    child.kill()
                                    child.communicate()
                    self.assertIsNotNone(children[0].poll())
                    self.assertFalse(inputs[0].exists())
                    response = json.loads(next(m["body"] for m in sent if m["type"] == "http.response.body"))
                    self.assertTrue(response["canceled"])
                    self.assertNotIn("model_log_dir", response)
                    self.assertIsNone(unrelated.poll())
                    self.assertTrue(server.health()["ok"])
        finally:
            unrelated.kill()
            unrelated.wait()

    async def test_normal_classification_still_works_after_cancelable_wait(self):
        popen = subprocess.Popen
        inputs = []

        def spawn(cmd, **kwargs):
            inputs.append(Path(cmd[cmd.index("--input") + 1]))
            return popen([sys.executable, "-c", 'print(\'[{"class":"FlatRoofHouse","prob":0.99}]\')'], **kwargs)

        sent = []
        with patch.object(server.subprocess, "Popen", side_effect=spawn):
            await asyncio.wait_for(self.request("/classify", {"points_text": "0 0 0\n"}, asyncio.Queue(), sent), 5)
        response = json.loads(next(m["body"] for m in sent if m["type"] == "http.response.body"))
        self.assertTrue(response["ok"])
        self.assertEqual(response["result"][0]["class"], "FlatRoofHouse")
        self.assertEqual(response["model_log_dir"], str(server.CLS_LOG))
        self.assertFalse(inputs[0].exists())

    async def test_timeout_reaps_child_and_error_output_is_preserved(self):
        popen = subprocess.Popen
        children = []

        def spawn(*args, **kwargs):
            child = popen(*args, **kwargs)
            children.append(child)
            return child

        with patch.object(server.subprocess, "Popen", side_effect=spawn):
            response = server.run_json_command([sys.executable, "-c", "import time; time.sleep(30)"], timeout=.05)
        self.assertFalse(response["ok"])
        self.assertIn("timed out", response["error"])
        self.assertIsNotNone(children[0].poll())
        response = server.run_json_command([sys.executable, "-c", "import sys; sys.stderr.write('bad input'); sys.exit(2)"])
        self.assertEqual(response, {"ok": False, "error": "bad input"})
        response = server.run_json_command([sys.executable, "-c", "print('not json')"])
        self.assertFalse(response["ok"])
        self.assertIn("not json", response["stdout"])


if __name__ == "__main__":
    unittest.main()
