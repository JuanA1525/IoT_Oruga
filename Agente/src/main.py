from flask import Flask
from api.routes import api
from dotenv import load_dotenv
import logging
import os

# Load environment variables from .env if present (for local/dev)
load_dotenv()

# Basic logging configuration with env-controlled level
LOG_LEVEL = os.getenv("LOG_LEVEL", "INFO").upper()
logging.basicConfig(
    level=getattr(logging, LOG_LEVEL, logging.INFO),
    format="%(asctime)s | %(levelname)s | %(name)s | %(message)s",
)

app = Flask(__name__)
app.register_blueprint(api, url_prefix='/api')

@app.route('/')
def home():
    return "IoT Agent is running!"

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000, debug=True)