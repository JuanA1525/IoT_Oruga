# IoT Agent Project

This project implements an IoT agent that serves as a communication bridge between IoT devices and a cloud architecture. The agent is designed to receive movement instructions for a robot and simulate decision-making based on those instructions.

## Project Structure

```
iot-agent
├── src
│   ├── main.py               # Entry point of the application
│   ├── api                   # Contains API related files
│   │   ├── __init__.py       # Initializes the API package
│   │   └── routes.py         # Defines API routes for movement instructions
│   ├── services              # Contains service related files
│   │   ├── __init__.py       # Initializes the services package
│   │   └── instruction_service.py # Logic for managing movement instructions
│   └── models                # Contains model related files
│       ├── __init__.py       # Initializes the models package
│       └── instruction.py     # Defines the Instruction model
├── tests                     # Contains test files
│   ├── __init__.py           # Initializes the tests package
│   └── test_api.py           # Unit tests for the API routes
├── Dockerfile                 # Instructions to build the Docker image
├── docker-compose.yml         # Defines services for Docker application
├── requirements.txt           # Lists Python dependencies
├── .env.example               # Example of environment variables
├── .dockerignore              # Files to ignore when building Docker image
├── .gitignore                 # Files to ignore in version control
└── README.md                  # Documentation for the project
```

## Setup Instructions

1. **Clone the repository**:
   ```
   git clone <repository-url>
   cd iot-agent
   ```

2. **Install dependencies**:
   You can install the required Python packages using pip:
   ```
   pip install -r requirements.txt
   ```

3. **Run the application**:
   You can run the application using Docker:
   ```
   docker-compose up
   ```

4. **Access the API**:
   The API will be available at `http://localhost:5000/api/instruction`. You can send POST requests to this endpoint with movement instructions.

## Usage

The API accepts the following movement instructions:
- `forward`
- `backward`
- `left`
- `right`
- `stop`

You can test the API using tools like Postman or curl.

## Contributing

Contributions are welcome! Please feel free to submit a pull request or open an issue for any suggestions or improvements.

## License

This project is licensed under the MIT License. See the LICENSE file for more details.