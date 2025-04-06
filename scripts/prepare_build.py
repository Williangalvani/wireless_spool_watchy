Import("env")

# Generate compile_commands.json for better IDE integration
env.SetOption("compile_commands", True) 