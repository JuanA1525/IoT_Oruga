from flask import json


def test_receive_instruction(client):
    response = client.post('/api/instruction', json={'instruction': 'forward'})
    assert response.status_code == 200
    assert response.json['instruction'] == 'forward'

    response = client.post('/api/instruction', json={'instruction': 'backward'})
    assert response.status_code == 200
    assert response.json['instruction'] == 'backward'

    response = client.post('/api/instruction', json={'instruction': 'left'})
    assert response.status_code == 200
    assert response.json['instruction'] == 'left'

    response = client.post('/api/instruction', json={'instruction': 'right'})
    assert response.status_code == 200
    assert response.json['instruction'] == 'right'

    response = client.post('/api/instruction', json={'instruction': 'stop'})
    assert response.status_code == 200
    assert response.json['instruction'] == 'stop'


def test_invalid_instruction(client):
    response = client.post('/api/instruction', json={'instruction': 'invalid'})
    assert response.status_code == 400
    assert 'error' in response.json
    assert response.json['error'] == 'Invalid instruction'