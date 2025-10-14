from flask import jsonify

current_instruction = None

def set_instruction(instruction):
    global current_instruction
    current_instruction = instruction
    return jsonify({"message": "Instruction set to {}".format(instruction)}), 200

def get_instruction():
    return jsonify({"current_instruction": current_instruction}), 200