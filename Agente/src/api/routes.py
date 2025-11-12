from flask import Blueprint, request, jsonify
import logging
from services.instruction_service import InstructionService
from services.ingest_service import process_ingest, IngestError

api = Blueprint('api', __name__)
instruction_service = InstructionService()
logger = logging.getLogger(__name__)

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


@api.route('/ingest', methods=['POST'])
def ingest():
    body = request.get_json(silent=True)
    if body is None:
        return jsonify({'error': 'Invalid or missing JSON body'}), 400

    try:
        result = process_ingest(body)
        orion = result.get('orion', {})
        all_ok = orion.get('all_ok', False)
        any_ok = orion.get('any_ok', False)

        if all_ok:
            return jsonify({
                'status': 'ok',
                'node': result.get('node'),
                'ts_ms': result.get('ts_ms'),
                'orion': orion
            }), 200
        elif any_ok:
            # Partial success: return 200 but mark partial=true
            return jsonify({
                'status': 'partial',
                'partial': True,
                'node': result.get('node'),
                'ts_ms': result.get('ts_ms'),
                'orion': orion
            }), 200
        else:
            # Total failure updating Orion
            return jsonify({
                'status': 'error',
                'partial': False,
                'node': result.get('node'),
                'ts_ms': result.get('ts_ms'),
                'orion': orion
            }), 502
    except IngestError as e:
        logger.error(f"IngestError: {e}")
        return jsonify({'error': 'validation_failed', 'detail': str(e)}), 400
    except ValueError as e:
        logger.error(f"Decryption/Value error: {e}")
        return jsonify({'error': 'bad_request', 'detail': str(e)}), 400
    except Exception as e:
        logger.exception("Unexpected error in /ingest")
        return jsonify({'error': 'internal_error'}), 500