import os
import sys

# Get project root from args
project_root = sys.argv[1]
env_file = os.path.join(project_root, ".env")
header_file = os.path.join(project_root, "main", "env_config.h")

if not os.path.exists(env_file):
    print(f"No .env file found at {env_file}, skipping header generation.")
    sys.exit(0)

def parse_env_file(env_path):
    env_data = {}
    with open(env_path, 'r') as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            if "=" in line:
                key, value = line.split("=", 1)
                env_data[key.strip()] = value.strip()
    return env_data

def generate_header(env_data, output_path):
    with open(output_path, 'w') as f:
        f.write("// Auto-generated from .env\n\n")
        for key, value in env_data.items():
            f.write(f"#define {key} \"{value}\"\n")

env_data = parse_env_file(env_file)
generate_header(env_data, header_file)
print(f"Generated header at {header_file}")
