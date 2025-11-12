import os
import sys
import pytest
from pathlib import Path

# Ensure source tree layout works for imports like `api.routes` used by main.py
_TESTS_DIR = Path(__file__).resolve().parent
_SRC_DIR = (_TESTS_DIR.parent / "src").resolve()
if str(_SRC_DIR) not in sys.path:
    sys.path.insert(0, str(_SRC_DIR))

from src.main import app as flask_app


@pytest.fixture()
def client():
    # Ensure a default token exists for tests that might touch ingest (can be overridden per-test)
    os.environ.setdefault("AGENT_TOKEN", "Benchopo2025")
    with flask_app.test_client() as client:
        yield client
