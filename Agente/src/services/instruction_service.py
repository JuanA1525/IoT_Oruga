class InstructionService:
    def __init__(self):
        self.current_instruction = 'stop'

    def set_instruction(self, instruction):
        self.current_instruction = instruction

    def get_instruction(self):
        return self.current_instruction