from flask import Blueprint, request, jsonify
from services.instruction_service import InstructionService

api = Blueprint('api', __name__)
instruction_service = InstructionService()

@api.route('/instruction', methods=['POST'])
def set_instruction():
    data = request.json
    instruction = data.get('instruction')

    if instruction not in ['forward', 'backward', 'left', 'right', 'stop']:
        return jsonify({'error': 'Invalid instruction'}), 400

    instruction_service.set_instruction(instruction)
    return jsonify({'instruction': instruction_service.get_instruction()})

@api.route('/instruction', methods=['GET'])
def get_instruction():
    return jsonify({'instruction': instruction_service.get_instruction()})