from flask import json
from src.api.routes import app

def test_receive_instruction(client):
    response = client.post('/api/instruction', json={'instruction': 'forward'})
    assert response.status_code == 200
    assert response.json['current_instruction'] == 'forward'

    response = client.post('/api/instruction', json={'instruction': 'backward'})
    assert response.status_code == 200
    assert response.json['current_instruction'] == 'backward'

    response = client.post('/api/instruction', json={'instruction': 'left'})
    assert response.status_code == 200
    assert response.json['current_instruction'] == 'left'

    response = client.post('/api/instruction', json={'instruction': 'right'})
    assert response.status_code == 200
    assert response.json['current_instruction'] == 'right'

    response = client.post('/api/instruction', json={'instruction': 'stop'})
    assert response.status_code == 200
    assert response.json['current_instruction'] == 'stop'

def test_invalid_instruction(client):
    response = client.post('/api/instruction', json={'instruction': 'invalid'})
    assert response.status_code == 400
    assert 'error' in response.json
    assert response.json['error'] == 'Invalid instruction'