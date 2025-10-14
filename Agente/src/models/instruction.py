class Instruction:
    def __init__(self, command):
        self.command = command

    def is_valid(self):
        valid_commands = ['forward', 'backward', 'left', 'right', 'stop']
        return self.command in valid_commands

    def __repr__(self):
        return f"Instruction(command={self.command})"