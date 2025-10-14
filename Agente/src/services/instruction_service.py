from flask import jsonify

class InstructionService:
    def __init__(self):
        self.current_instruction = 'stop'

    def set_instruction(self, instruction):
        """Set the current movement instruction"""
        self.current_instruction = instruction
        return jsonify({"message": "Instruction set to {}".format(instruction)}), 200

    def get_instruction(self):
        """Get the current movement instruction"""
        return jsonify({"current_instruction": self.current_instruction}), 200